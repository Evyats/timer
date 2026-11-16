#include "WifiSetupPortal.h"

#include <WiFi.h>

const uint16_t DNS_PORT = 53;
const uint16_t HTTP_PORT = 80;
const size_t MAX_SSID_LENGTH = 32;
const size_t MIN_PASSWORD_LENGTH = 8;
const size_t MAX_PASSWORD_LENGTH = 63;

const char PAGE_HEAD[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Timer Wi-Fi</title>
<style>
body{font-family:system-ui,sans-serif;margin:0 auto;padding:16px;max-width:420px;background:#111;color:#eee}
h1{font-size:22px;margin:4px 0 12px}
h2{font-size:16px;margin:20px 0 6px}
label{display:block;margin:14px 0 4px}
input{box-sizing:border-box;width:100%;padding:10px;font-size:16px;border-radius:6px;border:1px solid #555;background:#222;color:#eee}
input[type=checkbox]{width:auto;margin-right:6px}
button{margin-top:18px;width:100%;padding:12px;font-size:16px;border:0;border-radius:6px;background:#2a7;color:#fff}
a{color:#6bf}
.net{display:flex;justify-content:space-between;padding:10px;margin:4px 0;background:#222;border-radius:6px;color:#eee;text-decoration:none}
.muted{color:#999;font-size:14px}
.ok{color:#6d6}
.err{color:#f77}
</style></head><body><h1>Timer Wi-Fi</h1>
)HTML";

const char PAGE_FOOT[] PROGMEM = R"HTML(<script>
function pick(e){document.getElementById('ssid').value=e.dataset.ssid;document.getElementById('password').focus();return false}
</script></body></html>
)HTML";

WifiSetupPortal::WifiSetupPortal(const char* apSsid, ClockSync& clockSync)
  : apSsid_(apSsid),
    clockSync_(clockSync),
    server_(HTTP_PORT),
    active_(false),
    routesConfigured_(false),
    savedThisSession_(false),
    lastActivityMs_(0) {
}

void WifiSetupPortal::start() {
  if (active_) {
    return;
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsid_);

  configureRoutes();
  server_.begin();
  dnsServer_.start(DNS_PORT, "*", WiFi.softAPIP());
  WiFi.scanNetworks(true);

  active_ = true;
  savedThisSession_ = false;
  lastActivityMs_ = millis();

  Serial.print("Wi-Fi setup AP started: ");
  Serial.print(apSsid_);
  Serial.print(" at http://");
  Serial.println(ipAddress());
}

void WifiSetupPortal::stop() {
  if (!active_) {
    return;
  }

  server_.stop();
  dnsServer_.stop();
  WiFi.scanDelete();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  active_ = false;
  Serial.println("Wi-Fi setup AP stopped.");
}

void WifiSetupPortal::update() {
  if (!active_) {
    return;
  }

  dnsServer_.processNextRequest();
  server_.handleClient();

  if (phoneConnected()) {
    lastActivityMs_ = millis();
  }
}

bool WifiSetupPortal::isActive() const {
  return active_;
}

bool WifiSetupPortal::savedThisSession() const {
  return savedThisSession_;
}

bool WifiSetupPortal::phoneConnected() const {
  return active_ && WiFi.softAPgetStationNum() > 0;
}

uint32_t WifiSetupPortal::idleForMs() const {
  return millis() - lastActivityMs_;
}

const char* WifiSetupPortal::apSsid() const {
  return apSsid_;
}

String WifiSetupPortal::ipAddress() const {
  return WiFi.softAPIP().toString();
}

void WifiSetupPortal::configureRoutes() {
  if (routesConfigured_) {
    return;
  }

  server_.on("/", HTTP_GET, [this]() { sendHome(); });
  server_.on("/save", HTTP_POST, [this]() { handleSave(); });
  server_.on("/scan", HTTP_GET, [this]() { handleRescan(); });

  // OS connectivity checks: redirecting them opens the captive portal page.
  server_.on("/generate_204", HTTP_ANY, [this]() { redirectHome(); });
  server_.on("/hotspot-detect.html", HTTP_ANY, [this]() { redirectHome(); });
  server_.on("/connecttest.txt", HTTP_ANY, [this]() { redirectHome(); });
  server_.on("/ncsi.txt", HTTP_ANY, [this]() { redirectHome(); });
  server_.on("/fwlink", HTTP_ANY, [this]() { redirectHome(); });
  server_.onNotFound([this]() { redirectHome(); });

  routesConfigured_ = true;
}

void WifiSetupPortal::sendHome() {
  String body;
  body.reserve(2048);

  body += "<p class=\"muted\">Saved network: ";
  body += clockSync_.hasCredentials()
            ? "<b>" + htmlEscape(clockSync_.wifiSsid()) + "</b>"
            : String("none");
  body += "</p>";

  body += "<h2>Networks nearby</h2>";
  body += scannedNetworksHtml();
  body += "<p><a href=\"/scan\">Scan again</a></p>";

  body += "<form method=\"post\" action=\"/save\">";
  body += "<label for=\"ssid\">Network name</label>";
  body += "<input id=\"ssid\" name=\"ssid\" maxlength=\"32\" required value=\"";
  body += htmlEscape(clockSync_.wifiSsid());
  body += "\">";
  body += "<label for=\"password\">Password</label>";
  body += "<input id=\"password\" name=\"password\" type=\"password\" maxlength=\"63\">";
  body += "<label class=\"muted\"><input type=\"checkbox\" ";
  body += "onclick=\"password.type=this.checked?'text':'password'\">Show password</label>";
  body += "<p class=\"muted\">Leave blank to keep the saved password for the same network, ";
  body += "or for an open network.</p>";
  body += "<button>Save</button></form>";

  sendPage(200, body);
}

void WifiSetupPortal::handleSave() {
  String ssid = server_.arg("ssid");
  String password = server_.arg("password");
  ssid.trim();

  String error;
  if (ssid.length() == 0) {
    error = "Network name is empty.";
  } else if (ssid.length() > MAX_SSID_LENGTH) {
    error = "Network name is longer than 32 characters.";
  } else if (password.length() > 0 &&
             (password.length() < MIN_PASSWORD_LENGTH || password.length() > MAX_PASSWORD_LENGTH)) {
    error = "Password must be 8 to 63 characters.";
  }

  if (error.length() > 0) {
    sendPage(400, "<p class=\"err\">" + error + "</p><p><a href=\"/\">Back</a></p>");
    return;
  }

  if (password.length() == 0 && ssid == clockSync_.wifiSsid()) {
    password = clockSync_.wifiPassword();
  }

  clockSync_.saveCredentials(ssid, password);
  savedThisSession_ = true;

  sendPage(
    200,
    "<p class=\"ok\">Saved <b>" + htmlEscape(ssid) + "</b>.</p>"
    "<p>Press the timer's button to finish. It will then sync the time.</p>"
    "<p><a href=\"/\">Back</a></p>"
  );
}

void WifiSetupPortal::handleRescan() {
  if (WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
    WiFi.scanDelete();
    WiFi.scanNetworks(true);
  }

  redirectHome();
}

void WifiSetupPortal::redirectHome() {
  server_.sendHeader("Location", "http://" + ipAddress() + "/", true);
  server_.send(302, "text/plain", "");
}

void WifiSetupPortal::sendPage(int statusCode, const String& body) {
  String page = FPSTR(PAGE_HEAD);
  page += body;
  page += FPSTR(PAGE_FOOT);
  server_.send(statusCode, "text/html", page);
}

String WifiSetupPortal::scannedNetworksHtml() const {
  int networkCount = WiFi.scanComplete();
  if (networkCount == WIFI_SCAN_RUNNING) {
    return "<p class=\"muted\">Scanning... <a href=\"/\">refresh</a></p>";
  }

  if (networkCount <= 0) {
    return "<p class=\"muted\">No networks found.</p>";
  }

  String html;
  for (int i = 0; i < networkCount; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) {
      continue;
    }

    bool duplicate = false;
    for (int j = 0; j < i; j++) {
      if (WiFi.SSID(j) == ssid) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      continue;
    }

    String escapedSsid = htmlEscape(ssid);
    html += "<a class=\"net\" href=\"#\" onclick=\"return pick(this)\" data-ssid=\"";
    html += escapedSsid;
    html += "\"><span>";
    html += escapedSsid;
    html += "</span><span class=\"muted\">";
    if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) {
      html += "open ";
    }
    html += String(WiFi.RSSI(i));
    html += " dBm</span></a>";
  }

  return html;
}

String WifiSetupPortal::htmlEscape(const String& text) {
  String escaped;
  escaped.reserve(text.length());

  for (size_t i = 0; i < text.length(); i++) {
    char c = text[i];
    switch (c) {
      case '&': escaped += "&amp;"; break;
      case '<': escaped += "&lt;"; break;
      case '>': escaped += "&gt;"; break;
      case '"': escaped += "&quot;"; break;
      case '\'': escaped += "&#39;"; break;
      default: escaped += c; break;
    }
  }

  return escaped;
}
