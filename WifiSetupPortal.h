#ifndef WIFI_SETUP_PORTAL_H
#define WIFI_SETUP_PORTAL_H

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>

#include "ClockSync.h"

class WifiSetupPortal {
public:
  WifiSetupPortal(const char* apSsid, ClockSync& clockSync);

  void start();
  void stop();
  void update();

  bool isActive() const;
  bool savedThisSession() const;
  bool phoneConnected() const;
  uint32_t idleForMs() const;
  const char* apSsid() const;
  String ipAddress() const;

private:
  void configureRoutes();
  void sendHome();
  void handleSave();
  void handleRescan();
  void redirectHome();
  void sendPage(int statusCode, const String& body);
  String scannedNetworksHtml() const;
  static String htmlEscape(const String& text);

  const char* apSsid_;
  ClockSync& clockSync_;
  WebServer server_;
  DNSServer dnsServer_;

  bool active_;
  bool routesConfigured_;
  bool savedThisSession_;
  uint32_t lastActivityMs_;
};

#endif
