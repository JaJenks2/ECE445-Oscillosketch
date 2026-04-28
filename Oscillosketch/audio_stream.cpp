#include "audio_stream.h"
#include "config.h"
#include <Arduino.h>

namespace {

constexpr uint8_t AUDIO_PKT_MAGIC0 = 0xA5;
constexpr uint8_t AUDIO_PKT_MAGIC1 = 0x5A;
constexpr uint8_t AUDIO_PKT_TYPE_PCM_STEREO_S16 = 0x01;

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

static StereoFrame g_ring[AUDIO_BUFFER_FRAMES];
static size_t g_ringHead = 0;
static size_t g_ringTail = 0;
static size_t g_ringCount = 0;

static ParseState g_parseState = ParseState::WAIT_MAGIC0;
static uint8_t g_header[6];
static size_t g_headerIndex = 0;
static uint8_t g_payload[AUDIO_PACKET_FRAMES * 4];
static size_t g_payloadLen = 0;
static size_t g_payloadIndex = 0;

static uint16_t g_lastSeq = 0;
static bool g_haveSeq = false;
static uint32_t g_lastPacketMs = 0;

static inline void ringPush(int16_t left, int16_t right) {
  if (g_ringCount >= AUDIO_BUFFER_FRAMES) {
    // Overwrite oldest to keep latency bounded.
    g_ringTail = (g_ringTail + 1) % AUDIO_BUFFER_FRAMES;
    g_ringCount--;
  }

  g_ring[g_ringHead].left = left;
  g_ring[g_ringHead].right = right;
  g_ringHead = (g_ringHead + 1) % AUDIO_BUFFER_FRAMES;
  g_ringCount++;
}

static void resetParser() {
  g_parseState = ParseState::WAIT_MAGIC0;
  g_headerIndex = 0;
  g_payloadLen = 0;
  g_payloadIndex = 0;
}

static void handleCompletePacket() {
  const uint8_t type = g_header[0];
  const uint8_t flags = g_header[1];
  (void)flags;

  const uint16_t seq =
      static_cast<uint16_t>(g_header[2]) |
      (static_cast<uint16_t>(g_header[3]) << 8);

  const uint16_t frameCount =
      static_cast<uint16_t>(g_header[4]) |
      (static_cast<uint16_t>(g_header[5]) << 8);

  if (type != AUDIO_PKT_TYPE_PCM_STEREO_S16) {
    resetParser();
    return;
  }

  if (frameCount == 0 || frameCount > AUDIO_PACKET_FRAMES) {
    resetParser();
    return;
  }

  const size_t expectedPayloadLen = static_cast<size_t>(frameCount) * 4;
  if (g_payloadLen != expectedPayloadLen) {
    resetParser();
    return;
  }

  // Sequence is currently tracked only for sanity/future debug.
  if (!g_haveSeq) {
    g_lastSeq = seq;
    g_haveSeq = true;
  } else {
    g_lastSeq = seq;
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

    ringPush(left, right);
  }

  g_lastPacketMs = millis();
  resetParser();
}

}  // namespace

void audioStreamBegin() {
  Serial.begin(AUDIO_SERIAL_BAUD);
  audioStreamReset();
}

void audioStreamReset() {
  g_ringHead = 0;
  g_ringTail = 0;
  g_ringCount = 0;
  g_haveSeq = false;
  g_lastSeq = 0;
  g_lastPacketMs = 0;
  resetParser();
}

void audioStreamPollSerial() {
  while (Serial.available() > 0) {
    const int raw = Serial.read();
    if (raw < 0) {
      return;
    }

    const uint8_t b = static_cast<uint8_t>(raw);

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
          const uint16_t frameCount =
              static_cast<uint16_t>(g_header[4]) |
              (static_cast<uint16_t>(g_header[5]) << 8);

          if (frameCount == 0 || frameCount > AUDIO_PACKET_FRAMES) {
            resetParser();
          } else {
            g_payloadLen = static_cast<size_t>(frameCount) * 4;
            g_payloadIndex = 0;
            g_parseState = ParseState::READ_PAYLOAD;
          }
        }
        break;

      case ParseState::READ_PAYLOAD:
        g_payload[g_payloadIndex++] = b;
        if (g_payloadIndex >= g_payloadLen) {
          handleCompletePacket();
        }
        break;
    }
  }
}

size_t audioStreamAvailableFrames() {
  return g_ringCount;
}

bool audioStreamPopFrame(int16_t& left, int16_t& right) {
  if (g_ringCount == 0) {
    return false;
  }

  left = g_ring[g_ringTail].left;
  right = g_ring[g_ringTail].right;

  g_ringTail = (g_ringTail + 1) % AUDIO_BUFFER_FRAMES;
  g_ringCount--;
  return true;
}

bool audioStreamIsActive() {
  if (g_lastPacketMs == 0) {
    return false;
  }
  return (millis() - g_lastPacketMs) <= AUDIO_STREAM_ACTIVE_TIMEOUT_MS;
}