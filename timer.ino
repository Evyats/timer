#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <Wire.h>

#include "AlarmPlayer.h"
#include "BatteryMonitor.h"
#include "ClockSync.h"
#include "DisplayManager.h"
#include "InputController.h"
#include "PirMotion.h"
#include "SoundTrigger.h"
#include "wifi_secrets.h"

/*
  OLED timer with rotary encoder, two passive piezos, active piezo, LED,
  PIR, sound detector, and rotary encoder button.

  128x64 SSD1306 OLED:
  VCC -> 3.3V
  GND -> GND
  SDA -> SDA pin from the active pin assignment block
  SCL -> SCL pin from the active pin assignment block

  Rotary encoder:
  CLK/S1 -> encoder S1 pin from the active pin assignment block
  DT/S2  -> encoder S2 pin from the active pin assignment block
  SW/KEY -> encoder button pin from the active pin assignment block
  +      -> 3.3V
  GND    -> GND

  Main passive piezo:
  + -> main piezo pin from the active pin assignment block
  - -> GND

  Second passive piezo:
  + -> harmony piezo pin from the active pin assignment block
  - -> GND

  Active piezo:
  + -> active piezo pin from the active pin assignment block
  - -> GND

  Active piezo LED:
  + -> resistor -> active piezo LED pin from the active pin assignment block
  - -> GND

  HC-SR602 PIR:
  VCC -> 3.3V
  GND -> GND
  OUT -> PIR pin from the active pin assignment block

  LM393 sound detection module:
  VCC -> 5V
  GND -> GND
  OUT -> sound pin from the active pin assignment block

  Battery voltage sense:
  TP4056 OUT+ -> 100k resistor -> battery ADC pin
  battery ADC pin -> 100k resistor -> TP4056 OUT- / GND

  If the sound module OUT is 5V HIGH, protect the ESP32 input with a voltage
  divider or level shifter.
*/

enum DeviceMode {
  MODE_SET_HOUR,
  MODE_SET_MINUTE,
  MODE_READY,
  MODE_TIMER,
  MODE_MUSIC
};

void showReadyForPirState(bool force, bool forceDisplayOn = false);

const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;
const int OLED_ADDRESS = 0x3C;

//// Regular ESP32 pin assignment:
// const int SDA_PIN = 21;
// const int SCL_PIN = 22;
// const int BATTERY_ADC_PIN = 34;
// const int ENCODER_S1_PIN = 32;
// const int ENCODER_S2_PIN = 33;
// const int ENCODER_BUTTON_PIN = 13;
// const int PIR_PIN = 18;
// const int SOUND_PIN = 19;
// const uint8_t MAIN_PIEZO_PIN = 25;
// const uint8_t HARMONY_PIEZO_PIN = 26;
// const uint8_t ACTIVE_PIEZO_PIN = 27;
// const uint8_t ACTIVE_PIEZO_LED_PIN = 14;

//// ESP32-C3 Super Mini pin assignment:
// GPIO2, GPIO8, and GPIO9 are C3 strapping pins. GPIO8 is currently being tested
// for encoder S1.
// const int SDA_PIN = 4;
// const int SCL_PIN = 5;
// const int BATTERY_ADC_PIN = 0;  // ADC1 pin.
// const int ENCODER_S1_PIN = 1;
// const int ENCODER_S2_PIN = 10;
// const int ENCODER_BUTTON_PIN = 6;
// const int PIR_PIN = 20;
// const int SOUND_PIN = 21;
// const uint8_t MAIN_PIEZO_PIN = 7;
// const uint8_t HARMONY_PIEZO_PIN = 8;
// const uint8_t ACTIVE_PIEZO_PIN = 3;
// const uint8_t ACTIVE_PIEZO_LED_PIN = ACTIVE_PIEZO_PIN;

