#pragma once
#include <Arduino.h>

// =========================
// MCU / PCB Pin Definitions
// =========================

// DAC / SPI
constexpr int PIN_DAC_MOSI = 16;
constexpr int PIN_DAC_SCK  = 17;
constexpr int PIN_DAC_CS   = 18;
constexpr int PIN_DAC_LDAC = 15;

// Left rotary encoder (S1)
constexpr int PIN_LEFT_ENC_A   = 36;
constexpr int PIN_LEFT_ENC_B   = 35;
constexpr int PIN_LEFT_ENC_SW  = 37;   // mode select

// Right rotary encoder (S2)
constexpr int PIN_RIGHT_ENC_A  = 40;
constexpr int PIN_RIGHT_ENC_B  = 39;
constexpr int PIN_RIGHT_ENC_SW = 38;   // reset

// Front buttons
constexpr int PIN_B1 = 14;   // up
constexpr int PIN_B2 = 21;   // left
constexpr int PIN_B3 = 47;   // right
constexpr int PIN_B4 = 48;   // down