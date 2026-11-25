#include "tosc.h"
#include "xml_mini.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace tosc {

// ---------------------------------------------------------------------------
// Control
// ---------------------------------------------------------------------------
std::string Control::prop(const char *k, const char *def) const {
  auto it = props.find(k);
  return it == props.end() ? std::string(def) : it->second;
}
float Control::propF(const char *k, float def) const {
  auto it = props.find(k);
  return it == props.end() ? def : (float)atof(it->second.c_str());
}
int Control::propI(const char *k, int def) const {
  auto it = props.find(k);
  return it == props.end() ? def : atoi(it->second.c_str());
}
bool Control::propB(const char *k, bool def) const {
  auto it = props.find(k);
  if (it == props.end()) return def;
  return it->second == "1" || it->second == "true";
}
Color Control::color(const char *k) const {
  auto it = colors.find(k);
  return it == colors.end() ? Color{} : it->second;
}
Value *Control::value(const std::string &k) {
  for (auto &v : values)
    if (v.key == k) return &v;
  return nullptr;
}
const Value *Control::value(const std::string &k) const {
  for (auto &v : values)
    if (v.key == k) return &v;
  return nullptr;
}
float Control::num(const char *k, float def) const {
  const Value *v = value(k);
  return v ? v->num : def;
}

// ---------------------------------------------------------------------------
// Laden
// ---------------------------------------------------------------------------
namespace {

float toF(const std::string &s) { return (float)atof(s.c_str()); }

bool isNumber(const std::string &s) {
  if (s.empty()) return false;
  char *end = nullptr;
  strtod(s.c_str(), &end);
  return end && *end == 0;
}

const char *const TYPE_NAMES[] = {"GROUP", "BUTTON", "FADER", "LABEL", "TEXT",  "BOX",   "XY",
                                  "RADIAL", "ENCODER", "RADAR", "RADIO", "GRID", "PAGER"};

Type typeFromName(const std::string &t) {
  for (size_t i = 0; i < sizeof TYPE_NAMES / sizeof TYPE_NAMES[0]; i++)
    if (t == TYPE_NAMES[i]) return (Type)i;
  return Type::OTHER;
}

Partial parsePartial(const xml::Node &n) {
  Partial p;
  std::string k = n.childText("type", "CONSTANT");
  if (k == "INDEX") p.kind = Partial::INDEX;
  else if (k == "VALUE") p.kind = Partial::VALUE;
  else if (k == "PROPERTY") p.kind = Partial::PROPERTY;
  else p.kind = Partial::CONSTANT;
  std::string c = n.childText("conversion", "STRING");
  if (c == "BOOLEAN") p.conv = Partial::BOOLEAN;
  else if (c == "INTEGER") p.conv = Partial::INTEGER;
  else if (c == "FLOAT") p.conv = Partial::FLOAT;
  else p.conv = Partial::STRING;
  p.value = n.childText("value");
  p.scaleMin = toF(n.childText("scaleMin", "0"));
  p.scaleMax = toF(n.childText("scaleMax", "1"));
  return p;
}

bool b01(const xml::Node &n, const char *k, bool def) {
  const xml::Node *c = n.child(k);
  return c ? (c->text == "1" || c->text == "true") : def;
}

std::string fmtNum(float v) {
  char b[32];
  if (std::fabs(v - std::round(v)) < 1e-6f) snprintf(b, sizeof b, "%ld", (long)std::lround(v));
  else snprintf(b, sizeof b, "%g", v);
  return b;
}

}  // namespace

const char *typeName(Type t) {
  size_t i = (size_t)t;
  return i < sizeof TYPE_NAMES / sizeof TYPE_NAMES[0] ? TYPE_NAMES[i] : "OTHER";
}

