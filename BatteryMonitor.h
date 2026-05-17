#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <Arduino.h>

class BatteryMonitor {
public:
  BatteryMonitor(
    int adcPin,
    float r1Ohms,
    float r2Ohms,
    int adcSamples,
    int smoothingWindow,
    uint32_t updateEveryMs,
    uint32_t sampleEveryMs
  );

  void begin();
  void update(bool force = false);

  int percent() const;
  int stablePercent() const;
  float adcVolts() const;
  float batteryVolts() const;
  bool displayDirty() const;
  void clearDisplayDirty();

private:
  static const int MAX_SMOOTHING_WINDOW = 16;

  int percentFromVoltage(float volts) const;
  int smoothPercent(int percent);
  void finishReading(float adcVolts);

  int adcPin_;
  float dividerMultiplier_;
  int adcSamples_;
  int smoothingWindow_;
  uint32_t updateEveryMs_;
  uint32_t sampleEveryMs_;

  int percent_;
  int stablePercent_;
  float adcVolts_;
  float batteryVolts_;
  bool displayDirty_;

  uint32_t lastUpdateMs_;
  bool sampling_;
  uint32_t lastSampleMs_;
  uint32_t sampleTotalMv_;
  int sampleCount_;

  int smoothingReadings_[MAX_SMOOTHING_WINDOW];
  int smoothingIndex_;
  int smoothingCount_;
  int smoothingTotal_;
};

#endif
