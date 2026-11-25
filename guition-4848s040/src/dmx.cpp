#include "dmx.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <strings.h>

namespace dmx {

namespace {

// Zahl ohne Vorzeichen aus einem Pfadteil; -1 bei Fehler
long pathNumber(const std::string &s) {
  if (s.empty() || s.size() > 6) return -1;
  for (char c : s)
    if (c < '0' || c > '9') return -1;
  return atol(s.c_str());
}

bool toDmx(const osc::Arg &a, uint8_t &out) {
  switch (a.type) {
    case 'f':
    case 'd': {
      float v = std::isnan(a.f) ? 0.0f : std::clamp(a.f, 0.0f, 1.0f);
      out = (uint8_t)std::lround(v * 255.0f);
      return true;
    }
    case 'i':
    case 'h':
      out = (uint8_t)std::clamp<int32_t>(a.i, 0, 255);
      return true;
    case 'T': out = 255; return true;
    case 'F': out = 0; return true;
    default: return false;
  }
}

}  // namespace

bool parse(const osc::Message &m, uint16_t defaultUniverse, std::vector<Change> &out,
           std::string &err) {
  out.clear();
  err.clear();
  const std::string &p = m.path;
  if (p.size() < 4 || p[0] != '/' || strncasecmp(p.c_str() + 1, "dmx", 3) != 0) return false;
  if (p.size() > 4 && p[4] != '/') return false;  // z. B. /dmxfoo gehört nicht dazu

  // Pfadteile nach /dmx
  std::vector<std::string> parts;
  size_t pos = 5;
  while (pos <= p.size()) {
    size_t next = p.find('/', pos);
    if (next == std::string::npos) next = p.size();
    if (next > pos) parts.push_back(p.substr(pos, next - pos));
    pos = next + 1;
  }
  long uni = defaultUniverse, ch;
  if (parts.size() == 1) {
    ch = pathNumber(parts[0]);
  } else if (parts.size() == 2) {
    uni = pathNumber(parts[0]);
    ch = pathNumber(parts[1]);
  } else {
    err = "Pfad muss /dmx/<Kanal> oder /dmx/<Universe>/<Kanal> sein";
    return true;
  }
  if (uni < 0 || uni > 32767) {
    err = "Universe muss zwischen 0 und 32767 liegen";
    return true;
  }
  if (ch < 1 || ch > (long)CHANNELS) {
    err = "Kanal muss zwischen 1 und 512 liegen";
    return true;
  }
  for (size_t i = 0; i < m.args.size(); i++) {
    if (ch + (long)i > (long)CHANNELS) break;
    uint8_t v;
    if (!toDmx(m.args[i], v)) {
      out.clear();
      err = "Wert muss Zahl oder true/false sein";
      return true;
    }
    out.push_back({(uint16_t)uni, (uint16_t)(ch + i), v});
  }
  if (out.empty()) err = "kein Wert";
  return true;
}

std::string describe(const std::vector<Change> &c) {
  if (c.empty()) return "DMX (leer)";
  char b[48];
  if (c.size() == 1) snprintf(b, sizeof b, "DMX %u/%u =", c[0].universe, c[0].channel);
  else snprintf(b, sizeof b, "DMX %u/%u–%u =", c[0].universe, c[0].channel, c.back().channel);
  std::string s = b;
  for (auto &x : c) s += " " + std::to_string(x.value);
  return s;
}

size_t buildArtDmx(uint16_t universe, uint8_t sequence, const uint8_t *data, uint16_t len,
                   uint8_t *out) {
  if (len & 1) len++;  // Länge muss gerade sein
  len = std::clamp<uint16_t>(len, 2, CHANNELS);
  memcpy(out, "Art-Net\0", 8);
  out[8] = 0x00;  // OpCode ArtDmx 0x5000, Little Endian
  out[9] = 0x50;
  out[10] = 0;    // Protokollversion 14, Big Endian
  out[11] = 14;
  out[12] = sequence;
  out[13] = 0;    // Physical
  out[14] = universe & 0xFF;         // SubUni
  out[15] = (universe >> 8) & 0x7F;  // Net
  out[16] = len >> 8;                // Länge, Big Endian
  out[17] = len & 0xFF;
  memcpy(out + ARTDMX_HEADER, data, len);
  return ARTDMX_HEADER + len;
}

Output::Universe *Output::find(uint16_t num) const {
  for (auto &u : unis_)
    if (u->num == num) return u.get();
  return nullptr;
}

bool Output::apply(const std::vector<Change> &changes) {
  bool ok = true;
  for (auto &c : changes) {
    Universe *u = find(c.universe);
    if (!u) {
      if (unis_.size() >= MAX_UNIVERSES) { ok = false; continue; }
      unis_.push_back(std::make_unique<Universe>());
      u = unis_.back().get();
      u->num = c.universe;
    }
    if (u->data[c.channel - 1] != c.value) {
      u->data[c.channel - 1] = c.value;
      u->dirty = true;
    }
  }
  return ok;
}

void Output::poll(uint32_t now, const std::function<void(const uint8_t *, size_t)> &send) {
  uint8_t pkt[ARTDMX_HEADER + CHANNELS];
  for (auto &u : unis_) {
    uint32_t since = now - u->lastSent;
    bool due = u->dirty ? (!u->sentOnce || since >= MIN_INTERVAL_MS) : since >= REFRESH_MS;
    if (!due) continue;
    u->sequence = u->sequence == 255 ? 1 : u->sequence + 1;  // 0 = Reihenfolge aus, also 1–255
    // Immer alle 512 Kanäle: manche Empfänger erwarten das volle Universe
    size_t n = buildArtDmx(u->num, u->sequence, u->data, CHANNELS, pkt);
    send(pkt, n);
    u->dirty = false;
    u->sentOnce = true;
    u->lastSent = now;
  }
}

int Output::value(uint16_t universe, uint16_t channel) const {
  Universe *u = find(universe);
  if (!u || channel < 1 || channel > CHANNELS) return -1;
  return u->data[channel - 1];
}

}  // namespace dmx
