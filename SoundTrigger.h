#ifndef SOUND_TRIGGER_H
#define SOUND_TRIGGER_H

#include <Arduino.h>

class SoundTrigger {
public:
  SoundTrigger(
    int soundPin,
    int detectedState,
    uint8_t minPulses,
    uint32_t windowMs,
    uint32_t cooldownMs,
    bool logRawEdges
  );

  void begin();
  bool update();

private:
  bool recordPulse();
  void printState(int state);

  int soundPin_;
  int detectedState_;
  uint8_t minPulses_;
  uint32_t windowMs_;
  uint32_t cooldownMs_;
  bool logRawEdges_;

  int lastState_;
  uint8_t pulseCount_;
  uint32_t firstPulseMs_;
  uint32_t lastActivationMs_;
};

#endif
