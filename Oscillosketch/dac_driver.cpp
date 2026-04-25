#include "dac_driver.h"
#include "pins.h"
#include "config.h"

#include <SPI.h>
#include <soc/gpio_struct.h>

static SPIClass g_dacSPI(FSPI);

static inline uint16_t buildDACWord(bool dacB, uint16_t value) {
  value &= 0x0FFF;
  // bit15: channel select
  // bit14: buffer = 0
  // bit13: gain = 1 (1x)
  // bit12: shutdown = 1 (active)
  return ((dacB ? 1 : 0) << 15) | (1 << 13) | (1 << 12) | value;
}

static inline void gpioSetFast(int pin) {
  if (pin < 32) {
    GPIO.out_w1ts = (1UL << pin);
  } else {
    GPIO.out1_w1ts.val = (1UL << (pin - 32));
  }
}

static inline void gpioClearFast(int pin) {
  if (pin < 32) {
    GPIO.out_w1tc = (1UL << pin);
  } else {
    GPIO.out1_w1tc.val = (1UL << (pin - 32));
  }
}

static inline void pulseLDAC() {
  // This replaces digitalWrite(PIN_DAC_LDAC, LOW/HIGH) in the low-latency DAC path.
  gpioClearFast(PIN_DAC_LDAC);
  gpioSetFast(PIN_DAC_LDAC);
}

static inline void sendWord(uint16_t cmd) {
  // This replaces digitalWrite(PIN_DAC_CS, LOW/HIGH) around each SPI word.
  gpioClearFast(PIN_DAC_CS);
  g_dacSPI.transfer(static_cast<uint8_t>(cmd >> 8));
  g_dacSPI.transfer(static_cast<uint8_t>(cmd & 0xFF));
  gpioSetFast(PIN_DAC_CS);
}

void dacBegin() {
  pinMode(PIN_DAC_CS, OUTPUT);
  pinMode(PIN_DAC_LDAC, OUTPUT);

  gpioSetFast(PIN_DAC_CS);
  gpioSetFast(PIN_DAC_LDAC);  // pulsed-LDAC design intent

  g_dacSPI.begin(PIN_DAC_SCK, -1, PIN_DAC_MOSI, PIN_DAC_CS);

  // This replaces beginTransaction()/endTransaction() on every single replay point.
  // The DAC is the only SPI client in this project, so we keep the bus configured once.
  g_dacSPI.beginTransaction(SPISettings(DAC_SPI_HZ, MSBFIRST, SPI_MODE0));
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
