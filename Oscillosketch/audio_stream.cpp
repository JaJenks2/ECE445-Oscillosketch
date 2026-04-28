#include "audio_stream.h"
#include "config.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

constexpr uint8_t AUDIO_PKT_MAGIC0 = 0xA5;
constexpr uint8_t AUDIO_PKT_MAGIC1 = 0x5A;
constexpr uint8_t AUDIO_PKT_TYPE_START    = 0x10;
constexpr uint8_t AUDIO_PKT_TYPE_DATA_S16 = 0x11;
constexpr uint8_t AUDIO_PKT_TYPE_STOP     = 0x12;

enum class ParseState : uint8_t {
  WAIT_MAGIC0,
  WAIT_MAGIC1,
  READ_HEADER,
  READ_PAYLOAD
};

struct StereoFrame {
  int16_t left;
  int16_t right;
};

static portMUX_TYPE g_audioMux = portMUX_INITIALIZER_UNLOCKED;

static StereoFrame g_ring[AUDIO_BUFFER_FRAMES];
static size_t g_ringHead = 0;
static size_t g_ringTail = 0;
static size_t g_ringCount = 0;
static size_t g_maxRingCount = 0;

static ParseState g_parseState = ParseState::WAIT_MAGIC0;
static uint8_t g_header[6];
static size_t g_headerIndex = 0;
static uint8_t g_payload[AUDIO_PACKET_FRAMES * 4];
static size_t g_payloadLen = 0;
static size_t g_payloadIndex = 0;

static uint16_t g_lastAcceptedSeq = 0;
static bool g_haveAcceptedSeq = false;
static uint32_t g_lastPacketMs = 0;
static bool g_sessionStarted = false;

// Telemetry counters
static uint32_t g_packetsAccepted = 0;
static uint32_t g_duplicatePackets = 0;
static uint32_t g_sequenceErrors = 0;
static uint32_t g_parserResets = 0;
static uint32_t g_bufferOverwrites = 0;
static uint32_t g_underruns = 0;

static TaskHandle_t g_audioRxTaskHandle = nullptr;

static inline size_t ringFillLocked() {
  return g_ringCount;
}

static void clearRingAndSessionLocked() {
  g_ringHead = 0;
  g_ringTail = 0;
  g_ringCount = 0;
  g_maxRingCount = 0;

  g_haveAcceptedSeq = false;
  g_lastAcceptedSeq = 0;
  g_lastPacketMs = 0;
  g_sessionStarted = false;

  g_packetsAccepted = 0;
  g_duplicatePackets = 0;
  g_sequenceErrors = 0;
  g_parserResets = 0;
  g_bufferOverwrites = 0;
  g_underruns = 0;
}

static inline void ringPushLocked(int16_t left, int16_t right) {
  if (g_ringCount >= AUDIO_BUFFER_FRAMES) {
    g_ringTail = (g_ringTail + 1) % AUDIO_BUFFER_FRAMES;
    g_ringCount--;
    g_bufferOverwrites++;
  }

  g_ring[g_ringHead].left = left;
  g_ring[g_ringHead].right = right;
  g_ringHead = (g_ringHead + 1) % AUDIO_BUFFER_FRAMES;
  g_ringCount++;

  if (g_ringCount > g_maxRingCount) {
    g_maxRingCount = g_ringCount;
  }
}

static void sendAckLine(const char* kind, uint16_t seq) {
  size_t fill = 0;
  size_t maxFill = 0;
  uint32_t accepted = 0;
  uint32_t dup = 0;
  uint32_t seqErr = 0;
  uint32_t pres = 0;
  uint32_t over = 0;
  uint32_t und = 0;

  portENTER_CRITICAL(&g_audioMux);
  fill = g_ringCount;
  maxFill = g_maxRingCount;
  accepted = g_packetsAccepted;
  dup = g_duplicatePackets;
  seqErr = g_sequenceErrors;
  pres = g_parserResets;
  over = g_bufferOverwrites;
  und = g_underruns;
  portEXIT_CRITICAL(&g_audioMux);

  Serial.printf("ACK %s %u %u %u %lu %lu %lu %lu %lu %lu\n",
                kind,
                static_cast<unsigned>(seq),
                static_cast<unsigned>(fill),
                static_cast<unsigned>(maxFill),
                static_cast<unsigned long>(accepted),
                static_cast<unsigned long>(dup),
                static_cast<unsigned long>(seqErr),
                static_cast<unsigned long>(pres),
                static_cast<unsigned long>(over),
                static_cast<unsigned long>(und));
}

