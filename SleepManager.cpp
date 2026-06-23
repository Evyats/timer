#include "SleepManager.h"

#include <driver/gpio.h>
#include <esp_sleep.h>

SleepManager::SleepManager(
  int pirPin,
  uint32_t pirDisplayHoldMs,
  uint64_t lightSleepToDeepSleepUs,
  DisplayManager& displayManager,
  ClockSync& clockSync,
  PirMotion& pirMotion,
  BatteryMonitor& batteryMonitor
) : pirPin_(pirPin),
    pirDisplayHoldMs_(pirDisplayHoldMs),
    lightSleepToDeepSleepUs_(lightSleepToDeepSleepUs),
    displayManager_(displayManager),
    clockSync_(clockSync),
    pirMotion_(pirMotion),
    batteryMonitor_(batteryMonitor) {
}

int SleepManager::wakeupCauseCode() const {
  return (int)esp_sleep_get_wakeup_cause();
}

LightSleepResult SleepManager::enterLightSleep() {
  Serial.println("Entering light sleep. PIR motion or 5-minute timer wakes the timer.");
  Serial.flush();

  displayManager_.setPower(false);
  gpio_wakeup_enable((gpio_num_t)pirPin_, GPIO_INTR_HIGH_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup(lightSleepToDeepSleepUs_);
  esp_light_sleep_start();
  gpio_wakeup_disable((gpio_num_t)pirPin_);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);

  delay(50);
  Serial.begin(115200);
  esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();

  if (wakeCause == ESP_SLEEP_WAKEUP_TIMER && !pirMotion_.rawMotionDetected()) {
    Serial.println("Light sleep lasted 5 minutes without motion. Escalating to deep sleep.");
    enterDeepSleep();
  }

  Serial.println("Woke from light sleep.");

  bool wokeForMotion = wakeCause == ESP_SLEEP_WAKEUP_GPIO && pirMotion_.rawMotionDetected();
  if (wokeForMotion) {
    pirMotion_.keepDisplayOn(pirDisplayHoldMs_);
  }

  batteryMonitor_.update(true);
  return { wokeForMotion };
}

void SleepManager::enterDeepSleep() {
  Serial.println("Entering deep sleep. PIR motion will reboot the timer.");
  Serial.flush();

  clockSync_.disconnectAndDisableWifi();
  displayManager_.setPower(false);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << pirPin_, ESP_GPIO_WAKEUP_GPIO_HIGH);
  esp_deep_sleep_start();
}
