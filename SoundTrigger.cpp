#include "SoundTrigger.h"

SoundTrigger::SoundTrigger(
  int soundPin,
  int detectedState,
  uint8_t minPulses,
  uint32_t windowMs,
  uint32_t cooldownMs,
  bool logRawEdges
) : soundPin_(soundPin),
    detectedState_(detectedState),
    minPulses_(minPulses),
    windowMs_(windowMs),
    cooldownMs_(cooldownMs),
    logRawEdges_(logRawEdges),
    lastState_(HIGH),
    pulseCount_(0),
    firstPulseMs_(0),
    lastActivationMs_(0) {
}

void SoundTrigger::begin() {
  pinMode(soundPin_, INPUT);
  lastState_ = digitalRead(soundPin_);
}

bool SoundTrigger::update() {
  int state = digitalRead(soundPin_);
  bool activation = false;

  if (state == lastState_) {
    return false;
  }

  if (state == detectedState_) {
    activation = recordPulse();
  }

  lastState_ = state;
  if (logRawEdges_) {
    printState(state);
  }

  return activation;
}

bool SoundTrigger::recordPulse() {
  uint32_t now = millis();

  if (now - lastActivationMs_ < cooldownMs_) {
    return false;
  }

  if (pulseCount_ == 0 || now - firstPulseMs_ > windowMs_) {
    pulseCount_ = 1;
    firstPulseMs_ = now;
    return false;
  }

  pulseCount_++;

  if (pulseCount_ < minPulses_) {
    return false;
  }

  pulseCount_ = 0;
  firstPulseMs_ = 0;
  lastActivationMs_ = now;
  return true;
}

void SoundTrigger::printState(int state) {
  Serial.print(millis());
  Serial.print(" ms sound: ");
  Serial.println(state == detectedState_ ? "Sound detected" : "Quiet");
}