//// Seeed Studio XIAO ESP32C3 pin assignment:
// GPIO2, GPIO8, and GPIO9 are C3 strapping pins. D9 is also connected to BOOT.
const int SDA_PIN = 6;              // D4
const int SCL_PIN = 7;              // D5
const int BATTERY_ADC_PIN = 3;      // D1 / ADC1_CH3
const int ENCODER_S1_PIN = 20;      // D7
const int ENCODER_S2_PIN = 10;      // D10
const int ENCODER_BUTTON_PIN = 9;   // D9 / BOOT
const int PIR_PIN = 4;              // D2 / wake-capable
const int SOUND_PIN = 21;           // D6
const uint8_t MAIN_PIEZO_PIN = 2;   // D0
const uint8_t HARMONY_PIEZO_PIN = 8;  // D8
const uint8_t ACTIVE_PIEZO_PIN = 5;   // D3
const uint8_t ACTIVE_PIEZO_LED_PIN = ACTIVE_PIEZO_PIN;

const bool REVERSE_ENCODER_DIRECTION = true;
const uint32_t BUTTON_DEBOUNCE_MS = 35;
const bool LOG_ENCODER_RAW_STATES = true;

const int PIR_MOTION_STATE = HIGH;
const int SOUND_DETECTED_STATE = LOW;
const unsigned long PIR_WARMUP_MS = 3000;
const uint32_t PIR_CONFIRM_MS = 200;
const uint32_t PIR_DISPLAY_HOLD_MS = 2000;
const uint32_t BUTTON_DISPLAY_HOLD_MS = PIR_DISPLAY_HOLD_MS;
const uint32_t LIGHT_SLEEP_AFTER_MS = 120000;
const uint64_t LIGHT_SLEEP_TO_DEEP_SLEEP_US = 5ULL * 60ULL * 1000000ULL;
const bool ENABLE_LIGHT_SLEEP = true;
const uint8_t SOUND_TRIGGER_MIN_PULSES = 3;
const uint32_t SOUND_TRIGGER_WINDOW_MS = 250;
const uint32_t SOUND_TRIGGER_COOLDOWN_MS = 600;
const bool LOG_RAW_SOUND_EDGES = false;

const uint8_t ACTIVE_PIEZO_VOICE = 0;
const int PIEZO_SELF_TEST_TONE_HZ = 880;
const int PIEZO_SELF_TEST_MS = 300;
const uint16_t PIEZO_SLIDE_UPDATE_INTERVAL_MS = 10;
const uint32_t TIMER_START_DELAY_MS = 2000;
const uint32_t TIMER_COLON_BLINK_DELAY_MS = 1000;
const uint32_t TIME_SYNC_TOTAL_TIMEOUT_MS = 10000;
const char* const ISRAEL_TZ = "IST-2IDT,M3.4.4/26,M10.5.0";
const char* const NTP_SERVER_1 = "pool.ntp.org";
const char* const NTP_SERVER_2 = "time.google.com";
const char* const NTP_SERVER_3 = "time.cloudflare.com";

const int STEP_SECONDS = 60;
const int MAX_SECONDS = 99 * 60 + 59;
const int BATTERY_ADC_SAMPLES = 32;
const int BATTERY_PERCENT_SMOOTHING_WINDOW = 8;
const uint32_t BATTERY_UPDATE_MS = 3000;
const uint32_t BATTERY_SAMPLE_INTERVAL_MS = 2;
const float BATTERY_R1_OHMS = 100000.0;
const float BATTERY_R2_OHMS = 100000.0;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
BatteryMonitor batteryMonitor(
  BATTERY_ADC_PIN,
  BATTERY_R1_OHMS,
  BATTERY_R2_OHMS,
  BATTERY_ADC_SAMPLES,
  BATTERY_PERCENT_SMOOTHING_WINDOW,
  BATTERY_UPDATE_MS,
  BATTERY_SAMPLE_INTERVAL_MS
);
ClockSync clockSync(
  WIFI_SSID,
  WIFI_PASSWORD,
  ISRAEL_TZ,
  NTP_SERVER_1,
  NTP_SERVER_2,
  NTP_SERVER_3,
  TIME_SYNC_TOTAL_TIMEOUT_MS
);
DisplayManager displayManager(display, batteryMonitor);
InputController inputController(
  ENCODER_S1_PIN,
  ENCODER_S2_PIN,
  ENCODER_BUTTON_PIN,
  REVERSE_ENCODER_DIRECTION,
  BUTTON_DEBOUNCE_MS,
  LOG_ENCODER_RAW_STATES
);
SoundTrigger soundTrigger(
  SOUND_PIN,
  SOUND_DETECTED_STATE,
  SOUND_TRIGGER_MIN_PULSES,
  SOUND_TRIGGER_WINDOW_MS,
  SOUND_TRIGGER_COOLDOWN_MS,
  LOG_RAW_SOUND_EDGES
);
PirMotion pirMotion(
  PIR_PIN,
  PIR_MOTION_STATE,
  PIR_WARMUP_MS,
  PIR_CONFIRM_MS,
  PIR_DISPLAY_HOLD_MS
);
AlarmPlayer alarmPlayer(
  ACTIVE_PIEZO_PIN,
  ACTIVE_PIEZO_LED_PIN,
  MAIN_PIEZO_PIN,
  HARMONY_PIEZO_PIN,
  ACTIVE_PIEZO_VOICE,
  PIEZO_SELF_TEST_TONE_HZ,
  PIEZO_SELF_TEST_MS,
  PIEZO_SLIDE_UPDATE_INTERVAL_MS
);

