#include "audio_mode.h"
#include "audio_stream.h"
#include "drawing_engine.h"
#include "config.h"
#include <math.h>

// =====================================================
// Frame builder
// =====================================================

static XYPoint g_audioPts[AUDIO_FRAME_MAX_POINTS];
static bool g_blankBefore[AUDIO_FRAME_MAX_POINTS];
static size_t g_audioPtCount = 0;
static bool g_nextPointStartsBlanked = true;

static inline void frameClear() {
  g_audioPtCount = 0;
  g_nextPointStartsBlanked = true;
}

static inline void framePush(uint16_t x, uint16_t y) {
  if (g_audioPtCount < AUDIO_FRAME_MAX_POINTS) {
    g_audioPts[g_audioPtCount] = { x, y };
    g_blankBefore[g_audioPtCount] = g_nextPointStartsBlanked;
    g_nextPointStartsBlanked = false;
    g_audioPtCount++;
  }
}

static inline void frameMoveToNextPrimitive() {
  g_nextPointStartsBlanked = true;
}

// =====================================================
// Source selection
// =====================================================

enum class AudioSource : uint8_t {
  NOISY_COMPOSITE = 0,
  SMOOTH_LISSAJOUS = 1,
  HARMONIC_RICH = 2,
  LIVE_SERIAL = 3
};

static AudioSource g_source = AudioSource::NOISY_COMPOSITE;

enum class LivePlaybackState : uint8_t {
  HOLDING = 0,
  RUNNING = 1
};

static LivePlaybackState g_liveState = LivePlaybackState::HOLDING;

// =====================================================
// Filter state
// =====================================================

struct FirstOrderLPF {
  float alpha = 1.0f;
  float y = 0.0f;
};

struct FirstOrderHPF {
  float alpha = 0.0f;
  float prevX = 0.0f;
  float prevY = 0.0f;
};

static FirstOrderLPF g_lpfL;
static FirstOrderLPF g_lpfR;
static FirstOrderHPF g_hpfL;
static FirstOrderHPF g_hpfR;

static float g_hpfHz = AUDIO_HPF_MIN_HZ;
static float g_lpfHz = AUDIO_LPF_MAX_HZ;

// =====================================================
// Signal generation state
// =====================================================

static float g_phaseA = 0.0f;
static float g_phaseB = 0.0f;
static float g_phaseC = 0.0f;
static float g_phaseD = 0.0f;

// =====================================================
// Timing / rendering
// =====================================================

static uint32_t g_lastAudioBlockUs = 0;

static constexpr float AUDIO_CENTER_CODE_F =
    0.5f * (AUDIO_DRAW_MIN_CODE + AUDIO_DRAW_MAX_CODE);
static constexpr float AUDIO_HALF_SPAN_F =
    0.45f * (AUDIO_DRAW_MAX_CODE - AUDIO_DRAW_MIN_CODE);

// =====================================================
// Helpers
// =====================================================

