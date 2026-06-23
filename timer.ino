#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <PiezoMidiPlayer.h>
#include <WiFi.h>
#include <Wire.h>
#include <time.h>

#include "BatteryMonitor.h"
#include "songs/Songs.h"
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

enum TimeSyncState {
  TIME_SYNC_IDLE,
  TIME_SYNC_CONNECTING_WIFI,
  TIME_SYNC_WAITING_FOR_TIME,
  TIME_SYNC_FAILED,
  TIME_SYNC_COMPLETE
};

void showReadyForPirState(bool force, bool forceDisplayOn = false);

const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;
const int HEADER_HEIGHT = 16;
const int CONTENT_TOP = HEADER_HEIGHT + 1;
const int CONTENT_HEIGHT = SCREEN_HEIGHT - HEADER_HEIGHT - 1;
const int CLOCK_TEXT_SIZE = 3;
const int CLOCK_TEXT_HEIGHT = 8 * CLOCK_TEXT_SIZE;
const int CLOCK_AM_PM_GAP = 6;
const int CLOCK_AM_PM_Y_OFFSET = 8;
const int OLED_ADDRESS = 0x3C;
const bool SHOW_SECTION_BORDERS = false;
const bool SHOW_STATE_TEXT = true;
const bool SHOW_BATTERY_TEXT = true;
const bool CENTER_BATTERY_ICON = false;
const bool USE_12_HOUR_CLOCK = false;

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

const PiezoVoice PIEZO_VOICES_WITH_ACTIVE[] = {
  { ACTIVE_PIEZO_PIN, 0, true },
  { MAIN_PIEZO_PIN, 1 },
  { HARMONY_PIEZO_PIN, 2 },
};

const PiezoVoice PIEZO_VOICES_PASSIVE_ONLY[] = {
  { ACTIVE_PIEZO_PIN, 0, true, true },
  { MAIN_PIEZO_PIN, 1 },
  { HARMONY_PIEZO_PIN, 2 },
};

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
PiezoPlayer piezoPlayer;
BatteryMonitor batteryMonitor(
  BATTERY_ADC_PIN,
  BATTERY_R1_OHMS,
  BATTERY_R2_OHMS,
  BATTERY_ADC_SAMPLES,
  BATTERY_PERCENT_SMOOTHING_WINDOW,
  BATTERY_UPDATE_MS,
  BATTERY_SAMPLE_INTERVAL_MS
);
const PiezoSong* currentSong = nullptr;

DeviceMode mode = MODE_SET_HOUR;
TimeSyncState timeSyncState = TIME_SYNC_IDLE;

int remainingSeconds = 0;
uint32_t nextTimerTickMs = 0;
uint32_t timerBlinkStartedAtMs = 0;
int lastDisplayedSeconds = -1;
bool lastDisplayedTimerColon = false;

uint8_t clockHour = 0;
uint8_t clockMinute = 0;
uint8_t clockSecond = 0;
uint8_t settingHour = 0;
uint8_t settingMinute = 0;
unsigned long lastClockTickMs = 0;
TimeSyncState lastDisplayedSyncState = TIME_SYNC_IDLE;
int lastDisplayedClockHour = -1;
int lastDisplayedClockMinute = -1;
bool lastDisplayedClockColon = false;
bool lastDisplayedSettingBlink = true;
uint32_t settingBlinkStartedAtMs = 0;

bool alarmPlaying = false;
bool activePiezoEnabled = false;
bool activePiezoLedOn = false;
uint32_t nextActivePiezoLedEvent = 0;
uint32_t soundIgnoredUntilSongMs = 0;

int lastPirState = LOW;
int lastPirReading = LOW;
uint32_t pirHighStartedAtMs = 0;
bool pirMotionConfirmed = false;
uint32_t pirDisplayUntilMs = 0;
int lastSoundState = HIGH;
bool displayBlank = false;
uint8_t soundPulseCount = 0;
uint32_t firstSoundPulseMs = 0;
uint32_t lastSoundActivationMs = 0;

bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
uint32_t lastButtonChangeMs = 0;
bool clockHasValidTime = false;
uint32_t timeSyncStartedAtMs = 0;
wl_status_t lastLoggedWifiStatus = WL_IDLE_STATUS;
uint32_t pirWarmupUntilMs = 0;
bool displayPowerEnabled = true;