static void resetParserNoCount() {
  g_parseState = ParseState::WAIT_MAGIC0;
  g_headerIndex = 0;
  g_payloadLen = 0;
  g_payloadIndex = 0;
}

static void resetParserCounted() {
  g_parserResets++;
  resetParserNoCount();
}

static void handleStartPacket(uint16_t seq) {
  portENTER_CRITICAL(&g_audioMux);
  clearRingAndSessionLocked();
  g_sessionStarted = true;
  g_lastPacketMs = millis();
  portEXIT_CRITICAL(&g_audioMux);

  sendAckLine("START", seq);
  resetParserNoCount();
}

static void handleStopPacket(uint16_t seq) {
  portENTER_CRITICAL(&g_audioMux);
  clearRingAndSessionLocked();
  portEXIT_CRITICAL(&g_audioMux);

  sendAckLine("STOP", seq);
  resetParserNoCount();
}

static void handleCompleteDataPacket(uint16_t seq, uint16_t frameCount) {
  if (!g_sessionStarted) {
    // Ignore data until START is seen.
    g_sequenceErrors++;
    sendAckLine("ERR", seq);
    resetParserNoCount();
    return;
  }

  const size_t expectedPayloadLen = static_cast<size_t>(frameCount) * 4;
  if (g_payloadLen != expectedPayloadLen) {
    resetParserCounted();
    return;
  }

  // Duplicate retransmit of the most recently accepted packet.
  if (g_haveAcceptedSeq && seq == g_lastAcceptedSeq) {
    g_duplicatePackets++;
    g_lastPacketMs = millis();
    sendAckLine("DATA", seq);
    resetParserNoCount();
    return;
  }

  // Stop-and-wait expects strictly monotonic sequence numbers.
  if (g_haveAcceptedSeq) {
    const uint16_t expected = static_cast<uint16_t>(g_lastAcceptedSeq + 1);
    if (seq != expected) {
      g_sequenceErrors++;
      sendAckLine("ERR", seq);
      resetParserNoCount();
      return;
    }
  }

  portENTER_CRITICAL(&g_audioMux);
  for (size_t i = 0; i < frameCount; ++i) {
    const size_t base = i * 4;
    const int16_t left =
        static_cast<int16_t>(
            static_cast<uint16_t>(g_payload[base + 0]) |
            (static_cast<uint16_t>(g_payload[base + 1]) << 8));

    const int16_t right =
        static_cast<int16_t>(
            static_cast<uint16_t>(g_payload[base + 2]) |
            (static_cast<uint16_t>(g_payload[base + 3]) << 8));

    ringPushLocked(left, right);
  }

  g_packetsAccepted++;
  g_haveAcceptedSeq = true;
  g_lastAcceptedSeq = seq;
  g_lastPacketMs = millis();
  portEXIT_CRITICAL(&g_audioMux);

  sendAckLine("DATA", seq);
  resetParserNoCount();
}

static void handleHeaderComplete() {
  const uint8_t type = g_header[0];
  const uint8_t flags = g_header[1];
  (void)flags;

  const uint16_t seq =
      static_cast<uint16_t>(g_header[2]) |
      (static_cast<uint16_t>(g_header[3]) << 8);

  const uint16_t frameCount =
      static_cast<uint16_t>(g_header[4]) |
      (static_cast<uint16_t>(g_header[5]) << 8);

  switch (type) {
    case AUDIO_PKT_TYPE_START:
      if (frameCount != 0) {
        resetParserCounted();
      } else {
        handleStartPacket(seq);
      }
      break;

    case AUDIO_PKT_TYPE_STOP:
      if (frameCount != 0) {
        resetParserCounted();
      } else {
        handleStopPacket(seq);
      }
      break;

    case AUDIO_PKT_TYPE_DATA_S16:
      if (frameCount == 0 || frameCount > AUDIO_PACKET_FRAMES) {
        resetParserCounted();
      } else {
        g_payloadLen = static_cast<size_t>(frameCount) * 4;
        g_payloadIndex = 0;
        g_parseState = ParseState::READ_PAYLOAD;
      }
      break;

    default:
      resetParserCounted();
      break;
  }
}

