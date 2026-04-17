#include <Arduino.h>
#include <esp_timer.h>

#include "pins.h"
#include "config.h"
#include "dac_driver.h"
#include "drawing_engine.h"
#include "input_manager.h"
#include "app_modes.h"

static esp_timer_handle_t g_replayTimer = nullptr;

static void replayTimerCallback(void* arg) {
  (void)arg;
  const XYPoint p = drawingGetNextReplayPoint();
  dacWriteXY(p.x, p.y);
}

static void startReplayTimer() {
  const esp_timer_create_args_t timerArgs = {
    .callback = &replayTimerCallback,
    .arg = nullptr,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "oscillo_replay"
  };

  esp_timer_create(&timerArgs, &g_replayTimer);
  esp_timer_start_periodic(g_replayTimer, REPLAY_PERIOD_US);
}

void setup() {
  dacBegin();
  drawingBegin();
  inputBegin();
  appModesBegin();

  // Safe startup state
  dacWriteCentered();
  delay(10);

  startReplayTimer();
}

void loop() {
  appModesUpdate();

}