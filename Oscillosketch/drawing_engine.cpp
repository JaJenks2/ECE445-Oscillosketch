#include "drawing_engine.h"
#include "config.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static portMUX_TYPE g_etchMux = portMUX_INITIALIZER_UNLOCKED;

// =====================================
// Etch path storage
// =====================================
static XYPoint g_path[MAX_PATH_POINTS];
static volatile size_t g_pathCount = 0;
static volatile size_t g_replayIndex = 0;
static volatile uint16_t g_cursorX = DAC_CENTER_CODE;
static volatile uint16_t g_cursorY = DAC_CENTER_CODE;

// Current mode
static volatile AppMode g_mode = AppMode::ETCH;

// =====================================
// Shape demo storage
// =====================================
static XYPoint g_demoCircle[DEMO_POINT_COUNT];
static XYPoint g_demoSquare[DEMO_POINT_COUNT];
static volatile size_t g_demoIndex = 0;

// =====================================
// Pong frame storage (double-buffered)
// =====================================
static XYPoint g_pongFrame[2][PONG_FRAME_MAX_POINTS];
static volatile size_t g_pongFrameCount[2] = {0, 0};
static volatile uint8_t g_pongActiveBuf = 0;
static volatile size_t g_pongReplayIndex = 0;

// =====================================
// Audio frame storage (double-buffered)
// =====================================
static XYPoint g_audioFrame[2][AUDIO_FRAME_MAX_POINTS];
static volatile size_t g_audioFrameCount[2] = {0, 0};
static volatile uint8_t g_audioActiveBuf = 0;
static volatile size_t g_audioReplayIndex = 0;

static inline uint16_t clampCode(int32_t v) {
  if (v < DRAW_MIN_CODE) return DRAW_MIN_CODE;
  if (v > DRAW_MAX_CODE) return DRAW_MAX_CODE;
  return static_cast<uint16_t>(v);
}

static void pathClearAndCenterLocked() {
  g_cursorX = DAC_CENTER_CODE;
  g_cursorY = DAC_CENTER_CODE;
  g_path[0] = { DAC_CENTER_CODE, DAC_CENTER_CODE };
  g_pathCount = 1;
  g_replayIndex = 0;
}

void drawingBegin() {
  portENTER_CRITICAL(&g_etchMux);
  pathClearAndCenterLocked();
  portEXIT_CRITICAL(&g_etchMux);

  g_mode = AppMode::ETCH;
  g_demoIndex = 0;
  g_pongFrameCount[0] = g_pongFrameCount[1] = 0;
  g_pongActiveBuf = 0;
  g_pongReplayIndex = 0;
  g_audioFrameCount[0] = g_audioFrameCount[1] = 0;
  g_audioActiveBuf = 0;
  g_audioReplayIndex = 0;

  drawingBuildDemoShape();
}

void drawingResetToCenter() {
  portENTER_CRITICAL(&g_etchMux);
  pathClearAndCenterLocked();
  portEXIT_CRITICAL(&g_etchMux);

  g_demoIndex = 0;
}

void drawingSetMode(AppMode mode) {
  g_mode = mode;

  switch (mode) {
    case AppMode::ETCH:
      g_replayIndex = 0;
      break;
    case AppMode::SHAPE_DEMO:
      g_demoIndex = 0;
      break;
    case AppMode::PONG:
      g_pongReplayIndex = 0;
      break;
    case AppMode::USB_STREAM:
      g_audioReplayIndex = 0;
      break;
  }
}

AppMode drawingGetMode() {
  return g_mode;
}

size_t drawingGetPathCount() {
  return g_pathCount;
}

bool drawingIsPathFull() {
  return g_pathCount >= MAX_PATH_POINTS;
}

XYPoint drawingGetCursor() {
  XYPoint p;
  p.x = g_cursorX;
  p.y = g_cursorY;
  return p;
}

