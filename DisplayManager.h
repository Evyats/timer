#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Adafruit_SSD1306.h>
#include <Arduino.h>

#include "BatteryMonitor.h"
#include "ClockSync.h"

class DisplayManager {
public:
  DisplayManager(Adafruit_SSD1306& display, BatteryMonitor& batteryMonitor);

  void setPower(bool enabled);
  bool isBlank() const;
  void blank();

  void showClockSetting(uint8_t hour, uint8_t minute, bool editingHour, bool editingMinute, bool fieldVisible);
  void showSyncStatus(bool force, const ClockSync& clockSync);
  void showTimer(int remainingSeconds, bool colonVisible);
  void showAlarmCountdown(int remainingSeconds);
  void showClock(bool force, const char* label, uint8_t hour, uint8_t minute, uint8_t second);

  bool timerNeedsUpdate(int remainingSeconds, bool colonVisible) const;
  bool settingNeedsUpdate(bool fieldVisible) const;
  void resetTimerCache();
  void resetClockCache();
  void setTimerColonCache(bool colonVisible);

private:
  void drawScreenFrame(const char* stateLabel);
  void drawBatteryStatus();
  void printTwoDigits(int value);
  void drawClockTime(uint8_t hour24, uint8_t minute, bool colonVisible, bool showHour, bool showMinute);
  uint8_t hour12Value(uint8_t hour24) const;

  Adafruit_SSD1306& display_;
  BatteryMonitor& batteryMonitor_;

  bool displayPowerEnabled_;
  bool displayBlank_;
  int lastDisplayedSeconds_;
  bool lastDisplayedTimerColon_;
  TimeSyncState lastDisplayedSyncState_;
  int lastDisplayedClockHour_;
  int lastDisplayedClockMinute_;
  bool lastDisplayedClockColon_;
  bool lastDisplayedSettingBlink_;
};

#endif
