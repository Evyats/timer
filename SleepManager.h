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
    DisplayManager& displayManager,
    ClockSync& clockSync,
    PirMotion& pirMotion
  );

  int wakeupCauseCode() const;
  void enterDeepSleep();

private:
  int pirPin_;
  DisplayManager& displayManager_;
  ClockSync& clockSync_;
  PirMotion& pirMotion_;
};

#endif