bool drawingAppendMoveClamped(int32_t dxCodes, int32_t dyCodes) {
  if (dxCodes == 0 && dyCodes == 0) {
    return false;
  }

  // Snapshot current cursor without holding the lock long-term.
  const uint16_t startX = g_cursorX;
  const uint16_t startY = g_cursorY;

  int32_t adjDx = dxCodes;
  int32_t adjDy = dyCodes;

  if ((startX <= DRAW_MIN_CODE && adjDx < 0) ||
      (startX >= DRAW_MAX_CODE && adjDx > 0)) {
    adjDx = 0;
  }

  if ((startY <= DRAW_MIN_CODE && adjDy < 0) ||
      (startY >= DRAW_MAX_CODE && adjDy > 0)) {
    adjDy = 0;
  }

  if (adjDx == 0 && adjDy == 0) {
    return false;
  }

  const uint16_t endX = clampCode(static_cast<int32_t>(startX) + adjDx);
  const uint16_t endY = clampCode(static_cast<int32_t>(startY) + adjDy);

  if (endX == startX && endY == startY) {
    return false;
  }

  const int32_t segDx = static_cast<int32_t>(endX) - static_cast<int32_t>(startX);
  const int32_t segDy = static_cast<int32_t>(endY) - static_cast<int32_t>(startY);

  const int32_t maxAbsDelta = max(abs(segDx), abs(segDy));
  int32_t steps = maxAbsDelta / INTERP_CODES_PER_POINT;
  if (steps < 1) steps = 1;

  // Stage interpolated points locally first so replay is not blocked while we build them.
  constexpr size_t MAX_STAGED_APPEND_POINTS =
      ((DAC_MAX_CODE - DAC_MIN_CODE) / INTERP_CODES_PER_POINT) + 8;
  XYPoint staged[MAX_STAGED_APPEND_POINTS];
  size_t stagedCount = 0;

  const int32_t x0 = static_cast<int32_t>(startX);
  const int32_t y0 = static_cast<int32_t>(startY);

  XYPoint lastPoint = { startX, startY };

  for (int32_t i = 1; i <= steps; ++i) {
    const int32_t px = x0 + (segDx * i) / steps;
    const int32_t py = y0 + (segDy * i) / steps;

    XYPoint p = {
      static_cast<uint16_t>(px),
      static_cast<uint16_t>(py)
    };

    if (p.x == lastPoint.x && p.y == lastPoint.y) {
      continue;
    }

    if (stagedCount >= MAX_STAGED_APPEND_POINTS) {
      break;
    }

    staged[stagedCount++] = p;
    lastPoint = p;
  }

  if (stagedCount == 0) {
    return false;
  }

  // Brief critical section only for the append and cursor/count commit.
  portENTER_CRITICAL(&g_etchMux);

  if (g_pathCount + stagedCount >= MAX_PATH_POINTS) {
    portEXIT_CRITICAL(&g_etchMux);
    return false;
  }

  memcpy(&g_path[g_pathCount], staged, stagedCount * sizeof(XYPoint));
  g_pathCount += stagedCount;
  g_cursorX = endX;
  g_cursorY = endY;

  portEXIT_CRITICAL(&g_etchMux);
  return true;
}

void drawingBuildDemoShape() {
  const float center = static_cast<float>(DAC_CENTER_CODE);
  const float radius = 600.0f;

  for (size_t i = 0; i < DEMO_POINT_COUNT; ++i) {
    const float t = (2.0f * PI * static_cast<float>(i)) / static_cast<float>(DEMO_POINT_COUNT);
    const int32_t x = static_cast<int32_t>(lroundf(center + radius * cosf(t)));
    const int32_t y = static_cast<int32_t>(lroundf(center + radius * sinf(t)));
    g_demoCircle[i] = { clampCode(x), clampCode(y) };
  }

  const size_t edgePts = DEMO_POINT_COUNT / 4;
  size_t idx = 0;

  for (size_t i = 0; i < edgePts && idx < DEMO_POINT_COUNT; ++i) {
    uint16_t x = DRAW_MIN_CODE + ((DRAW_MAX_CODE - DRAW_MIN_CODE) * i) / (edgePts - 1);
    g_demoSquare[idx++] = { x, DRAW_MAX_CODE };
  }

  for (size_t i = 0; i < edgePts && idx < DEMO_POINT_COUNT; ++i) {
    uint16_t y = DRAW_MAX_CODE - ((DRAW_MAX_CODE - DRAW_MIN_CODE) * i) / (edgePts - 1);
    g_demoSquare[idx++] = { DRAW_MAX_CODE, y };
  }

  for (size_t i = 0; i < edgePts && idx < DEMO_POINT_COUNT; ++i) {
    uint16_t x = DRAW_MAX_CODE - ((DRAW_MAX_CODE - DRAW_MIN_CODE) * i) / (edgePts - 1);
    g_demoSquare[idx++] = { x, DRAW_MIN_CODE };
  }

  for (; idx < DEMO_POINT_COUNT; ++idx) {
    size_t i = idx - 3 * edgePts;
    uint16_t y = DRAW_MIN_CODE + ((DRAW_MAX_CODE - DRAW_MIN_CODE) * i) / (DEMO_POINT_COUNT - 3 * edgePts - 1);
    g_demoSquare[idx] = { DRAW_MIN_CODE, y };
  }

  g_demoIndex = 0;
}

