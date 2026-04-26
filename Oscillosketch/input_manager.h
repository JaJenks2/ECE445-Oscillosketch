#pragma once
#include <Arduino.h>

struct InputSnapshot {
  int32_t leftEncoderDelta;
  int32_t rightEncoderDelta;

  bool modePressedEdge;   // S1 falling-edge, debounced
  bool resetPressedEdge;  // S2 falling-edge, debounced

  bool upHeld;
  bool leftHeld;
  bool rightHeld;
  bool downHeld;
};

void inputBegin();
void inputPoll(InputSnapshot& snap);