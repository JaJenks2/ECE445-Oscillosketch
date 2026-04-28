#include "audio_mode.h"
#include "audio_stream.h"
#include "config.h"
#include <math.h>

namespace {

enum class AudioSource : uint8_t {
  NOISY_COMPOSITE = 0,
  SMOOTH_LISSAJOUS = 1,
  HARMONIC_RICH = 2,
  LIVE_SERIAL = 3
};

enum class LivePlaybackState : uint8_t {
  HOLDING = 0,
  RUNNING = 1
};

struct FirstOrderLPF {
  float alpha = 1.0f;
  float y = 0.0f;
};

struct FirstOrderHPF {
  float alpha = 0.0f;
  float prevX = 0.0f;
  float prevY = 0.0f;
};

struct AudioControlState {
  uint8_t source = static_cast<uint8_t>(AudioSource::NOISY_COMPOSITE);
  float hpfHz = AUDIO_HPF_MIN_HZ;
  float lpfHz = AUDIO_LPF_MAX_HZ;
  uint32_t resyncNonce = 0;
};

struct AudioPlaybackState {
  AudioSource source = AudioSource::NOISY_COMPOSITE;
  float hpfHz = AUDIO_HPF_MIN_HZ;
  float lpfHz = AUDIO_LPF_MAX_HZ;

  FirstOrderLPF lpfL;
  FirstOrderLPF lpfR;
  FirstOrderHPF hpfL;
  FirstOrderHPF hpfR;

  float phaseA = 0.0f;
  float phaseB = 0.0f;
  float phaseC = 0.0f;
  float phaseD = 0.0f;

  LivePlaybackState liveState = LivePlaybackState::HOLDING;

  bool primed = false;
  float prevL = 0.0f;
  float prevR = 0.0f;
  float nextL = 0.0f;
  float nextR = 0.0f;
  float frac = 0.0f;

