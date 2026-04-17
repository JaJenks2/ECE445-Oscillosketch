#pragma once
#include <Arduino.h>

struct XYPoint {
  uint16_t x;
  uint16_t y;
};

enum class AppMode : uint8_t {
  ETCH = 0,
  SHAPE_DEMO = 1,
  PONG = 2,
  USB_STREAM = 3
};

// Shared state access
void drawingBegin();
void drawingResetToCenter();
void drawingSetMode(AppMode mode);
AppMode drawingGetMode();

// Etch-path operations
bool drawingAppendMoveClamped(int32_t dxCodes, int32_t dyCodes);
size_t drawingGetPathCount();
bool drawingIsPathFull();
XYPoint drawingGetCursor();

// Replay-source access
XYPoint drawingGetNextReplayPoint();

// Demo / placeholders
void drawingBuildDemoShape();
void drawingResetDemoIndex();