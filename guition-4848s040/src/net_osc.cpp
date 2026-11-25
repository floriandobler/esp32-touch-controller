#include "net_osc.h"
#include <WiFi.h>
#include <AsyncUDP.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <ESPmDNS.h>
#include "config.h"
#include <vector>

namespace net_osc {

namespace {

Settings cfg;
AsyncUDP udp;
// Empfangene Pakete: AsyncUDP ruft onPacket in seinem eigenen Task auf, unabhängig davon, ob die
// Hauptschleife gerade zeichnet oder eine Webseite ausliefert. Die Pakete landen hier und werden
// in loop() abgearbeitet. (Der lwIP-Socketpuffer selbst fasst nur 6 Pakete.)
struct Packet {
  uint16_t len;
  uint8_t data[];
};
constexpr int QUEUE_LEN = 512;  // reicht für ~5 s Stillstand bei 100 Nachrichten/s
constexpr size_t MAX_PACKET = 1536;
QueueHandle_t rxQueue = nullptr;
volatile uint32_t nDropped = 0;
IPAddress target;
bool targetOk = false;
bool online = false;
bool udpOpen = false;
bool ap = false;
bool mdnsStarted = false;
uint32_t lastAttempt = 0;
uint32_t startedAt = 0;
uint32_t onlineSince = 0;
uint32_t nSent = 0;
volatile uint32_t nRecv = 0;
StatusFn statusCb;

void report(const char *msg, bool sticky = false) {
  Serial.println(msg);
  if (statusCb) statusCb(msg, sticky);
}

void resolveTarget() {
  targetOk = target.fromString(cfg.targetHost.c_str());
  if (!targetOk && online) targetOk = WiFi.hostByName(cfg.targetHost.c_str(), target) == 1;
}

void onPacket(AsyncUDPPacket &p) {
  size_t len = p.length();
  if (len == 0 || len > MAX_PACKET) return;
  // Im PSRAM ablegen, damit volle Warteschlangen keinen internen RAM belegen
  auto *pkt = (Packet *)heap_caps_malloc(sizeof(Packet) + len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!pkt) pkt = (Packet *)malloc(sizeof(Packet) + len);
  if (!pkt) { nDropped++; return; }
  pkt->len = (uint16_t)len;
  memcpy(pkt->data, p.data(), len);
  if (xQueueSend(rxQueue, &pkt, 0) != pdTRUE) {
    free(pkt);
    nDropped++;
    return;
  }
  nRecv++;
}

void openUdp() {
  if (!rxQueue) rxQueue = xQueueCreate(QUEUE_LEN, sizeof(Packet *));
  if (udpOpen) udp.close();
  udpOpen = udp.listen(cfg.listenPort);
  if (udpOpen) udp.onPacket(onPacket);
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

  if (!rxQueue) return;
  // Alle wartenden Pakete dekodieren. Kommen mehrere Nachrichten an denselben Pfad, zählt nur
  // die neueste – so staut sich bei schnellen Fader-Fahrten nichts auf.
  static std::vector<osc::Message> batch;
  batch.clear();
  Packet *pkt;
  int n = 0;
  while (n++ < QUEUE_LEN && xQueueReceive(rxQueue, &pkt, 0) == pdTRUE) {
    bool ok = osc::decode(pkt->data, pkt->len, [](const osc::Message &m) { batch.push_back(m); });
    if (!ok) Serial.printf("OSC: ungültiges Paket (%u Bytes)\n", pkt->len);
    free(pkt);
  }
  for (size_t i = 0; i < batch.size(); i++) {
    bool newerFollows = false;
    for (size_t j = i + 1; j < batch.size() && !newerFollows; j++)
      newerFollows = batch[j].path == batch[i].path;
    if (!newerFollows) onMessage(batch[i]);
  }
}

void send(const osc::Message &m) {
  if (!udpOpen || !targetOk) return;
  uint8_t buf[512];
  size_t n = osc::encode(m, buf, sizeof buf);
  if (!n) return;
  if (udp.writeTo(buf, n, target, cfg.targetPort) == n) nSent++;
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
uint32_t droppedCount() { return nDropped; }

}  // namespace net_osc
