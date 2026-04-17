#include "drawing_engine.h"
#include "config.h"
#include <stdlib.h>

static portMUX_TYPE g_stateMux = portMUX_INITIALIZER_UNLOCKED;

// Dense replay path for current Etch drawing
static XYPoint g_path[MAX_PATH_POINTS];

// Total built points in memory
static volatile size_t g_pathCount = 0;

// Replay only uses committed/visible points
static volatile size_t g_visiblePathCount = 0;

// Replay cursor
static volatile size_t g_replayIndex = 0;

// Current cursor
static volatile uint16_t g_cursorX = DAC_CENTER_CODE;
static volatile uint16_t g_cursorY = DAC_CENTER_CODE;

// Current mode
static volatile AppMode g_mode = AppMode::ETCH;

// Demo shape storage
static XYPoint g_demoCircle[DEMO_POINT_COUNT];
static XYPoint g_demoSquare[DEMO_POINT_COUNT];
static volatile size_t g_demoIndex = 0;

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
  g_visiblePathCount = 1;
  g_replayIndex = 0;
}

void drawingBegin() {
  portENTER_CRITICAL(&g_stateMux);
  pathClearAndCenterLocked();
  g_mode = AppMode::ETCH;
  g_demoIndex = 0;
  portEXIT_CRITICAL(&g_stateMux);

  drawingBuildDemoShape();
}

void drawingResetToCenter() {
  portENTER_CRITICAL(&g_stateMux);
  pathClearAndCenterLocked();
  g_demoIndex = 0;
  portEXIT_CRITICAL(&g_stateMux);
}

void drawingSetMode(AppMode mode) {
  portENTER_CRITICAL(&g_stateMux);
  g_mode = mode;

  if (mode == AppMode::ETCH) {
    g_replayIndex = 0;
  } else if (mode == AppMode::SHAPE_DEMO) {
    g_demoIndex = 0;
  } else {
    g_replayIndex = 0;
    g_demoIndex = 0;
  }

  portEXIT_CRITICAL(&g_stateMux);
}

AppMode drawingGetMode() {
  portENTER_CRITICAL(&g_stateMux);
  AppMode m = g_mode;
  portEXIT_CRITICAL(&g_stateMux);
  return m;
}

size_t drawingGetPathCount() {
  portENTER_CRITICAL(&g_stateMux);
  size_t n = g_visiblePathCount;
  portEXIT_CRITICAL(&g_stateMux);
  return n;
}

bool drawingIsPathFull() {
  portENTER_CRITICAL(&g_stateMux);
  bool full = (g_visiblePathCount >= MAX_PATH_POINTS);
  portEXIT_CRITICAL(&g_stateMux);
  return full;
}

XYPoint drawingGetCursor() {
  portENTER_CRITICAL(&g_stateMux);
  XYPoint p { g_cursorX, g_cursorY };
  portEXIT_CRITICAL(&g_stateMux);
  return p;
}

bool drawingAppendMoveClamped(int32_t dxCodes, int32_t dyCodes) {
  if (dxCodes == 0 && dyCodes == 0) {
    return false;
  }

  portENTER_CRITICAL(&g_stateMux);

  const uint16_t startX = g_cursorX;
  const uint16_t startY = g_cursorY;

  // =========================
  // Sticky boundary handling
  // =========================
  // If already at a boundary and user tries to push farther outward on that axis,
  // completely ignore that component. Still allow motion along the other axis.
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
    portEXIT_CRITICAL(&g_stateMux);
    return false;
  }

  const uint16_t endX = clampCode(static_cast<int32_t>(startX) + adjDx);
  const uint16_t endY = clampCode(static_cast<int32_t>(startY) + adjDy);

  // If nothing actually changes after clamp, add nothing
  if (endX == startX && endY == startY) {
    portEXIT_CRITICAL(&g_stateMux);
    return false;
  }

  const int32_t segDx = static_cast<int32_t>(endX) - static_cast<int32_t>(startX);
  const int32_t segDy = static_cast<int32_t>(endY) - static_cast<int32_t>(startY);

  const int32_t maxAbsDelta = max(abs(segDx), abs(segDy));
  int32_t steps = maxAbsDelta / INTERP_CODES_PER_POINT;
  if (steps < 1) steps = 1;

  // Ensure space
  if (g_pathCount + static_cast<size_t>(steps) >= MAX_PATH_POINTS) {
    portEXIT_CRITICAL(&g_stateMux);
    return false;
  }

  // =====================================
  // Integer DDA interpolation
  // =====================================
  // This avoids float rounding drift and preserves exact horizontal/vertical lines.
  const int32_t x0 = static_cast<int32_t>(startX);
  const int32_t y0 = static_cast<int32_t>(startY);

  for (int32_t i = 1; i <= steps; ++i) {
    const int32_t px = x0 + (segDx * i) / steps;
    const int32_t py = y0 + (segDy * i) / steps;

    // Avoid redundant duplicate point insertion
    const XYPoint last = g_path[g_pathCount - 1];
    if (last.x == static_cast<uint16_t>(px) && last.y == static_cast<uint16_t>(py)) {
      continue;
    }

    g_path[g_pathCount++] = {
      static_cast<uint16_t>(px),
      static_cast<uint16_t>(py)
    };
  }

  g_cursorX = endX;
  g_cursorY = endY;

  // Only now expose the newly appended points to replay
  g_visiblePathCount = g_pathCount;

  portEXIT_CRITICAL(&g_stateMux);
  return true;
}

