#include <Arduino.h>
#include <esp_timer.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "pins.h"
#include "config.h"
#include "dac_driver.h"
#include "drawing_engine.h"
#include "input_manager.h"
#include "app_modes.h"

static TaskHandle_t g_replayTaskHandle = nullptr;

static void replayOnePoint() {
  const XYPoint p = drawingGetNextReplayPoint();
  dacWriteXY(p.x, p.y);
}

static void replayTask(void* arg) {
  (void)arg;

  uint64_t nextWakeUs = static_cast<uint64_t>(esp_timer_get_time());
  uint32_t pointCounter = 0;

  // Exact-average pacing:
  // Instead of using only REPLAY_PERIOD_US (which truncates to 9 us at 110 kHz),
  // accumulate the remainder so we alternate 9 us / 10 us as needed.
  const uint32_t periodWholeUs = 1000000UL / REPLAY_RATE_HZ;
  const uint32_t periodRemainder = 1000000UL % REPLAY_RATE_HZ;
  uint32_t fracAccum = 0;

  for (;;) {
    replayOnePoint();

    nextWakeUs += periodWholeUs;
    fracAccum += periodRemainder;
    if (fracAccum >= REPLAY_RATE_HZ) {
      nextWakeUs += 1;
      fracAccum -= REPLAY_RATE_HZ;
    }

    const int64_t nowUs = esp_timer_get_time();
    const int64_t remainingUs = static_cast<int64_t>(nextWakeUs) - nowUs;

    if (remainingUs > 1000) {
      vTaskDelay(pdMS_TO_TICKS(static_cast<uint32_t>(remainingUs / 1000)));
    } else if (remainingUs > 0) {
      delayMicroseconds(static_cast<uint32_t>(remainingUs));
    } else {
      // If we fall behind, resync to current time but preserve fractional accumulator.
      nextWakeUs = static_cast<uint64_t>(nowUs);
    }

    if ((++pointCounter & 0x7F) == 0) {
      taskYIELD();
    }
  }
}

static void startReplayEngine() {
  const BaseType_t setupCore = xPortGetCoreID();
  const BaseType_t replayCore = (setupCore == 0) ? 1 : 0;
  const UBaseType_t replayTaskPriority = 3;

  TaskHandle_t replayIdleHandle = xTaskGetIdleTaskHandleForCore(replayCore);
  if (replayIdleHandle != nullptr) {
    esp_task_wdt_delete(replayIdleHandle);
  }

  const BaseType_t ok = xTaskCreatePinnedToCore(
      replayTask,
      "oscillo_replay",
      4096,
      nullptr,
      replayTaskPriority,
      &g_replayTaskHandle,
      replayCore);

  (void)ok;
}

void setup() {
  dacBegin();
  drawingBegin();
  inputBegin();
  appModesBegin();

  dacWriteCentered();
  delay(10);

  startReplayEngine();
}

void loop() {
  appModesUpdate();
}