static void parseByte(uint8_t b) {
  switch (g_parseState) {
    case ParseState::WAIT_MAGIC0:
      if (b == AUDIO_PKT_MAGIC0) {
        g_parseState = ParseState::WAIT_MAGIC1;
      }
      break;

    case ParseState::WAIT_MAGIC1:
      if (b == AUDIO_PKT_MAGIC1) {
        g_parseState = ParseState::READ_HEADER;
        g_headerIndex = 0;
      } else if (b == AUDIO_PKT_MAGIC0) {
        g_parseState = ParseState::WAIT_MAGIC1;
      } else {
        g_parseState = ParseState::WAIT_MAGIC0;
      }
      break;

    case ParseState::READ_HEADER:
      g_header[g_headerIndex++] = b;
      if (g_headerIndex >= sizeof(g_header)) {
        handleHeaderComplete();
      }
      break;

    case ParseState::READ_PAYLOAD:
      g_payload[g_payloadIndex++] = b;
      if (g_payloadIndex >= g_payloadLen) {
        const uint16_t seq =
            static_cast<uint16_t>(g_header[2]) |
            (static_cast<uint16_t>(g_header[3]) << 8);
        const uint16_t frameCount =
            static_cast<uint16_t>(g_header[4]) |
            (static_cast<uint16_t>(g_header[5]) << 8);
        handleCompleteDataPacket(seq, frameCount);
      }
      break;
  }
}

static void audioRxTask(void* arg) {
  (void)arg;
  static uint8_t rxBuf[256];

  for (;;) {
    const int availInt = Serial.available();
    if (availInt <= 0) {
      vTaskDelay(1);
      continue;
    }

    const size_t avail = static_cast<size_t>(availInt);
    const size_t toRead = min(avail, sizeof(rxBuf));
    const size_t got = Serial.readBytes(reinterpret_cast<char*>(rxBuf), toRead);

    for (size_t i = 0; i < got; ++i) {
      parseByte(rxBuf[i]);
    }

    taskYIELD();
  }
}

}  // namespace

void audioStreamBegin() {
  Serial.begin(AUDIO_SERIAL_BAUD);
  audioStreamReset();

  if (g_audioRxTaskHandle == nullptr) {
    const BaseType_t appCore = xPortGetCoreID();
    xTaskCreatePinnedToCore(
        audioRxTask,
        "audio_rx",
        4096,
        nullptr,
        2,
        &g_audioRxTaskHandle,
        appCore);
  }
}

void audioStreamReset() {
  portENTER_CRITICAL(&g_audioMux);
  clearRingAndSessionLocked();
  portEXIT_CRITICAL(&g_audioMux);

  resetParserNoCount();
}

void audioStreamPollSerial() {
  // No-op now: RX is handled by the dedicated audioRxTask().
}

size_t audioStreamAvailableFrames() {
  portENTER_CRITICAL(&g_audioMux);
  const size_t n = g_ringCount;
  portEXIT_CRITICAL(&g_audioMux);
  return n;
}

bool audioStreamPopFrame(int16_t& left, int16_t& right) {
  portENTER_CRITICAL(&g_audioMux);

  if (g_ringCount == 0) {
    g_underruns++;
    portEXIT_CRITICAL(&g_audioMux);
    return false;
  }

  left = g_ring[g_ringTail].left;
  right = g_ring[g_ringTail].right;

  g_ringTail = (g_ringTail + 1) % AUDIO_BUFFER_FRAMES;
  g_ringCount--;

  portEXIT_CRITICAL(&g_audioMux);
  return true;
}

bool audioStreamIsActive() {
  if (!g_sessionStarted || g_lastPacketMs == 0) {
    return false;
  }
  return (millis() - g_lastPacketMs) <= AUDIO_STREAM_ACTIVE_TIMEOUT_MS;
}

void audioStreamGetStats(AudioStreamStats& out) {
  portENTER_CRITICAL(&g_audioMux);
  out.currentFill = g_ringCount;
  out.maxFill = g_maxRingCount;
  portEXIT_CRITICAL(&g_audioMux);

  out.packetsAccepted = g_packetsAccepted;
  out.duplicatePackets = g_duplicatePackets;
  out.sequenceErrors = g_sequenceErrors;
  out.parserResets = g_parserResets;
  out.bufferOverwrites = g_bufferOverwrites;
  out.underruns = g_underruns;
  out.lastSeq = g_lastAcceptedSeq;
  out.active = audioStreamIsActive();
}
