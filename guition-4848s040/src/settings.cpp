#include "settings.h"
#include <Preferences.h>
#include "config.h"

namespace settings {

static const char *NS = "oscpanel";

Settings load() {
  Preferences p;
  p.begin(NS, false);  // schreibend öffnen: legt den Bereich beim ersten Start an (sonst Fehlermeldung)
  Settings s;
  s.ssid = p.getString("ssid", WIFI_SSID);
  s.password = p.getString("pass", WIFI_PASSWORD);
  s.targetHost = p.getString("host", OSC_TARGET_HOST);
  s.targetPort = p.getUShort("tport", OSC_TARGET_PORT);
  s.listenPort = p.getUShort("lport", OSC_LISTEN_PORT);
  s.layout = p.getString("layout", "");
  s.dmxEnabled = p.getBool("dmxon", ARTNET_ENABLED);
  s.dmxHost = p.getString("dmxhost", ARTNET_HOST);
  s.dmxUniverse = p.getUShort("dmxuni", ARTNET_UNIVERSE);
  p.end();
  return s;
}

void save(const Settings &s) {
  Preferences p;
  p.begin(NS, false);
  p.putString("ssid", s.ssid);
  p.putString("pass", s.password);
  p.putString("host", s.targetHost);
  p.putUShort("tport", s.targetPort);
  p.putUShort("lport", s.listenPort);
  p.putString("layout", s.layout);
  p.putBool("dmxon", s.dmxEnabled);
  p.putString("dmxhost", s.dmxHost);
  p.putUShort("dmxuni", s.dmxUniverse);
  p.end();
}

void reset() {
  Preferences p;
  p.begin(NS, false);
  p.clear();
  p.end();
}

}  // namespace settings
