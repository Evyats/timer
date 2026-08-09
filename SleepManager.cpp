#include "SleepManager.h"

#include "TimerConfig.h"

#include <driver/rtc_io.h>
#include <esp32-hal-rgb-led.h>
#include <esp_sleep.h>

SleepManager::SleepManager(
  int pirPin,
  int buttonPin,
  DisplayManager& displayManager,
  ClockSync& clockSync,
  PirMotion& pirMotion
) : pirPin_(pirPin),
    buttonPin_(buttonPin),
    displayManager_(displayManager),
    clockSync_(clockSync),
    pirMotion_(pirMotion) {
}

int SleepManager::wakeupCauseCode() const {
  return (int)esp_sleep_get_wakeup_cause();
}

void SleepManager::enterDeepSleep(bool pirWakeEnabled) {
  Serial.println(
    pirWakeEnabled
      ? "Preparing for deep sleep. PIR or encoder button will wake the timer."
      : "Preparing for deep sleep. Encoder button will wake the timer."
  );
  Serial.flush();

  for (uint8_t i = 0; i < SLEEP_BLINK_COUNT; ++i) {
    rgbLedWrite(
      ONBOARD_RGB_LED_PIN,
      0,
      0,
      SLEEP_BLINK_BRIGHTNESS
    );
    delay(SLEEP_BLINK_ON_MS);
    rgbLedWrite(ONBOARD_RGB_LED_PIN, 0, 0, 0);
    delay(SLEEP_BLINK_OFF_MS);
  }

  clockSync_.disconnectAndDisableWifi();
  displayManager_.setPower(false);
  esp_err_t pirWakeResult = ESP_OK;
  if (pirWakeEnabled) {
    pirWakeResult = esp_sleep_enable_ext1_wakeup(
      1ULL << pirPin_,
      ESP_EXT1_WAKEUP_ANY_HIGH
    );
  } else {
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);
  }

  rtc_gpio_pullup_en((gpio_num_t)buttonPin_);
  rtc_gpio_pulldown_dis((gpio_num_t)buttonPin_);
  esp_err_t buttonWakeResult = esp_sleep_enable_ext0_wakeup(
    (gpio_num_t)buttonPin_,
    0
  );

  if (pirWakeEnabled) {
    Serial.print("PIR wake config result: ");
    Serial.println((int)pirWakeResult);
  } else {
    Serial.println("PIR wake disabled by settings.");
  }
  Serial.print("Encoder button wake config result: ");
  Serial.println((int)buttonWakeResult);
  Serial.println("Entering deep sleep now.");
  Serial.flush();
  esp_deep_sleep_start();
}
