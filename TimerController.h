#ifndef TIMER_CONTROLLER_H
#define TIMER_CONTROLLER_H

#include <Arduino.h>

class TimerController {
public:
  TimerController(int stepSeconds, int maxSeconds, uint32_t startDelayMs, uint32_t colonBlinkDelayMs);

  void begin();
  void adjustSettingHour(int direction);
  void adjustSettingMinute(int direction);
  void restartSettingBlink();
  bool settingFieldVisible() const;

  void adjustTimer(int direction);
  bool updateCountdown();
  void resetTimer();
  void restartTimerBlink();
  bool timerColonVisible() const;

  uint8_t settingHour() const;
  uint8_t settingMinute() const;
  int remainingSeconds() const;

private:
  int wrapValue(int value, int minimumValue, int maximumValue) const;

  int stepSeconds_;
  int maxSeconds_;
  uint32_t startDelayMs_;
  uint32_t colonBlinkDelayMs_;

  int remainingSeconds_;
  uint32_t nextTimerTickMs_;
  uint32_t timerBlinkStartedAtMs_;
  uint8_t settingHour_;
  uint8_t settingMinute_;
  uint32_t settingBlinkStartedAtMs_;
};

#endif
