// TouchOSC-Layout (.tosc, Format "lexml" Version 6): Modell und OSC-Logik.
// Plattformunabhängig (kein Arduino/LVGL), damit es am Rechner getestet werden kann.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <unordered_map>
#include <functional>
#include "osc.h"

namespace xml { struct Node; }

namespace tosc {

enum class Type {
  GROUP, BUTTON, FADER, LABEL, TEXT, BOX,
  XY, RADIAL, ENCODER, RADAR, RADIO, GRID, PAGER,
  OTHER
};

const char *typeName(Type t);

struct Color { float r = 0, g = 0, b = 0, a = 1; };
struct Rect { float x = 0, y = 0, w = 0, h = 0; };

struct Partial {
  enum Kind { CONSTANT, INDEX, VALUE, PROPERTY } kind = CONSTANT;
  enum Conv { BOOLEAN, INTEGER, FLOAT, STRING } conv = STRING;
  std::string value;  // Konstante bzw. Name des Werts/der Eigenschaft
  float scaleMin = 0, scaleMax = 1;
};

struct Trigger {
  std::string var;
  enum Cond { ANY, RISE, FALL } cond = ANY;
};

struct OscMessage {
  bool enabled = true, send = true, receive = true, feedback = false, noDuplicates = false;
  std::vector<Trigger> triggers;
  std::vector<Partial> path, args;
  std::string lastSent;  // für noDuplicates
};

struct Value {
  std::string key;
  bool isString = false;
  float num = 0;
  std::string str;
};

struct Control {
  Type type = Type::OTHER;
  std::string typeName;  // z. B. "BUTTON", auch für unbekannte Typen
  std::string name;
  Rect frame;            // relativ zum Elternelement, in Layout-Einheiten
  std::map<std::string, std::string> props;  // einfache Eigenschaften als Text
  std::map<std::string, Color> colors;       // Farbeigenschaften
  std::vector<Value> values;
  std::vector<OscMessage> osc;
  std::vector<std::unique_ptr<Control>> children;
  Control *parent = nullptr;
  int index = 0;         // Position im Elternelement
  void *ui = nullptr;    // gehört der Darstellung (lv_obj_t*)

  bool has(const char *k) const { return props.count(k) != 0; }
  std::string prop(const char *k, const char *def = "") const;
  float propF(const char *k, float def = 0) const;
  int propI(const char *k, int def = 0) const;
  bool propB(const char *k, bool def = false) const;
  Color color(const char *k = "color") const;

  Value *value(const std::string &k);
  const Value *value(const std::string &k) const;
  float num(const char *k, float def = 0) const;
};

class Engine {
 public:
  // Lädt das unkomprimierte lexml-XML. Bei Fehler false und err gesetzt.
  bool load(const char *xmlData, size_t len, std::string *err);
  Control *root() { return root_.get(); }
  size_t controlCount() const { return count_; }

  // Wird aufgerufen, wenn eine OSC-Nachricht gesendet werden soll.
  std::function<void(const osc::Message &)> onSend;
  // Wird aufgerufen, wenn sich ein Wert durch eingehendes OSC geändert hat (Neuzeichnen).
  std::function<void(Control &)> onRemoteChange;

  // Wert setzen, z. B. durch Touch. Löst passende OSC-Nachrichten aus.
  // Der Wert wird vorher auf den gültigen Bereich gebracht (siehe limit).
  void setNum(Control &c, const std::string &key, float v);

  // Gültiger Bereich je Element: x/y 0..1, Radio-Index 0..steps-1, Pager-Seite 0..n-1.
  static float limit(const Control &c, const std::string &key, float v);

  // Eingehende Nachricht verarbeiten. Gibt true zurück, wenn ein Element reagiert hat.
  bool handleIncoming(const osc::Message &m);

  // Pfad einer Nachricht (für Logausgaben und Tests)
  std::string buildPath(const Control &c, const OscMessage &m) const;
  bool buildMessage(const Control &c, const OscMessage &m, osc::Message &out) const;

 private:
  std::unique_ptr<Control> root_;
  size_t count_ = 0;
  // Empfangs-Index: Pfad -> (Element, Nachrichtennummer). Nur für statische Pfade.
  std::unordered_multimap<std::string, std::pair<Control *, int>> rxIndex_;
  std::vector<std::pair<Control *, int>> rxDynamic_;  // Pfade mit VALUE-Anteilen

  std::unique_ptr<Control> parseNode(const xml::Node &n, Control *parent, int index);
  void buildIndex(Control &c);
  void fire(Control &c, const std::string &key, float oldV, float newV, bool remote);
  void sendMessage(Control &c, OscMessage &m);
  std::string partialString(const Control &c, const Partial &p) const;
  bool partialArg(const Control &c, const Partial &p, osc::Arg &out) const;
};

}  // namespace tosc
