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

constexpr size_t AUDIO_HEADER_LEN_AFTER_MAGIC = 10;  // type, flags, seq, frame_count, crc32
constexpr size_t AUDIO_START_PAYLOAD_LEN = 8;        // u32 rate, u16 packet_frames, u8 channels, u8 sample_bytes
constexpr uint8_t AUDIO_STREAM_CHANNELS = 2;
constexpr uint8_t AUDIO_STREAM_SAMPLE_BYTES = 2;
constexpr uint32_t AUDIO_STATUS_PERIOD_MS = 100;
constexpr uint16_t AUDIO_MAX_RECOVER_GAP_PACKETS = 16;
constexpr uint16_t AUDIO_CONCEAL_FADE_FRAMES = 96;

static_assert((AUDIO_BUFFER_FRAMES & (AUDIO_BUFFER_FRAMES - 1)) == 0,
              "AUDIO_BUFFER_FRAMES must be a power of two for the SPSC ring.");

constexpr uint32_t AUDIO_RING_MASK = static_cast<uint32_t>(AUDIO_BUFFER_FRAMES - 1);

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

// Single-producer/single-consumer audio ring:
//   producer = audio RX task
//   consumer = replay task through audioGetReplayStep()
// The producer owns g_ringHead, the consumer owns g_ringTail. Both read the
// other index. This avoids taking a FreeRTOS critical section inside the
// 110 kHz replay loop.
static StereoFrame g_ring[AUDIO_BUFFER_FRAMES];
static volatile uint32_t g_ringHead = 0;
static volatile uint32_t g_ringTail = 0;
static volatile size_t g_maxRingCount = 0;

static ParseState g_parseState = ParseState::WAIT_MAGIC0;
static uint8_t g_header[AUDIO_HEADER_LEN_AFTER_MAGIC];
static size_t g_headerIndex = 0;
static uint8_t g_payload[AUDIO_PACKET_FRAMES * 4];
static size_t g_payloadLen = 0;
static size_t g_payloadIndex = 0;

static volatile uint16_t g_expectedSeq = 0;
static volatile uint32_t g_lastPacketMs = 0;
static volatile bool g_sessionStarted = false;

// Telemetry counters. Single-writer in normal operation except underruns,
// which are consumer-side. 32-bit loads/stores are atomic on ESP32-S3.
static volatile uint32_t g_packetsAccepted = 0;
static volatile uint32_t g_duplicatePackets = 0;
static volatile uint32_t g_sequenceErrors = 0;
static volatile uint32_t g_missingPackets = 0;
static volatile uint32_t g_crcErrors = 0;
static volatile uint32_t g_parserResets = 0;
static volatile uint32_t g_bufferOverwrites = 0;
static volatile uint32_t g_underruns = 0;
static volatile uint32_t g_concealmentFrames = 0;

// Last accepted PCM sample for packet-loss concealment. These are producer-side
// values updated only by the RX task while pushing accepted DATA packets.
static int16_t g_lastGoodLeft = 0;
static int16_t g_lastGoodRight = 0;

static TaskHandle_t g_audioRxTaskHandle = nullptr;
static uint32_t g_lastStatusMs = 0;

static inline size_t ringCount() {
  return static_cast<uint32_t>(g_ringHead - g_ringTail);
}

static void clearRingAndSession() {
  g_ringHead = 0;
  g_ringTail = 0;
  g_maxRingCount = 0;

  g_expectedSeq = 0;
  g_lastPacketMs = 0;
  g_sessionStarted = false;

  g_packetsAccepted = 0;
  g_duplicatePackets = 0;
  g_sequenceErrors = 0;
  g_missingPackets = 0;
  g_crcErrors = 0;
  g_parserResets = 0;
  g_bufferOverwrites = 0;
  g_underruns = 0;
  g_concealmentFrames = 0;
  g_lastGoodLeft = 0;
  g_lastGoodRight = 0;
}

static inline bool ringPush(int16_t left, int16_t right) {
  const uint32_t head = g_ringHead;
  const uint32_t tail = g_ringTail;
  const uint32_t count = head - tail;

  if (count >= AUDIO_BUFFER_FRAMES) {
    g_bufferOverwrites++;
    return false;
  }

  g_ring[head & AUDIO_RING_MASK].left = left;
  g_ring[head & AUDIO_RING_MASK].right = right;
  g_ringHead = head + 1;

  const size_t newCount = static_cast<size_t>(count + 1);
  if (newCount > g_maxRingCount) {
    g_maxRingCount = newCount;
  }
  return true;
}

