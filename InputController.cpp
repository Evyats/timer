#include "InputController.h"

InputController::InputController(
  int encoderS1Pin,
  int encoderS2Pin,
  int encoderButtonPin,
  bool reverseEncoderDirection,
  uint32_t buttonDebounceMs,
  bool logRawStates
) : encoderS1Pin_(encoderS1Pin),
    encoderS2Pin_(encoderS2Pin),
    encoderButtonPin_(encoderButtonPin),
    reverseEncoderDirection_(reverseEncoderDirection),
    buttonDebounceMs_(buttonDebounceMs),
    logRawStates_(logRawStates),
    lastEncoderState_(0),
    encoderMovement_(0),
    encoderInitialized_(false),
    lastButtonReading_(HIGH),
    stableButtonState_(HIGH),
    lastButtonChangeMs_(0) {
}

void InputController::begin() {
  pinMode(encoderS1Pin_, INPUT_PULLUP);
  pinMode(encoderS2Pin_, INPUT_PULLUP);
  pinMode(encoderButtonPin_, INPUT_PULLUP);

  lastButtonReading_ = digitalRead(encoderButtonPin_);
  stableButtonState_ = lastButtonReading_;
  lastButtonChangeMs_ = millis();
}

int InputController::readEncoderStep() {
  int s1 = digitalRead(encoderS1Pin_);
  int s2 = digitalRead(encoderS2Pin_);
  int state = (s1 << 1) | s2;

  if (!encoderInitialized_) {
    lastEncoderState_ = state;
    encoderInitialized_ = true;
    printEncoderRawState(s1, s2, state);
    return 0;
  }

  if (state == lastEncoderState_) {
    return 0;
  }

  printEncoderRawState(s1, s2, state);

  int transition = (lastEncoderState_ << 2) | state;
  lastEncoderState_ = state;
  int direction = 0;

  switch (transition) {
    case 0b0001:
    case 0b0111:
    case 0b1110:
    case 0b1000:
      direction = 1;
      break;

    case 0b0010:
    case 0b1011:
    case 0b1101:
    case 0b0100:
      direction = -1;
      break;

    default:
      return 0;
  }

  encoderMovement_ += direction;

  if (encoderMovement_ >= 4) {
    encoderMovement_ = 0;
    return reverseEncoderDirection_ ? -1 : 1;
  }

  if (encoderMovement_ <= -4) {
    encoderMovement_ = 0;
    return reverseEncoderDirection_ ? 1 : -1;
  }

  return 0;
}

bool InputController::buttonPressed() {
  bool reading = digitalRead(encoderButtonPin_);
  uint32_t now = millis();

  if (reading != lastButtonReading_) {
    lastButtonChangeMs_ = now;
    lastButtonReading_ = reading;
  }

  if (now - lastButtonChangeMs_ < buttonDebounceMs_) {
    return false;
  }

  if (reading == stableButtonState_) {
    return false;
  }

  stableButtonState_ = reading;

  if (stableButtonState_ != LOW) {
    return false;
  }

  printButtonPress();
  return true;
}

bool InputController::buttonDown() const {
  return stableButtonState_ == LOW;
}

void InputController::printEncoderRawState(int s1, int s2, int state) {
  if (!logRawStates_) {
    return;
  }

  Serial.print(millis());
  Serial.print(" ms encoder rotated: S1 GPIO");
  Serial.print(encoderS1Pin_);
  Serial.print("=");
  Serial.print(s1 == HIGH ? "HIGH" : "LOW");
  Serial.print(" S2 GPIO");
  Serial.print(encoderS2Pin_);
  Serial.print("=");
  Serial.print(s2 == HIGH ? "HIGH" : "LOW");
  Serial.print(" BUTTON GPIO");
  Serial.print(encoderButtonPin_);
  Serial.print("=");
  Serial.print(digitalRead(encoderButtonPin_) == HIGH ? "HIGH" : "LOW");
  Serial.print(" state=");
  Serial.println(state, BIN);
}

void InputController::printButtonPress() {
  if (!logRawStates_) {
    return;
  }

  Serial.print(millis());
  Serial.print(" ms encoder button pressed: S1 GPIO");
  Serial.print(encoderS1Pin_);
  Serial.print("=");
  Serial.print(digitalRead(encoderS1Pin_) == HIGH ? "HIGH" : "LOW");
  Serial.print(" S2 GPIO");
  Serial.print(encoderS2Pin_);
  Serial.print("=");
  Serial.print(digitalRead(encoderS2Pin_) == HIGH ? "HIGH" : "LOW");
  Serial.print(" BUTTON GPIO");
  Serial.print(encoderButtonPin_);
  Serial.print("=");
  Serial.println(digitalRead(encoderButtonPin_) == HIGH ? "HIGH" : "LOW");
}
