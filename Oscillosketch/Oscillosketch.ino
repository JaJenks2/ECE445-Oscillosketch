#include <Arduino.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "pins.h"
#include "config.h"
#include "dac_driver.h"
#include "drawing_engine.h"
#include "input_manager.h"
#include "app_modes.h"

static esp_timer_handle_t g_replayTimer = nullptr;
static TaskHandle_t g_replayTaskHandle = nullptr;

static void replayOnePoint() {
  const XYPoint p = drawingGetNextReplayPoint();
  dacWriteXY(p.x, p.y);
}

static void replayTimerCallback(void* arg) {
  (void)arg;
  replayOnePoint();
}

static void replayTask(void* arg) {
  (void)arg;

  uint64_t nextWakeUs = static_cast<uint64_t>(esp_timer_get_time());

  for (;;) {
    replayOnePoint();

    nextWakeUs += REPLAY_PERIOD_US;
    const int64_t nowUs = esp_timer_get_time();
    const int64_t remainingUs = static_cast<int64_t>(nextWakeUs) - nowUs;

    if (remainingUs > 1000) {
      vTaskDelay(pdMS_TO_TICKS(static_cast<uint32_t>(remainingUs / 1000)));
    } else if (remainingUs > 0) {
      // Replaces the previous esp_timer task-dispatch wakeup with a tight local wait
      // so the replay loop does not pay a scheduler callback cost every point.
      delayMicroseconds(static_cast<uint32_t>(remainingUs));
    } else {
      // If we fall behind, resync to the current time instead of accumulating drift.
      nextWakeUs = static_cast<uint64_t>(nowUs);
      taskYIELD();
    }
  }
}

static void startReplayFallbackTimer() {
  const esp_timer_create_args_t timerArgs = {
    .callback = &replayTimerCallback,
    .arg = nullptr,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "oscillo_replay_fallback"
  };

  esp_timer_create(&timerArgs, &g_replayTimer);
  esp_timer_start_periodic(g_replayTimer, REPLAY_PERIOD_US);
}

static void startReplayEngine() {
  const BaseType_t currentCore = xPortGetCoreID();
  const BaseType_t replayCore = (currentCore == 0) ? 1 : 0;

  // Replaces the previous single esp_timer callback-driven replay path with a dedicated
  // high-priority task pinned to the opposite core to reduce callback and scheduler overhead.
  const BaseType_t ok = xTaskCreatePinnedToCore(
    replayTask,
    "oscillo_replay",
    4096,
    nullptr,
    3,
    &g_replayTaskHandle,
    replayCore
  );

  if (ok != pdPASS) {
    startReplayFallbackTimer();
  }
}

void setup() {
  dacBegin();
  drawingBegin();
  inputBegin();
  appModesBegin();

  // Safe startup state
  dacWriteCentered();
  delay(10);

  startReplayEngine();
}

void loop() {
  appModesUpdate();
}