static void ringPushConcealment(size_t frames) {
  // Preserve the audio timeline when DATA packets are lost, but avoid the
  // obvious click of stepping instantly from the last real sample to zero.
  // This is intentionally a short fade-to-center, followed by center samples
  // for the rest of the missing interval. It is much less objectionable than
  // replaying later packets early, which makes the song sound sped up.
  const int16_t startL = g_lastGoodLeft;
  const int16_t startR = g_lastGoodRight;
  const size_t fadeFrames = (frames < AUDIO_CONCEAL_FADE_FRAMES) ? frames : AUDIO_CONCEAL_FADE_FRAMES;

  for (size_t i = 0; i < fadeFrames; ++i) {
    const float t = static_cast<float>(i + 1) / static_cast<float>(fadeFrames);
    const float gain = 1.0f - t;
    const int16_t l = static_cast<int16_t>(static_cast<float>(startL) * gain);
    const int16_t r = static_cast<int16_t>(static_cast<float>(startR) * gain);
    ringPush(l, r);
  }

  for (size_t i = fadeFrames; i < frames; ++i) {
    ringPush(0, 0);
  }

  g_lastGoodLeft = 0;
  g_lastGoodRight = 0;
  g_concealmentFrames += static_cast<uint32_t>(frames);
}

static uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint32_t mask = static_cast<uint32_t>(-static_cast<int32_t>(crc & 1U));
      crc = (crc >> 1) ^ (0xEDB88320UL & mask);
    }
  }
  return crc;
}

static uint32_t packetCrc32(uint8_t type,
                            uint8_t flags,
                            uint16_t seq,
                            uint16_t frameCount,
                            const uint8_t* payload,
                            size_t payloadLen) {
  uint8_t hdr[6];
  hdr[0] = type;
  hdr[1] = flags;
  hdr[2] = static_cast<uint8_t>(seq & 0xFF);
  hdr[3] = static_cast<uint8_t>((seq >> 8) & 0xFF);
  hdr[4] = static_cast<uint8_t>(frameCount & 0xFF);
  hdr[5] = static_cast<uint8_t>((frameCount >> 8) & 0xFF);

  uint32_t crc = 0xFFFFFFFFUL;
  crc = crc32Update(crc, hdr, sizeof(hdr));
  if (payloadLen > 0) {
    crc = crc32Update(crc, payload, payloadLen);
  }
  return ~crc;
}

static void sendStatusLine(const char* tag) {
  Serial.printf(
      "%s fill=%u max=%u acc=%lu dup=%lu seqerr=%lu miss=%lu crc=%lu pres=%lu over=%lu und=%lu conceal=%lu exp=%u active=%u\n",
      tag,
      static_cast<unsigned>(ringCount()),
      static_cast<unsigned>(g_maxRingCount),
      static_cast<unsigned long>(g_packetsAccepted),
      static_cast<unsigned long>(g_duplicatePackets),
      static_cast<unsigned long>(g_sequenceErrors),
      static_cast<unsigned long>(g_missingPackets),
      static_cast<unsigned long>(g_crcErrors),
      static_cast<unsigned long>(g_parserResets),
      static_cast<unsigned long>(g_bufferOverwrites),
      static_cast<unsigned long>(g_underruns),
      static_cast<unsigned long>(g_concealmentFrames),
      static_cast<unsigned>(g_expectedSeq),
      audioStreamIsActive() ? 1U : 0U);
}

static void sendAckLine(const char* kind, uint16_t seq) {
  Serial.printf("ACK %s seq=%u fill=%u rate=%lu pkt=%u\n",
                kind,
                static_cast<unsigned>(seq),
                static_cast<unsigned>(ringCount()),
                static_cast<unsigned long>(AUDIO_SAMPLE_RATE),
                static_cast<unsigned>(AUDIO_PACKET_FRAMES));
}

