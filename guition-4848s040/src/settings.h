// Dauerhafte Einstellungen (NVS). Beim ersten Start gelten die Werte aus config.h/secrets.h.
#pragma once
#include <Arduino.h>

struct Settings {
  String ssid;
  String password;
  String targetHost;
  uint16_t targetPort = 0;
  uint16_t listenPort = 0;
  String layout;  // Dateiname des aktiven Layouts in /layouts
};

namespace settings {
Settings load();
void save(const Settings &s);
void reset();  // auf Werte aus config.h/secrets.h zurücksetzen
}  // namespace settings
