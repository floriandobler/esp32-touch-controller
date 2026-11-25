// Art-Net-Ausgang: wandelt OSC-Nachrichten an /dmx/... in DMX (siehe dmx.h) und sendet per UDP.
#pragma once
#include <Arduino.h>
#include "osc.h"
#include "settings.h"

namespace artnet {

// Einstellungen übernehmen (an/aus, Ziel, Standard-Universe)
void apply(const Settings &s);

// Ist m eine DMX-Nachricht? Dann wird sie übernommen (nicht als OSC gesendet) und
// logText beschreibt sie für den Monitor. Bei ausgeschaltetem Art-Net immer false.
bool handle(const osc::Message &m, String &logText);

// Im loop() aufrufen: sendet fällige Pakete.
void loop();

bool enabled();
String target();      // tatsächliche Zieladresse, z. B. "10.10.1.255 (Broadcast)"
uint32_t sentCount();

}  // namespace artnet
