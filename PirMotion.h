#ifndef PIR_MOTION_H
#define PIR_MOTION_H

#include <Arduino.h>

class PirMotion {
public:
  PirMotion(int pirPin, int motionState, uint32_t warmupMs, uint32_t confirmMs, uint32_t displayHoldMs);

  void begin();
  bool update();
  bool motionConfirmed() const;
  bool displayHoldActive() const;
  bool idleForAtLeast(uint32_t idleMs) const;
  bool rawMotionDetected() const;
  void keepDisplayOn(uint32_t holdMs);

private:
  void printState(int state);

  int pirPin_;
  int motionState_;
  uint32_t warmupMs_;
  uint32_t confirmMs_;
  uint32_t displayHoldMs_;

  int lastReading_;
  uint32_t highStartedAtMs_;
  bool motionConfirmed_;
  uint32_t displayUntilMs_;
  uint32_t warmupUntilMs_;
};

#endif