void setup() {
  Serial.begin(115200);
  randomSeed(micros());

  pinMode(ENCODER_S1_PIN, INPUT_PULLUP);
  pinMode(ENCODER_S2_PIN, INPUT_PULLUP);
  pinMode(ENCODER_BUTTON_PIN, INPUT_PULLUP);
  pinMode(PIR_PIN, INPUT);
  pinMode(SOUND_PIN, INPUT);

  if (ACTIVE_PIEZO_LED_PIN != ACTIVE_PIEZO_PIN) {
    pinMode(ACTIVE_PIEZO_LED_PIN, OUTPUT);
    digitalWrite(ACTIVE_PIEZO_LED_PIN, LOW);
  }
  configurePiezoVoices(true);
  piezoPlayer.setSlideUpdateIntervalMs(PIEZO_SLIDE_UPDATE_INTERVAL_MS);

  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    while (true) {
      delay(100);
    }
  }

  batteryMonitor.begin();

  display.clearDisplay();
  display.display();

  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);

  Serial.println();
  Serial.println("OLED rotary timer");
  Serial.print("Wakeup cause: ");
  Serial.println((int)esp_sleep_get_wakeup_cause());
  Serial.println("PIR warmup running in background.");
  pirWarmupUntilMs = millis() + PIR_WARMUP_MS;
  lastPirState = digitalRead(PIR_PIN);
  lastPirReading = lastPirState;
  pirMotionConfirmed = false;
  lastSoundState = digitalRead(SOUND_PIN);
  lastClockTickMs = millis();
  settingBlinkStartedAtMs = millis();
  mode = MODE_READY;
  beginTimeSync();
  showReadyForPirState(true, true);

  Serial.println("Starting background Wi-Fi time sync.");
  Serial.println("Send 't' in Serial Monitor to test the three piezo outputs.");
}

void loop() {
  handleSerialCommands();
  updateTimeSync();
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

void drawScreenFrame(const char* stateLabel) {
  if (SHOW_SECTION_BORDERS) {
    display.drawRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, SSD1306_WHITE);
    display.drawRect(0, HEADER_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - HEADER_HEIGHT, SSD1306_WHITE);
  }

  if (SHOW_STATE_TEXT) {
    display.setTextSize(1);
    display.setCursor(2, 4);
    display.print(stateLabel);
  }
}

void drawBatteryStatus() {
  const int batteryWidth = 18;
  const int batteryHeight = 7;
  const int batteryX = CENTER_BATTERY_ICON ? (SCREEN_WIDTH - batteryWidth) / 2 : SCREEN_WIDTH - batteryWidth - 3;
  const int batteryY = (HEADER_HEIGHT - batteryHeight) / 2;
  const int terminalWidth = 2;
  const int terminalHeight = 3;
  const int terminalY = batteryY + (batteryHeight - terminalHeight) / 2;
  const int fillWidth = map(constrain(batteryMonitor.stablePercent(), 0, 100), 0, 100, 0, batteryWidth - 4);
  String batteryVoltsText = String(batteryMonitor.batteryVolts(), 2) + "V";
  String percentText = String(batteryMonitor.percent()) + "%";
  const int batteryVoltsTextWidth = batteryVoltsText.length() * 6;
  const int percentTextWidth = percentText.length() * 6;
  const int textY = 4;
  const int percentTextX = batteryX - terminalWidth - 3 - percentTextWidth;
  const int batteryVoltsTextX = percentTextX - 4 - batteryVoltsTextWidth;

  if (SHOW_BATTERY_TEXT) {
    display.setTextSize(1);
    display.setCursor(batteryVoltsTextX, textY);
    display.print(batteryVoltsText);
    display.setCursor(percentTextX, textY);
    display.print(percentText);
  }

  display.drawRect(batteryX, batteryY, batteryWidth, batteryHeight, SSD1306_WHITE);
  display.fillRect(batteryX - terminalWidth, terminalY, terminalWidth, terminalHeight, SSD1306_WHITE);
  display.fillRect(batteryX + batteryWidth - 2 - fillWidth, batteryY + 2, fillWidth, batteryHeight - 4, SSD1306_WHITE);
}

void configurePiezoVoices(bool includeActivePiezo) {
  if (includeActivePiezo) {
    piezoPlayer.begin(PIEZO_VOICES_WITH_ACTIVE, sizeof(PIEZO_VOICES_WITH_ACTIVE) / sizeof(PIEZO_VOICES_WITH_ACTIVE[0]));
  } else {
    piezoPlayer.begin(PIEZO_VOICES_PASSIVE_ONLY, sizeof(PIEZO_VOICES_PASSIVE_ONLY) / sizeof(PIEZO_VOICES_PASSIVE_ONLY[0]));
    digitalWrite(ACTIVE_PIEZO_PIN, LOW);
  }

  writeActivePiezoLed(false);
}

void writeActivePiezoLed(bool on) {
  if (ACTIVE_PIEZO_LED_PIN == ACTIVE_PIEZO_PIN) {
    return;
  }

  digitalWrite(ACTIVE_PIEZO_LED_PIN, on ? HIGH : LOW);
}

