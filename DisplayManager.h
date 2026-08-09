#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Adafruit_SSD1306.h>
#include <Arduino.h>

#include "AnimationAssets.h"
#include "BatteryMonitor.h"
#include "ClockSync.h"

class DisplayManager {
public:
  DisplayManager(Adafruit_SSD1306& display, BatteryMonitor& batteryMonitor);

  void setPower(bool enabled);
  bool isBlank() const;
  bool blankedForAtLeast(uint32_t durationMs) const;
  void blank();

  void showClockSetting(uint8_t hour, uint8_t minute, bool editingHour, bool editingMinute, bool fieldVisible);
  void showSyncStatus(bool force, const ClockSync& clockSync);
  void showTimer(int remainingSeconds, int startSeconds, bool colonVisible, bool countdownRunning);
  void showAlarmCountdown(int remainingSeconds);
  void showLoadingAnimationFrame(uint8_t animationIndex, uint8_t frameIndex);
  void showSoundAnimationFrame(uint8_t frameIndex);
  void showClock(bool force, const char* label, uint8_t hour, uint8_t minute, uint8_t second);
  void showSettings(
    uint8_t selectedRow,
    bool musicEnabled,
    bool pirEnabled,
    bool use12HourClock,
    bool debugBottomBar
  );
  void setUse12HourClock(bool enabled);
  void setDebugBottomBar(bool enabled);

  bool timerNeedsUpdate(int remainingSeconds, bool colonVisible, bool countdownRunning) const;
  bool settingNeedsUpdate(bool fieldVisible) const;
  void resetTimerCache();
  void resetClockCache();
  void setTimerColonCache(bool colonVisible);

private:
  void drawScreenFrame(const char* stateLabel);
  void drawBatteryStatus();
  void drawTimerProgress(int remainingSeconds, int startSeconds);
  void drawAnimationScreen(const char* stateLabel, const AnimationClip& clip, uint8_t frameIndex);
  void drawCountdownScreen(const char* stateLabel, int remainingSeconds, bool colonVisible);
  void printTwoDigits(int value);
  void drawClockTime(uint8_t hour24, uint8_t minute, bool colonVisible, bool showHour, bool showMinute);
  uint8_t hour12Value(uint8_t hour24) const;

  Adafruit_SSD1306& display_;
  BatteryMonitor& batteryMonitor_;

  bool displayPowerEnabled_;
  bool displayBlank_;
  uint32_t blankedAtMs_;
  int lastDisplayedSeconds_;
  bool lastDisplayedTimerColon_;
  bool lastDisplayedCountdownRunning_;
  TimeSyncState lastDisplayedSyncState_;
  int lastDisplayedClockHour_;
  int lastDisplayedClockMinute_;
  bool lastDisplayedClockColon_;
  bool lastDisplayedSettingBlink_;
  bool use12HourClock_;
  bool debugBottomBar_;
};

#endif
