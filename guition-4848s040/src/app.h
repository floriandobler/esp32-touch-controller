// Funktionen des Hauptprogramms, die die Weboberfläche aufruft (Umsetzung in main.cpp).
#pragma once
#include <Arduino.h>
#include "settings.h"

namespace app {

const Settings &settings();
// Speichern und übernehmen. Ein WLAN-Wechsel wird kurz verzögert, damit die Antwort noch rausgeht.
void updateSettings(const Settings &s);

// Layout aktivieren und anzeigen. Bei Fehler bleibt das bisherige Layout aktiv.
bool activateLayout(const String &name, String &err);
const String &activeLayout();
size_t controlCount();

void restart();  // Neustart nach kurzer Verzögerung

}  // namespace app
