#include "ClockSync.h"

#include "esp_sntp.h"
#include <time.h>

const uint32_t WIFI_CONNECT_RETRY_MS = 12000;
const uint32_t NTP_WAIT_LOG_INTERVAL_MS = 5000;
const time_t VALID_TIME_THRESHOLD = 1700000000;

namespace {
volatile bool ntpTimeSyncReceived = false;

void onNtpTimeSync(struct timeval*) {
  ntpTimeSyncReceived = true;
}
}

ClockSync::ClockSync(
  const char* wifiSsid,
  const char* wifiPassword,
  const char* timezone,
  const char* ntpServer1,
  const char* ntpServer2,
  const char* ntpServer3,
  uint32_t wifiTimeoutMs,
  uint32_t ntpTimeoutMs
) : wifiSsid_(wifiSsid),
    wifiPassword_(wifiPassword),
    timezone_(timezone),
    ntpServer1_(ntpServer1),
    ntpServer2_(ntpServer2),
    ntpServer3_(ntpServer3),
    wifiTimeoutMs_(wifiTimeoutMs),
    ntpTimeoutMs_(ntpTimeoutMs),
    syncState_(TIME_SYNC_IDLE),
    hasValidTime_(false),
    hour_(0),
    minute_(0),
    second_(0),
    lastClockTickMs_(0),
    syncStartedAtMs_(0),
    ntpStartedAtMs_(0),
    lastNtpWaitLogMs_(0),
    connectAttemptStartedAtMs_(0),
    connectAttempt_(0),
    lastLoggedWifiStatus_(WL_IDLE_STATUS),
    loggedExistingSystemTime_(false) {
}

void ClockSync::begin() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.setSleep(false);
  sntp_set_time_sync_notification_cb(onNtpTimeSync);
  lastClockTickMs_ = millis();
}

void ClockSync::startSync() {
  if (syncState_ == TIME_SYNC_CONNECTING_WIFI || syncState_ == TIME_SYNC_WAITING_FOR_TIME) {
    return;
  }

  syncState_ = TIME_SYNC_CONNECTING_WIFI;
  syncStartedAtMs_ = millis();
  connectAttempt_ = 0;
  loggedExistingSystemTime_ = false;
  ntpTimeSyncReceived = false;
  beginConnectionAttempt();
}

bool ClockSync::updateSync() {
  if (syncState_ == TIME_SYNC_IDLE ||
      syncState_ == TIME_SYNC_COMPLETE ||
      syncState_ == TIME_SYNC_FAILED) {
    return false;
  }

  uint32_t now = millis();
  if (syncState_ == TIME_SYNC_CONNECTING_WIFI) {
    if (now - syncStartedAtMs_ >= wifiTimeoutMs_) {
      Serial.println("Wi-Fi connection timed out.");
      logScanResult();
      finishSync(false);
      return true;
    }

    wl_status_t wifiStatus = WiFi.status();
    if (wifiStatus != lastLoggedWifiStatus_) {
      lastLoggedWifiStatus_ = wifiStatus;
      logWifiStatus(wifiStatus);
    }

    if (wifiStatus == WL_CONNECTED) {
      Serial.print("Wi-Fi connected. IP: ");
      Serial.println(WiFi.localIP());
      Serial.print("Wi-Fi RSSI: ");
      Serial.print(WiFi.RSSI());
      Serial.println(" dBm");
      configTzTime(timezone_, ntpServer1_, ntpServer2_, ntpServer3_);
      ntpStartedAtMs_ = now;
      lastNtpWaitLogMs_ = now;
      syncState_ = TIME_SYNC_WAITING_FOR_TIME;
      Serial.println("Waiting for NTP time...");
      return true;
    }

    if (now - connectAttemptStartedAtMs_ >= WIFI_CONNECT_RETRY_MS) {
      Serial.println("Wi-Fi connect attempt timed out. Restarting Wi-Fi connection.");
      beginConnectionAttempt();
      return true;
    }

    return false;
  }

  if (syncState_ != TIME_SYNC_WAITING_FOR_TIME) {
    return false;
  }

  wl_status_t wifiStatus = WiFi.status();
  if (wifiStatus != lastLoggedWifiStatus_) {
    lastLoggedWifiStatus_ = wifiStatus;
    logWifiStatus(wifiStatus);
  }

  if (ntpTimeSyncReceived) {
    applySyncedClock();
    finishSync(true);
    return true;
  }

  time_t nowSeconds = time(nullptr);
  if (!loggedExistingSystemTime_ && nowSeconds > VALID_TIME_THRESHOLD) {
    loggedExistingSystemTime_ = true;
    Serial.println("System time is already valid-looking; waiting for fresh NTP sync event.");
  }

  if (now - ntpStartedAtMs_ >= ntpTimeoutMs_) {
    Serial.print("NTP sync timed out after ");
    Serial.print((now - ntpStartedAtMs_) / 1000);
    Serial.println(" seconds.");
    Serial.print("Wi-Fi still connected: ");
    Serial.println(WiFi.status() == WL_CONNECTED ? "yes" : "no");
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("Wi-Fi RSSI at NTP timeout: ");
      Serial.print(WiFi.RSSI());
      Serial.println(" dBm");
    }
    finishSync(false);
    return true;
  }

  if (now - lastNtpWaitLogMs_ >= NTP_WAIT_LOG_INTERVAL_MS) {
    lastNtpWaitLogMs_ = now;
    Serial.print("Still waiting for NTP time, elapsed ");
    Serial.print((now - ntpStartedAtMs_) / 1000);
    Serial.println(" seconds.");
  }

  return false;
}