static inline float clampf(float x, float lo, float hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

static inline float wrapPhase(float p) {
  const float twoPi = 2.0f * PI;
  while (p >= twoPi) p -= twoPi;
  while (p < 0.0f) p += twoPi;
  return p;
}

static float updateLPF(FirstOrderLPF& f, float x) {
  f.y = f.y + f.alpha * (x - f.y);
  return f.y;
}

static float updateHPF(FirstOrderHPF& f, float x) {
  const float y = f.alpha * (f.prevY + x - f.prevX);
  f.prevX = x;
  f.prevY = y;
  return y;
}

static void resetFilterState() {
  g_lpfL.y = 0.0f;
  g_lpfR.y = 0.0f;

  g_hpfL.prevX = 0.0f;
  g_hpfL.prevY = 0.0f;
  g_hpfR.prevX = 0.0f;
  g_hpfR.prevY = 0.0f;
}

static void resetPresetPhases() {
  g_phaseA = 0.0f;
  g_phaseB = 0.0f;
  g_phaseC = 0.0f;
  g_phaseD = 0.0f;
}

static void updateFilterCoefficients() {
  if (g_hpfHz > g_lpfHz) {
    g_hpfHz = g_lpfHz;
  }

  const float dt = 1.0f / static_cast<float>(AUDIO_SAMPLE_RATE);

  {
    const float rc = 1.0f / (2.0f * PI * g_lpfHz);
    const float alpha = dt / (rc + dt);
    g_lpfL.alpha = alpha;
    g_lpfR.alpha = alpha;
  }

  {
    const float rc = 1.0f / (2.0f * PI * g_hpfHz);
    const float alpha = rc / (rc + dt);
    g_hpfL.alpha = alpha;
    g_hpfR.alpha = alpha;
  }
}

static void adjustCutoffs(const InputSnapshot& in) {
  int32_t lpfDelta = in.leftEncoderDelta;
  int32_t hpfDelta = in.rightEncoderDelta;

  if (INVERT_AUDIO_LPF_ENCODER) lpfDelta = -lpfDelta;
  if (INVERT_AUDIO_HPF_ENCODER) hpfDelta = -hpfDelta;

  if (lpfDelta != 0) {
    g_lpfHz *= powf(1.02f, static_cast<float>(lpfDelta));
    g_lpfHz = clampf(g_lpfHz, AUDIO_LPF_MIN_HZ, AUDIO_LPF_MAX_HZ);
  }

  if (hpfDelta != 0) {
    g_hpfHz *= powf(1.02f, static_cast<float>(hpfDelta));
    g_hpfHz = clampf(g_hpfHz, AUDIO_HPF_MIN_HZ, AUDIO_HPF_MAX_HZ);
  }

  if (g_hpfHz > g_lpfHz) {
    g_hpfHz = g_lpfHz;
  }

  updateFilterCoefficients();
}

static void cycleSource() {
  switch (g_source) {
    case AudioSource::NOISY_COMPOSITE:
      g_source = AudioSource::SMOOTH_LISSAJOUS;
      break;
    case AudioSource::SMOOTH_LISSAJOUS:
      g_source = AudioSource::HARMONIC_RICH;
      break;
    case AudioSource::HARMONIC_RICH:
      g_source = AudioSource::LIVE_SERIAL;
      break;
    case AudioSource::LIVE_SERIAL:
      g_source = AudioSource::NOISY_COMPOSITE;
      break;
  }

  g_liveState = LivePlaybackState::HOLDING;
  resetPresetPhases();
  resetFilterState();
  updateFilterCoefficients();
}

static void getPresetSample(AudioSource src, float& left, float& right) {
  const float dt = 1.0f / static_cast<float>(AUDIO_SAMPLE_RATE);

  switch (src) {
    case AudioSource::NOISY_COMPOSITE: {
      const float f1 = 180.0f;
      const float f2 = 730.0f;
      const float f3 = 2600.0f;
      const float f4 = 6200.0f;

      left =
          0.60f * sinf(g_phaseA) +
          0.22f * sinf(g_phaseB) +
          0.12f * sinf(g_phaseC) +
          0.06f * sinf(g_phaseD);

      right =
          0.60f * sinf(g_phaseA + 0.9f) +
          0.20f * sinf(g_phaseB + 0.4f) +
          0.14f * sinf(g_phaseC + 1.3f) +
          0.06f * sinf(g_phaseD + 0.2f);

      g_phaseA = wrapPhase(g_phaseA + 2.0f * PI * f1 * dt);
      g_phaseB = wrapPhase(g_phaseB + 2.0f * PI * f2 * dt);
      g_phaseC = wrapPhase(g_phaseC + 2.0f * PI * f3 * dt);
      g_phaseD = wrapPhase(g_phaseD + 2.0f * PI * f4 * dt);
      break;
    }

    case AudioSource::SMOOTH_LISSAJOUS: {
      const float f1 = 220.0f;
      const float f2 = 440.0f;

      left =
          0.78f * sinf(g_phaseA) +
          0.22f * sinf(g_phaseB);

      right =
          0.78f * sinf(g_phaseA + PI / 2.0f) +
          0.22f * sinf(g_phaseB + 0.35f);

      g_phaseA = wrapPhase(g_phaseA + 2.0f * PI * f1 * dt);
      g_phaseB = wrapPhase(g_phaseB + 2.0f * PI * f2 * dt);
      break;
    }

    case AudioSource::HARMONIC_RICH: {
      const float f = 210.0f;

      left =
          0.72f * sinf(g_phaseA) +
          0.24f * sinf(3.0f * g_phaseA) +
          0.14f * sinf(5.0f * g_phaseA) +
          0.08f * sinf(7.0f * g_phaseA);

      right =
          0.72f * sinf(g_phaseA + 0.7f) +
          0.24f * sinf(3.0f * g_phaseA + 0.2f) +
          0.14f * sinf(5.0f * g_phaseA + 1.0f) +
          0.08f * sinf(7.0f * g_phaseA + 0.5f);

      g_phaseA = wrapPhase(g_phaseA + 2.0f * PI * f * dt);
      break;
    }

    case AudioSource::LIVE_SERIAL:
      left = 0.0f;
      right = 0.0f;
      break;
  }
}

static void updateLivePlaybackState() {
  if (g_source != AudioSource::LIVE_SERIAL) {
    g_liveState = LivePlaybackState::HOLDING;
    return;
  }

  const bool active = audioStreamIsActive();
  const size_t fill = audioStreamAvailableFrames();

  switch (g_liveState) {
    case LivePlaybackState::HOLDING:
      if (active && fill >= AUDIO_LIVE_START_FILL_FRAMES) {
        g_liveState = LivePlaybackState::RUNNING;
      }
      break;

    case LivePlaybackState::RUNNING:
      if (!active || fill < AUDIO_LIVE_REBUFFER_LOW_FRAMES) {
        g_liveState = LivePlaybackState::HOLDING;
      }
      break;
  }
}

static bool fetchFilteredSourceBlock(float* outL, float* outR) {
  bool hadLiveFrames = false;

  for (size_t i = 0; i < AUDIO_INPUT_BLOCK_FRAMES; ++i) {
    float xL = 0.0f;
    float xR = 0.0f;

    if (g_source == AudioSource::LIVE_SERIAL) {
      int16_t pcmL = 0;
      int16_t pcmR = 0;

      if (audioStreamPopFrame(pcmL, pcmR)) {
        xL = static_cast<float>(pcmL) / 32768.0f;
        xR = static_cast<float>(pcmR) / 32768.0f;
        hadLiveFrames = true;
      } else {
        xL = 0.0f;
        xR = 0.0f;
      }
    } else {
      getPresetSample(g_source, xL, xR);
    }

    xL = updateHPF(g_hpfL, xL);
    xR = updateHPF(g_hpfR, xR);
    xL = updateLPF(g_lpfL, xL);
    xR = updateLPF(g_lpfR, xR);

    outL[i] = clampf(xL, -1.0f, 1.0f);
    outR[i] = clampf(xR, -1.0f, 1.0f);
  }

  return hadLiveFrames;
}

static void buildAudioFrame() {
  frameClear();

  if (fabsf(g_lpfHz - g_hpfHz) < 1.0f) {
    frameMoveToNextPrimitive();
    framePush(DAC_CENTER_CODE, DAC_CENTER_CODE);
    drawingSetAudioFrame(g_audioPts, g_blankBefore, g_audioPtCount);
    return;
  }

  updateLivePlaybackState();

  if (g_source == AudioSource::LIVE_SERIAL && g_liveState != LivePlaybackState::RUNNING) {
    frameMoveToNextPrimitive();
    framePush(DAC_CENTER_CODE, DAC_CENTER_CODE);
    drawingSetAudioFrame(g_audioPts, g_blankBefore, g_audioPtCount);
    return;
  }

  float blockL[AUDIO_INPUT_BLOCK_FRAMES];
  float blockR[AUDIO_INPUT_BLOCK_FRAMES];
  const bool hadFrames = fetchFilteredSourceBlock(blockL, blockR);

  if (g_source == AudioSource::LIVE_SERIAL && !hadFrames) {
    g_liveState = LivePlaybackState::HOLDING;
    frameMoveToNextPrimitive();
    framePush(DAC_CENTER_CODE, DAC_CENTER_CODE);
    drawingSetAudioFrame(g_audioPts, g_blankBefore, g_audioPtCount);
    return;
  }

  frameMoveToNextPrimitive();

  if (AUDIO_FRAME_MAX_POINTS <= 1 || AUDIO_INPUT_BLOCK_FRAMES <= 1) {
    const int32_t dacX = static_cast<int32_t>(lroundf(AUDIO_CENTER_CODE_F + AUDIO_HALF_SPAN_F * blockL[0]));
    const int32_t dacY = static_cast<int32_t>(lroundf(AUDIO_CENTER_CODE_F + AUDIO_HALF_SPAN_F * blockR[0]));

    framePush(
      static_cast<uint16_t>(clampf(static_cast<float>(dacX), AUDIO_DRAW_MIN_CODE, AUDIO_DRAW_MAX_CODE)),
      static_cast<uint16_t>(clampf(static_cast<float>(dacY), AUDIO_DRAW_MIN_CODE, AUDIO_DRAW_MAX_CODE))
    );
  } else {
    for (size_t j = 0; j < AUDIO_FRAME_MAX_POINTS; ++j) {
      const float srcPos =
          (static_cast<float>(j) * static_cast<float>(AUDIO_INPUT_BLOCK_FRAMES - 1)) /
          static_cast<float>(AUDIO_FRAME_MAX_POINTS - 1);

      const size_t i0 = static_cast<size_t>(srcPos);
      const size_t i1 = (i0 + 1 < AUDIO_INPUT_BLOCK_FRAMES) ? (i0 + 1) : i0;
      const float frac = srcPos - static_cast<float>(i0);

      const float xL = blockL[i0] + frac * (blockL[i1] - blockL[i0]);
      const float xR = blockR[i0] + frac * (blockR[i1] - blockR[i0]);

      const int32_t dacX = static_cast<int32_t>(lroundf(AUDIO_CENTER_CODE_F + AUDIO_HALF_SPAN_F * xL));
      const int32_t dacY = static_cast<int32_t>(lroundf(AUDIO_CENTER_CODE_F + AUDIO_HALF_SPAN_F * xR));

      framePush(
        static_cast<uint16_t>(clampf(static_cast<float>(dacX), AUDIO_DRAW_MIN_CODE, AUDIO_DRAW_MAX_CODE)),
        static_cast<uint16_t>(clampf(static_cast<float>(dacY), AUDIO_DRAW_MIN_CODE, AUDIO_DRAW_MAX_CODE))
      );
    }
  }

  drawingSetAudioFrame(g_audioPts, g_blankBefore, g_audioPtCount);
}

void audioBegin() {
  g_source = AudioSource::NOISY_COMPOSITE;
  g_liveState = LivePlaybackState::HOLDING;
  g_hpfHz = AUDIO_HPF_MIN_HZ;
  g_lpfHz = AUDIO_LPF_MAX_HZ;
  g_lastAudioBlockUs = micros();

  resetPresetPhases();
  resetFilterState();
  updateFilterCoefficients();

  audioStreamBegin();
  buildAudioFrame();
}

void audioOnEnter() {
  g_liveState = LivePlaybackState::HOLDING;
  buildAudioFrame();
}

void audioUpdate(const InputSnapshot& in) {
  audioStreamPollSerial();  // no-op in the RX-task transport version

  adjustCutoffs(in);

  if (in.resetPressedEdge) {
    cycleSource();
    buildAudioFrame();
    g_lastAudioBlockUs = micros();
    return;
  }

  const uint32_t nowUs = micros();
  if ((uint32_t)(nowUs - g_lastAudioBlockUs) < AUDIO_BLOCK_PERIOD_US) {
    return;
  }

  g_lastAudioBlockUs += AUDIO_BLOCK_PERIOD_US;
  if ((uint32_t)(nowUs - g_lastAudioBlockUs) > AUDIO_BLOCK_PERIOD_US) {
    g_lastAudioBlockUs = nowUs;
  }

  buildAudioFrame();
}
