#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <PiezoMidiPlayer.h>
#include <Wire.h>

#include "BatteryMonitor.h"
#include "belle.h"

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
const bool SHOW_BATTERY_TEXT = true;

// Regular ESP32 pin assignment:
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

// ESP32-C3 Super Mini pin assignment:
// GPIO2, GPIO8, and GPIO9 are C3 strapping pins. This mapping keeps sensors,
// encoder switches, and OLED I2C off those pins. GPIO2/GPIO8 are output-only here.
const int SDA_PIN = 4;
const int SCL_PIN = 5;
const int BATTERY_ADC_PIN = 0;  // ADC1 pin.
const int ENCODER_S1_PIN = 1;
const int ENCODER_S2_PIN = 3;
const int ENCODER_BUTTON_PIN = 6;
const int PIR_PIN = 20;
const int SOUND_PIN = 21;
const uint8_t MAIN_PIEZO_PIN = 7;
const uint8_t HARMONY_PIEZO_PIN = 8;
const uint8_t ACTIVE_PIEZO_PIN = 2;
const uint8_t ACTIVE_PIEZO_LED_PIN = ACTIVE_PIEZO_PIN;

const bool REVERSE_ENCODER_DIRECTION = true;
const uint32_t BUTTON_DEBOUNCE_MS = 35;

const int PIR_MOTION_STATE = HIGH;
const int SOUND_DETECTED_STATE = LOW;
const unsigned long PIR_WARMUP_MS = 3000;
const uint32_t PIR_DISPLAY_HOLD_MS = 2000;
const uint32_t LIGHT_SLEEP_AFTER_MS = 120000;
const bool ENABLE_LIGHT_SLEEP = true;
const uint8_t SOUND_TRIGGER_MIN_PULSES = 3;
const uint32_t SOUND_TRIGGER_WINDOW_MS = 250;
const uint32_t SOUND_TRIGGER_COOLDOWN_MS = 600;
const bool LOG_RAW_SOUND_EDGES = false;

const uint8_t ACTIVE_PIEZO_VOICE = 2;
const int PIEZO_SELF_TEST_TONE_HZ = 880;
const int PIEZO_SELF_TEST_MS = 300;
const uint16_t PIEZO_SLIDE_UPDATE_INTERVAL_MS = 10;

const int STEP_SECONDS = 3;
const int MAX_SECONDS = 99 * 60 + 59;
const int BATTERY_ADC_SAMPLES = 32;
const int BATTERY_PERCENT_SMOOTHING_WINDOW = 8;
const uint32_t BATTERY_UPDATE_MS = 3000;
const uint32_t BATTERY_SAMPLE_INTERVAL_MS = 2;
const float BATTERY_R1_OHMS = 100000.0;
const float BATTERY_R2_OHMS = 100000.0;

const PiezoVoice PIEZO_VOICES_WITH_ACTIVE[] = {
  { MAIN_PIEZO_PIN, 0 },
  { HARMONY_PIEZO_PIN, 1 },
  { ACTIVE_PIEZO_PIN, 2, true },
};

