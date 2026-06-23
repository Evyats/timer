#ifndef CLOCK_SYNC_H
#define CLOCK_SYNC_H

#include <Arduino.h>
#include <WiFi.h>

enum TimeSyncState {
  TIME_SYNC_IDLE,
  TIME_SYNC_CONNECTING_WIFI,
  TIME_SYNC_WAITING_FOR_TIME,
  TIME_SYNC_FAILED,
  TIME_SYNC_COMPLETE
};

class ClockSync {
public:
  ClockSync(
    const char* wifiSsid,
    const char* wifiPassword,
    const char* timezone,
    const char* ntpServer1,
    const char* ntpServer2,
    const char* ntpServer3,
    uint32_t syncTimeoutMs
  );

  void begin();
  void startSync();
  bool updateSync();
  void updateClock(bool pauseClock);
  void setManualTime(uint8_t hour, uint8_t minute);
  void disconnectAndDisableWifi();

  bool hasValidTime() const;
  bool isSyncActive() const;
  TimeSyncState syncState() const;
  const char* syncStatusText() const;
  const char* wifiSsid() const;
  uint8_t hour() const;
  uint8_t minute() const;
  uint8_t second() const;

private:
  void finishSync(bool success);
  void applySyncedClock();

  const char* wifiSsid_;
  const char* wifiPassword_;
  const char* timezone_;
  const char* ntpServer1_;
  const char* ntpServer2_;
  const char* ntpServer3_;
  uint32_t syncTimeoutMs_;

  TimeSyncState syncState_;
  bool hasValidTime_;
  uint8_t hour_;
  uint8_t minute_;
  uint8_t second_;
  uint32_t lastClockTickMs_;
  uint32_t syncStartedAtMs_;
  wl_status_t lastLoggedWifiStatus_;
};

#endif