static void maybeSendStatus(bool force) {
  if (!g_sessionStarted && !force) {
    return;
  }

  const uint32_t now = millis();
  if (force || (now - g_lastStatusMs) >= AUDIO_STATUS_PERIOD_MS) {
    g_lastStatusMs = now;
    sendStatusLine("STAT");
  }
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

static bool validatePacketCrc(uint8_t type,
                              uint8_t flags,
                              uint16_t seq,
                              uint16_t frameCount,
                              uint32_t rxCrc) {
  const uint32_t calc = packetCrc32(type, flags, seq, frameCount, g_payload, g_payloadLen);
  if (calc != rxCrc) {
    g_crcErrors++;
    return false;
  }
  return true;
}

static void handleStartPacket(uint16_t seq) {
  if (g_payloadLen != AUDIO_START_PAYLOAD_LEN) {
    resetParserCounted();
    return;
  }

  const uint32_t sampleRate =
      static_cast<uint32_t>(g_payload[0]) |
      (static_cast<uint32_t>(g_payload[1]) << 8) |
      (static_cast<uint32_t>(g_payload[2]) << 16) |
      (static_cast<uint32_t>(g_payload[3]) << 24);
  const uint16_t packetFrames =
      static_cast<uint16_t>(g_payload[4]) |
      (static_cast<uint16_t>(g_payload[5]) << 8);
  const uint8_t channels = g_payload[6];
  const uint8_t sampleBytes = g_payload[7];

  if (sampleRate != AUDIO_SAMPLE_RATE ||
      packetFrames != AUDIO_PACKET_FRAMES ||
      channels != AUDIO_STREAM_CHANNELS ||
      sampleBytes != AUDIO_STREAM_SAMPLE_BYTES) {
    Serial.printf("ERR START seq=%u fill=%u rate=%lu pkt=%u\n",
                  static_cast<unsigned>(seq),
                  static_cast<unsigned>(ringCount()),
                  static_cast<unsigned long>(AUDIO_SAMPLE_RATE),
                  static_cast<unsigned>(AUDIO_PACKET_FRAMES));
    resetParserNoCount();
    return;
  }

  clearRingAndSession();
  g_sessionStarted = true;
  g_expectedSeq = 0;
  g_lastPacketMs = millis();
  g_lastStatusMs = 0;

  sendAckLine("START", seq);
  maybeSendStatus(true);
  resetParserNoCount();
}

static void handleStopPacket(uint16_t seq) {
  clearRingAndSession();
  sendAckLine("STOP", seq);
  resetParserNoCount();
}

static bool sequenceIsOlder(uint16_t seq, uint16_t expected) {
  return static_cast<uint16_t>(expected - seq) < 0x8000U;
}

static void handleCompleteDataPacket(uint16_t seq, uint16_t frameCount) {
  if (!g_sessionStarted) {
    g_sequenceErrors++;
    resetParserNoCount();
    return;
  }

  const uint16_t expected = g_expectedSeq;
  const uint16_t forwardGap = static_cast<uint16_t>(seq - expected);

  if (seq != expected) {
    if (sequenceIsOlder(seq, expected)) {
      // Old/stale packet from the host or USB stack. Ignore it without
      // touching playback time.
      g_duplicatePackets++;
      g_lastPacketMs = millis();
      resetParserNoCount();
      return;
    }

    // Future sequence number means one or more DATA packets were lost or
    // rejected by CRC before this valid packet arrived. Accepting this packet
    // without occupying the missing time compresses the song timeline and is
    // the direct cause of the v5 "fast burst" symptom. Instead, insert bounded
    // fade-to-center concealment for the missing packet duration, then queue
    // the current packet at the correct time position.
    g_sequenceErrors++;
    g_missingPackets += forwardGap;

    const uint16_t packetsToConceal =
        (forwardGap > AUDIO_MAX_RECOVER_GAP_PACKETS)
          ? AUDIO_MAX_RECOVER_GAP_PACKETS
          : forwardGap;
    ringPushConcealment(static_cast<size_t>(packetsToConceal) * AUDIO_PACKET_FRAMES);
  }

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

    if (ringPush(left, right)) {
      g_lastGoodLeft = left;
      g_lastGoodRight = right;
    }
  }

  g_packetsAccepted++;
  g_expectedSeq = static_cast<uint16_t>(seq + 1);
  g_lastPacketMs = millis();

  maybeSendStatus(false);
  resetParserNoCount();
}