DeviceMode mode = MODE_SET_HOUR;

int remainingSeconds = 0;
uint32_t nextTimerTickMs = 0;
uint32_t timerBlinkStartedAtMs = 0;

uint8_t settingHour = 0;
uint8_t settingMinute = 0;
uint32_t settingBlinkStartedAtMs = 0;

void setup() {
  Serial.begin(115200);
  randomSeed(micros());

  inputController.begin();
  pirMotion.begin();
  soundTrigger.begin();
  alarmPlayer.begin();

  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    while (true) {
      delay(100);
    }
  }

  batteryMonitor.begin();

  display.clearDisplay();
  display.display();

  clockSync.begin();

  Serial.println();
  Serial.println("OLED rotary timer");
  Serial.print("Wakeup cause: ");
  Serial.println((int)esp_sleep_get_wakeup_cause());
  Serial.println("PIR warmup running in background.");
  settingBlinkStartedAtMs = millis();
  mode = MODE_READY;
  clockSync.startSync();
  showReadyForPirState(true, true);

  Serial.println("Starting background Wi-Fi time sync.");
  Serial.println("Send 't' in Serial Monitor to test the three piezo outputs.");
}

void loop() {
  handleSerialCommands();
  bool syncDisplayChanged = clockSync.updateSync();
  if (syncDisplayChanged && mode == MODE_READY) {
    displayManager.resetClockCache();
    showReadyForPirState(true, true);
  }
  updateClock();
  updateSensors();
  handleEncoder();
  handleEncoderButton();
  updateTimer();
  updateMusic();
  batteryMonitor.update();
  updateDisplayIfNeeded();
  handleLightSleep();
}

void handleSerialCommands() {
  if (!Serial.available()) {
    return;
  }

  char command = Serial.read();
  if (command == 't' || command == 'T') {
    testPiezoOutputs();
  }
}

void handleEncoder() {
  int direction = inputController.readEncoderStep();

  if (direction == 0) {
    return;
  }

  if (mode == MODE_SET_HOUR) {
    settingHour = wrapValue(settingHour + direction, 0, 23);
    restartSettingBlink();
    showClockSetting();
    return;
  }

  if (mode == MODE_SET_MINUTE) {
    settingMinute = wrapValue(settingMinute + direction, 0, 59);
    restartSettingBlink();
    showClockSetting();
    return;
  }

  if (mode == MODE_MUSIC) {
    stopMusic();
  }

  int change = direction * STEP_SECONDS;
  remainingSeconds += change;

  if (remainingSeconds < 0) {
    remainingSeconds = 0;
  }

  if (remainingSeconds > MAX_SECONDS) {
    remainingSeconds = MAX_SECONDS;
  }

  if (remainingSeconds > 0) {
    uint32_t now = millis();
    mode = MODE_TIMER;
    nextTimerTickMs = now + TIMER_START_DELAY_MS;
    timerBlinkStartedAtMs = now + TIMER_COLON_BLINK_DELAY_MS;
    displayManager.setTimerColonCache(true);
    displayManager.showTimer(remainingSeconds, timerColonVisible());
  } else {
    enterReadyMode();
  }

  Serial.print("Timer set to ");
  Serial.print(remainingSeconds);
  Serial.println(" seconds");
}

