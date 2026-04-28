#pragma once
#include "input_manager.h"
#include "drawing_engine.h"

void audioBegin();
void audioOnEnter();
void audioUpdate(const InputSnapshot& in);

// Direct replay path for Audio Mode. When AppMode::USB_STREAM is active, the
// replay core should pull audio samples from here rather than from drawing_engine.
ReplayStep audioGetReplayStep();
