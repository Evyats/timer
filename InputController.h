#ifndef INPUT_CONTROLLER_H
#define INPUT_CONTROLLER_H

#include <Arduino.h>

class InputController {
public:
  InputController(
    int encoderS1Pin,
    int encoderS2Pin,
    int encoderButtonPin,
    bool reverseEncoderDirection,
    uint32_t buttonDebounceMs,
    bool logRawStates
  );

  void begin();
  int readEncoderStep();
  bool buttonPressed();
  bool buttonDown() const;

private:
  void printEncoderRawState(int s1, int s2, int state);
  void printButtonPress();

  int encoderS1Pin_;
  int encoderS2Pin_;
  int encoderButtonPin_;
  bool reverseEncoderDirection_;
  uint32_t buttonDebounceMs_;
  bool logRawStates_;

  int lastEncoderState_;
  int encoderMovement_;
  bool encoderInitialized_;
  bool lastButtonReading_;
  bool stableButtonState_;
  uint32_t lastButtonChangeMs_;
};

#endif