void drawingResetDemoIndex() {
  g_demoIndex = 0;
}

void drawingSetPongFrame(const XYPoint* pts, size_t count) {
  if (pts == nullptr) return;
  if (count > PONG_FRAME_MAX_POINTS) count = PONG_FRAME_MAX_POINTS;

  const uint8_t currentActive = g_pongActiveBuf;
  const uint8_t writeBuf = currentActive ^ 1U;

  memcpy(g_pongFrame[writeBuf], pts, count * sizeof(XYPoint));

  g_pongFrameCount[writeBuf] = count;

  // Atomic-ish front/back swap. Very short critical section.
  portENTER_CRITICAL(&g_etchMux);
  g_pongActiveBuf = writeBuf;
  if (g_pongReplayIndex >= count) {
    g_pongReplayIndex = 0;
  }
  portEXIT_CRITICAL(&g_etchMux);
}

void drawingClearPongFrame() {
  g_pongFrame[1][0] = { DAC_CENTER_CODE, DAC_CENTER_CODE };
  g_pongFrameCount[1] = 1;

  portENTER_CRITICAL(&g_etchMux);
  g_pongActiveBuf = 1;
  g_pongReplayIndex = 0;
  portEXIT_CRITICAL(&g_etchMux);
}

void drawingSetAudioFrame(const XYPoint* pts, size_t count) {
  if (pts == nullptr) return;
  if (count > AUDIO_FRAME_MAX_POINTS) count = AUDIO_FRAME_MAX_POINTS;

  const uint8_t currentActive = g_audioActiveBuf;
  const uint8_t writeBuf = currentActive ^ 1U;

  memcpy(g_audioFrame[writeBuf], pts, count * sizeof(XYPoint));

  g_audioFrameCount[writeBuf] = count;

  portENTER_CRITICAL(&g_etchMux);
  g_audioActiveBuf = writeBuf;
  if (g_audioReplayIndex >= count) {
    g_audioReplayIndex = 0;
  }
  portEXIT_CRITICAL(&g_etchMux);
}

void drawingClearAudioFrame() {
  g_audioFrame[1][0] = { DAC_CENTER_CODE, DAC_CENTER_CODE };
  g_audioFrameCount[1] = 1;

  portENTER_CRITICAL(&g_etchMux);
  g_audioActiveBuf = 1;
  g_audioReplayIndex = 0;
  portEXIT_CRITICAL(&g_etchMux);
}

XYPoint drawingGetNextReplayPoint() {
  XYPoint out { DAC_CENTER_CODE, DAC_CENTER_CODE };
  const AppMode mode = g_mode;

  switch (mode) {
    case AppMode::ETCH: {
      size_t count = g_pathCount;
      size_t idx = g_replayIndex;

      if (count == 0) {
        out = { DAC_CENTER_CODE, DAC_CENTER_CODE };
      } else {
        if (idx >= count) {
          idx = 0;
        }
        out = g_path[idx];
        g_replayIndex = idx + 1;
      }
      break;
    }

    case AppMode::SHAPE_DEMO: {
      const bool showSquare = ((millis() / 2000UL) % 2UL) != 0UL;
      size_t idx = g_demoIndex;
      if (idx >= DEMO_POINT_COUNT) {
        idx = 0;
      }
      out = showSquare ? g_demoSquare[idx] : g_demoCircle[idx];
      g_demoIndex = idx + 1;
      break;
    }

    case AppMode::PONG: {
      const uint8_t activeBuf = g_pongActiveBuf;
      const size_t count = g_pongFrameCount[activeBuf];
      size_t idx = g_pongReplayIndex;

      if (count == 0) {
        out = { DAC_CENTER_CODE, DAC_CENTER_CODE };
      } else {
        if (idx >= count) {
          idx = 0;
        }
        out = g_pongFrame[activeBuf][idx];
        g_pongReplayIndex = idx + 1;
      }
      break;
    }

    case AppMode::USB_STREAM: {
      const uint8_t activeBuf = g_audioActiveBuf;
      const size_t count = g_audioFrameCount[activeBuf];
      size_t idx = g_audioReplayIndex;

      if (count == 0) {
        out = { DAC_CENTER_CODE, DAC_CENTER_CODE };
      } else {
        if (idx >= count) {
          idx = 0;
        }
        out = g_audioFrame[activeBuf][idx];
        g_audioReplayIndex = idx + 1;
      }
      break;
    }
  }

  return out;
}