void setDisplayPower(bool enabled) {
  if (displayPowerEnabled == enabled) {
    return;
  }

  display.ssd1306_command(enabled ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
  displayPowerEnabled = enabled;
}

void disconnectAndDisableWifi() {
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
}

void beginTimeSync() {
  if (timeSyncState == TIME_SYNC_CONNECTING_WIFI || timeSyncState == TIME_SYNC_WAITING_FOR_TIME) {
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  timeSyncState = TIME_SYNC_CONNECTING_WIFI;
  timeSyncStartedAtMs = millis();
  lastLoggedWifiStatus = WL_IDLE_STATUS;
  Serial.print("Connecting to Wi-Fi SSID ");
  Serial.println(WIFI_SSID);
}

void finishTimeSync(bool success) {
  disconnectAndDisableWifi();

  if (success) {
    timeSyncState = TIME_SYNC_COMPLETE;
    Serial.println("Wi-Fi disconnected. Time sync complete.");
  } else {
    timeSyncState = TIME_SYNC_FAILED;
    Serial.println("Wi-Fi disconnected. Time sync failed.");
    if (mode == MODE_READY) {
      showReadyForPirState(true, true);
    }
  }
}

void applySyncedClock() {
  time_t nowSeconds = time(nullptr);
  struct tm timeInfo;
  localtime_r(&nowSeconds, &timeInfo);

  clockHasValidTime = true;
  clockHour = timeInfo.tm_hour;
  clockMinute = timeInfo.tm_min;
  clockSecond = timeInfo.tm_sec;
  lastClockTickMs = millis();
  lastDisplayedClockHour = -1;
  lastDisplayedClockMinute = -1;
  lastDisplayedClockColon = false;

  Serial.print("Clock synced from NTP: ");
  printClockToSerial();

  if (mode == MODE_READY) {
    showReadyForPirState(true, true);
  }
}

void updateTimeSync() {
  if (timeSyncState == TIME_SYNC_IDLE ||
      timeSyncState == TIME_SYNC_COMPLETE ||
      timeSyncState == TIME_SYNC_FAILED) {
    return;
  }

  uint32_t now = millis();
  if (now - timeSyncStartedAtMs >= TIME_SYNC_TOTAL_TIMEOUT_MS) {
    Serial.println("Time sync timed out.");
    finishTimeSync(false);
    return;
  }

  if (timeSyncState == TIME_SYNC_CONNECTING_WIFI) {
    wl_status_t wifiStatus = WiFi.status();
    if (wifiStatus != lastLoggedWifiStatus) {
      lastLoggedWifiStatus = wifiStatus;
      Serial.print("Wi-Fi status changed: ");
      Serial.println((int)wifiStatus);
    }

    if (wifiStatus == WL_CONNECTED) {
      Serial.print("Wi-Fi connected. IP: ");
      Serial.println(WiFi.localIP());
      configTzTime(ISRAEL_TZ, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
      timeSyncState = TIME_SYNC_WAITING_FOR_TIME;
      Serial.println("Waiting for NTP time...");
      return;
    }
    return;
  }

  if (timeSyncState != TIME_SYNC_WAITING_FOR_TIME) {
    return;
  }

  time_t nowSeconds = time(nullptr);
  if (nowSeconds > 1700000000) {
    applySyncedClock();
    finishTimeSync(true);
    return;
  }
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
  int direction = readEncoderStep();

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
    lastDisplayedTimerColon = true;
    showTimer();
  } else {
    enterReadyMode();
  }

  Serial.print("Timer set to ");
  Serial.print(remainingSeconds);
  Serial.println(" seconds");
}

int readEncoderStep() {
  static int lastState = 0;
  static int movement = 0;
  static bool initialized = false;

  int s1 = digitalRead(ENCODER_S1_PIN);
  int s2 = digitalRead(ENCODER_S2_PIN);
  int state = (s1 << 1) | s2;

  if (!initialized) {
    lastState = state;
    initialized = true;
    printEncoderRawState(s1, s2, state);
    return 0;
  }

  if (state == lastState) {
    return 0;
  }

  printEncoderRawState(s1, s2, state);

  int transition = (lastState << 2) | state;
  lastState = state;
  int direction = 0;

  switch (transition) {
    case 0b0001:
    case 0b0111:
    case 0b1110:
    case 0b1000:
      direction = 1;
      break;

    case 0b0010:
    case 0b1011:
    case 0b1101:
    case 0b0100:
      direction = -1;
      break;

    default:
      return 0;
  }

  movement += direction;

  if (movement >= 4) {
    movement = 0;
    return REVERSE_ENCODER_DIRECTION ? -1 : 1;
  }

  if (movement <= -4) {
    movement = 0;
    return REVERSE_ENCODER_DIRECTION ? 1 : -1;
  }

  return 0;
}

void printEncoderRawState(int s1, int s2, int state) {
  if (!LOG_ENCODER_RAW_STATES) {
    return;
  }

  Serial.print(millis());
  Serial.print(" ms encoder rotated: S1 GPIO");
  Serial.print(ENCODER_S1_PIN);
  Serial.print("=");
  Serial.print(s1 == HIGH ? "HIGH" : "LOW");
  Serial.print(" S2 GPIO");
  Serial.print(ENCODER_S2_PIN);
  Serial.print("=");
  Serial.print(s2 == HIGH ? "HIGH" : "LOW");
  Serial.print(" BUTTON GPIO");
  Serial.print(ENCODER_BUTTON_PIN);
  Serial.print("=");
  Serial.print(digitalRead(ENCODER_BUTTON_PIN) == HIGH ? "HIGH" : "LOW");
  Serial.print(" state=");
  Serial.println(state, BIN);
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
  bool reading = digitalRead(ENCODER_BUTTON_PIN);
  uint32_t now = millis();

  if (reading != lastButtonReading) {
    lastButtonChangeMs = now;
    lastButtonReading = reading;
  }

  if (now - lastButtonChangeMs < BUTTON_DEBOUNCE_MS) {
    return;
  }

  if (reading == stableButtonState) {
    return;
  }

  stableButtonState = reading;

  if (stableButtonState == LOW) {
    handleButtonPress();
  }
}

void handleButtonPress() {
  if (LOG_ENCODER_RAW_STATES) {
    Serial.print(millis());
    Serial.print(" ms encoder button pressed: S1 GPIO");
    Serial.print(ENCODER_S1_PIN);
    Serial.print("=");
    Serial.print(digitalRead(ENCODER_S1_PIN) == HIGH ? "HIGH" : "LOW");
    Serial.print(" S2 GPIO");
    Serial.print(ENCODER_S2_PIN);
    Serial.print("=");
    Serial.print(digitalRead(ENCODER_S2_PIN) == HIGH ? "HIGH" : "LOW");
    Serial.print(" BUTTON GPIO");
    Serial.print(ENCODER_BUTTON_PIN);
    Serial.print("=");
    Serial.println(digitalRead(ENCODER_BUTTON_PIN) == HIGH ? "HIGH" : "LOW");
  }

  keepDisplayOnAfterButtonPress();

  if (mode == MODE_SET_HOUR) {
    mode = MODE_SET_MINUTE;
    restartSettingBlink();
    showClockSetting();
    return;
  }

  if (mode == MODE_SET_MINUTE) {
    clockHour = settingHour;
    clockMinute = settingMinute;
    clockSecond = 0;
    lastClockTickMs = millis();
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
  pirDisplayUntilMs = millis() + BUTTON_DISPLAY_HOLD_MS;
}

void updateClock() {
  if (mode == MODE_SET_HOUR || mode == MODE_SET_MINUTE) {
    lastClockTickMs = millis();
    return;
  }

  if (!clockHasValidTime) {
    lastClockTickMs = millis();
    return;
  }

  uint32_t now = millis();
  uint32_t elapsedMs = now - lastClockTickMs;
  uint32_t elapsedSeconds = elapsedMs / 1000;
  if (elapsedSeconds == 0) {
    return;
  }

  lastClockTickMs += elapsedSeconds * 1000;

  uint32_t totalSeconds = clockSecond + elapsedSeconds;
  clockSecond = totalSeconds % 60;

  uint32_t totalMinutes = clockMinute + (totalSeconds / 60);
  clockMinute = totalMinutes % 60;
  clockHour = (clockHour + (totalMinutes / 60)) % 24;
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
    if (batteryMonitor.displayDirty() || settingFieldVisible() != lastDisplayedSettingBlink) {
      showClockSetting();
    }
    return;
  }

  if (mode == MODE_MUSIC) {
    if (activePiezoEnabled) {
      if (batteryMonitor.displayDirty()) {
        showMusic();
      }
    } else {
      showClock(batteryMonitor.displayDirty(), "MUSIC");
    }
    return;
  }

  if (mode == MODE_READY) {
    showReadyForPirState(batteryMonitor.displayDirty());
    return;
  }

  if (mode == MODE_TIMER &&
      (batteryMonitor.displayDirty() || remainingSeconds != lastDisplayedSeconds || timerColonVisible() != lastDisplayedTimerColon)) {
    showTimer();
  }
}

const char* syncStatusText() {
  switch (timeSyncState) {
    case TIME_SYNC_CONNECTING_WIFI:
      return "WIFI...";
    case TIME_SYNC_WAITING_FOR_TIME:
      return "NTP...";
    case TIME_SYNC_FAILED:
      return "NO WIFI";
    case TIME_SYNC_COMPLETE:
      return "DONE";
    case TIME_SYNC_IDLE:
    default:
      return "IDLE";
  }
}

void showClockSetting() {
  setDisplayPower(true);
  displayBlank = false;
  lastDisplayedSeconds = -1;
  lastDisplayedClockHour = -1;
  lastDisplayedClockMinute = -1;
  lastDisplayedSettingBlink = settingFieldVisible();

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  drawScreenFrame(mode == MODE_SET_HOUR ? "SET H" : "SET M");
  drawBatteryStatus();

  drawClockTime(settingHour, settingMinute, true, mode != MODE_SET_HOUR || lastDisplayedSettingBlink, mode != MODE_SET_MINUTE || lastDisplayedSettingBlink);

  display.display();
  batteryMonitor.clearDisplayDirty();
}

void showSyncStatus(bool force) {
  if (!force &&
      !displayBlank &&
      timeSyncState == lastDisplayedSyncState &&
      !batteryMonitor.displayDirty()) {
    return;
  }

  setDisplayPower(true);
  displayBlank = false;
  lastDisplayedSeconds = -1;
  lastDisplayedClockHour = -1;
  lastDisplayedClockMinute = -1;
  lastDisplayedSyncState = timeSyncState;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  drawScreenFrame("SYNC");
  drawBatteryStatus();

  display.setTextSize(2);
  const char* status = syncStatusText();
  int statusWidth = strlen(status) * 12;
  int statusX = (SCREEN_WIDTH - statusWidth) / 2;
  int statusY = CONTENT_TOP + 8;
  display.setCursor(statusX, statusY);
  display.print(status);

  display.setTextSize(1);
  int detailY = statusY + 24;
  if (timeSyncState == TIME_SYNC_CONNECTING_WIFI) {
    display.setCursor(10, detailY);
    display.print(WIFI_SSID);
  } else if (timeSyncState == TIME_SYNC_WAITING_FOR_TIME) {
    display.setCursor(22, detailY);
    display.print("Israel time");
  } else if (timeSyncState == TIME_SYNC_FAILED) {
    display.setCursor(12, detailY);
    display.print("sync timeout");
  }

  display.display();
  batteryMonitor.clearDisplayDirty();
}

void showTimer() {
  setDisplayPower(true);
  lastDisplayedSeconds = remainingSeconds;
  lastDisplayedTimerColon = timerColonVisible();
  displayBlank = false;

  int minutes = remainingSeconds / 60;
  int secs = remainingSeconds % 60;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  drawScreenFrame("TIMER");
  drawBatteryStatus();

  display.setTextSize(3);
  display.setCursor(18, 28);
  printTwoDigits(minutes);
  display.print(lastDisplayedTimerColon ? ":" : " ");
  printTwoDigits(secs);

  display.display();
  batteryMonitor.clearDisplayDirty();
}

void showMusic() {
  setDisplayPower(true);
  lastDisplayedSeconds = remainingSeconds;
  displayBlank = false;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  drawScreenFrame("MUSIC");
  drawBatteryStatus();

  display.setTextSize(3);
  display.setCursor(18, 28);
  printTwoDigits(remainingSeconds / 60);
  display.print(":");
  printTwoDigits(remainingSeconds % 60);

  display.display();
  batteryMonitor.clearDisplayDirty();
}

void showClock(bool force, const char* label) {
  bool colonVisible = clockSecond % 2 == 0;

  if (!force &&
      !displayBlank &&
      clockHour == lastDisplayedClockHour &&
      clockMinute == lastDisplayedClockMinute &&
      colonVisible == lastDisplayedClockColon) {
    return;
  }

  setDisplayPower(true);
  displayBlank = false;
  lastDisplayedSeconds = -1;
  lastDisplayedClockHour = clockHour;
  lastDisplayedClockMinute = clockMinute;
  lastDisplayedClockColon = colonVisible;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  drawScreenFrame(label);
  drawBatteryStatus();

  drawClockTime(clockHour, clockMinute, colonVisible, true, true);

  display.display();
  batteryMonitor.clearDisplayDirty();
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

bool blinkVisible() {
  return (millis() / 500) % 2 == 0;
}

void restartSettingBlink() {
  settingBlinkStartedAtMs = millis();
  lastDisplayedSettingBlink = true;
}

void restartTimerBlink() {
  timerBlinkStartedAtMs = millis();
  lastDisplayedTimerColon = true;
}

void printTwoDigits(int value) {
  if (value < 10) {
    display.print("0");
  }
  display.print(value);
}

void drawClockTime(uint8_t hour24, uint8_t minute, bool colonVisible, bool showHour, bool showMinute) {
  uint8_t displayHour = USE_12_HOUR_CLOCK ? hour12Value(hour24) : hour24;
  int hourDigits = USE_12_HOUR_CLOCK ? (displayHour < 10 ? 1 : 2) : 2;
  int timeChars = hourDigits + 1 + 2;
  int timeWidth = timeChars * 6 * CLOCK_TEXT_SIZE;
  int amPmWidth = USE_12_HOUR_CLOCK ? 2 * 6 : 0;
  int blockWidth = timeWidth + (USE_12_HOUR_CLOCK ? CLOCK_AM_PM_GAP + amPmWidth : 0);
  int timeX = (SCREEN_WIDTH - blockWidth) / 2;
  int timeY = CONTENT_TOP + (CONTENT_HEIGHT - CLOCK_TEXT_HEIGHT) / 2;
  int amPmX = timeX + timeWidth + CLOCK_AM_PM_GAP;
  int amPmY = timeY + CLOCK_AM_PM_Y_OFFSET;

  display.setTextSize(CLOCK_TEXT_SIZE);
  display.setCursor(timeX, timeY);
  if (showHour) {
    if (!USE_12_HOUR_CLOCK && displayHour < 10) {
      display.print("0");
    }
    display.print(displayHour);
  } else {
    for (int i = 0; i < hourDigits; i++) {
      display.print(" ");
    }
  }
  display.print(colonVisible ? ":" : " ");
  if (showMinute) {
    printTwoDigits(minute);
  } else {
    display.print("  ");
  }

  if (USE_12_HOUR_CLOCK) {
    display.setTextSize(1);
    display.setCursor(amPmX, amPmY);
    display.print(hour24 < 12 ? "AM" : "PM");
  }
}

void printHour12(uint8_t hour24) {
  uint8_t hour12 = hour24 % 12;
  if (hour12 == 0) {
    hour12 = 12;
  }

  display.print(hour12);
}

uint8_t hour12Value(uint8_t hour24) {
  uint8_t hour12 = hour24 % 12;
  return hour12 == 0 ? 12 : hour12;
}

void enterReadyMode() {
  mode = MODE_READY;
  remainingSeconds = 0;
  lastDisplayedSeconds = -1;
  showReadyForPirState(true, true);
}

void showReadyForPirState(bool force, bool forceDisplayOn) {
  bool syncActive = timeSyncState == TIME_SYNC_CONNECTING_WIFI || timeSyncState == TIME_SYNC_WAITING_FOR_TIME;
  bool displayShouldStayOn = forceDisplayOn || syncActive || pirMotionConfirmed || millis() < pirDisplayUntilMs;

  if (displayShouldStayOn) {
    if (!clockHasValidTime) {
      showSyncStatus(force);
    } else {
      showClock(force, "READY");
    }
    return;
  }

  blankDisplay();
}

void blankDisplay() {
  if (displayBlank) {
    batteryMonitor.clearDisplayDirty();
    return;
  }

  display.clearDisplay();
  display.display();
  displayBlank = true;
  batteryMonitor.clearDisplayDirty();
  lastDisplayedSeconds = -1;
  lastDisplayedClockHour = -1;
  lastDisplayedClockMinute = -1;
}

void handleLightSleep() {
  if (!ENABLE_LIGHT_SLEEP || mode != MODE_READY || !displayBlank || alarmPlaying || piezoPlayer.isPlaying()) {
    return;
  }

  if (timeSyncState == TIME_SYNC_CONNECTING_WIFI || timeSyncState == TIME_SYNC_WAITING_FOR_TIME) {
    return;
  }

  uint32_t now = millis();
  if (now - pirDisplayUntilMs < LIGHT_SLEEP_AFTER_MS) {
    return;
  }

  if (pirMotionConfirmed) {
    pirDisplayUntilMs = now + PIR_DISPLAY_HOLD_MS;
    showReadyForPirState(true, true);
    return;
  }

  enterLightSleep();
}

void enterDeepSleep() {
  Serial.println("Entering deep sleep. PIR motion will reboot the timer.");
  Serial.flush();

  disconnectAndDisableWifi();
  setDisplayPower(false);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << PIR_PIN, ESP_GPIO_WAKEUP_GPIO_HIGH);
  esp_deep_sleep_start();
}

void enterLightSleep() {
  Serial.println("Entering light sleep. PIR motion or 5-minute timer wakes the timer.");
  Serial.flush();

  setDisplayPower(false);
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

  if (wakeCause == ESP_SLEEP_WAKEUP_TIMER && digitalRead(PIR_PIN) != PIR_MOTION_STATE) {
    Serial.println("Light sleep lasted 5 minutes without motion. Escalating to deep sleep.");
    enterDeepSleep();
  }

  Serial.println("Woke from light sleep.");

  lastPirState = digitalRead(PIR_PIN);
  bool wokeForMotion = wakeCause == ESP_SLEEP_WAKEUP_GPIO && lastPirState == PIR_MOTION_STATE;
  if (wokeForMotion) {
    pirDisplayUntilMs = millis() + PIR_DISPLAY_HOLD_MS;
  }

  batteryMonitor.update(true);
  if (wokeForMotion) {
    showReadyForPirState(true, true);
  } else {
    blankDisplay();
  }
}

void startMusic(bool includeActivePiezo) {
  currentSong = pickRandomSong();
  if (currentSong == nullptr) {
    Serial.println("No songs configured.");
    enterReadyMode();
    return;
  }

  mode = MODE_MUSIC;
  alarmPlaying = true;
  activePiezoEnabled = includeActivePiezo;
  soundIgnoredUntilSongMs = includeActivePiezo ? findLastActivePiezoEventMs() : 0;
  activePiezoLedOn = false;
  nextActivePiezoLedEvent = 0;
  writeActivePiezoLed(false);
  configurePiezoVoices(includeActivePiezo);
  piezoPlayer.play(*currentSong, false);
  if (includeActivePiezo) {
    showMusic();
  } else {
    showClock(true, "MUSIC");
  }

  Serial.println(includeActivePiezo ? "Playing random alarm song." : "Playing random song without active piezo.");
  if (includeActivePiezo) {
    Serial.print("Sound detector ignored until song position ");
    Serial.print(soundIgnoredUntilSongMs);
    Serial.println(" ms.");
  }
}

void stopMusic() {
  if (!alarmPlaying && !piezoPlayer.isPlaying()) {
    enterReadyMode();
    return;
  }

  piezoPlayer.stop();
  alarmPlaying = false;
  activePiezoEnabled = false;
  soundIgnoredUntilSongMs = 0;
  activePiezoLedOn = false;
  nextActivePiezoLedEvent = 0;
  digitalWrite(ACTIVE_PIEZO_PIN, LOW);
  writeActivePiezoLed(false);
  configurePiezoVoices(true);
  enterReadyMode();
}

const PiezoSong* pickRandomSong() {
  if (TIMER_SONG_COUNT == 0) {
    return nullptr;
  }

  return TIMER_SONGS[random(TIMER_SONG_COUNT)];
}

void updateMusic() {
  if (mode != MODE_MUSIC) {
    return;
  }

  piezoPlayer.update();

  if (!piezoPlayer.isPlaying()) {
    stopMusic();
    return;
  }

  if (activePiezoEnabled) {
    updateActivePiezoLed(piezoPlayer.positionMs());
  }
}

void updateSensors() {
  uint32_t now = millis();
  int pirReading = digitalRead(PIR_PIN);

  if ((int32_t)(now - pirWarmupUntilMs) >= 0) {
    if (pirReading != lastPirReading) {
      lastPirReading = pirReading;
      if (pirReading == PIR_MOTION_STATE) {
        pirHighStartedAtMs = now;
      }
    }

    bool pirStateChanged = false;
    int confirmedPirState = pirMotionConfirmed ? PIR_MOTION_STATE : LOW;

    if (pirReading == PIR_MOTION_STATE) {
      if (!pirMotionConfirmed && (int32_t)(now - pirHighStartedAtMs) >= (int32_t)PIR_CONFIRM_MS) {
        pirMotionConfirmed = true;
        confirmedPirState = PIR_MOTION_STATE;
        pirStateChanged = true;
      }
    } else if (pirMotionConfirmed) {
      pirMotionConfirmed = false;
      confirmedPirState = LOW;
      pirStateChanged = true;
    }

    if (pirMotionConfirmed) {
      pirDisplayUntilMs = now + PIR_DISPLAY_HOLD_MS;
    }

    if (pirStateChanged) {
      lastPirState = confirmedPirState;
      printPirState(confirmedPirState);
      if (mode == MODE_READY) {
        showReadyForPirState(true, true);
      }
    }
  }

  int soundState = digitalRead(SOUND_PIN);
  bool soundActivation = false;

  if (soundState != lastSoundState) {
    if (soundState == SOUND_DETECTED_STATE) {
      soundActivation = recordSoundPulse();
    }

    lastSoundState = soundState;
    if (LOG_RAW_SOUND_EDGES) {
      printSoundState(soundState);
    }
  }

  if (mode != MODE_MUSIC) {
    return;
  }

  if (activePiezoEnabled && piezoPlayer.positionMs() <= soundIgnoredUntilSongMs) {
    return;
  }

  if (soundActivation) {
    Serial.println("Sound burst detected after active piezo section. Stopping music.");
    stopMusic();
  }
}

uint32_t findLastActivePiezoEventMs() {
  if (currentSong == nullptr) {
    return 0;
  }

  uint32_t lastActiveEventMs = 0;

  for (uint32_t i = 0; i < currentSong->eventCount; i++) {
    PiezoEvent event;
    memcpy_P(&event, &currentSong->events[i], sizeof(event));

    if (event.voice == ACTIVE_PIEZO_VOICE) {
      lastActiveEventMs = event.timeMs;
    }
  }

  return lastActiveEventMs;
}

bool recordSoundPulse() {
  uint32_t now = millis();

  if (now - lastSoundActivationMs < SOUND_TRIGGER_COOLDOWN_MS) {
    return false;
  }

  if (soundPulseCount == 0 || now - firstSoundPulseMs > SOUND_TRIGGER_WINDOW_MS) {
    soundPulseCount = 1;
    firstSoundPulseMs = now;
    return false;
  }

  soundPulseCount++;

  if (soundPulseCount < SOUND_TRIGGER_MIN_PULSES) {
    return false;
  }

  soundPulseCount = 0;
  firstSoundPulseMs = 0;
  lastSoundActivationMs = now;
  return true;
}

void testPiezoOutputs() {
  Serial.print("Testing main passive piezo on GPIO ");
  Serial.print(MAIN_PIEZO_PIN);
  Serial.println(".");
  if (mode == MODE_MUSIC) {
    stopMusic();
  }

  ledcWriteTone(MAIN_PIEZO_PIN, PIEZO_SELF_TEST_TONE_HZ);
  delay(PIEZO_SELF_TEST_MS);
  ledcWriteTone(MAIN_PIEZO_PIN, 0);
  delay(150);

  Serial.print("Testing second passive piezo on GPIO ");
  Serial.print(HARMONY_PIEZO_PIN);
  Serial.println(".");
  ledcWriteTone(HARMONY_PIEZO_PIN, PIEZO_SELF_TEST_TONE_HZ);
  delay(PIEZO_SELF_TEST_MS);
  ledcWriteTone(HARMONY_PIEZO_PIN, 0);
  delay(150);

  Serial.print("Testing active piezo and LED on GPIO ");
  Serial.print(ACTIVE_PIEZO_PIN);
  Serial.print(" / GPIO ");
  Serial.print(ACTIVE_PIEZO_LED_PIN);
  Serial.println(".");
  digitalWrite(ACTIVE_PIEZO_PIN, HIGH);
  writeActivePiezoLed(true);
  delay(PIEZO_SELF_TEST_MS);
  digitalWrite(ACTIVE_PIEZO_PIN, LOW);
  writeActivePiezoLed(false);
  configurePiezoVoices(true);

  Serial.println("Piezo output test done.");
}

void printPirState(int state) {
  Serial.print(millis());
  Serial.print(" ms PIR: ");
  Serial.println(state == PIR_MOTION_STATE ? "MOTION detected" : "No motion");
}

void printSoundState(int state) {
  Serial.print(millis());
  Serial.print(" ms sound: ");
  Serial.println(state == SOUND_DETECTED_STATE ? "Sound detected" : "Quiet");
}

void printClockToSerial() {
  uint8_t hour12 = clockHour % 12;
  if (hour12 == 0) {
    hour12 = 12;
  }

  if (hour12 < 10) {
    Serial.print("0");
  }
  Serial.print(hour12);
  Serial.print(":");
  if (clockMinute < 10) {
    Serial.print("0");
  }
  Serial.print(clockMinute);
  Serial.println(clockHour < 12 ? " AM" : " PM");
}

void updateActivePiezoLed(uint32_t songPositionMs) {
  if (currentSong == nullptr) {
    return;
  }

  while (nextActivePiezoLedEvent < currentSong->eventCount) {
    PiezoEvent event;
    memcpy_P(&event, &currentSong->events[nextActivePiezoLedEvent], sizeof(event));

    if (event.timeMs > songPositionMs) {
      break;
    }

    if (event.voice == ACTIVE_PIEZO_VOICE) {
      bool ledOn = event.frequency > 0;
      if (ledOn != activePiezoLedOn) {
        activePiezoLedOn = ledOn;
        writeActivePiezoLed(activePiezoLedOn);
      }
    }

    nextActivePiezoLedEvent++;
  }
}
