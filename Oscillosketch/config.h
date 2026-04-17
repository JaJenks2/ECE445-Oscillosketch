#pragma once
#include <Arduino.h>

// DAC numeric space
constexpr uint16_t DAC_MIN_CODE    = 0;
constexpr uint16_t DAC_MAX_CODE    = 4095;
constexpr uint16_t DAC_CENTER_CODE = 2048;

// Small safety margin from hard rails
constexpr uint16_t DRAW_MIN_CODE = 1410; //1410
constexpr uint16_t DRAW_MAX_CODE = 3730; //3730

// Replay timing
constexpr uint32_t REPLAY_RATE_HZ = 60000;      // was 10000
constexpr uint32_t REPLAY_PERIOD_US = 1000000UL / REPLAY_RATE_HZ;

// SPI
constexpr uint32_t DAC_SPI_HZ = 20000000;

// Path buffer sizing
constexpr size_t MAX_PATH_POINTS = 20000;

// Interpolation density
// Increase this for now so lines do not explode the buffer too fast.
// We can tighten it later once basic motion is confirmed.
constexpr uint16_t INTERP_CODES_PER_POINT = 60;   // was 4

// Encoder mapping
constexpr int16_t ENCODER_CODES_PER_COUNT = 8;    // was 2

// Front button movement in Etch mode
constexpr uint16_t BUTTON_MOVE_STEP_CODES = 12;   // was 8
constexpr uint32_t BUTTON_REPEAT_MS = 4;          // was 8

// Debounce
constexpr uint32_t DEBOUNCE_MS = 20;

// Demo shape
constexpr size_t DEMO_POINT_COUNT = 500;           // was 720

// Optional invert controls if motion feels opposite on scope
constexpr bool INVERT_X_ENCODER = false;
constexpr bool INVERT_Y_ENCODER = false;
constexpr bool INVERT_BTN_UP    = false;
constexpr bool INVERT_BTN_LEFT  = false;
constexpr bool INVERT_BTN_RIGHT = false;
constexpr bool INVERT_BTN_DOWN  = false;