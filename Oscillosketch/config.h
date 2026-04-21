#pragma once
#include <Arduino.h>

// DAC numeric space
constexpr uint16_t DAC_MIN_CODE    = 0;
constexpr uint16_t DAC_MAX_CODE    = 4095;
constexpr uint16_t DAC_CENTER_CODE = 2048;

// Small safety margin from hard rails
constexpr uint16_t DRAW_MIN_CODE = 153; //1410 
constexpr uint16_t DRAW_MAX_CODE = 4025; //3730

// Replay timing
constexpr uint32_t REPLAY_RATE_HZ = 40000;      // was 10000
constexpr uint32_t REPLAY_PERIOD_US = 1000000UL / REPLAY_RATE_HZ;

// SPI
constexpr uint32_t DAC_SPI_HZ = 20000000;

// Path buffer sizing
constexpr size_t MAX_PATH_POINTS = 20000;

// Interpolation density
// Increase this for now so lines do not explode the buffer too fast.
// We can tighten it later once basic motion is confirmed.
constexpr uint16_t INTERP_CODES_PER_POINT = 500;   // was 4

// Encoder mapping
constexpr int16_t ENCODER_CODES_PER_COUNT = 40;    // was 2

// Front button movement in Etch mode
constexpr uint16_t BUTTON_MOVE_STEP_CODES = 12;   // was 8
constexpr uint32_t BUTTON_REPEAT_MS = 4;          // was 8

// Debounce
constexpr uint32_t DEBOUNCE_MS = 20;

// Demo shape
constexpr size_t DEMO_POINT_COUNT = 500;           // was 720

// Pong frame buffer
constexpr size_t PONG_FRAME_MAX_POINTS = 2048;

// Optional invert controls if motion feels opposite on scope
constexpr bool INVERT_X_ENCODER = false;
constexpr bool INVERT_Y_ENCODER = false;
constexpr bool INVERT_BTN_UP    = false;
constexpr bool INVERT_BTN_LEFT  = false;
constexpr bool INVERT_BTN_RIGHT = false;
constexpr bool INVERT_BTN_DOWN  = false;

// Pong-only per-paddle direction controls.
// Set true to invert the corresponding paddle direction.
constexpr bool INVERT_PONG_LEFT_PADDLE  = false;
constexpr bool INVERT_PONG_RIGHT_PADDLE = true;



constexpr size_t AUDIO_FRAME_MAX_POINTS = 1024;
constexpr uint32_t AUDIO_FRAME_UPDATE_MS = 30;

// Audio mode cutoff ranges in Hz
constexpr float AUDIO_HPF_MIN_HZ = 20.0f;
constexpr float AUDIO_HPF_MAX_HZ = 8000.0f;
constexpr float AUDIO_LPF_MIN_HZ = 100.0f;
constexpr float AUDIO_LPF_MAX_HZ = 12000.0f;

// Audio mode encoder direction controls
constexpr bool INVERT_AUDIO_LPF_ENCODER = false;
constexpr bool INVERT_AUDIO_HPF_ENCODER = false;