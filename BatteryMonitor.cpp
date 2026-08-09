#include "BatteryMonitor.h"

struct BatteryPoint {
  float volts;
  int percent;
};

const BatteryPoint LIPO_CURVE[] = {
  {4.20, 100},
  {4.00, 80},
  {3.80, 50},
  {3.65, 20},
  {3.30, 0}
};

BatteryMonitor::BatteryMonitor(
  int adcPin,
  float r1Ohms,
  float r2Ohms,
  uint32_t updateEveryMs
) : adcPin_(adcPin),
    dividerMultiplier_((r1Ohms + r2Ohms) / r2Ohms),
    updateEveryMs_(updateEveryMs),
    percent_(0),
    stablePercent_(0),
    adcVolts_(0.0),
    batteryVolts_(0.0),
    displayDirty_(true),
    lastUpdateMs_(0) {
}

void BatteryMonitor::begin() {
  analogReadResolution(12);
  analogSetPinAttenuation(adcPin_, ADC_11db);
  update(true);
}

void BatteryMonitor::update(bool force) {
  uint32_t now = millis();

  if (!force && now - lastUpdateMs_ < updateEveryMs_) {
    return;
  }

  lastUpdateMs_ = now;
  finishReading(analogReadMilliVolts(adcPin_) / 1000.0f);
}

int BatteryMonitor::percent() const {
  return percent_;
}

int BatteryMonitor::stablePercent() const {
  return stablePercent_;
}

float BatteryMonitor::adcVolts() const {
  return adcVolts_;
}

float BatteryMonitor::batteryVolts() const {
  return batteryVolts_;
}

bool BatteryMonitor::displayDirty() const {
  return displayDirty_;
}

void BatteryMonitor::clearDisplayDirty() {
  displayDirty_ = false;
}

int BatteryMonitor::percentFromVoltage(float volts) const {
  const int pointCount = sizeof(LIPO_CURVE) / sizeof(LIPO_CURVE[0]);

  if (volts >= LIPO_CURVE[0].volts) {
    return 100;
  }

  if (volts <= LIPO_CURVE[pointCount - 1].volts) {
    return 0;
  }

  for (int i = 0; i < pointCount - 1; i++) {
    BatteryPoint high = LIPO_CURVE[i];
    BatteryPoint low = LIPO_CURVE[i + 1];

    if (volts <= high.volts && volts >= low.volts) {
      float span = high.volts - low.volts;
      float position = (volts - low.volts) / span;
      return low.percent + (int)((high.percent - low.percent) * position + 0.5);
    }
  }

  return 0;
}

void BatteryMonitor::finishReading(float adcVolts) {
  float newBatteryVolts = adcVolts * dividerMultiplier_;
  int newPercent = percentFromVoltage(newBatteryVolts);

  if (newPercent != percent_ ||
      newBatteryVolts > batteryVolts_ + 0.001 ||
      newBatteryVolts < batteryVolts_ - 0.001) {
    displayDirty_ = true;
  }

  adcVolts_ = adcVolts;
  batteryVolts_ = newBatteryVolts;
  percent_ = newPercent;
  stablePercent_ = newPercent;
}
