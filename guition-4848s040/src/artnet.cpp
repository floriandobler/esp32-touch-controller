#include "artnet.h"
#include <AsyncUDP.h>
#include <WiFi.h>
#include "dmx.h"

namespace artnet {

namespace {

AsyncUDP udp;  // nur zum Senden
dmx::Output out;
bool on = false;
String host;  // leer = Broadcast ins eigene Netz
uint16_t universe = 0;
uint32_t nSent = 0;
std::vector<dmx::Change> changes;

// Zieladresse: feste IP oder Broadcast-Adresse des WLANs (z. B. 10.10.1.255)
bool targetAddress(IPAddress &ip, bool &broadcast) {
  broadcast = host.isEmpty();
  if (!broadcast) return ip.fromString(host.c_str());
  if (WiFi.status() != WL_CONNECTED) return false;
  uint32_t a = (uint32_t)WiFi.localIP(), m = (uint32_t)WiFi.subnetMask();
  ip = IPAddress((a & m) | ~m);
  return true;
}

}  // namespace

void apply(const Settings &s) {
  bool wasOn = on;
  on = s.dmxEnabled;
  host = s.dmxHost;
  host.trim();
  universe = s.dmxUniverse;
  if (wasOn && !on) out.clear();  // beim Ausschalten nichts mehr wiederholen
}

bool handle(const osc::Message &m, String &logText) {
  if (!on) return false;
  std::string err;
  if (!dmx::parse(m, universe, changes, err)) return false;
  if (!err.empty()) {
    logText = String(m.path.c_str()) + "  (DMX: " + err.c_str() + ")";
    return true;
  }
  if (!out.apply(changes)) err = "  (zu viele Universes, max. 8)";
  logText = String(dmx::describe(changes).c_str()) + err.c_str();
  return true;
}

void loop() {
  if (!on || out.universeCount() == 0) return;
  IPAddress ip;
  bool bc;
  if (!targetAddress(ip, bc)) return;
  out.poll(millis(), [&](const uint8_t *d, size_t n) {
    if (udp.writeTo(d, n, ip, dmx::ARTNET_PORT) == n) nSent++;
  });
}

bool enabled() { return on; }

String target() {
  IPAddress ip;
  bool bc;
  if (!targetAddress(ip, bc)) return host.isEmpty() ? "Broadcast (kein WLAN)" : host + " (ungültig)";
  return ip.toString() + (bc ? " (Broadcast)" : "");
}

uint32_t sentCount() { return nSent; }

}  // namespace artnet
