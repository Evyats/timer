#include "SleepManager.h"

#include <esp_sleep.h>

SleepManager::SleepManager(
  int pirPin,
  DisplayManager& displayManager,
  ClockSync& clockSync,
  PirMotion& pirMotion
) : pirPin_(pirPin),
    displayManager_(displayManager),
    clockSync_(clockSync),
    pirMotion_(pirMotion) {
}

int SleepManager::wakeupCauseCode() const {
  return (int)esp_sleep_get_wakeup_cause();
}

void SleepManager::enterDeepSleep() {
  Serial.println("Preparing to enter deep sleep. PIR motion will reboot the timer.");
  Serial.flush();

  clockSync_.disconnectAndDisableWifi();
  displayManager_.setPower(false);
  esp_err_t wakeupConfigResult = esp_deep_sleep_enable_gpio_wakeup(1ULL << pirPin_, ESP_GPIO_WAKEUP_GPIO_HIGH);
  Serial.print("Deep sleep GPIO wake config result: ");
  Serial.println((int)wakeupConfigResult);
  Serial.println("Entering deep sleep now.");
  Serial.flush();
  esp_deep_sleep_start();
}