  uint32_t appliedGeneration = 0;
  uint32_t appliedResyncNonce = 0;
};

static portMUX_TYPE g_audioCtrlMux = portMUX_INITIALIZER_UNLOCKED;

// UI / app-core owned desired settings.
static AudioSource g_uiSource = AudioSource::LIVE_SERIAL;
static float g_uiHpfHz = AUDIO_HPF_MIN_HZ;
static float g_uiLpfHz = AUDIO_LPF_MAX_HZ;
static uint32_t g_uiResyncNonce = 0;

// Shared desired control block; replay core copies this only when generation changes.
static AudioControlState g_ctrl;
static volatile uint32_t g_ctrlGeneration = 0;

// Replay-core owned playback state.
static AudioPlaybackState g_pb;

static constexpr float AUDIO_CENTER_CODE_F = static_cast<float>(DAC_CENTER_CODE);
static constexpr float AUDIO_NEG_SPAN_F = static_cast<float>(DAC_CENTER_CODE - AUDIO_DRAW_MIN_CODE);
static constexpr float AUDIO_POS_SPAN_F = static_cast<float>(AUDIO_DRAW_MAX_CODE - DAC_CENTER_CODE);
static constexpr float AUDIO_HALF_SPAN_F =
    0.90f * ((AUDIO_NEG_SPAN_F < AUDIO_POS_SPAN_F) ? AUDIO_NEG_SPAN_F : AUDIO_POS_SPAN_F);
static constexpr float AUDIO_PHASE_INCREMENT =
    static_cast<float>(AUDIO_SAMPLE_RATE) / static_cast<float>(REPLAY_RATE_HZ);

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

static inline uint16_t clampAudioCode(int32_t v) {
  if (v < AUDIO_DRAW_MIN_CODE) return AUDIO_DRAW_MIN_CODE;
  if (v > AUDIO_DRAW_MAX_CODE) return AUDIO_DRAW_MAX_CODE;
  return static_cast<uint16_t>(v);
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

static void resetFilterState(FirstOrderLPF& lpfL,
                             FirstOrderLPF& lpfR,
                             FirstOrderHPF& hpfL,
                             FirstOrderHPF& hpfR) {
  lpfL.y = 0.0f;
  lpfR.y = 0.0f;

  hpfL.prevX = 0.0f;
  hpfL.prevY = 0.0f;
  hpfR.prevX = 0.0f;
  hpfR.prevY = 0.0f;
}

static void updateFilterCoefficients(AudioPlaybackState& st) {
  if (st.hpfHz > st.lpfHz) {
    st.hpfHz = st.lpfHz;
  }

  const float dt = 1.0f / static_cast<float>(AUDIO_SAMPLE_RATE);

  {
    const float rc = 1.0f / (2.0f * PI * st.lpfHz);
    const float alpha = dt / (rc + dt);
    st.lpfL.alpha = alpha;
    st.lpfR.alpha = alpha;
  }

  {
    const float rc = 1.0f / (2.0f * PI * st.hpfHz);
    const float alpha = rc / (rc + dt);
    st.hpfL.alpha = alpha;
    st.hpfR.alpha = alpha;
  }
}

static void resetPresetPhases(AudioPlaybackState& st) {
  st.phaseA = 0.0f;
  st.phaseB = 0.0f;
  st.phaseC = 0.0f;
  st.phaseD = 0.0f;
}

static void publishControls(bool forceResync) {
  if (forceResync) {
    ++g_uiResyncNonce;
  }

  portENTER_CRITICAL(&g_audioCtrlMux);
  g_ctrl.source = static_cast<uint8_t>(g_uiSource);
  g_ctrl.hpfHz = g_uiHpfHz;
  g_ctrl.lpfHz = g_uiLpfHz;
  g_ctrl.resyncNonce = g_uiResyncNonce;
  ++g_ctrlGeneration;
  portEXIT_CRITICAL(&g_audioCtrlMux);
}

static void resetPlaybackState(AudioPlaybackState& st) {
  resetFilterState(st.lpfL, st.lpfR, st.hpfL, st.hpfR);
  updateFilterCoefficients(st);
  resetPresetPhases(st);

  st.liveState = (st.source == AudioSource::LIVE_SERIAL)
                   ? LivePlaybackState::HOLDING
                   : LivePlaybackState::RUNNING;

  st.primed = false;
  st.prevL = 0.0f;
  st.prevR = 0.0f;
  st.nextL = 0.0f;
  st.nextR = 0.0f;
  st.frac = 0.0f;
}

static void applyPendingControlsIfNeeded() {
  const uint32_t gen = g_ctrlGeneration;
  if (gen == g_pb.appliedGeneration) {
    return;
  }

  AudioControlState local;
  uint32_t localGen = 0;
  portENTER_CRITICAL(&g_audioCtrlMux);
  local = g_ctrl;
  localGen = g_ctrlGeneration;
  portEXIT_CRITICAL(&g_audioCtrlMux);

  const AudioSource newSource = static_cast<AudioSource>(local.source);
  const bool sourceChanged = (newSource != g_pb.source);
  const bool resync = (local.resyncNonce != g_pb.appliedResyncNonce);
  const bool cutoffChanged = (local.hpfHz != g_pb.hpfHz) || (local.lpfHz != g_pb.lpfHz);

  g_pb.source = newSource;
  g_pb.hpfHz = local.hpfHz;
  g_pb.lpfHz = local.lpfHz;
  g_pb.appliedResyncNonce = local.resyncNonce;
  g_pb.appliedGeneration = localGen;

  if (sourceChanged || resync) {
    resetPlaybackState(g_pb);
  } else if (cutoffChanged) {
    updateFilterCoefficients(g_pb);
  }
}

static bool liveReadyForSample(AudioPlaybackState& st) {
  const bool active = audioStreamIsActive();
  const size_t fill = audioStreamAvailableFrames();

  switch (st.liveState) {
    case LivePlaybackState::HOLDING:
      if (active && fill >= AUDIO_LIVE_START_FILL_FRAMES) {
        st.liveState = LivePlaybackState::RUNNING;
      } else {
        return false;
      }
      break;

    case LivePlaybackState::RUNNING:
      if (!active || fill < AUDIO_LIVE_REBUFFER_LOW_FRAMES) {
        st.liveState = LivePlaybackState::HOLDING;
        st.primed = false;
        return false;
      }
      break;
  }

  return true;
}

static void getPresetRawSample(AudioPlaybackState& st, float& left, float& right) {
  const float dt = 1.0f / static_cast<float>(AUDIO_SAMPLE_RATE);

  switch (st.source) {
    case AudioSource::NOISY_COMPOSITE: {
      const float f1 = 180.0f;
      const float f2 = 730.0f;
      const float f3 = 2600.0f;
      const float f4 = 6200.0f;

      left =
          0.60f * sinf(st.phaseA) +
          0.22f * sinf(st.phaseB) +
          0.12f * sinf(st.phaseC) +
          0.06f * sinf(st.phaseD);

      right =
          0.60f * sinf(st.phaseA + 0.9f) +
          0.20f * sinf(st.phaseB + 0.4f) +
          0.14f * sinf(st.phaseC + 1.3f) +
          0.06f * sinf(st.phaseD + 0.2f);

      st.phaseA = wrapPhase(st.phaseA + 2.0f * PI * f1 * dt);
      st.phaseB = wrapPhase(st.phaseB + 2.0f * PI * f2 * dt);
      st.phaseC = wrapPhase(st.phaseC + 2.0f * PI * f3 * dt);
      st.phaseD = wrapPhase(st.phaseD + 2.0f * PI * f4 * dt);
      break;
    }

    case AudioSource::SMOOTH_LISSAJOUS: {
      const float f1 = 220.0f;
      const float f2 = 440.0f;

      left =
          0.78f * sinf(st.phaseA) +
          0.22f * sinf(st.phaseB);

      right =
          0.78f * sinf(st.phaseA + PI / 2.0f) +
          0.22f * sinf(st.phaseB + 0.35f);

      st.phaseA = wrapPhase(st.phaseA + 2.0f * PI * f1 * dt);
      st.phaseB = wrapPhase(st.phaseB + 2.0f * PI * f2 * dt);
      break;
    }

    case AudioSource::HARMONIC_RICH: {
      const float f = 210.0f;

      left =
          0.72f * sinf(st.phaseA) +
          0.24f * sinf(3.0f * st.phaseA) +
          0.14f * sinf(5.0f * st.phaseA) +
          0.08f * sinf(7.0f * st.phaseA);

      right =
          0.72f * sinf(st.phaseA + 0.7f) +
          0.24f * sinf(3.0f * st.phaseA + 0.2f) +
          0.14f * sinf(5.0f * st.phaseA + 1.0f) +
          0.08f * sinf(7.0f * st.phaseA + 0.5f);

      st.phaseA = wrapPhase(st.phaseA + 2.0f * PI * f * dt);
      break;
    }

    case AudioSource::LIVE_SERIAL:
      left = 0.0f;
      right = 0.0f;
      break;
  }
}

static bool fetchFilteredSample(AudioPlaybackState& st, float& outL, float& outR) {
  float rawL = 0.0f;
  float rawR = 0.0f;

  if (st.source == AudioSource::LIVE_SERIAL) {
    if (!liveReadyForSample(st)) {
      return false;
    }

    int16_t pcmL = 0;
    int16_t pcmR = 0;
    if (!audioStreamPopFrame(pcmL, pcmR)) {
      st.liveState = LivePlaybackState::HOLDING;
      st.primed = false;
      return false;
    }

    rawL = static_cast<float>(pcmL) / 32768.0f;
    rawR = static_cast<float>(pcmR) / 32768.0f;
  } else {
    getPresetRawSample(st, rawL, rawR);
  }

  rawL = updateHPF(st.hpfL, rawL);
  rawR = updateHPF(st.hpfR, rawR);
  rawL = updateLPF(st.lpfL, rawL);
  rawR = updateLPF(st.lpfR, rawR);

  outL = clampf(rawL, -1.0f, 1.0f);
  outR = clampf(rawR, -1.0f, 1.0f);
  return true;
}

static bool primePlayback(AudioPlaybackState& st) {
  if (st.primed) {
    return true;
  }

  float s0L = 0.0f;
  float s0R = 0.0f;
  float s1L = 0.0f;
  float s1R = 0.0f;

  if (!fetchFilteredSample(st, s0L, s0R)) {
    return false;
  }
  if (!fetchFilteredSample(st, s1L, s1R)) {
    return false;
  }

  st.prevL = s0L;
  st.prevR = s0R;
  st.nextL = s1L;
  st.nextR = s1R;
  st.frac = 0.0f;
  st.primed = true;
  return true;
}

static ReplayStep makeCenterStep() {
  ReplayStep step;
  step.point = { DAC_CENTER_CODE, DAC_CENTER_CODE };
  step.blankBefore = false;
  return step;
}

}  // namespace

void audioBegin() {
  g_uiSource = AudioSource::LIVE_SERIAL;
  g_uiHpfHz = AUDIO_HPF_MIN_HZ;
  g_uiLpfHz = AUDIO_LPF_MAX_HZ;
  g_uiResyncNonce = 0;

  audioStreamBegin();
  publishControls(true);
}

void audioOnEnter() {
  publishControls(true);
}

void audioUpdate(const InputSnapshot& in) {
  audioStreamPollSerial();  // no-op in the dedicated RX-task transport version

  bool changed = false;
  bool forceResync = false;

  int32_t lpfDelta = in.leftEncoderDelta;
  int32_t hpfDelta = in.rightEncoderDelta;

  if (INVERT_AUDIO_LPF_ENCODER) lpfDelta = -lpfDelta;
  if (INVERT_AUDIO_HPF_ENCODER) hpfDelta = -hpfDelta;

  if (lpfDelta != 0) {
    g_uiLpfHz *= powf(1.02f, static_cast<float>(lpfDelta));
    g_uiLpfHz = clampf(g_uiLpfHz, AUDIO_LPF_MIN_HZ, AUDIO_LPF_MAX_HZ);
    changed = true;
  }

  if (hpfDelta != 0) {
    g_uiHpfHz *= powf(1.02f, static_cast<float>(hpfDelta));
    g_uiHpfHz = clampf(g_uiHpfHz, AUDIO_HPF_MIN_HZ, AUDIO_HPF_MAX_HZ);
    changed = true;
  }

  if (g_uiHpfHz > g_uiLpfHz) {
    g_uiHpfHz = g_uiLpfHz;
    changed = true;
  }

  if (in.resetPressedEdge) {
    switch (g_uiSource) {
      case AudioSource::LIVE_SERIAL:
        g_uiSource = AudioSource::NOISY_COMPOSITE;
        break;
      case AudioSource::NOISY_COMPOSITE:
        g_uiSource = AudioSource::SMOOTH_LISSAJOUS;
        break;
      case AudioSource::SMOOTH_LISSAJOUS:
        g_uiSource = AudioSource::HARMONIC_RICH;
        break;
      case AudioSource::HARMONIC_RICH:
        g_uiSource = AudioSource::LIVE_SERIAL;
        break;
    }
    changed = true;
    forceResync = true;
  }

  if (changed || forceResync) {
    publishControls(forceResync);
  }
}

ReplayStep audioGetReplayStep() {
  applyPendingControlsIfNeeded();

  if (fabsf(g_pb.lpfHz - g_pb.hpfHz) < 1.0f) {
    return makeCenterStep();
  }

  if (!primePlayback(g_pb)) {
    return makeCenterStep();
  }

  const float outL = g_pb.prevL + g_pb.frac * (g_pb.nextL - g_pb.prevL);
  const float outR = g_pb.prevR + g_pb.frac * (g_pb.nextR - g_pb.prevR);

  ReplayStep step;
  step.blankBefore = false;
  step.point.x = clampAudioCode(static_cast<int32_t>(lroundf(AUDIO_CENTER_CODE_F + AUDIO_HALF_SPAN_F * outL)));
  step.point.y = clampAudioCode(static_cast<int32_t>(lroundf(AUDIO_CENTER_CODE_F + AUDIO_HALF_SPAN_F * outR)));

  g_pb.frac += AUDIO_PHASE_INCREMENT;
  while (g_pb.frac >= 1.0f) {
    g_pb.frac -= 1.0f;
    g_pb.prevL = g_pb.nextL;
    g_pb.prevR = g_pb.nextR;

    if (!fetchFilteredSample(g_pb, g_pb.nextL, g_pb.nextR)) {
      g_pb.primed = false;
      break;
    }
  }

  return step;
}
