// Weboberfläche: Layouts hochladen/wechseln, Einstellungen, OSC-Monitor.
#pragma once
#include <Arduino.h>

namespace web {

void begin();
void loop();

// Zeile im OSC-Monitor der Weboberfläche ablegen (dir: '>' gesendet, '<' empfangen, '!' Info).
void log(char dir, const String &text);

}  // namespace web
