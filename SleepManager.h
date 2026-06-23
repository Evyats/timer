#ifndef SLEEP_MANAGER_H
#define SLEEP_MANAGER_H

#include <Arduino.h>

#include "BatteryMonitor.h"
#include "ClockSync.h"
#include "DisplayManager.h"
#include "PirMotion.h"

struct LightSleepResult {
  bool wokeForMotion;
};

class SleepManager {
public:
  SleepManager(
    int pirPin,
    uint32_t pirDisplayHoldMs,
    uint64_t lightSleepToDeepSleepUs,
    DisplayManager& displayManager,
    ClockSync& clockSync,
    PirMotion& pirMotion,
    BatteryMonitor& batteryMonitor
  );

  int wakeupCauseCode() const;
  LightSleepResult enterLightSleep();
  void enterDeepSleep();

private:
  int pirPin_;
  uint32_t pirDisplayHoldMs_;
  uint64_t lightSleepToDeepSleepUs_;
  DisplayManager& displayManager_;
  ClockSync& clockSync_;
  PirMotion& pirMotion_;
  BatteryMonitor& batteryMonitor_;
};

#endif
