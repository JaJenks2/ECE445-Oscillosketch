#include "app_modes.h"
#include "drawing_engine.h"
#include "input_manager.h"
#include "config.h"
#include "pong_mode.h"
#include "audio_mode.h"

static uint32_t g_lastButtonMoveMs = 0;

static AppMode nextMode(AppMode m) {
  switch (m) {
    case AppMode::ETCH:       return AppMode::SHAPE_DEMO;
    case AppMode::SHAPE_DEMO: return AppMode::PONG;
    case AppMode::PONG:       return AppMode::USB_STREAM;
    case AppMode::USB_STREAM: return AppMode::ETCH;
    default:                  return AppMode::ETCH;
  }
}

void appModesBegin() {
  g_lastButtonMoveMs = millis();
  pongBegin();
  audioBegin();
}

void appModesUpdate() {
  InputSnapshot in;
  inputPoll(in);

  // Mode button always cycles modes
  if (in.modePressedEdge) {
    AppMode newMode = nextMode(drawingGetMode());
    drawingSetMode(newMode);

    if (newMode == AppMode::PONG) {
      pongOnEnter();
    } else if (newMode == AppMode::USB_STREAM) {
      audioOnEnter();
    }

    return;
  }

  const AppMode mode = drawingGetMode();

  switch (mode) {
    case AppMode::ETCH: {
      if (in.resetPressedEdge) {
        drawingResetToCenter();
        return;
      }

      int32_t dx = in.leftEncoderDelta * ENCODER_CODES_PER_COUNT;
      int32_t dy = in.rightEncoderDelta * ENCODER_CODES_PER_COUNT;

      if (INVERT_X_ENCODER) dx = -dx;
      if (INVERT_Y_ENCODER) dy = -dy;

      if (dx != 0 || dy != 0) {
        drawingAppendMoveClamped(dx, dy);
      }

      const uint32_t now = millis();
      if ((now - g_lastButtonMoveMs) >= BUTTON_REPEAT_MS) {
        g_lastButtonMoveMs = now;

        int32_t bdx = 0;
        int32_t bdy = 0;

        if (in.upHeld)    bdy += (INVERT_BTN_UP    ? -BUTTON_MOVE_STEP_CODES :  BUTTON_MOVE_STEP_CODES);
        if (in.downHeld)  bdy += (INVERT_BTN_DOWN  ?  BUTTON_MOVE_STEP_CODES : -BUTTON_MOVE_STEP_CODES);
        if (in.leftHeld)  bdx += (INVERT_BTN_LEFT  ?  BUTTON_MOVE_STEP_CODES : -BUTTON_MOVE_STEP_CODES);
        if (in.rightHeld) bdx += (INVERT_BTN_RIGHT ? -BUTTON_MOVE_STEP_CODES :  BUTTON_MOVE_STEP_CODES);

        if (bdx != 0 || bdy != 0) {
          drawingAppendMoveClamped(bdx, bdy);
        }
      }
      break;
    }

    case AppMode::SHAPE_DEMO:
      if (in.resetPressedEdge) {
        drawingResetDemoIndex();
      }
      break;

    case AppMode::PONG:
      pongUpdate(in);
      break;

    case AppMode::USB_STREAM:
      audioUpdate(in);
      break;
  }
}