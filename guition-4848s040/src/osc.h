// Minimale OSC-1.0-Kodierung/Dekodierung (Nachrichten und Bundles).
// Plattformunabhängig, damit sie auch am Rechner getestet werden kann.
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <functional>

namespace osc {

struct Arg {
  char type = 'N';  // f, i, s, T, F, N (beim Empfang zusätzlich d, h)
  float f = 0;
  int32_t i = 0;
  std::string s;

  static Arg Float(float v) { Arg a; a.type = 'f'; a.f = v; return a; }
  static Arg Int(int32_t v) { Arg a; a.type = 'i'; a.i = v; return a; }
  static Arg Str(std::string v) { Arg a; a.type = 's'; a.s = std::move(v); return a; }
  static Arg Bool(bool v) { Arg a; a.type = v ? 'T' : 'F'; return a; }

  bool isNumeric() const;
  float asFloat() const;
  std::string asString() const;
};

struct Message {
  std::string path;
  std::vector<Arg> args;
};

// Kodiert eine Nachricht. Gibt die Länge zurück, 0 wenn der Puffer zu klein ist.
size_t encode(const Message &m, uint8_t *buf, size_t cap);

// Dekodiert ein Paket (Nachricht oder Bundle) und ruft cb für jede Nachricht auf.
// Gibt false zurück, wenn das Paket fehlerhaft ist.
bool decode(const uint8_t *buf, size_t len, const std::function<void(const Message &)> &cb);

// Lesbare Form für Logausgaben, z. B. "/a/b ,fi 0.5 3"
std::string toString(const Message &m);

}  // namespace osc
