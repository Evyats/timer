#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

#include "AlarmPlayer.h"
#include "BatteryMonitor.h"
#include "ClockSync.h"
#include "DisplayManager.h"
#include "InputController.h"
#include "PirMotion.h"
#include "SleepManager.h"
#include "SoundTrigger.h"
#include "TimerConfig.h"
#include "TimerController.h"
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
SleepManager sleepManager(
  PIR_PIN,
  PIR_DISPLAY_HOLD_MS,
  LIGHT_SLEEP_TO_DEEP_SLEEP_US,
  displayManager,
  clockSync,
  pirMotion,
  batteryMonitor
);
TimerController timerController(
  STEP_SECONDS,
  MAX_SECONDS,
  TIMER_START_DELAY_MS,
  TIMER_COLON_BLINK_DELAY_MS
);

DeviceMode mode = MODE_SET_HOUR;

void setup() {
  Serial.begin(115200);
  randomSeed(micros());

  inputController.begin();
  pirMotion.begin();
  soundTrigger.begin();
  alarmPlayer.begin();
  timerController.begin();

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
  Serial.println(sleepManager.wakeupCauseCode());
  Serial.println("PIR warmup running in background.");
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
    timerController.adjustSettingHour(direction);
    displayManager.showClockSetting(
      timerController.settingHour(),
      timerController.settingMinute(),
      true,
      false,
      timerController.settingFieldVisible()
    );
    return;
  }

  if (mode == MODE_SET_MINUTE) {
    timerController.adjustSettingMinute(direction);
    displayManager.showClockSetting(
      timerController.settingHour(),
      timerController.settingMinute(),
      false,
      true,
      timerController.settingFieldVisible()
    );
    return;
  }

  if (mode == MODE_MUSIC) {
    stopMusic();
  }

  timerController.adjustTimer(direction);
  if (timerController.remainingSeconds() > 0) {
    mode = MODE_TIMER;
    displayManager.setTimerColonCache(true);
    displayManager.showTimer(timerController.remainingSeconds(), timerController.timerColonVisible());
  } else {
    enterReadyMode();
  }

  Serial.print("Timer set to ");
  Serial.print(timerController.remainingSeconds());
  Serial.println(" seconds");
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
    timerController.restartSettingBlink();
    displayManager.showClockSetting(
      timerController.settingHour(),
      timerController.settingMinute(),
      false,
      true,
      timerController.settingFieldVisible()
    );
    return;
  }

  if (mode == MODE_SET_MINUTE) {
    clockSync.setManualTime(timerController.settingHour(), timerController.settingMinute());
    enterReadyMode();
    Serial.print("Clock set to ");
    printClockToSerial();
    return;
  }

  if (mode == MODE_TIMER) {
    timerController.resetTimer();
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

  bool timerDone = timerController.updateCountdown();
  if (timerDone) {
    displayManager.setTimerColonCache(true);
    startMusic(true);
  }
}

void updateDisplayIfNeeded() {
  if (mode == MODE_SET_HOUR || mode == MODE_SET_MINUTE) {
    if (batteryMonitor.displayDirty() || displayManager.settingNeedsUpdate(timerController.settingFieldVisible())) {
      displayManager.showClockSetting(
        timerController.settingHour(),
        timerController.settingMinute(),
        mode == MODE_SET_HOUR,
        mode == MODE_SET_MINUTE,
        timerController.settingFieldVisible()
      );
    }
    return;
  }

  if (mode == MODE_MUSIC) {
    if (alarmPlayer.activePiezoEnabled()) {
      if (batteryMonitor.displayDirty()) {
        displayManager.showMusic(timerController.remainingSeconds());
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
      (batteryMonitor.displayDirty() ||
       displayManager.timerNeedsUpdate(timerController.remainingSeconds(), timerController.timerColonVisible()))) {
    displayManager.showTimer(timerController.remainingSeconds(), timerController.timerColonVisible());
  }
}

void enterReadyMode() {
  mode = MODE_READY;
  timerController.resetTimer();
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

  LightSleepResult sleepResult = sleepManager.enterLightSleep();
  if (sleepResult.wokeForMotion) {
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
    displayManager.showMusic(timerController.remainingSeconds());
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
