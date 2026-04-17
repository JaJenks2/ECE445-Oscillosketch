#include "app_modes.h"
#include "drawing_engine.h"
#include "input_manager.h"
#include "config.h"

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
}

void appModesUpdate() {
  InputSnapshot in;
  inputPoll(in);

  if (in.resetPressedEdge) {
    drawingResetToCenter();
    return;
  }

  if (in.modePressedEdge) {
    AppMode m = drawingGetMode();
    drawingSetMode(nextMode(m));
    return;
  }

  const AppMode mode = drawingGetMode();

  switch (mode) {
    case AppMode::ETCH: {
      int32_t dx = in.leftEncoderDelta * ENCODER_CODES_PER_COUNT;
      int32_t dy = in.rightEncoderDelta * ENCODER_CODES_PER_COUNT;

      if (INVERT_X_ENCODER) dx = -dx;
      if (INVERT_Y_ENCODER) dy = -dy;

      if (dx != 0 || dy != 0) {
        drawingAppendMoveClamped(dx, dy);
      }

      // Front buttons move while held
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
      // Nothing needed in the main loop right now.
      // Timer-driven replay continuously scans the demo shape.
      break;

    case AppMode::PONG:
      // Placeholder:
      // Later: game state update, paddle control, ball state, collision, then
      // rebuild a replay path or stream the active frame to the replay engine.
      break;

    case AppMode::USB_STREAM:
      // Placeholder:
      // Later: decode incoming serial/USB data or generated sample stream into
      // XY replay points.
      break;
  }
}