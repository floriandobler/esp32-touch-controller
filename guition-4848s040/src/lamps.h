// Tuya-Lampen (Smart Life) im lokalen Netz steuern: OSC an /tuya/... (siehe tuya_proto.h).
// Die Verbindungen laufen in einem eigenen Task, damit eine nicht erreichbare Lampe nie
// Display oder Touch aufhält.
#pragma once
#include <Arduino.h>
#include <functional>
#include <vector>
#include "osc.h"

namespace lamps {

struct Info {
  String name, ip, idTail;  // Geräte-ID nur die letzten Zeichen (Schlüssel werden nie herausgegeben)
  bool online = false;
};

// Lampenliste aus dem Dateisystem laden und den Task starten (nach LittleFS.begin()).
void begin();

// Ist m eine /tuya-Nachricht? Dann wird sie übernommen (nicht als OSC gesendet).
bool handle(const osc::Message &m, String &logText);

// Im loop() aufrufen: liefert Rückmeldungen der Lampen (für das Layout) und Logzeilen.
void loop(const std::function<void(const osc::Message &)> &onFeedback,
          const std::function<void(const String &)> &onLog);

// Neue Lampenliste: je Zeile "ID<TAB>Schlüssel<TAB>IP<TAB>Name". Leer = alle entfernen.
bool configure(const String &lines, String &err);

std::vector<Info> list();

// Bekannten Zustand aller Lampen erneut ans Layout melden (nach einem Layout-Wechsel).
void resendState();

}  // namespace lamps
