#include "dac_driver.h"
#include "pins.h"
#include "config.h"
#include <SPI.h>
#include <soc/gpio_reg.h>
#include <soc/soc.h>

static SPIClass g_dacSPI(FSPI);
static SPISettings g_dacSettings(DAC_SPI_HZ, MSBFIRST, SPI_MODE0);

static inline uint16_t buildDACWord(bool dacB, uint16_t value) {
  value &= 0x0FFF;
  // bit15: channel select
  // bit14: buffer = 0
  // bit13: gain = 1 (1x)
  // bit12: shutdown = 1 (active)
  return ((dacB ? 1 : 0) << 15) | (1 << 13) | (1 << 12) | value;
}

static inline void fastPinHigh(int pin) {
  if (pin < 32) {
    REG_WRITE(GPIO_OUT_W1TS_REG, (1UL << pin));
  } else {
    REG_WRITE(GPIO_OUT1_W1TS_REG, (1UL << (pin - 32)));
  }
}

static inline void fastPinLow(int pin) {
  if (pin < 32) {
    REG_WRITE(GPIO_OUT_W1TC_REG, (1UL << pin));
  } else {
    REG_WRITE(GPIO_OUT1_W1TC_REG, (1UL << (pin - 32)));
  }
}

static inline void pulseLDAC() {
  // Replaces the previous digitalWrite(PIN_DAC_LDAC, LOW/HIGH) pulse.
  fastPinLow(PIN_DAC_LDAC);
  fastPinHigh(PIN_DAC_LDAC);
}

static inline void sendWord(uint16_t cmd) {
  // Replaces the previous digitalWrite(PIN_DAC_CS, LOW/HIGH) framing.
  fastPinLow(PIN_DAC_CS);
  g_dacSPI.transfer16(cmd);
  fastPinHigh(PIN_DAC_CS);
}

void dacBegin() {
  pinMode(PIN_DAC_CS, OUTPUT);
  pinMode(PIN_DAC_LDAC, OUTPUT);

  // Replaces the previous digitalWrite startup idle levels.
  fastPinHigh(PIN_DAC_CS);
  fastPinHigh(PIN_DAC_LDAC);  // pulsed-LDAC design intent

  g_dacSPI.begin(PIN_DAC_SCK, -1, PIN_DAC_MOSI, PIN_DAC_CS);

  // Replaces the previous per-point beginTransaction()/endTransaction() calls.
  // This bus is dedicated to the DAC in this project, so we configure it once here.
  g_dacSPI.beginTransaction(g_dacSettings);
}

void dacWriteXY(uint16_t x, uint16_t y) {
  const uint16_t cmdX = buildDACWord(false, x); // DAC A = X
  const uint16_t cmdY = buildDACWord(true,  y); // DAC B = Y

  sendWord(cmdX);
  sendWord(cmdY);
  pulseLDAC();
}

void dacWriteCentered() {
  dacWriteXY(DAC_CENTER_CODE, DAC_CENTER_CODE);
}
