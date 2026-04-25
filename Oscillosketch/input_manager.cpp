#include "input_manager.h"
#include "pins.h"
#include "config.h"
#include <ESP32Encoder.h>

static ESP32Encoder g_leftEnc;
static ESP32Encoder g_rightEnc;

static int64_t g_lastLeftCount = 0;
static int64_t g_lastRightCount = 0;

struct DebouncedButton {
  int pin;
  bool stableState;
  bool lastRawState;
  uint32_t lastChangeMs;
};

static DebouncedButton btnMode   { PIN_LEFT_ENC_SW,  true, true, 0 };
static DebouncedButton btnReset  { PIN_RIGHT_ENC_SW, true, true, 0 };
static DebouncedButton btnUp     { PIN_B1,           true, true, 0 };
static DebouncedButton btnLeft   { PIN_B2,           true, true, 0 };
static DebouncedButton btnRight  { PIN_B3,           true, true, 0 };
static DebouncedButton btnDown   { PIN_B4,           true, true, 0 };

static bool updateDebounced(DebouncedButton& b) {
  const bool raw = digitalRead(b.pin);

  if (raw != b.lastRawState) {
    b.lastRawState = raw;
    b.lastChangeMs = millis();
  }

  bool changed = false;
  if ((millis() - b.lastChangeMs) >= DEBOUNCE_MS && b.stableState != b.lastRawState) {
    b.stableState = b.lastRawState;
    changed = true;
  }

  return changed;
}

void inputBegin() {
  // External pull-ups already exist on the PCB
  pinMode(PIN_LEFT_ENC_SW, INPUT);
  pinMode(PIN_RIGHT_ENC_SW, INPUT);
  pinMode(PIN_B1, INPUT);
  pinMode(PIN_B2, INPUT);
  pinMode(PIN_B3, INPUT);
  pinMode(PIN_B4, INPUT);

  ESP32Encoder::useInternalWeakPullResistors = puType::none;

  // Full quadrature for maximum resolution
  g_leftEnc.attachFullQuad(PIN_LEFT_ENC_A, PIN_LEFT_ENC_B);
  g_rightEnc.attachFullQuad(PIN_RIGHT_ENC_A, PIN_RIGHT_ENC_B);

  // Max filter recommended for mechanical encoders
  g_leftEnc.setFilter(1023);
  g_rightEnc.setFilter(1023);

  g_leftEnc.setCount(0);
  g_rightEnc.setCount(0);

  g_lastLeftCount = 0;
  g_lastRightCount = 0;
}

void inputPoll(InputSnapshot& snap) {
  snap.leftEncoderDelta = 0;
  snap.rightEncoderDelta = 0;
  snap.modePressedEdge = false;
  snap.resetPressedEdge = false;
  snap.upHeld = false;
  snap.leftHeld = false;
  snap.rightHeld = false;
  snap.downHeld = false;

  // Encoder deltas
  const int64_t leftNow  = g_leftEnc.getCount();
  const int64_t rightNow = g_rightEnc.getCount();

  snap.leftEncoderDelta  = static_cast<int32_t>(leftNow  - g_lastLeftCount);
  snap.rightEncoderDelta = static_cast<int32_t>(rightNow - g_lastRightCount);

  g_lastLeftCount  = leftNow;
  g_lastRightCount = rightNow;

  // Debounced buttons
  const bool modeChanged  = updateDebounced(btnMode);
  const bool resetChanged = updateDebounced(btnReset);
  updateDebounced(btnUp);
  updateDebounced(btnLeft);
  updateDebounced(btnRight);
  updateDebounced(btnDown);

  // Active-low buttons:
  // pressed = LOW
  if (modeChanged && btnMode.stableState == LOW) {
    snap.modePressedEdge = true;
  }
  if (resetChanged && btnReset.stableState == LOW) {
    snap.resetPressedEdge = true;
  }

  snap.upHeld    = (btnUp.stableState == LOW);
  snap.leftHeld  = (btnLeft.stableState == LOW);
  snap.rightHeld = (btnRight.stableState == LOW);
  snap.downHeld  = (btnDown.stableState == LOW);
}