std::unique_ptr<Control> Engine::parseNode(const xml::Node &n, Control *parent, int index) {
  auto c = std::make_unique<Control>();
  c->parent = parent;
  c->index = index;
  c->typeName = n.attr("type");
  c->type = typeFromName(c->typeName);
  count_++;

  if (const xml::Node *props = n.child("properties")) {
    for (auto &pp : props->children) {
      if (pp->name != "property") continue;
      std::string key = pp->childText("key");
      std::string t = pp->attr("type");
      const xml::Node *val = pp->child("value");
      if (!val) continue;
      if (t == "c") {
        Color col;
        col.r = toF(val->childText("r", "0"));
        col.g = toF(val->childText("g", "0"));
        col.b = toF(val->childText("b", "0"));
        col.a = toF(val->childText("a", "1"));
        c->colors[key] = col;
      } else if (t == "r") {
        Rect r{toF(val->childText("x", "0")), toF(val->childText("y", "0")),
               toF(val->childText("w", "0")), toF(val->childText("h", "0"))};
        if (key == "frame") c->frame = r;
      } else {
        c->props[key] = val->text;
      }
    }
  }
  c->name = c->prop("name");

  if (const xml::Node *vals = n.child("values")) {
    for (auto &vn : vals->children) {
      if (vn->name != "value") continue;
      Value v;
      v.key = vn->childText("key");
      std::string def = vn->childText("default");
      if (def == "true" || def == "false") {
        v.num = def == "true" ? 1.0f : 0.0f;
      } else if (v.key == "text" || !isNumber(def)) {
        v.isString = true;
        v.str = def;
      } else {
        v.num = toF(def);
      }
      c->values.push_back(std::move(v));
    }
  }
  // Elemente ohne gespeicherten Textwert zeigen die Text-Eigenschaft an
  if ((c->type == Type::LABEL || c->type == Type::TEXT) && !c->value("text")) {
    Value v;
    v.key = "text";
    v.isString = true;
    v.str = c->prop("text");
    c->values.push_back(v);
  }

  if (const xml::Node *msgs = n.child("messages")) {
    for (auto &mn : msgs->children) {
      if (mn->name != "osc") continue;  // MIDI, UDP-Rohdaten usw. werden ignoriert
      OscMessage m;
      m.enabled = b01(*mn, "enabled", true);
      m.send = b01(*mn, "send", true);
      m.receive = b01(*mn, "receive", true);
      m.feedback = b01(*mn, "feedback", false);
      m.noDuplicates = b01(*mn, "noDuplicates", false);
      if (const xml::Node *tr = mn->child("triggers")) {
        for (auto &tn : tr->children) {
          Trigger t;
          t.var = tn->childText("var", "x");
          std::string cond = tn->childText("condition", "ANY");
          t.cond = cond == "RISE" ? Trigger::RISE : cond == "FALL" ? Trigger::FALL : Trigger::ANY;
          m.triggers.push_back(t);
        }
      }
      if (const xml::Node *p = mn->child("path"))
        for (auto &pn : p->children) m.path.push_back(parsePartial(*pn));
      if (const xml::Node *a = mn->child("arguments"))
        for (auto &an : a->children) m.args.push_back(parsePartial(*an));
      if (m.enabled && !m.path.empty()) c->osc.push_back(std::move(m));
    }
  }

  if (const xml::Node *ch = n.child("children")) {
    int i = 0;
    for (auto &cn : ch->children) {
      if (cn->name != "node") continue;
      c->children.push_back(parseNode(*cn, c.get(), i++));
    }
  }
  return c;
}

bool Engine::load(const char *xmlData, size_t len, std::string *err) {
  root_.reset();
  rxIndex_.clear();
  rxDynamic_.clear();
  count_ = 0;

  std::string perr;
  auto doc = xml::parse(xmlData, len, &perr);
  if (!doc) {
    if (err) *err = "XML-Fehler: " + perr;
    return false;
  }
  if (doc->name != "lexml") {
    if (err) *err = "Kein TouchOSC-Layout (Wurzel <" + doc->name + ">)";
    return false;
  }
  const xml::Node *rootNode = doc->child("node");
  if (!rootNode) {
    if (err) *err = "Layout enthält kein Element";
    return false;
  }
  root_ = parseNode(*rootNode, nullptr, 0);
  buildIndex(*root_);
  return true;
}

void Engine::buildIndex(Control &c) {
  for (int i = 0; i < (int)c.osc.size(); i++) {
    const OscMessage &m = c.osc[i];
    if (!m.receive) continue;
    bool dynamic = false;
    for (auto &p : m.path)
      if (p.kind == Partial::VALUE) dynamic = true;
    if (dynamic) rxDynamic_.push_back({&c, i});
    else rxIndex_.emplace(buildPath(c, m), std::make_pair(&c, i));
  }
  for (auto &ch : c.children) buildIndex(*ch);
}

// ---------------------------------------------------------------------------
// Nachrichten bauen
// ---------------------------------------------------------------------------
std::string Engine::partialString(const Control &c, const Partial &p) const {
  osc::Arg a;
  if (!partialArg(c, p, a)) return "";
  return a.asString();
}

bool Engine::partialArg(const Control &c, const Partial &p, osc::Arg &out) const {
  bool isStr = false;
  float num = 0;
  std::string str;

  switch (p.kind) {
    case Partial::CONSTANT:
      str = p.value;
      isStr = !isNumber(str);
      num = isStr ? (str == "true" ? 1.0f : 0.0f) : toF(str);
      break;
    case Partial::INDEX:
      // Skalierung wie bei Werten: mit 1..2 beginnt die Zählung bei 1
      num = p.scaleMin + (float)c.index * (p.scaleMax - p.scaleMin);
      break;
    case Partial::PROPERTY: {
      // "parent.name", "parent.parent.name" … gehen im Baum nach oben
      const Control *src = &c;
      std::string key = p.value;
      while (key.compare(0, 7, "parent.") == 0) {
        if (!src->parent) return false;
        src = src->parent;
        key.erase(0, 7);
      }
      str = key == "name" ? src->name : src->prop(key.c_str());
      isStr = !isNumber(str);
      num = isStr ? 0.0f : toF(str);
      break;
    }
    case Partial::VALUE: {
      const Value *v = c.value(p.value);
      if (!v) return false;
      if (v->isString) {
        isStr = true;
        str = v->str;
        num = toF(str);
      } else {
        num = p.scaleMin + v->num * (p.scaleMax - p.scaleMin);
      }
      break;
    }
  }

  switch (p.conv) {
    case Partial::FLOAT: out = osc::Arg::Float(isStr && p.kind != Partial::VALUE ? toF(str) : num); break;
    case Partial::INTEGER: out = osc::Arg::Int((int32_t)std::lround(num)); break;
    case Partial::BOOLEAN: out = osc::Arg::Bool(num != 0.0f); break;
    case Partial::STRING: out = osc::Arg::Str(isStr ? str : fmtNum(num)); break;
  }
  return true;
}

