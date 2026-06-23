#include "PirMotion.h"

PirMotion::PirMotion(int pirPin, int motionState, uint32_t warmupMs, uint32_t confirmMs, uint32_t displayHoldMs)
  : pirPin_(pirPin),
    motionState_(motionState),
    warmupMs_(warmupMs),
    confirmMs_(confirmMs),
    displayHoldMs_(displayHoldMs),
    lastReading_(LOW),
    highStartedAtMs_(0),
    motionConfirmed_(false),
    displayUntilMs_(0),
    warmupUntilMs_(0) {
}

void PirMotion::begin() {
  pinMode(pirPin_, INPUT);
  warmupUntilMs_ = millis() + warmupMs_;
  lastReading_ = digitalRead(pirPin_);
  motionConfirmed_ = false;
}

bool PirMotion::update() {
  uint32_t now = millis();
  int reading = digitalRead(pirPin_);

  if ((int32_t)(now - warmupUntilMs_) < 0) {
    return false;
  }

  if (reading != lastReading_) {
    lastReading_ = reading;
    if (reading == motionState_) {
      highStartedAtMs_ = now;
    }
  }

  bool stateChanged = false;
  int confirmedState = motionConfirmed_ ? motionState_ : LOW;

  if (reading == motionState_) {
    if (!motionConfirmed_ && (int32_t)(now - highStartedAtMs_) >= (int32_t)confirmMs_) {
      motionConfirmed_ = true;
      confirmedState = motionState_;
      stateChanged = true;
    }
  } else if (motionConfirmed_) {
    motionConfirmed_ = false;
    confirmedState = LOW;
    stateChanged = true;
  }

  if (motionConfirmed_) {
    displayUntilMs_ = now + displayHoldMs_;
  }

  if (stateChanged) {
    printState(confirmedState);
  }

  return stateChanged;
}

bool PirMotion::motionConfirmed() const {
  return motionConfirmed_;
}

bool PirMotion::displayHoldActive() const {
  return (int32_t)(millis() - displayUntilMs_) < 0;
}

bool PirMotion::idleForAtLeast(uint32_t idleMs) const {
  return (int32_t)(millis() - displayUntilMs_) >= (int32_t)idleMs;
}

bool PirMotion::rawMotionDetected() const {
  return digitalRead(pirPin_) == motionState_;
}

void PirMotion::keepDisplayOn(uint32_t holdMs) {
  displayUntilMs_ = millis() + holdMs;
}

void PirMotion::printState(int state) {
  Serial.print(millis());
  Serial.print(" ms PIR: ");
  Serial.println(state == motionState_ ? "MOTION detected" : "No motion");
}
