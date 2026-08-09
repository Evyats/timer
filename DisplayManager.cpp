#include "DisplayManager.h"

#include <string.h>

const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;
const int HEADER_HEIGHT = 16;
const int HEADER_TOP = SCREEN_HEIGHT - HEADER_HEIGHT;
const int CONTENT_TOP = 0;
const int CONTENT_HEIGHT = SCREEN_HEIGHT - HEADER_HEIGHT - 1;
const int CLOCK_TEXT_SIZE = 3;
const int CLOCK_TEXT_HEIGHT = 8 * CLOCK_TEXT_SIZE;
const int COUNTDOWN_TEXT_SIZE = 3;
const int COUNTDOWN_TEXT_HEIGHT = 8 * COUNTDOWN_TEXT_SIZE;
const int CLOCK_AM_PM_GAP = 6;
const int CLOCK_AM_PM_Y_OFFSET = 8;
const bool SHOW_SECTION_BORDERS = false;
const bool SHOW_STATE_TEXT = true;
const bool SHOW_BATTERY_TEXT = true;
const bool CENTER_BATTERY_ICON = false;

DisplayManager::DisplayManager(Adafruit_SSD1306& display, BatteryMonitor& batteryMonitor)
  : display_(display),
    batteryMonitor_(batteryMonitor),
    displayPowerEnabled_(true),
    displayBlank_(false),
    blankedAtMs_(0),
    lastDisplayedSeconds_(-1),
    lastDisplayedTimerColon_(false),
    lastDisplayedCountdownRunning_(false),
    lastDisplayedSyncState_(TIME_SYNC_IDLE),
    lastDisplayedClockHour_(-1),
    lastDisplayedClockMinute_(-1),
    lastDisplayedClockColon_(false),
    lastDisplayedSettingBlink_(true),
    use12HourClock_(false),
    debugBottomBar_(false) {
}

