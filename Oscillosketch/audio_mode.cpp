#include "audio_mode.h"
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
// Source presets
// =====================================================

enum class AudioPreset : uint8_t {
  NOISY_COMPOSITE = 0,
  SMOOTH_LISSAJOUS = 1,
  HARMONIC_RICH = 2
};

static AudioPreset g_preset = AudioPreset::NOISY_COMPOSITE;

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

static uint32_t g_lastAudioUpdateMs = 0;
static constexpr float AUDIO_SYNTH_SAMPLE_RATE =
    (1000.0f * static_cast<float>(AUDIO_FRAME_MAX_POINTS)) /
    static_cast<float>(AUDIO_FRAME_UPDATE_MS);

static constexpr float AUDIO_CENTER_CODE_F = 0.5f * (DRAW_MIN_CODE + DRAW_MAX_CODE);
static constexpr float AUDIO_HALF_SPAN_F =
    0.45f * (DRAW_MAX_CODE - DRAW_MIN_CODE);

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

static void updateFilterCoefficients() {
  if (g_hpfHz > g_lpfHz) {
    g_hpfHz = g_lpfHz;
  }

  const float dt = 1.0f / AUDIO_SYNTH_SAMPLE_RATE;

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

static void getPresetSample(float& left, float& right) {
  const float dt = 1.0f / AUDIO_SYNTH_SAMPLE_RATE;

  switch (g_preset) {
    case AudioPreset::NOISY_COMPOSITE: {
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

    case AudioPreset::SMOOTH_LISSAJOUS: {
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

    case AudioPreset::HARMONIC_RICH: {
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
  }
}

static void buildAudioFrame() {
  frameClear();

  if (fabsf(g_lpfHz - g_hpfHz) < 1.0f) {
    frameMoveToNextPrimitive();
    framePush(DAC_CENTER_CODE, DAC_CENTER_CODE);
    drawingSetAudioFrame(g_audioPts, g_blankBefore, g_audioPtCount);
    return;
  }

  frameMoveToNextPrimitive();

  for (size_t i = 0; i < AUDIO_FRAME_MAX_POINTS; ++i) {
    float xL = 0.0f;
    float xR = 0.0f;
    getPresetSample(xL, xR);

    xL = updateHPF(g_hpfL, xL);
    xR = updateHPF(g_hpfR, xR);

    xL = updateLPF(g_lpfL, xL);
    xR = updateLPF(g_lpfR, xR);

    xL = clampf(xL, -1.0f, 1.0f);
    xR = clampf(xR, -1.0f, 1.0f);

    const int32_t dacX = static_cast<int32_t>(lroundf(AUDIO_CENTER_CODE_F + AUDIO_HALF_SPAN_F * xL));
    const int32_t dacY = static_cast<int32_t>(lroundf(AUDIO_CENTER_CODE_F + AUDIO_HALF_SPAN_F * xR));

    framePush(
      static_cast<uint16_t>(clampf(static_cast<float>(dacX), DRAW_MIN_CODE, DRAW_MAX_CODE)),
      static_cast<uint16_t>(clampf(static_cast<float>(dacY), DRAW_MIN_CODE, DRAW_MAX_CODE))
    );
  }

  drawingSetAudioFrame(g_audioPts, g_blankBefore, g_audioPtCount);
}

static void cyclePreset() {
  switch (g_preset) {
    case AudioPreset::NOISY_COMPOSITE:
      g_preset = AudioPreset::SMOOTH_LISSAJOUS;
      break;
    case AudioPreset::SMOOTH_LISSAJOUS:
      g_preset = AudioPreset::HARMONIC_RICH;
      break;
    case AudioPreset::HARMONIC_RICH:
      g_preset = AudioPreset::NOISY_COMPOSITE;
      break;
  }

  g_phaseA = 0.0f;
  g_phaseB = 0.0f;
  g_phaseC = 0.0f;
  g_phaseD = 0.0f;
  resetFilterState();
  updateFilterCoefficients();
}

void audioBegin() {
  g_preset = AudioPreset::NOISY_COMPOSITE;
  g_hpfHz = AUDIO_HPF_MIN_HZ;
  g_lpfHz = AUDIO_LPF_MAX_HZ;
  g_phaseA = 0.0f;
  g_phaseB = 0.0f;
  g_phaseC = 0.0f;
  g_phaseD = 0.0f;
  g_lastAudioUpdateMs = millis();

  resetFilterState();
  updateFilterCoefficients();
  buildAudioFrame();
}

void audioOnEnter() {
  buildAudioFrame();
}

void audioUpdate(const InputSnapshot& in) {
  adjustCutoffs(in);

  if (in.resetPressedEdge) {
    cyclePreset();
    buildAudioFrame();
    g_lastAudioUpdateMs = millis();
    return;
  }

  const uint32_t now = millis();
  if ((now - g_lastAudioUpdateMs) < AUDIO_FRAME_UPDATE_MS) {
    return;
  }
  g_lastAudioUpdateMs = now;

  buildAudioFrame();
}