void ClockSync::beginConnectionAttempt() {
  connectAttempt_++;
  connectAttemptStartedAtMs_ = millis();
  lastLoggedWifiStatus_ = WL_IDLE_STATUS;

  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_STA);

  Serial.print("Connecting to Wi-Fi SSID ");
  Serial.print(wifiSsid_);
  Serial.print(" attempt ");
  Serial.println(connectAttempt_);

  WiFi.begin(wifiSsid_, wifiPassword_);
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

void ClockSync::logWifiStatus(wl_status_t wifiStatus) const {
  Serial.print("Wi-Fi status changed: ");
  Serial.print((int)wifiStatus);
  Serial.print(" (");
  Serial.print(wifiStatusName(wifiStatus));
  Serial.println(")");
}

void ClockSync::logScanResult() {
  Serial.print("Scanning for Wi-Fi SSID ");
  Serial.print(wifiSsid_);
  Serial.println(" before giving up...");

  WiFi.disconnect(false, false);
  int networkCount = WiFi.scanNetworks(false, true);
  if (networkCount < 0) {
    Serial.print("Wi-Fi scan failed: ");
    Serial.println(networkCount);
    return;
  }

  bool foundSsid = false;
  for (int i = 0; i < networkCount; i++) {
    if (WiFi.SSID(i) == wifiSsid_) {
      foundSsid = true;
      Serial.print("Found target SSID. RSSI: ");
      Serial.print(WiFi.RSSI(i));
      Serial.print(" dBm, channel: ");
      Serial.print(WiFi.channel(i));
      Serial.print(", encryption: ");
      Serial.println((int)WiFi.encryptionType(i));
    }
  }

  if (!foundSsid) {
    Serial.print("Target SSID not found. Networks seen: ");
    Serial.println(networkCount);
  }

  WiFi.scanDelete();
}

const char* ClockSync::wifiStatusName(wl_status_t wifiStatus) const {
  switch (wifiStatus) {
    case WL_IDLE_STATUS:
      return "IDLE";
    case WL_NO_SSID_AVAIL:
      return "NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED:
      return "SCAN_COMPLETED";
    case WL_CONNECTED:
      return "CONNECTED";
    case WL_CONNECT_FAILED:
      return "CONNECT_FAILED";
    case WL_CONNECTION_LOST:
      return "CONNECTION_LOST";
    case WL_DISCONNECTED:
      return "DISCONNECTED";
    case WL_NO_SHIELD:
      return "NO_SHIELD";
    default:
      return "UNKNOWN";
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