void DisplayManager::setPower(bool enabled) {
  if (displayPowerEnabled_ == enabled) {
    return;
  }

  display_.ssd1306_command(enabled ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
  displayPowerEnabled_ = enabled;
}

bool DisplayManager::isBlank() const {
  return displayBlank_;
}

bool DisplayManager::blankedForAtLeast(uint32_t durationMs) const {
  return displayBlank_ && (int32_t)(millis() - blankedAtMs_) >= (int32_t)durationMs;
}

void DisplayManager::blank() {
  if (displayBlank_) {
    batteryMonitor_.clearDisplayDirty();
    return;
  }

  display_.clearDisplay();
  display_.display();
  displayBlank_ = true;
  blankedAtMs_ = millis();
  batteryMonitor_.clearDisplayDirty();
  lastDisplayedSeconds_ = -1;
  lastDisplayedClockHour_ = -1;
  lastDisplayedClockMinute_ = -1;
}

void DisplayManager::showClockSetting(
  uint8_t hour,
  uint8_t minute,
  bool editingHour,
  bool editingMinute,
  bool fieldVisible
) {
  setPower(true);
  displayBlank_ = false;
  lastDisplayedSeconds_ = -1;
  lastDisplayedClockHour_ = -1;
  lastDisplayedClockMinute_ = -1;
  lastDisplayedSettingBlink_ = fieldVisible;

  display_.clearDisplay();
  display_.setTextColor(SSD1306_WHITE);

  drawScreenFrame(editingHour ? "SET H" : "SET M");
  drawBatteryStatus();

  drawClockTime(hour, minute, true, !editingHour || lastDisplayedSettingBlink_, !editingMinute || lastDisplayedSettingBlink_);

  display_.display();
  batteryMonitor_.clearDisplayDirty();
}

void DisplayManager::showSyncStatus(bool force, const ClockSync& clockSync) {
  if (!force &&
      !displayBlank_ &&
      clockSync.syncState() == lastDisplayedSyncState_ &&
      !batteryMonitor_.displayDirty()) {
    return;
  }

  setPower(true);
  displayBlank_ = false;
  lastDisplayedSeconds_ = -1;
  lastDisplayedClockHour_ = -1;
  lastDisplayedClockMinute_ = -1;
  lastDisplayedSyncState_ = clockSync.syncState();

  display_.clearDisplay();
  display_.setTextColor(SSD1306_WHITE);

  drawScreenFrame("READY");
  drawBatteryStatus();

  display_.setTextSize(2);
  const char* status = clockSync.syncStatusText();
  int statusWidth = strlen(status) * 12;
  int statusX = (SCREEN_WIDTH - statusWidth) / 2;
  int statusY = CONTENT_TOP + 8;
  display_.setCursor(statusX, statusY);
  display_.print(status);

  display_.setTextSize(1);
  int detailY = statusY + 24;
  if (clockSync.syncState() == TIME_SYNC_CONNECTING_WIFI) {
    display_.setCursor(10, detailY);
    display_.print(clockSync.wifiSsid());
  } else if (clockSync.syncState() == TIME_SYNC_WAITING_FOR_TIME) {
    display_.setCursor(22, detailY);
    display_.print("Israel time");
  } else if (clockSync.syncState() == TIME_SYNC_FAILED) {
    display_.setCursor(12, detailY);
    display_.print("sync timeout");
  }

  display_.display();
  batteryMonitor_.clearDisplayDirty();
}

void DisplayManager::showTimer(int remainingSeconds, int startSeconds, bool colonVisible, bool countdownRunning) {
  setPower(true);
  lastDisplayedSeconds_ = remainingSeconds;
  lastDisplayedTimerColon_ = colonVisible;
  lastDisplayedCountdownRunning_ = countdownRunning;
  displayBlank_ = false;

  display_.clearDisplay();
  display_.setTextColor(SSD1306_WHITE);

  if (countdownRunning) {
    drawTimerProgress(remainingSeconds, startSeconds);
  } else {
    drawScreenFrame("TIMER");
    drawBatteryStatus();
  }

  int countdownWidth = 5 * 6 * COUNTDOWN_TEXT_SIZE;
  int countdownX = (SCREEN_WIDTH - countdownWidth) / 2;
  int countdownY = CONTENT_TOP + (CONTENT_HEIGHT - COUNTDOWN_TEXT_HEIGHT) / 2;

  display_.setTextSize(COUNTDOWN_TEXT_SIZE);
  display_.setCursor(countdownX, countdownY);
  printTwoDigits(remainingSeconds / 60);
  display_.print(colonVisible ? ":" : " ");
  printTwoDigits(remainingSeconds % 60);

  display_.display();
  batteryMonitor_.clearDisplayDirty();
}

void DisplayManager::showAlarmCountdown(int remainingSeconds) {
  drawCountdownScreen("ALARM", remainingSeconds, true);
}

void DisplayManager::showLoadingAnimationFrame(uint8_t animationIndex, uint8_t frameIndex) {
  drawAnimationScreen("READY", AnimationAssets::loadingAnimation(animationIndex), frameIndex);
}

void DisplayManager::showSoundAnimationFrame(uint8_t frameIndex) {
  drawAnimationScreen("ALARM", AnimationAssets::soundAnimation(), frameIndex);
}

void DisplayManager::showClock(bool force, const char* label, uint8_t hour, uint8_t minute, uint8_t second) {
  bool colonVisible = second % 2 == 0;

  if (!force &&
      !displayBlank_ &&
      hour == lastDisplayedClockHour_ &&
      minute == lastDisplayedClockMinute_ &&
      colonVisible == lastDisplayedClockColon_) {
    return;
  }

  setPower(true);
  displayBlank_ = false;
  lastDisplayedSeconds_ = -1;
  lastDisplayedClockHour_ = hour;
  lastDisplayedClockMinute_ = minute;
  lastDisplayedClockColon_ = colonVisible;

  display_.clearDisplay();
  display_.setTextColor(SSD1306_WHITE);

  drawScreenFrame(label);
  drawBatteryStatus();

  drawClockTime(hour, minute, colonVisible, true, true);

  display_.display();
  batteryMonitor_.clearDisplayDirty();
}

void DisplayManager::showSettings(
  uint8_t selectedRow,
  bool musicEnabled,
  bool pirEnabled,
  bool use12HourClock,
  bool debugBottomBar
) {
  const char* labels[] = { "Music", "PIR", "AM/PM mode", "Debug bar", "Done" };
  const char* values[] = {
    musicEnabled ? "ON" : "OFF",
    pirEnabled ? "ON" : "OFF",
    use12HourClock ? "ON" : "OFF",
    debugBottomBar ? "ON" : "OFF",
    ""
  };

  setPower(true);
  displayBlank_ = false;
  display_.clearDisplay();
  display_.setTextSize(1);
  display_.setTextWrap(false);

  for (uint8_t row = 0; row < 5; ++row) {
    int rowY = row < 4 ? row * 12 : HEADER_TOP;
    int rowHeight = row < 4 ? 12 : HEADER_HEIGHT;
    bool selected = row == selectedRow;
    if (selected) {
      display_.fillRect(0, rowY, SCREEN_WIDTH, rowHeight, SSD1306_WHITE);
      display_.setTextColor(SSD1306_BLACK);
    } else {
      display_.setTextColor(SSD1306_WHITE);
    }

    if (row < 4) {
      display_.setCursor(3, rowY + 2);
      display_.print(labels[row]);
      int valueX = SCREEN_WIDTH - strlen(values[row]) * 6 - 3;
      display_.setCursor(valueX, rowY + 2);
      display_.print(values[row]);
    } else {
      int doneX = (SCREEN_WIDTH - strlen(labels[row]) * 6) / 2;
      display_.setCursor(doneX, rowY + 4);
      display_.print(labels[row]);
    }
  }

  display_.display();
}

void DisplayManager::setUse12HourClock(bool enabled) {
  use12HourClock_ = enabled;
  resetClockCache();
}

void DisplayManager::setDebugBottomBar(bool enabled) {
  debugBottomBar_ = enabled;
  resetClockCache();
}

void DisplayManager::drawAnimationScreen(const char* stateLabel, const AnimationClip& clip, uint8_t frameIndex) {
  setPower(true);
  displayBlank_ = false;
  lastDisplayedSeconds_ = -1;
  lastDisplayedClockHour_ = -1;
  lastDisplayedClockMinute_ = -1;

  display_.clearDisplay();
  display_.setTextColor(SSD1306_WHITE);

  drawScreenFrame(stateLabel);
  drawBatteryStatus();

  int frameX = (SCREEN_WIDTH - clip.width) / 2;
  int frameY = CONTENT_TOP + (CONTENT_HEIGHT - clip.height) / 2;
  AnimationAssets::drawFrame(display_, clip, frameIndex, frameX, frameY);

  display_.display();
  batteryMonitor_.clearDisplayDirty();
}

bool DisplayManager::timerNeedsUpdate(int remainingSeconds, bool colonVisible, bool countdownRunning) const {
  return remainingSeconds != lastDisplayedSeconds_ ||
         colonVisible != lastDisplayedTimerColon_ ||
         countdownRunning != lastDisplayedCountdownRunning_;
}

bool DisplayManager::settingNeedsUpdate(bool fieldVisible) const {
  return fieldVisible != lastDisplayedSettingBlink_;
}

void DisplayManager::resetTimerCache() {
  lastDisplayedSeconds_ = -1;
}

void DisplayManager::resetClockCache() {
  lastDisplayedClockHour_ = -1;
  lastDisplayedClockMinute_ = -1;
  lastDisplayedClockColon_ = false;
}

void DisplayManager::setTimerColonCache(bool colonVisible) {
  lastDisplayedTimerColon_ = colonVisible;
}

void DisplayManager::drawScreenFrame(const char* stateLabel) {
  if (SHOW_SECTION_BORDERS) {
    display_.drawRect(0, HEADER_TOP, SCREEN_WIDTH, HEADER_HEIGHT, SSD1306_WHITE);
    display_.drawRect(0, 0, SCREEN_WIDTH, HEADER_TOP, SSD1306_WHITE);
  }

  if (debugBottomBar_ && SHOW_STATE_TEXT) {
    display_.setTextSize(1);
    display_.setCursor(2, HEADER_TOP + 4);
    display_.print(stateLabel);
  }
}

void DisplayManager::drawBatteryStatus() {
  const int batteryWidth = 18;
  const int batteryHeight = 7;
  const int batteryX = (!debugBottomBar_ || CENTER_BATTERY_ICON)
                         ? (SCREEN_WIDTH - batteryWidth) / 2
                         : SCREEN_WIDTH - batteryWidth - 3;
  const int batteryY = HEADER_TOP + (HEADER_HEIGHT - batteryHeight) / 2;
  const int terminalWidth = 2;
  const int terminalHeight = 3;
  const int terminalY = batteryY + (batteryHeight - terminalHeight) / 2;
  const int fillWidth = map(constrain(batteryMonitor_.stablePercent(), 0, 100), 0, 100, 0, batteryWidth - 4);
  String batteryVoltsText = String(batteryMonitor_.batteryVolts(), 2) + "V";
  String percentText = String(batteryMonitor_.percent()) + "%";
  const int batteryVoltsTextWidth = batteryVoltsText.length() * 6;
  const int percentTextWidth = percentText.length() * 6;
  const int textY = HEADER_TOP + 4;
  const int percentTextX = batteryX - terminalWidth - 3 - percentTextWidth;
  const int batteryVoltsTextX = percentTextX - 4 - batteryVoltsTextWidth;

  if (debugBottomBar_ && SHOW_BATTERY_TEXT) {
    display_.setTextSize(1);
    display_.setCursor(batteryVoltsTextX, textY);
    display_.print(batteryVoltsText);
    display_.setCursor(percentTextX, textY);
    display_.print(percentText);
  }

  display_.drawRect(batteryX, batteryY, batteryWidth, batteryHeight, SSD1306_WHITE);
  display_.fillRect(batteryX - terminalWidth, terminalY, terminalWidth, terminalHeight, SSD1306_WHITE);
  display_.fillRect(batteryX + batteryWidth - 2 - fillWidth, batteryY + 2, fillWidth, batteryHeight - 4, SSD1306_WHITE);
}

void DisplayManager::drawTimerProgress(int remainingSeconds, int startSeconds) {
  const int barWidth = 5 * 6 * COUNTDOWN_TEXT_SIZE;
  const int barX = (SCREEN_WIDTH - barWidth) / 2;
  const int barY = HEADER_TOP + 4;
  const int barHeight = 8;
  const int barRadius = 3;
  const int innerWidth = barWidth - 4;
  int fillWidth = 0;

  if (startSeconds > 0) {
    fillWidth = (int)(((int64_t)constrain(remainingSeconds, 0, startSeconds) * innerWidth) /
                      startSeconds);
  }

  display_.drawRoundRect(
    barX,
    barY,
    barWidth,
    barHeight,
    barRadius,
    SSD1306_WHITE
  );
  if (fillWidth > 0) {
    if (fillWidth >= 4) {
      display_.fillRoundRect(
        barX + 2,
        barY + 2,
        fillWidth,
        barHeight - 4,
        2,
        SSD1306_WHITE
      );
    } else {
      display_.fillRect(
        barX + 2,
        barY + 2,
        fillWidth,
        barHeight - 4,
        SSD1306_WHITE
      );
    }
  }
}

void DisplayManager::drawCountdownScreen(const char* stateLabel, int remainingSeconds, bool colonVisible) {
  setPower(true);
  lastDisplayedSeconds_ = remainingSeconds;
  lastDisplayedTimerColon_ = colonVisible;
  displayBlank_ = false;

  display_.clearDisplay();
  display_.setTextColor(SSD1306_WHITE);

  drawScreenFrame(stateLabel);
  drawBatteryStatus();

  int countdownWidth = 5 * 6 * COUNTDOWN_TEXT_SIZE;
  int countdownX = (SCREEN_WIDTH - countdownWidth) / 2;
  int countdownY = CONTENT_TOP + (CONTENT_HEIGHT - COUNTDOWN_TEXT_HEIGHT) / 2;

  display_.setTextSize(COUNTDOWN_TEXT_SIZE);
  display_.setCursor(countdownX, countdownY);
  printTwoDigits(remainingSeconds / 60);
  display_.print(colonVisible ? ":" : " ");
  printTwoDigits(remainingSeconds % 60);

  display_.display();
  batteryMonitor_.clearDisplayDirty();
}

void DisplayManager::printTwoDigits(int value) {
  if (value < 10) {
    display_.print("0");
  }
  display_.print(value);
}

void DisplayManager::drawClockTime(uint8_t hour24, uint8_t minute, bool colonVisible, bool showHour, bool showMinute) {
  uint8_t displayHour = use12HourClock_ ? hour12Value(hour24) : hour24;
  int hourDigits = use12HourClock_ ? (displayHour < 10 ? 1 : 2) : 2;
  int timeChars = hourDigits + 1 + 2;
  int timeWidth = timeChars * 6 * CLOCK_TEXT_SIZE;
  int amPmWidth = use12HourClock_ ? 2 * 6 : 0;
  int blockWidth = timeWidth + (use12HourClock_ ? CLOCK_AM_PM_GAP + amPmWidth : 0);
  int timeX = (SCREEN_WIDTH - blockWidth) / 2;
  int timeY = CONTENT_TOP + (CONTENT_HEIGHT - CLOCK_TEXT_HEIGHT) / 2;
  int amPmX = timeX + timeWidth + CLOCK_AM_PM_GAP;
  int amPmY = timeY + CLOCK_AM_PM_Y_OFFSET;

  display_.setTextSize(CLOCK_TEXT_SIZE);
  display_.setCursor(timeX, timeY);
  if (showHour) {
    if (!use12HourClock_ && displayHour < 10) {
      display_.print("0");
    }
    display_.print(displayHour);
  } else {
    for (int i = 0; i < hourDigits; i++) {
      display_.print(" ");
    }
  }
  display_.print(colonVisible ? ":" : " ");
  if (showMinute) {
    printTwoDigits(minute);
  } else {
    display_.print("  ");
  }

  if (use12HourClock_) {
    display_.setTextSize(1);
    display_.setCursor(amPmX, amPmY);
    display_.print(hour24 < 12 ? "AM" : "PM");
  }
}

uint8_t DisplayManager::hour12Value(uint8_t hour24) const {
  uint8_t hour12 = hour24 % 12;
  return hour12 == 0 ? 12 : hour12;
}
