#include <Arduino.h>
#include <SPI.h>
#include <driver/gptimer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "pins.h"
#include "config.h"
#include "dac_driver.h"
#include "drawing_engine.h"
#include "input_manager.h"
#include "app_modes.h"

static TaskHandle_t g_replayTaskHandle = nullptr;
static gptimer_handle_t g_replayTimer = nullptr;
static int g_appCore = 0;

static void replayTask(void* arg) {
  (void)arg;

  for (;;) {
    const uint32_t pendingTicks = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // This replaces the old esp_timer callback path that emitted exactly one
    // point per software-timer callback. With GPTimer, we drain however many
    // timer ticks arrived while the task was blocked/asleep.
    for (uint32_t i = 0; i < pendingTicks; ++i) {
      const XYPoint p = drawingGetNextReplayPoint();
      dacWriteXY(p.x, p.y);
    }
  }
}

static bool IRAM_ATTR replayTimerISR(gptimer_handle_t timer,
                                     const gptimer_alarm_event_data_t* edata,
                                     void* user_ctx) {
  (void)timer;
  (void)edata;

  BaseType_t higherPriorityTaskWoken = pdFALSE;
  TaskHandle_t replayHandle = static_cast<TaskHandle_t>(user_ctx);

  // This replaces the old esp_timer software-dispatch callback wakeup.
  // GPTimer runs from a hardware timer ISR and directly notifies the replay task.
  vTaskNotifyGiveFromISR(replayHandle, &higherPriorityTaskWoken);
  return (higherPriorityTaskWoken == pdTRUE);
}

static void startReplayTimer() {
  const gptimer_config_t timerConfig = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT,
    .direction = GPTIMER_COUNT_UP,
    .resolution_hz = 1000000,
    .flags = {
      .intr_shared = 0,
      .allow_pd = 0,
      .backup_before_sleep = 0,
    },
  };

  ESP_ERROR_CHECK(gptimer_new_timer(&timerConfig, &g_replayTimer));

  const gptimer_event_callbacks_t callbacks = {
    .on_alarm = replayTimerISR,
  };
  ESP_ERROR_CHECK(gptimer_register_event_callbacks(g_replayTimer, &callbacks, g_replayTaskHandle));

  const gptimer_alarm_config_t alarmConfig = {
    .alarm_count = REPLAY_PERIOD_US,
    .reload_count = 0,
    .flags = {
      .auto_reload_on_alarm = 1,
    },
  };

  ESP_ERROR_CHECK(gptimer_enable(g_replayTimer));
  ESP_ERROR_CHECK(gptimer_set_alarm_action(g_replayTimer, &alarmConfig));
  ESP_ERROR_CHECK(gptimer_start(g_replayTimer));
}

void setup() {
  g_appCore = xPortGetCoreID();

  dacBegin();
  drawingBegin();
  inputBegin();
  appModesBegin();

  // Safe startup state
  dacWriteCentered();
  delay(10);

  // This replaces the previous dedicated replay task pinned to the opposite core.
  // Replay now stays on the current app core and blocks on GPTimer notifications.
  xTaskCreatePinnedToCore(
      replayTask,
      "oscillo_replay",
      4096,
      nullptr,
      3,
      &g_replayTaskHandle,
      g_appCore);

  startReplayTimer();
}

void loop() {
  appModesUpdate();
}
