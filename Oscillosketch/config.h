#pragma once
#include <Arduino.h>

// DAC numeric space
constexpr uint16_t DAC_MIN_CODE    = 0;
constexpr uint16_t DAC_MAX_CODE    = 4095;
constexpr uint16_t DAC_CENTER_CODE = 2048;

// General draw bounds for the non-audio modes
constexpr uint16_t DRAW_MIN_CODE = 153;   // charger-powered wide bounds
constexpr uint16_t DRAW_MAX_CODE = 4025;  // charger-powered wide bounds

// Replay timing
constexpr uint32_t REPLAY_RATE_HZ = 110000;
constexpr uint32_t REPLAY_PERIOD_US = 1000000UL / REPLAY_RATE_HZ;

// SPI
constexpr uint32_t DAC_SPI_HZ = 20000000;

// Path buffer sizing
constexpr size_t MAX_PATH_POINTS = 20000;

// Interpolation density
constexpr uint16_t INTERP_CODES_PER_POINT = 20;

// Encoder mapping
constexpr int16_t ENCODER_CODES_PER_COUNT = 12;

// Front button movement in Etch mode
constexpr uint16_t BUTTON_MOVE_STEP_CODES = 12;
constexpr uint32_t BUTTON_REPEAT_MS = 4;

// Debounce
constexpr uint32_t DEBOUNCE_MS = 20;

// Demo shape
constexpr size_t DEMO_POINT_COUNT = 500;

// Pong frame buffer
constexpr size_t PONG_FRAME_MAX_POINTS = 2048;

// Optional invert controls if motion feels opposite on scope
constexpr bool INVERT_X_ENCODER = false;
constexpr bool INVERT_Y_ENCODER = false;
constexpr bool INVERT_BTN_UP    = false;
constexpr bool INVERT_BTN_LEFT  = false;
constexpr bool INVERT_BTN_RIGHT = false;
constexpr bool INVERT_BTN_DOWN  = false;

// Pong-only per-paddle direction controls
constexpr bool INVERT_PONG_LEFT_PADDLE  = false;
constexpr bool INVERT_PONG_RIGHT_PADDLE = true;

// =====================================================
// Audio mode configuration
// =====================================================

// Explicit configurable audio sample rate.
// Start at 32 kHz. Later you can try 44.1 kHz or 48 kHz if desired.
constexpr uint32_t AUDIO_SAMPLE_RATE = 32000;

// Input block size processed by the ESP32 each audio update.
constexpr size_t AUDIO_INPUT_BLOCK_FRAMES = 256;

// On-the-wire PCM packet payload size from Python -> ESP32.
constexpr size_t AUDIO_PACKET_FRAMES = 128;

// Ring buffer size on the ESP32 for live serial audio.
// 4096 frames at 32 kHz is about 128 ms of stereo audio.
constexpr size_t AUDIO_BUFFER_FRAMES = 4096;

// Derived timing for one audio block.
constexpr uint32_t AUDIO_BLOCK_PERIOD_US =
    static_cast<uint32_t>(
        (1000000ULL * AUDIO_INPUT_BLOCK_FRAMES + AUDIO_SAMPLE_RATE / 2) / AUDIO_SAMPLE_RATE);

// Number of XY replay points generated per audio block.
// Chosen so one frame approximately matches the replay consumption over one block period.
constexpr size_t AUDIO_FRAME_MAX_POINTS =
    static_cast<size_t>(
        (static_cast<uint64_t>(REPLAY_RATE_HZ) * AUDIO_INPUT_BLOCK_FRAMES + AUDIO_SAMPLE_RATE / 2) / AUDIO_SAMPLE_RATE);

// Live-serial baud rate for USB CDC / Serial transport.
// The ESP32-S3 native USB CDC stack typically ignores the "baud" physically,
// but setting it consistently is still useful on the host side.
constexpr uint32_t AUDIO_SERIAL_BAUD = 2000000;

// Live-stream timeout window.
// If no packets arrive for this long, the stream is considered inactive.
constexpr uint32_t AUDIO_STREAM_ACTIVE_TIMEOUT_MS = 250;

// Audio mode uses tighter DAC bounds because laptop-powered operation
// has shown the bad edge behavior at the wider charger-powered limits.
constexpr uint16_t AUDIO_DRAW_MIN_CODE = 1410;
constexpr uint16_t AUDIO_DRAW_MAX_CODE = 3730;

// Audio mode cutoff ranges in Hz
constexpr float AUDIO_HPF_MIN_HZ = 20.0f;
constexpr float AUDIO_HPF_MAX_HZ = 8000.0f;
constexpr float AUDIO_LPF_MIN_HZ = 100.0f;
constexpr float AUDIO_LPF_MAX_HZ = 12000.0f;

// Audio mode encoder direction controls
constexpr bool INVERT_AUDIO_LPF_ENCODER = false;
constexpr bool INVERT_AUDIO_HPF_ENCODER = false;

// =====================================================
// Z blanking
// =====================================================
constexpr bool ENABLE_ZBLANK = true;
constexpr bool ZBLANK_ACTIVE_HIGH = true;
constexpr uint8_t ZBLANK_STRETCH_POINTS = 2;