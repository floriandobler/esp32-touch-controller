// Tuya-Lampen (Smart Life), lokales Protokoll 3.3: Rahmen, Verschlüsselung und Lampenlogik.
// Plattformunabhängig (mbedtls gibt es auf dem ESP32 und am Mac), damit es am Rechner testbar ist.
//
// OSC an die Lampen (Werte 0.0–1.0):
//   /tuya/<n>/on      Ein/Aus (> 0.5 = ein)
//   /tuya/<n>/bright  Helligkeit (im Weiß- und Farbmodus)
//   /tuya/<n>/temp    Weißton warm → kalt (schaltet auf Weiß)
//   /tuya/<n>/hue     Farbton, /tuya/<n>/sat Sättigung (schalten auf Farbe)
//   /tuya/<n>/color   Farbton und Sättigung zusammen (z. B. XY-Pad)
// <n> ist die Nummer der Lampe (ab 1) oder "all".
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "osc.h"

namespace tuya {

constexpr uint16_t PORT = 6668;            // TCP, Befehle
constexpr uint16_t DISCOVERY_PORT = 6667;  // UDP-Broadcast der Geräte (verschlüsselt)

enum Cmd : uint32_t { CONTROL = 7, STATUS = 8, HEART_BEAT = 9, DP_QUERY = 10, DISCOVERY = 0x13 };

// --- Rahmen --------------------------------------------------------------------------------

// Befehl verpacken: JSON verschlüsseln (AES-128-ECB, PKCS7), bei CONTROL mit "3.3"-Kopf.
std::string pack(const std::string &key, uint32_t seq, uint32_t cmd, const std::string &json);

struct Frame {
  uint32_t seq = 0, cmd = 0, ret = 0;
  std::string json;  // entschlüsselter Inhalt (leer, wenn keiner)
};

// Nimmt einen vollständigen Rahmen vom Anfang von buf. false = noch nicht genug Daten.
// true = etwas wurde verbraucht; bei unbrauchbaren Daten (falscher Anfang, CRC falsch,
// falscher Schlüssel) ist err gesetzt und out.json leer.
bool takeFrame(std::string &buf, const std::string &key, Frame &out, std::string &err);

// Erkennungs-Broadcast entschlüsseln (öffentlich bekannter Schlüssel). Liefert das JSON.
bool decodeDiscovery(const uint8_t *data, size_t len, std::string &json);

// Wert eines Schlüssels aus flachem JSON lesen ("key":"text" oder "key":123/true)
bool jsonValue(const std::string &json, const std::string &key, std::string &out);

// --- Lampenzustand ---------------------------------------------------------------------------

struct State {
  bool on = true;
  bool colour = false;  // Farb- statt Weißmodus
  float bright = 1;     // 0..1
  float temp = 0;       // 0 = warm, 1 = kalt
  float hue = 0;        // 0..1 (= 0..360°)
  float sat = 1;        // 0..1
};

// Pfad prüfen: false = keine /tuya-Nachricht. Sonst stehen die Lampen (ab 0) in lamps und der
// Parametername in param; bei Fehlern ist err gesetzt und lamps leer.
bool parsePath(const std::string &path, size_t lampCount, std::vector<int> &lamps,
               std::string &param, std::string &err);

// Parameter auf den Zustand anwenden. false + err bei unbekanntem Parameter oder fehlendem Wert.
bool apply(State &s, const std::string &param, const osc::Message &m, std::string &err);

// Datenpunkte (JSON-Objekt ohne Klammern-Umgebung, z. B. "\"28\":\"…\"")
std::string controlData(const State &s);  // DP 28: sofort, ohne Speichern (für Fader)
std::string modeDps(const State &s);      // DP 21 allein: Moduswechsel muss einzeln gehen
std::string persistDps(const State &s);   // DP 22/23 bzw. 24: dauerhaft (App sieht es)
std::string switchDps(const State &s);    // DP 20

// Status der Lampe ({"dps":{…}}) übernehmen; nur enthaltene Datenpunkte ändern s.
bool fromStatus(const std::string &json, State &s);

// Rückmeldung für das Layout: /tuya/<n>/on, bright, temp, hue, sat, color
std::vector<osc::Message> feedback(int lampNumber, const State &s);

}  // namespace tuya
