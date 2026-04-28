#pragma once
#include <Arduino.h>

void audioStreamBegin();
void audioStreamReset();
void audioStreamPollSerial();

size_t audioStreamAvailableFrames();
bool audioStreamPopFrame(int16_t& left, int16_t& right);
bool audioStreamIsActive();