void drawingBuildDemoShape() {
  const float boxCenterX = 0.5f * (DRAW_MIN_CODE + DRAW_MAX_CODE);
  const float boxCenterY = 0.5f * (DRAW_MIN_CODE + DRAW_MAX_CODE);
  const float radius = 0.5f * (DRAW_MAX_CODE - DRAW_MIN_CODE);

  portENTER_CRITICAL(&g_stateMux);

  // Build circle to match the square's bounding box
  for (size_t i = 0; i < DEMO_POINT_COUNT; ++i) {
    const float t = (2.0f * PI * static_cast<float>(i)) / static_cast<float>(DEMO_POINT_COUNT);
    const int32_t x = static_cast<int32_t>(lroundf(boxCenterX + radius * cosf(t)));
    const int32_t y = static_cast<int32_t>(lroundf(boxCenterY + radius * sinf(t)));
    g_demoCircle[i] = { clampCode(x), clampCode(y) };
  }

  // Build square using current clean bounds
  const size_t edgePts = DEMO_POINT_COUNT / 4;
  size_t idx = 0;

  // Top edge: left -> right
  for (size_t i = 0; i < edgePts && idx < DEMO_POINT_COUNT; ++i) {
    uint16_t x = DRAW_MIN_CODE + ((DRAW_MAX_CODE - DRAW_MIN_CODE) * i) / (edgePts - 1);
    g_demoSquare[idx++] = { x, DRAW_MAX_CODE };
  }

  // Right edge: top -> bottom
  for (size_t i = 0; i < edgePts && idx < DEMO_POINT_COUNT; ++i) {
    uint16_t y = DRAW_MAX_CODE - ((DRAW_MAX_CODE - DRAW_MIN_CODE) * i) / (edgePts - 1);
    g_demoSquare[idx++] = { DRAW_MAX_CODE, y };
  }

  // Bottom edge: right -> left
  for (size_t i = 0; i < edgePts && idx < DEMO_POINT_COUNT; ++i) {
    uint16_t x = DRAW_MAX_CODE - ((DRAW_MAX_CODE - DRAW_MIN_CODE) * i) / (edgePts - 1);
    g_demoSquare[idx++] = { x, DRAW_MIN_CODE };
  }

  // Left edge: bottom -> top
  for (; idx < DEMO_POINT_COUNT; ++idx) {
    size_t i = idx - 3 * edgePts;
    uint16_t y = DRAW_MIN_CODE + ((DRAW_MAX_CODE - DRAW_MIN_CODE) * i) / (DEMO_POINT_COUNT - 3 * edgePts - 1);
    g_demoSquare[idx] = { DRAW_MIN_CODE, y };
  }

  g_demoIndex = 0;
  portEXIT_CRITICAL(&g_stateMux);
}

void drawingResetDemoIndex() {
  portENTER_CRITICAL(&g_stateMux);
  g_demoIndex = 0;
  portEXIT_CRITICAL(&g_stateMux);
}

XYPoint drawingGetNextReplayPoint() {
  XYPoint out { DAC_CENTER_CODE, DAC_CENTER_CODE };

  portENTER_CRITICAL(&g_stateMux);

  switch (g_mode) {
    case AppMode::ETCH: {
      const size_t visibleCount = g_visiblePathCount;
      if (visibleCount == 0) {
        out = { DAC_CENTER_CODE, DAC_CENTER_CODE };
      } else {
        if (g_replayIndex >= visibleCount) {
          g_replayIndex = 0;
        }
        out = g_path[g_replayIndex++];
      }
      break;
    }

    case AppMode::SHAPE_DEMO: {
      if (g_demoIndex >= DEMO_POINT_COUNT) {
        g_demoIndex = 0;
      }

      const bool showSquare = ((millis() / 2000UL) % 2UL) != 0UL;
      out = showSquare ? g_demoSquare[g_demoIndex++] : g_demoCircle[g_demoIndex++];
      break;
    }

    case AppMode::PONG:
      out = { DAC_CENTER_CODE, DAC_CENTER_CODE };
      break;

    case AppMode::USB_STREAM:
      out = { DAC_CENTER_CODE, DAC_CENTER_CODE };
      break;
  }

  portEXIT_CRITICAL(&g_stateMux);
  return out;
}