// WLAN-Verbindung, Einrichtungs-WLAN und OSC über UDP.
#pragma once
#include <Arduino.h>
#include <functional>
#include "osc.h"
#include "settings.h"

namespace net_osc {

// Statusmeldung für Display/Log. sticky = bleibt stehen, bis die nächste kommt.
using StatusFn = std::function<void(const char *msg, bool sticky)>;

// Startet die WLAN-Verbindung (nicht blockierend).
void begin(const Settings &s, StatusFn status);

// Neue Einstellungen übernehmen: Ziel/Port sofort, WLAN nur wenn geändert.
void apply(const Settings &s);

// Im loop() aufrufen: Verbindung überwachen, eingehende Pakete verarbeiten.
void loop(const std::function<void(const osc::Message &)> &onMessage);

// Nachricht an das Ziel senden (wird verworfen, solange kein WLAN besteht).
void send(const osc::Message &m);

bool connected();     // im WLAN verbunden
bool apActive();      // Einrichtungs-WLAN läuft
String ip();          // aktuelle IP (WLAN, sonst Einrichtungs-WLAN)
int rssi();           // Signalstärke in dBm (0 = nicht verbunden)
bool targetResolved();
uint32_t sentCount();
uint32_t receivedCount();
uint32_t droppedCount();  // verworfen, weil die Warteschlange voll war

}  // namespace net_osc
