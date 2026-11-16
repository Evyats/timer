#ifndef CLOCK_SYNC_H
#define CLOCK_SYNC_H

#include <Arduino.h>
#include <WiFi.h>

enum TimeSyncState {
  TIME_SYNC_IDLE,
  TIME_SYNC_CONNECTING_WIFI,
  TIME_SYNC_WAITING_FOR_TIME,
  TIME_SYNC_FAILED,
  TIME_SYNC_COMPLETE,
  TIME_SYNC_NO_CREDENTIALS
};

class ClockSync {
public:
  ClockSync(
    const char* timezone,
    const char* ntpServer1,
    const char* ntpServer2,
    const char* ntpServer3,
    uint32_t wifiTimeoutMs,
    uint32_t ntpTimeoutMs
  );

  void begin();
  void startSync();
  void cancelSync();
  bool updateSync();
  void updateClock(bool pauseClock);
  void setManualTime(uint8_t hour, uint8_t minute);
  void disconnectAndDisableWifi();
  bool shouldSync(uint32_t maxAgeSeconds) const;
  void saveCredentials(const String& ssid, const String& password);
  bool hasCredentials() const;
  const String& wifiPassword() const;

  bool hasValidTime() const;
  bool isSyncActive() const;
  TimeSyncState syncState() const;
  const char* syncStatusText() const;
  const char* wifiSsid() const;
  uint8_t hour() const;
  uint8_t minute() const;
  uint8_t second() const;

private:
  void loadCredentials();
  void beginConnectionAttempt();
  void finishSync(bool success);
  void applySyncedClock();
  void applySystemClock();
  void logWifiStatus(wl_status_t wifiStatus) const;
  void logScanResult();
  const char* wifiStatusName(wl_status_t wifiStatus) const;

  String wifiSsid_;
  String wifiPassword_;
  const char* timezone_;
  const char* ntpServer1_;
  const char* ntpServer2_;
  const char* ntpServer3_;
  uint32_t wifiTimeoutMs_;
  uint32_t ntpTimeoutMs_;

  TimeSyncState syncState_;
  bool hasValidTime_;
  uint8_t hour_;
  uint8_t minute_;
  uint8_t second_;
  uint32_t lastClockTickMs_;
  uint32_t syncStartedAtMs_;
  uint32_t ntpStartedAtMs_;
  uint32_t lastNtpWaitLogMs_;
  uint32_t connectAttemptStartedAtMs_;
  uint8_t connectAttempt_;
  wl_status_t lastLoggedWifiStatus_;
  bool loggedExistingSystemTime_;
};

#endif
