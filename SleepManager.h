#ifndef SLEEP_MANAGER_H
#define SLEEP_MANAGER_H

#include <Arduino.h>

#include "ClockSync.h"
#include "DisplayManager.h"
#include "PirMotion.h"

class SleepManager {
public:
  SleepManager(
    int pirPin,
    int buttonPin,
    DisplayManager& displayManager,
    ClockSync& clockSync,
    PirMotion& pirMotion
  );

  int wakeupCauseCode() const;
  void enterDeepSleep(bool pirWakeEnabled);

private:
  int pirPin_;
  int buttonPin_;
  DisplayManager& displayManager_;
  ClockSync& clockSync_;
  PirMotion& pirMotion_;
};

#endif
