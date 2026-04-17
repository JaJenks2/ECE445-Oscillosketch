#include "dac_driver.h"
#include "pins.h"
#include "config.h"
#include <SPI.h>

static SPIClass g_dacSPI(FSPI);

static inline uint16_t buildDACWord(bool dacB, uint16_t value) {
  value &= 0x0FFF;
  // bit15: channel select
  // bit14: buffer = 0
  // bit13: gain = 1 (1x)
  // bit12: shutdown = 1 (active)
  return ((dacB ? 1 : 0) << 15) | (1 << 13) | (1 << 12) | value;
}

static inline void pulseLDAC() {
  // Short low pulse; default idle high
  digitalWrite(PIN_DAC_LDAC, LOW);
  digitalWrite(PIN_DAC_LDAC, HIGH);
}

static inline void sendWord(uint16_t cmd) {
  digitalWrite(PIN_DAC_CS, LOW);
  g_dacSPI.transfer(static_cast<uint8_t>(cmd >> 8));
  g_dacSPI.transfer(static_cast<uint8_t>(cmd & 0xFF));
  digitalWrite(PIN_DAC_CS, HIGH);
}

void dacBegin() {
  pinMode(PIN_DAC_CS, OUTPUT);
  pinMode(PIN_DAC_LDAC, OUTPUT);

  digitalWrite(PIN_DAC_CS, HIGH);
  digitalWrite(PIN_DAC_LDAC, HIGH);  // pulsed-LDAC design intent

  g_dacSPI.begin(PIN_DAC_SCK, -1, PIN_DAC_MOSI, PIN_DAC_CS);
}

void dacWriteXY(uint16_t x, uint16_t y) {
  const uint16_t cmdX = buildDACWord(false, x); // DAC A = X
  const uint16_t cmdY = buildDACWord(true,  y); // DAC B = Y

  g_dacSPI.beginTransaction(SPISettings(DAC_SPI_HZ, MSBFIRST, SPI_MODE0));
  sendWord(cmdX);
  sendWord(cmdY);
  pulseLDAC();
  g_dacSPI.endTransaction();
}

void dacWriteCentered() {
  dacWriteXY(DAC_CENTER_CODE, DAC_CENTER_CODE);
}