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
#include "zblank.h"

static TaskHandle_t g_replayTaskHandle = nullptr;

static void replayTask(void* arg) {
  (void)arg;

  uint64_t nextWakeUs = static_cast<uint64_t>(esp_timer_get_time());
  uint32_t pointCounter = 0;

  const uint32_t periodWholeUs = 1000000UL / REPLAY_RATE_HZ;
  const uint32_t periodRemainder = 1000000UL % REPLAY_RATE_HZ;
  uint32_t fracAccum = 0;

  uint8_t blankCountdown = 0;

  for (;;) {
    const ReplayStep step = drawingGetNextReplayStep();

    if (ENABLE_ZBLANK) {
      if (step.blankBefore) {
        blankCountdown = ZBLANK_STRETCH_POINTS;
      }

      if (blankCountdown > 0) {
        zblankBlank();
        blankCountdown--;
      } else {
        zblankVisible();
      }
    }

    dacWriteXY(step.point.x, step.point.y);

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
  zblankBegin();
  drawingBegin();
  inputBegin();
  appModesBegin();

  zblankVisible();
  dacWriteCentered();
  delay(10);

  startReplayEngine();
}

void loop() {
  appModesUpdate();
}