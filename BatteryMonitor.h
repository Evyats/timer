#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <Arduino.h>

class BatteryMonitor {
public:
  BatteryMonitor(
    int adcPin,
    float r1Ohms,
    float r2Ohms,
    uint32_t updateEveryMs
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
  int percentFromVoltage(float volts) const;
  void finishReading(float adcVolts);

  int adcPin_;
  float dividerMultiplier_;
  uint32_t updateEveryMs_;

  int percent_;
  int stablePercent_;
  float adcVolts_;
  float batteryVolts_;
  bool displayDirty_;

  uint32_t lastUpdateMs_;
};

#endif
