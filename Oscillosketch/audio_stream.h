#pragma once
#include <Arduino.h>

struct AudioStreamStats {
  uint32_t packetsReceived;
  uint32_t sequenceGaps;
  uint32_t parserResets;
  uint32_t bufferOverwrites;
  uint32_t underruns;
  size_t currentFill;
  size_t maxFill;
  uint16_t lastSeq;
  bool active;
};

void audioStreamBegin();
void audioStreamReset();
void audioStreamPollSerial();

size_t audioStreamAvailableFrames();
bool audioStreamPopFrame(int16_t& left, int16_t& right);
bool audioStreamIsActive();

void audioStreamGetStats(AudioStreamStats& out);