static void handlePacketComplete() {
  const uint8_t type = g_header[0];
  const uint8_t flags = g_header[1];
  const uint16_t seq =
      static_cast<uint16_t>(g_header[2]) |
      (static_cast<uint16_t>(g_header[3]) << 8);
  const uint16_t frameCount =
      static_cast<uint16_t>(g_header[4]) |
      (static_cast<uint16_t>(g_header[5]) << 8);
  const uint32_t rxCrc =
      static_cast<uint32_t>(g_header[6]) |
      (static_cast<uint32_t>(g_header[7]) << 8) |
      (static_cast<uint32_t>(g_header[8]) << 16) |
      (static_cast<uint32_t>(g_header[9]) << 24);

  if (!validatePacketCrc(type, flags, seq, frameCount, rxCrc)) {
    resetParserNoCount();
    return;
  }

  switch (type) {
    case AUDIO_PKT_TYPE_START:
      handleStartPacket(seq);
      break;

    case AUDIO_PKT_TYPE_STOP:
      handleStopPacket(seq);
      break;

    case AUDIO_PKT_TYPE_DATA_S16:
      handleCompleteDataPacket(seq, frameCount);
      break;

    default:
      resetParserCounted();
      break;
  }
}

static void handleHeaderComplete() {
  const uint8_t type = g_header[0];
  const uint16_t frameCount =
      static_cast<uint16_t>(g_header[4]) |
      (static_cast<uint16_t>(g_header[5]) << 8);

  switch (type) {
    case AUDIO_PKT_TYPE_START:
      if (frameCount != 0) {
        resetParserCounted();
      } else {
        g_payloadLen = AUDIO_START_PAYLOAD_LEN;
        g_payloadIndex = 0;
        g_parseState = ParseState::READ_PAYLOAD;
      }
      break;

    case AUDIO_PKT_TYPE_STOP:
      if (frameCount != 0) {
        resetParserCounted();
      } else {
        g_payloadLen = 0;
        g_payloadIndex = 0;
        handlePacketComplete();
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
      if (g_payloadIndex < sizeof(g_payload)) {
        g_payload[g_payloadIndex++] = b;
      } else {
        resetParserCounted();
        return;
      }

      if (g_payloadIndex >= g_payloadLen) {
        handlePacketComplete();
      }
      break;
  }
}

static void audioRxTask(void* arg) {
  (void)arg;
  static uint8_t rxBuf[1024];

  for (;;) {
    maybeSendStatus(false);

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
        6144,
        nullptr,
        4,
        &g_audioRxTaskHandle,
        appCore);
  }
}

void audioStreamReset() {
  clearRingAndSession();
  resetParserNoCount();
}

void audioStreamPollSerial() {
  // No-op: RX is handled by the dedicated audioRxTask().
}

size_t audioStreamAvailableFrames() {
  return ringCount();
}

bool audioStreamPopFrame(int16_t& left, int16_t& right) {
  const uint32_t tail = g_ringTail;
  const uint32_t head = g_ringHead;

  if (head == tail) {
    g_underruns++;
    return false;
  }

  const StereoFrame f = g_ring[tail & AUDIO_RING_MASK];
  g_ringTail = tail + 1;

  left = f.left;
  right = f.right;
  return true;
}

bool audioStreamIsActive() {
  if (!g_sessionStarted) {
    return false;
  }

  if (audioStreamAvailableFrames() > 0) {
    return true;
  }

  if (g_lastPacketMs == 0) {
    return false;
  }

  return (millis() - g_lastPacketMs) <= AUDIO_STREAM_ACTIVE_TIMEOUT_MS;
}

void audioStreamGetStats(AudioStreamStats& out) {
  out.currentFill = audioStreamAvailableFrames();
  out.maxFill = g_maxRingCount;
  out.packetsAccepted = g_packetsAccepted;
  out.duplicatePackets = g_duplicatePackets;
  out.sequenceErrors = g_sequenceErrors;
  out.missingPackets = g_missingPackets;
  out.crcErrors = g_crcErrors;
  out.parserResets = g_parserResets;
  out.bufferOverwrites = g_bufferOverwrites;
  out.underruns = g_underruns;
  out.expectedSeq = g_expectedSeq;
  out.sessionStarted = g_sessionStarted;
  out.active = audioStreamIsActive();
}
