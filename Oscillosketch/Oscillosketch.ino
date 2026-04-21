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
    // Replaces the previous dedicated replay task experiment.
    // That task either starved the system core at higher priority or got starved itself at priority 0.
    // Returning to the original esp_timer-driven replay path keeps refresh behavior stable on Arduino/ESP32-S3.
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