int wrapValue(int value, int minimumValue, int maximumValue) {
  if (value < minimumValue) {
    return maximumValue;
  }

  if (value > maximumValue) {
    return minimumValue;
  }

  return value;
}

void handleEncoderButton() {
  if (inputController.buttonPressed()) {
    handleButtonPress();
  }
}

void handleButtonPress() {
  keepDisplayOnAfterButtonPress();

  if (mode == MODE_SET_HOUR) {
    mode = MODE_SET_MINUTE;
    restartSettingBlink();
    showClockSetting();
    return;
  }

  if (mode == MODE_SET_MINUTE) {
    clockSync.setManualTime(settingHour, settingMinute);
    enterReadyMode();
    Serial.print("Clock set to ");
    printClockToSerial();
    return;
  }

  if (mode == MODE_TIMER) {
    remainingSeconds = 0;
    enterReadyMode();
    Serial.println("Timer reset by button.");
    return;
  }

  if (mode == MODE_MUSIC) {
    stopMusic();
    Serial.println("Music stopped by button.");
    return;
  }

  if (mode == MODE_READY) {
    startMusic(false);
  }
}

void keepDisplayOnAfterButtonPress() {
  pirMotion.keepDisplayOn(BUTTON_DISPLAY_HOLD_MS);
}

void updateClock() {
  clockSync.updateClock(mode == MODE_SET_HOUR || mode == MODE_SET_MINUTE);
}

void updateTimer() {
  if (mode != MODE_TIMER) {
    return;
  }

  uint32_t now = millis();

  while ((int32_t)(now - nextTimerTickMs) >= 0 && remainingSeconds > 0) {
    nextTimerTickMs += 1000;
    remainingSeconds--;
    restartTimerBlink();
  }

  if (remainingSeconds == 0) {
    startMusic(true);
  }
}

void updateDisplayIfNeeded() {
  if (mode == MODE_SET_HOUR || mode == MODE_SET_MINUTE) {
    if (batteryMonitor.displayDirty() || displayManager.settingNeedsUpdate(settingFieldVisible())) {
      showClockSetting();
    }
    return;
  }

  if (mode == MODE_MUSIC) {
    if (alarmPlayer.activePiezoEnabled()) {
      if (batteryMonitor.displayDirty()) {
        displayManager.showMusic(remainingSeconds);
      }
    } else {
      displayManager.showClock(batteryMonitor.displayDirty(), "MUSIC", clockSync.hour(), clockSync.minute(), clockSync.second());
    }
    return;
  }

  if (mode == MODE_READY) {
    showReadyForPirState(batteryMonitor.displayDirty());
    return;
  }

  if (mode == MODE_TIMER &&
      (batteryMonitor.displayDirty() || displayManager.timerNeedsUpdate(remainingSeconds, timerColonVisible()))) {
    displayManager.showTimer(remainingSeconds, timerColonVisible());
  }
}

void showClockSetting() {
  displayManager.showClockSetting(
    settingHour,
    settingMinute,
    mode == MODE_SET_HOUR,
    mode == MODE_SET_MINUTE,
    settingFieldVisible()
  );
}

bool timerColonVisible() {
  if ((int32_t)(millis() - timerBlinkStartedAtMs) < 0) {
    return true;
  }

  return ((millis() - timerBlinkStartedAtMs) / 500) % 2 == 0;
}

bool settingFieldVisible() {
  return ((millis() - settingBlinkStartedAtMs) / 500) % 2 == 0;
}

void restartSettingBlink() {
  settingBlinkStartedAtMs = millis();
}

void restartTimerBlink() {
  timerBlinkStartedAtMs = millis();
  displayManager.setTimerColonCache(true);
}

void enterReadyMode() {
  mode = MODE_READY;
  remainingSeconds = 0;
  displayManager.resetTimerCache();
  showReadyForPirState(true, true);
}

void showReadyForPirState(bool force, bool forceDisplayOn) {
  bool syncActive = clockSync.isSyncActive();
  bool displayShouldStayOn = forceDisplayOn || syncActive || pirMotion.motionConfirmed() || pirMotion.displayHoldActive();

  if (displayShouldStayOn) {
    if (!clockSync.hasValidTime()) {
      displayManager.showSyncStatus(force, clockSync);
    } else {
      displayManager.showClock(force, "READY", clockSync.hour(), clockSync.minute(), clockSync.second());
    }
    return;
  }

  displayManager.blank();
}

