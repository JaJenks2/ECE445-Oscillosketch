#include "zblank.h"
#include "pins.h"
#include "config.h"
#include <soc/gpio_reg.h>
#include <soc/soc.h>

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

void zblankBegin() {
  pinMode(PIN_ZBLANK, OUTPUT);

  // Safe default: visible beam
  zblankVisible();
}

void zblankVisible() {
  if (!ENABLE_ZBLANK) return;

  if (ZBLANK_ACTIVE_HIGH) {
    fastPinLow(PIN_ZBLANK);
  } else {
    fastPinHigh(PIN_ZBLANK);
  }
}

void zblankBlank() {
  if (!ENABLE_ZBLANK) return;

  if (ZBLANK_ACTIVE_HIGH) {
    fastPinHigh(PIN_ZBLANK);
  } else {
    fastPinLow(PIN_ZBLANK);
  }
}