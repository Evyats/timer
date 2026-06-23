#include "ClockSync.h"

#include <time.h>

ClockSync::ClockSync(
  const char* wifiSsid,
  const char* wifiPassword,
  const char* timezone,
  const char* ntpServer1,
  const char* ntpServer2,
  const char* ntpServer3,
  uint32_t syncTimeoutMs
) : wifiSsid_(wifiSsid),
    wifiPassword_(wifiPassword),
    timezone_(timezone),
    ntpServer1_(ntpServer1),
    ntpServer2_(ntpServer2),
    ntpServer3_(ntpServer3),
    syncTimeoutMs_(syncTimeoutMs),
    syncState_(TIME_SYNC_IDLE),
    hasValidTime_(false),
    hour_(0),
    minute_(0),
    second_(0),
    lastClockTickMs_(0),
    syncStartedAtMs_(0),
    lastLoggedWifiStatus_(WL_IDLE_STATUS) {
}

void ClockSync::begin() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  lastClockTickMs_ = millis();
}

void ClockSync::startSync() {
  if (syncState_ == TIME_SYNC_CONNECTING_WIFI || syncState_ == TIME_SYNC_WAITING_FOR_TIME) {
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid_, wifiPassword_);
  syncState_ = TIME_SYNC_CONNECTING_WIFI;
  syncStartedAtMs_ = millis();
  lastLoggedWifiStatus_ = WL_IDLE_STATUS;
  Serial.print("Connecting to Wi-Fi SSID ");
  Serial.println(wifiSsid_);
}

bool ClockSync::updateSync() {
  if (syncState_ == TIME_SYNC_IDLE ||
      syncState_ == TIME_SYNC_COMPLETE ||
      syncState_ == TIME_SYNC_FAILED) {
    return false;
  }

  uint32_t now = millis();
  if (now - syncStartedAtMs_ >= syncTimeoutMs_) {
    Serial.println("Time sync timed out.");
    finishSync(false);
    return true;
  }

  if (syncState_ == TIME_SYNC_CONNECTING_WIFI) {
    wl_status_t wifiStatus = WiFi.status();
    if (wifiStatus != lastLoggedWifiStatus_) {
      lastLoggedWifiStatus_ = wifiStatus;
      Serial.print("Wi-Fi status changed: ");
      Serial.println((int)wifiStatus);
    }

    if (wifiStatus == WL_CONNECTED) {
      Serial.print("Wi-Fi connected. IP: ");
      Serial.println(WiFi.localIP());
      configTzTime(timezone_, ntpServer1_, ntpServer2_, ntpServer3_);
      syncState_ = TIME_SYNC_WAITING_FOR_TIME;
      Serial.println("Waiting for NTP time...");
      return true;
    }
    return false;
  }

  if (syncState_ != TIME_SYNC_WAITING_FOR_TIME) {
    return false;
  }

  time_t nowSeconds = time(nullptr);
  if (nowSeconds > 1700000000) {
    applySyncedClock();
    finishSync(true);
    return true;
  }

  return false;
}

void ClockSync::updateClock(bool pauseClock) {
  if (pauseClock || !hasValidTime_) {
    lastClockTickMs_ = millis();
    return;
  }

  uint32_t now = millis();
  uint32_t elapsedMs = now - lastClockTickMs_;
  uint32_t elapsedSeconds = elapsedMs / 1000;
  if (elapsedSeconds == 0) {
    return;
  }

  lastClockTickMs_ += elapsedSeconds * 1000;

  uint32_t totalSeconds = second_ + elapsedSeconds;
  second_ = totalSeconds % 60;

  uint32_t totalMinutes = minute_ + (totalSeconds / 60);
  minute_ = totalMinutes % 60;
  hour_ = (hour_ + (totalMinutes / 60)) % 24;
}

void ClockSync::setManualTime(uint8_t hour, uint8_t minute) {
  hour_ = hour;
  minute_ = minute;
  second_ = 0;
  lastClockTickMs_ = millis();
  hasValidTime_ = true;
}

void ClockSync::disconnectAndDisableWifi() {
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
}

bool ClockSync::hasValidTime() const {
  return hasValidTime_;
}

bool ClockSync::isSyncActive() const {
  return syncState_ == TIME_SYNC_CONNECTING_WIFI || syncState_ == TIME_SYNC_WAITING_FOR_TIME;
}

TimeSyncState ClockSync::syncState() const {
  return syncState_;
}

const char* ClockSync::syncStatusText() const {
  switch (syncState_) {
    case TIME_SYNC_CONNECTING_WIFI:
      return "WIFI...";
    case TIME_SYNC_WAITING_FOR_TIME:
      return "NTP...";
    case TIME_SYNC_FAILED:
      return "NO WIFI";
    case TIME_SYNC_COMPLETE:
      return "DONE";
    case TIME_SYNC_IDLE:
    default:
      return "IDLE";
  }
}

const char* ClockSync::wifiSsid() const {
  return wifiSsid_;
}

uint8_t ClockSync::hour() const {
  return hour_;
}

uint8_t ClockSync::minute() const {
  return minute_;
}

uint8_t ClockSync::second() const {
  return second_;
}

void ClockSync::finishSync(bool success) {
  disconnectAndDisableWifi();

  if (success) {
    syncState_ = TIME_SYNC_COMPLETE;
    Serial.println("Wi-Fi disconnected. Time sync complete.");
  } else {
    syncState_ = TIME_SYNC_FAILED;
    Serial.println("Wi-Fi disconnected. Time sync failed.");
  }
}

void ClockSync::applySyncedClock() {
  time_t nowSeconds = time(nullptr);
  struct tm timeInfo;
  localtime_r(&nowSeconds, &timeInfo);

  hour_ = timeInfo.tm_hour;
  minute_ = timeInfo.tm_min;
  second_ = timeInfo.tm_sec;
  lastClockTickMs_ = millis();
  hasValidTime_ = true;

  Serial.print("Clock synced from NTP: ");
  uint8_t hour12 = hour_ % 12;
  if (hour12 == 0) {
    hour12 = 12;
  }
  if (hour12 < 10) {
    Serial.print("0");
  }
  Serial.print(hour12);
  Serial.print(":");
  if (minute_ < 10) {
    Serial.print("0");
  }
  Serial.print(minute_);
  Serial.println(hour_ < 12 ? " AM" : " PM");
}