void handleLightSleep() {
  if (!ENABLE_LIGHT_SLEEP || mode != MODE_READY || !displayManager.isBlank() || alarmPlayer.isActive() || alarmPlayer.isPlaying()) {
    return;
  }

  if (clockSync.isSyncActive()) {
    return;
  }

  if (!pirMotion.idleForAtLeast(LIGHT_SLEEP_AFTER_MS)) {
    return;
  }

  if (pirMotion.motionConfirmed()) {
    pirMotion.keepDisplayOn(PIR_DISPLAY_HOLD_MS);
    showReadyForPirState(true, true);
    return;
  }

  enterLightSleep();
}

void enterDeepSleep() {
  Serial.println("Entering deep sleep. PIR motion will reboot the timer.");
  Serial.flush();

  clockSync.disconnectAndDisableWifi();
  displayManager.setPower(false);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << PIR_PIN, ESP_GPIO_WAKEUP_GPIO_HIGH);
  esp_deep_sleep_start();
}

void enterLightSleep() {
  Serial.println("Entering light sleep. PIR motion or 5-minute timer wakes the timer.");
  Serial.flush();

  displayManager.setPower(false);
  gpio_wakeup_enable((gpio_num_t)PIR_PIN, GPIO_INTR_HIGH_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup(LIGHT_SLEEP_TO_DEEP_SLEEP_US);
  esp_light_sleep_start();
  gpio_wakeup_disable((gpio_num_t)PIR_PIN);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);

  delay(50);
  Serial.begin(115200);
  esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();

  if (wakeCause == ESP_SLEEP_WAKEUP_TIMER && !pirMotion.rawMotionDetected()) {
    Serial.println("Light sleep lasted 5 minutes without motion. Escalating to deep sleep.");
    enterDeepSleep();
  }

  Serial.println("Woke from light sleep.");

  bool wokeForMotion = wakeCause == ESP_SLEEP_WAKEUP_GPIO && pirMotion.rawMotionDetected();
  if (wokeForMotion) {
    pirMotion.keepDisplayOn(PIR_DISPLAY_HOLD_MS);
  }

  batteryMonitor.update(true);
  if (wokeForMotion) {
    showReadyForPirState(true, true);
  } else {
    displayManager.blank();
  }
}

void startMusic(bool includeActivePiezo) {
  if (!alarmPlayer.start(includeActivePiezo)) {
    enterReadyMode();
    return;
  }

  mode = MODE_MUSIC;
  if (includeActivePiezo) {
    displayManager.showMusic(remainingSeconds);
  } else {
    displayManager.showClock(true, "MUSIC", clockSync.hour(), clockSync.minute(), clockSync.second());
  }
}

void stopMusic() {
  if (!alarmPlayer.isActive() && !alarmPlayer.isPlaying()) {
    enterReadyMode();
    return;
  }

  alarmPlayer.stop();
  enterReadyMode();
}

void updateMusic() {
  if (mode != MODE_MUSIC) {
    return;
  }

  if (!alarmPlayer.update()) {
    stopMusic();
  }
}

void updateSensors() {
  if (pirMotion.update() && mode == MODE_READY) {
    showReadyForPirState(true, true);
  }

  bool soundActivation = soundTrigger.update();

  if (mode != MODE_MUSIC) {
    return;
  }

  if (alarmPlayer.shouldIgnoreSound()) {
    return;
  }

  if (soundActivation) {
    Serial.println("Sound burst detected after active piezo section. Stopping music.");
    stopMusic();
  }
}

void testPiezoOutputs() {
  if (mode == MODE_MUSIC) {
    stopMusic();
  }

  alarmPlayer.testOutputs();
}

void printClockToSerial() {
  uint8_t hour12 = clockSync.hour() % 12;
  if (hour12 == 0) {
    hour12 = 12;
  }

  if (hour12 < 10) {
    Serial.print("0");
  }
  Serial.print(hour12);
  Serial.print(":");
  if (clockSync.minute() < 10) {
    Serial.print("0");
  }
  Serial.print(clockSync.minute());
  Serial.println(clockSync.hour() < 12 ? " AM" : " PM");
}
