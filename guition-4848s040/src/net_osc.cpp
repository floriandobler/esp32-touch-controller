#include "net_osc.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include "config.h"

namespace net_osc {

namespace {

Settings cfg;
WiFiUDP udp;
IPAddress target;
bool targetOk = false;
bool online = false;
bool udpOpen = false;
bool ap = false;
bool mdnsStarted = false;
uint32_t lastAttempt = 0;
uint32_t startedAt = 0;
uint32_t onlineSince = 0;
uint32_t nSent = 0, nRecv = 0;
StatusFn statusCb;

void report(const char *msg, bool sticky = false) {
  Serial.println(msg);
  if (statusCb) statusCb(msg, sticky);
}

void resolveTarget() {
  targetOk = target.fromString(cfg.targetHost.c_str());
  if (!targetOk && online) targetOk = WiFi.hostByName(cfg.targetHost.c_str(), target) == 1;
}

void openUdp() {
  if (udpOpen) udp.stop();
  udpOpen = udp.begin(cfg.listenPort);
}

void startMdns() {
  if (mdnsStarted) return;
  mdnsStarted = MDNS.begin(PANEL_HOSTNAME);
  if (mdnsStarted) {
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("osc", "udp", cfg.listenPort);
  }
}

void reportOnline() {
  char msg[200];
  snprintf(msg, sizeof msg, "%s  empfängt auf Port %u\nsendet an %s:%u%s",
           WiFi.localIP().toString().c_str(), cfg.listenPort, cfg.targetHost.c_str(),
           cfg.targetPort, targetOk ? "" : "  (Ziel nicht gefunden!)");
  report(msg);
}

void startAp() {
  if (ap) return;
  WiFi.mode(WIFI_AP_STA);
  ap = WiFi.softAP(SETUP_AP_SSID, SETUP_AP_PASSWORD);
  if (!ap) return;
  openUdp();
  startMdns();
  char msg[200];
  snprintf(msg, sizeof msg, "Einrichtung: WLAN \"%s\" (Passwort %s)\ndann im Browser http://%s",
           SETUP_AP_SSID, SETUP_AP_PASSWORD, WiFi.softAPIP().toString().c_str());
  report(msg, true);
}

void stopAp() {
  if (!ap) return;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  ap = false;
}

void connectSta() {
  lastAttempt = millis();
  if (cfg.ssid.isEmpty()) return;
  WiFi.disconnect();
  WiFi.begin(cfg.ssid.c_str(), cfg.password.c_str());
}

}  // namespace

void begin(const Settings &s, StatusFn status) {
  cfg = s;
  statusCb = std::move(status);
  startedAt = millis();
  WiFi.setHostname(PANEL_HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // Energiesparen aus: sonst bis zu 100 ms Verzögerung pro Paket
  WiFi.setAutoReconnect(true);
  resolveTarget();
  if (cfg.ssid.isEmpty()) {
    startAp();
    return;
  }
  connectSta();
  char msg[96];
  snprintf(msg, sizeof msg, "Verbinde mit WLAN \"%s\" ...", cfg.ssid.c_str());
  report(msg, true);
}

void apply(const Settings &s) {
  bool wifiChanged = s.ssid != cfg.ssid || s.password != cfg.password;
  bool portChanged = s.listenPort != cfg.listenPort;
  cfg = s;
  resolveTarget();
  if (portChanged && (online || ap)) {
    openUdp();
    if (mdnsStarted) {
      MDNS.end();
      mdnsStarted = false;
      startMdns();
    }
  }
  if (wifiChanged) {
    online = false;
    onlineSince = 0;
    if (cfg.ssid.isEmpty()) {
      WiFi.disconnect();
      startAp();
    } else {
      connectSta();
      char msg[96];
      snprintf(msg, sizeof msg, "Verbinde mit WLAN \"%s\" ...", cfg.ssid.c_str());
      report(msg, true);
    }
  } else if (online) {
    reportOnline();
  }
}

void loop(const std::function<void(const osc::Message &)> &onMessage) {
  bool now = WiFi.status() == WL_CONNECTED;
  if (now && !online) {
    online = true;
    onlineSince = millis();
    resolveTarget();
    openUdp();
    startMdns();
    reportOnline();
  } else if (!now && online) {
    online = false;
    lastAttempt = millis();
    report("WLAN getrennt, verbinde neu ...", true);
  }

  if (!online) {
    // Kein WLAN nach 20 s: Einrichtungs-WLAN öffnen, weiter im Hintergrund versuchen
    if (!ap && millis() - startedAt > 20000) startAp();
    if (!cfg.ssid.isEmpty() && millis() - lastAttempt > 15000) connectSta();
  } else if (ap && millis() - onlineSince > 60000) {
    // Verbunden: Einrichtungs-WLAN nach einer Minute schließen (Zeit, um die neue IP abzulesen)
    stopAp();
    reportOnline();
  }

  if (!udpOpen) return;
  static uint8_t buf[1536];
  for (int i = 0; i < 16; i++) {  // pro Durchlauf begrenzen, damit die Oberfläche flüssig bleibt
    int n = udp.parsePacket();
    if (n <= 0) break;
    int len = udp.read(buf, sizeof buf);
    if (len <= 0) continue;
    nRecv++;
    if (!osc::decode(buf, len, onMessage)) {
      Serial.printf("OSC: ungültiges Paket (%d Bytes) von %s\n", len,
                    udp.remoteIP().toString().c_str());
    }
  }
}

void send(const osc::Message &m) {
  if (!udpOpen || !targetOk) return;
  uint8_t buf[512];
  size_t n = osc::encode(m, buf, sizeof buf);
  if (!n) return;
  udp.beginPacket(target, cfg.targetPort);
  udp.write(buf, n);
  if (udp.endPacket()) nSent++;
}

bool connected() { return online; }
bool apActive() { return ap; }
String ip() {
  if (online) return WiFi.localIP().toString();
  if (ap) return WiFi.softAPIP().toString();
  return "";
}
int rssi() { return online ? WiFi.RSSI() : 0; }
bool targetResolved() { return targetOk; }
uint32_t sentCount() { return nSent; }
uint32_t receivedCount() { return nRecv; }

}  // namespace net_osc
