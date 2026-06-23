#include "TimerController.h"

TimerController::TimerController(int stepSeconds, int maxSeconds, uint32_t startDelayMs, uint32_t colonBlinkDelayMs)
  : stepSeconds_(stepSeconds),
    maxSeconds_(maxSeconds),
    startDelayMs_(startDelayMs),
    colonBlinkDelayMs_(colonBlinkDelayMs),
    remainingSeconds_(0),
    nextTimerTickMs_(0),
    timerBlinkStartedAtMs_(0),
    settingHour_(0),
    settingMinute_(0),
    settingBlinkStartedAtMs_(0) {
}

void TimerController::begin() {
  settingBlinkStartedAtMs_ = millis();
}

void TimerController::adjustSettingHour(int direction) {
  settingHour_ = wrapValue(settingHour_ + direction, 0, 23);
  restartSettingBlink();
}

void TimerController::adjustSettingMinute(int direction) {
  settingMinute_ = wrapValue(settingMinute_ + direction, 0, 59);
  restartSettingBlink();
}

void TimerController::restartSettingBlink() {
  settingBlinkStartedAtMs_ = millis();
}

bool TimerController::settingFieldVisible() const {
  return ((millis() - settingBlinkStartedAtMs_) / 500) % 2 == 0;
}

void TimerController::adjustTimer(int direction) {
  remainingSeconds_ += direction * stepSeconds_;

  if (remainingSeconds_ < 0) {
    remainingSeconds_ = 0;
  }

  if (remainingSeconds_ > maxSeconds_) {
    remainingSeconds_ = maxSeconds_;
  }

  if (remainingSeconds_ > 0) {
    uint32_t now = millis();
    nextTimerTickMs_ = now + startDelayMs_;
    timerBlinkStartedAtMs_ = now + colonBlinkDelayMs_;
  }
}

bool TimerController::updateCountdown() {
  uint32_t now = millis();

  while ((int32_t)(now - nextTimerTickMs_) >= 0 && remainingSeconds_ > 0) {
    nextTimerTickMs_ += 1000;
    remainingSeconds_--;
    restartTimerBlink();
  }

  return remainingSeconds_ == 0;
}

void TimerController::resetTimer() {
  remainingSeconds_ = 0;
}

void TimerController::restartTimerBlink() {
  timerBlinkStartedAtMs_ = millis();
}

bool TimerController::timerColonVisible() const {
  if ((int32_t)(millis() - timerBlinkStartedAtMs_) < 0) {
    return true;
  }

  return ((millis() - timerBlinkStartedAtMs_) / 500) % 2 == 0;
}

uint8_t TimerController::settingHour() const {
  return settingHour_;
}

uint8_t TimerController::settingMinute() const {
  return settingMinute_;
}

int TimerController::remainingSeconds() const {
  return remainingSeconds_;
}

int TimerController::wrapValue(int value, int minimumValue, int maximumValue) const {
  if (value < minimumValue) {
    return maximumValue;
  }

  if (value > maximumValue) {
    return minimumValue;
  }

  return value;
}