const PiezoVoice PIEZO_VOICES_PASSIVE_ONLY[] = {
  { MAIN_PIEZO_PIN, 0 },
  { HARMONY_PIEZO_PIN, 1 },
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

DeviceMode mode = MODE_SET_HOUR;

int remainingSeconds = 0;
unsigned long lastTimerTickMs = 0;
uint32_t timerBlinkStartedAtMs = 0;
int lastDisplayedSeconds = -1;
bool lastDisplayedTimerColon = false;

uint8_t clockHour = 0;
uint8_t clockMinute = 0;
uint8_t clockSecond = 0;
uint8_t settingHour = 0;
uint8_t settingMinute = 0;
unsigned long lastClockTickMs = 0;
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
uint32_t pirDisplayUntilMs = 0;
int lastSoundState = HIGH;
bool displayBlank = false;
uint8_t soundPulseCount = 0;
uint32_t firstSoundPulseMs = 0;
uint32_t lastSoundActivationMs = 0;

bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
uint32_t lastButtonChangeMs = 0;

void setup() {
  Serial.begin(115200);

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

  Serial.println();
  Serial.println("OLED rotary timer");
  Serial.println("Warming up PIR sensor...");
  delay(PIR_WARMUP_MS);
  lastPirState = digitalRead(PIR_PIN);
  if (lastPirState == PIR_MOTION_STATE) {
    pirDisplayUntilMs = millis() + PIR_DISPLAY_HOLD_MS;
  }
  lastSoundState = digitalRead(SOUND_PIN);
  lastClockTickMs = millis();
  settingBlinkStartedAtMs = millis();
  showClockSetting();

  Serial.println("Set clock: choose hour, click, choose minute, click.");
  Serial.println("Send 't' in Serial Monitor to test the three piezo outputs.");
}

void loop() {
  handleSerialCommands();
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
  display.drawRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, SSD1306_WHITE);
  display.drawRect(0, HEADER_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - HEADER_HEIGHT, SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(2, 4);
  display.print(stateLabel);
}

void drawBatteryStatus() {
  const int batteryWidth = 18;
  const int batteryHeight = 7;
  const int batteryX = SCREEN_WIDTH - batteryWidth - 3;
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
    mode = MODE_TIMER;
    lastTimerTickMs = millis();
    restartTimerBlink();
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
    return 0;
  }

  if (state == lastState) {
    return 0;
  }

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
    pirDisplayUntilMs = millis() + PIR_DISPLAY_HOLD_MS;
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

void updateClock() {
  if (mode == MODE_SET_HOUR || mode == MODE_SET_MINUTE) {
    lastClockTickMs = millis();
    return;
  }

  uint32_t now = millis();

  while (now - lastClockTickMs >= 1000) {
    lastClockTickMs += 1000;
    clockSecond++;

    if (clockSecond >= 60) {
      clockSecond = 0;
      clockMinute++;
    }

    if (clockMinute >= 60) {
      clockMinute = 0;
      clockHour = (clockHour + 1) % 24;
    }
  }
}

void updateTimer() {
  if (mode != MODE_TIMER) {
    return;
  }

  uint32_t now = millis();

  while (now - lastTimerTickMs >= 1000 && remainingSeconds > 0) {
    lastTimerTickMs += 1000;
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

void showClockSetting() {
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

void showTimer() {
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
  uint8_t hour12 = hour12Value(hour24);
  int hourDigits = hour12 < 10 ? 1 : 2;
  int timeChars = hourDigits + 1 + 2;
  int timeWidth = timeChars * 6 * CLOCK_TEXT_SIZE;
  int amPmWidth = 2 * 6;
  int blockWidth = timeWidth + CLOCK_AM_PM_GAP + amPmWidth;
  int timeX = (SCREEN_WIDTH - blockWidth) / 2;
  int timeY = CONTENT_TOP + (CONTENT_HEIGHT - CLOCK_TEXT_HEIGHT) / 2;
  int amPmX = timeX + timeWidth + CLOCK_AM_PM_GAP;
  int amPmY = timeY + CLOCK_AM_PM_Y_OFFSET;

  display.setTextSize(CLOCK_TEXT_SIZE);
  display.setCursor(timeX, timeY);
  if (showHour) {
    display.print(hour12);
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

  display.setTextSize(1);
  display.setCursor(amPmX, amPmY);
  display.print(hour24 < 12 ? "AM" : "PM");
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
  showReadyForPirState(true);
}

void showReadyForPirState(bool force) {
  bool displayShouldStayOn = digitalRead(PIR_PIN) == PIR_MOTION_STATE || millis() < pirDisplayUntilMs;

  if (displayShouldStayOn) {
    showClock(force, "READY");
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

  uint32_t now = millis();
  if (now - pirDisplayUntilMs < LIGHT_SLEEP_AFTER_MS) {
    return;
  }

  if (digitalRead(PIR_PIN) == PIR_MOTION_STATE) {
    pirDisplayUntilMs = now + PIR_DISPLAY_HOLD_MS;
    showReadyForPirState(true);
    return;
  }

  enterLightSleep();
}

void enterLightSleep() {
  Serial.println("Entering light sleep. PIR motion wakes the timer.");
  Serial.flush();

  display.ssd1306_command(SSD1306_DISPLAYOFF);
  gpio_wakeup_enable((gpio_num_t)PIR_PIN, GPIO_INTR_HIGH_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_light_sleep_start();
  gpio_wakeup_disable((gpio_num_t)PIR_PIN);

  delay(50);
  Serial.begin(115200);
  Serial.println("Woke from light sleep.");

  display.ssd1306_command(SSD1306_DISPLAYON);
  lastPirState = digitalRead(PIR_PIN);
  if (lastPirState == PIR_MOTION_STATE) {
    pirDisplayUntilMs = millis() + PIR_DISPLAY_HOLD_MS;
  }

  batteryMonitor.update(true);
  showReadyForPirState(true);
}

void startMusic(bool includeActivePiezo) {
  mode = MODE_MUSIC;
  alarmPlaying = true;
  activePiezoEnabled = includeActivePiezo;
  soundIgnoredUntilSongMs = includeActivePiezo ? findLastActivePiezoEventMs() : 0;
  activePiezoLedOn = false;
  nextActivePiezoLedEvent = 0;
  writeActivePiezoLed(false);
  configurePiezoVoices(includeActivePiezo);
  piezoPlayer.play(PIEZO_SONG, false);
  if (includeActivePiezo) {
    showMusic();
  } else {
    showClock(true, "MUSIC");
  }

  Serial.println(includeActivePiezo ? "Playing Belle alarm." : "Playing Belle without active piezo.");
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
  int pirState = digitalRead(PIR_PIN);
  if (pirState == PIR_MOTION_STATE) {
    pirDisplayUntilMs = millis() + PIR_DISPLAY_HOLD_MS;
  }

  if (pirState != lastPirState) {
    lastPirState = pirState;
    printPirState(pirState);

    if (mode == MODE_READY) {
      showReadyForPirState(true);
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
  uint32_t lastActiveEventMs = 0;

  for (uint32_t i = 0; i < PIEZO_SONG.eventCount; i++) {
    PiezoEvent event;
    memcpy_P(&event, &PIEZO_SONG.events[i], sizeof(event));

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
  while (nextActivePiezoLedEvent < PIEZO_SONG.eventCount) {
    PiezoEvent event;
    memcpy_P(&event, &PIEZO_SONG.events[nextActivePiezoLedEvent], sizeof(event));

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
