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

// Core init / mode control
void drawingBegin();
void drawingResetToCenter();
void drawingSetMode(AppMode mode);
AppMode drawingGetMode();

// Etch mode drawing path
bool drawingAppendMoveClamped(int32_t dxCodes, int32_t dyCodes);
size_t drawingGetPathCount();
bool drawingIsPathFull();
XYPoint drawingGetCursor();

// Replay source
XYPoint drawingGetNextReplayPoint();

// Shape demo helpers
void drawingBuildDemoShape();
void drawingResetDemoIndex();

// Pong frame helpers
void drawingSetPongFrame(const XYPoint* pts, size_t count);
void drawingClearPongFrame();

// Audio frame helpers
void drawingSetAudioFrame(const XYPoint* pts, size_t count);
void drawingClearAudioFrame();