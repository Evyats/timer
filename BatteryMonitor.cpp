#include "BatteryMonitor.h"

struct BatteryPoint {
  float volts;
  int percent;
};

const BatteryPoint LIPO_CURVE[] = {
  {4.20, 100},
  {4.15, 95},
  {4.11, 90},
  {4.08, 85},
  {4.02, 80},
  {3.98, 75},
  {3.95, 70},
  {3.91, 65},
  {3.87, 60},
  {3.85, 55},
  {3.83, 50},
  {3.80, 45},
  {3.79, 40},
  {3.77, 35},
  {3.75, 30},
  {3.73, 25},
  {3.71, 20},
  {3.69, 15},
  {3.61, 10},
  {3.50, 5},
  {3.30, 0}
};

BatteryMonitor::BatteryMonitor(
  int adcPin,
  float r1Ohms,
  float r2Ohms,
  int adcSamples,
  int smoothingWindow,
  uint32_t updateEveryMs,
  uint32_t sampleEveryMs
) : adcPin_(adcPin),
    dividerMultiplier_((r1Ohms + r2Ohms) / r2Ohms),
    adcSamples_(adcSamples),
    smoothingWindow_(constrain(smoothingWindow, 1, MAX_SMOOTHING_WINDOW)),
    updateEveryMs_(updateEveryMs),
    sampleEveryMs_(sampleEveryMs),
    percent_(0),
    stablePercent_(0),
    adcVolts_(0.0),
    batteryVolts_(0.0),
    displayDirty_(true),
    lastUpdateMs_(0),
    sampling_(false),
    lastSampleMs_(0),
    sampleTotalMv_(0),
    sampleCount_(0),
    smoothingIndex_(0),
    smoothingCount_(0),
    smoothingTotal_(0) {
  for (int i = 0; i < MAX_SMOOTHING_WINDOW; i++) {
    smoothingReadings_[i] = 0;
  }
}

void BatteryMonitor::begin() {
  analogReadResolution(12);
  analogSetPinAttenuation(adcPin_, ADC_11db);
  update(true);
}

void BatteryMonitor::update(bool force) {
  uint32_t now = millis();

  if (force) {
    uint32_t totalMv = 0;
    for (int i = 0; i < adcSamples_; i++) {
      totalMv += analogReadMilliVolts(adcPin_);
    }
    sampling_ = false;
    lastUpdateMs_ = now;
    finishReading((totalMv / (float)adcSamples_) / 1000.0);
    return;
  }

  if (!sampling_) {
    if (now - lastUpdateMs_ < updateEveryMs_) {
      return;
    }

    sampling_ = true;
    sampleTotalMv_ = 0;
    sampleCount_ = 0;
    lastSampleMs_ = now - sampleEveryMs_;
  }

  if (now - lastSampleMs_ < sampleEveryMs_) {
    return;
  }

  lastSampleMs_ = now;
  sampleTotalMv_ += analogReadMilliVolts(adcPin_);
  sampleCount_++;

  if (sampleCount_ < adcSamples_) {
    return;
  }

  sampling_ = false;
  lastUpdateMs_ = now;
  finishReading((sampleTotalMv_ / (float)adcSamples_) / 1000.0);
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

int BatteryMonitor::smoothPercent(int percent) {
  if (smoothingCount_ < smoothingWindow_) {
    smoothingReadings_[smoothingIndex_] = percent;
    smoothingTotal_ += percent;
    smoothingCount_++;
  } else {
    smoothingTotal_ -= smoothingReadings_[smoothingIndex_];
    smoothingReadings_[smoothingIndex_] = percent;
    smoothingTotal_ += percent;
  }

  smoothingIndex_ = (smoothingIndex_ + 1) % smoothingWindow_;

  return (smoothingTotal_ + smoothingCount_ / 2) / smoothingCount_;
}

void BatteryMonitor::finishReading(float adcVolts) {
  float newBatteryVolts = adcVolts * dividerMultiplier_;
  int newPercent = percentFromVoltage(newBatteryVolts);
  int newStablePercent = smoothPercent(newPercent);

  if (newPercent != percent_ ||
      newStablePercent != stablePercent_ ||
      newBatteryVolts > batteryVolts_ + 0.001 ||
      newBatteryVolts < batteryVolts_ - 0.001) {
    displayDirty_ = true;
  }

  adcVolts_ = adcVolts;
  batteryVolts_ = newBatteryVolts;
  percent_ = newPercent;
  stablePercent_ = newStablePercent;
}
