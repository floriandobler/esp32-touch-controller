// Einstellungen für den OSC-Player. Persönliche Werte (WLAN, Ziel-IP) gehören in
// include/secrets.h – diese Datei wird nicht weitergegeben (.gitignore).
#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#endif

// WLAN (leer = ohne Netzwerk starten, das Layout funktioniert trotzdem)
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

// Wohin das Panel sendet (IP-Adresse oder Hostname)
#ifndef OSC_TARGET_HOST
#define OSC_TARGET_HOST "192.168.1.10"
#endif
// Resolume: "OSC Input Port" in den Einstellungen, Standard 7000
#ifndef OSC_TARGET_PORT
#define OSC_TARGET_PORT 7000
#endif
// Auf welchem Port das Panel empfängt. Resolume: "OSC Output Port", Standard 7001
#ifndef OSC_LISTEN_PORT
#define OSC_LISTEN_PORT 7001
#endif

// Name im Netzwerk (erreichbar als <name>.local)
#ifndef PANEL_HOSTNAME
#define PANEL_HOSTNAME "osc-panel"
#endif

// Einrichtungs-WLAN: öffnet sich, wenn 20 s lang keine WLAN-Verbindung zustande kommt.
// Passwort mindestens 8 Zeichen.
#ifndef SETUP_AP_SSID
#define SETUP_AP_SSID "OSC-Panel"
#endif
#ifndef SETUP_AP_PASSWORD
#define SETUP_AP_PASSWORD "oscpanel"
#endif

// Layouts liegen im Dateisystem unter /layouts (Upload über die Weboberfläche,
// oder Ordner data/layouts/ mit: pio run -t uploadfs – das löscht alle hochgeladenen Layouts!)
#define LAYOUT_DIR "/layouts"
#define LAYOUT_MAX_BYTES (2 * 1024 * 1024)