std::string Engine::buildPath(const Control &c, const OscMessage &m) const {
  std::string path;
  for (auto &p : m.path) path += partialString(c, p);
  return path;
}

bool Engine::buildMessage(const Control &c, const OscMessage &m, osc::Message &out) const {
  out.path = buildPath(c, m);
  out.args.clear();
  if (out.path.empty() || out.path[0] != '/') return false;
  for (auto &p : m.args) {
    osc::Arg a;
    if (partialArg(c, p, a)) out.args.push_back(std::move(a));
  }
  return true;
}

// ---------------------------------------------------------------------------
// Senden
// ---------------------------------------------------------------------------
void Engine::sendMessage(Control &c, OscMessage &m) {
  osc::Message out;
  if (!buildMessage(c, m, out)) return;
  if (m.noDuplicates) {
    std::string sig = osc::toString(out);
    if (sig == m.lastSent) return;
    m.lastSent = sig;
  }
  if (onSend) onSend(out);
}

void Engine::fire(Control &c, const std::string &key, float oldV, float newV, bool remote) {
  for (auto &m : c.osc) {
    if (!m.send) continue;
    if (remote && !m.feedback) continue;
    for (auto &t : m.triggers) {
      if (t.var != key) continue;
      bool ok = t.cond == Trigger::ANY || (t.cond == Trigger::RISE && newV > oldV) ||
                (t.cond == Trigger::FALL && newV < oldV);
      if (ok) {
        sendMessage(c, m);
        break;
      }
    }
  }
}

float Engine::limit(const Control &c, const std::string &key, float v) {
  if (std::isnan(v)) v = 0;
  if (c.type == Type::RADIO && key == "x") {
    float hi = (float)std::max(1, c.propI("steps", 1)) - 1;
    return std::clamp(std::round(v), 0.0f, hi);
  }
  if (c.type == Type::PAGER && key == "page") {
    float hi = (float)std::max<size_t>(1, c.children.size()) - 1;
    return std::clamp(std::round(v), 0.0f, hi);
  }
  if (key == "x" || key == "y" || key == "touch") return std::clamp(v, 0.0f, 1.0f);
  return v;
}

void Engine::setNum(Control &c, const std::string &key, float v) {
  Value *val = c.value(key);
  if (!val || val->isString) return;
  v = limit(c, key, v);
  float old = val->num;
  if (old == v) return;
  val->num = v;
  fire(c, key, old, v, false);
}

// ---------------------------------------------------------------------------
// Empfangen
// ---------------------------------------------------------------------------
bool Engine::handleIncoming(const osc::Message &in) {
  bool any = false;

  auto apply = [&](Control &c, OscMessage &m) {
    bool changed = false;
    std::vector<std::pair<std::string, std::pair<float, float>>> fired;
    for (size_t i = 0; i < m.args.size() && i < in.args.size(); i++) {
      const Partial &p = m.args[i];
      if (p.kind != Partial::VALUE) continue;
      Value *v = c.value(p.value);
      if (!v) continue;
      if (v->isString) {
        std::string s = in.args[i].asString();
        if (s != v->str) { v->str = s; changed = true; }
      } else {
        float a = in.args[i].asFloat();
        float range = p.scaleMax - p.scaleMin;
        float nv = range != 0 ? (a - p.scaleMin) / range : a;
        nv = limit(c, v->key, nv);
        if (nv != v->num) {
          fired.push_back({v->key, {v->num, nv}});
          v->num = nv;
          changed = true;
        }
      }
    }
    any = true;
    if (changed) {
      if (onRemoteChange) onRemoteChange(c);
      for (auto &f : fired) fire(c, f.first, f.second.first, f.second.second, true);
    }
  };

  auto range = rxIndex_.equal_range(in.path);
  for (auto it = range.first; it != range.second; ++it)
    apply(*it->second.first, it->second.first->osc[it->second.second]);
  for (auto &d : rxDynamic_) {
    Control &c = *d.first;
    OscMessage &m = c.osc[d.second];
    if (buildPath(c, m) == in.path) apply(c, m);
  }
  return any;
}

}  // namespace tosc
