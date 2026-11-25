// DMX über Art-Net: OSC-Nachrichten an /dmx/... werden zu DMX-Werten.
// Plattformunabhängig (kein Arduino), damit es am Rechner getestet werden kann.
//
//   /dmx/<kanal>             Kanal 1–512 im eingestellten Universe
//   /dmx/<universe>/<kanal>  anderes Universe (0–32767)
//
// Werte: Float 0.0–1.0 → 0–255, Integer direkt 0–255, true/false → 255/0.
// Mehrere Werte gehen an aufeinanderfolgende Kanäle (/dmx/31 0.5 1.0 → Kanal 31 und 32).
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "osc.h"

namespace dmx {

constexpr uint16_t ARTNET_PORT = 6454;
constexpr size_t CHANNELS = 512;
constexpr size_t ARTDMX_HEADER = 18;
constexpr size_t MAX_UNIVERSES = 8;       // Speicherschutz gegen beliebige Universe-Nummern
constexpr uint32_t MIN_INTERVAL_MS = 25;  // höchstens 40 Pakete/s je Universe
constexpr uint32_t REFRESH_MS = 1000;     // aktive Universes regelmäßig wiederholen

struct Change {
  uint16_t universe;
  uint16_t channel;  // 1–512
  uint8_t value;
};

// Prüft den Pfad. false = keine DMX-Nachricht. Bei true stehen die Änderungen in out;
// passt etwas nicht (Kanal außerhalb, kein Wert), ist out leer und err gesetzt.
bool parse(const osc::Message &m, uint16_t defaultUniverse, std::vector<Change> &out,
           std::string &err);

// Kurzer Text für den Monitor, z. B. "DMX 0/31 = 128" oder "DMX 0/31–32 = 128 255"
std::string describe(const std::vector<Change> &changes);

// ArtDmx-Paket bauen (Art-Net 4). out muss ARTDMX_HEADER + len Bytes fassen.
size_t buildArtDmx(uint16_t universe, uint8_t sequence, const uint8_t *data, uint16_t len,
                   uint8_t *out);

// Hält die DMX-Werte je Universe und entscheidet, wann gesendet wird.
// Ein Universe wird erst gesendet, wenn es eine Nachricht dafür gab – so überschreibt das Panel
// nach dem Start nicht die Werte eines anderen Pults mit Nullen.
class Output {
 public:
  // Werte übernehmen. false, wenn schon MAX_UNIVERSES andere Universes belegt sind.
  bool apply(const std::vector<Change> &changes);
  // Regelmäßig aufrufen: sendet geänderte Universes und wiederholt aktive alle REFRESH_MS.
  void poll(uint32_t nowMs, const std::function<void(const uint8_t *, size_t)> &send);
  void clear() { unis_.clear(); }
  size_t universeCount() const { return unis_.size(); }
  int value(uint16_t universe, uint16_t channel) const;  // -1 = unbekannt

 private:
  struct Universe {
    uint16_t num = 0;
    uint8_t data[CHANNELS] = {};
    uint8_t sequence = 0;
    bool dirty = true;
    bool sentOnce = false;
    uint32_t lastSent = 0;
  };
  std::vector<std::unique_ptr<Universe>> unis_;
  Universe *find(uint16_t num) const;
};

}  // namespace dmx
