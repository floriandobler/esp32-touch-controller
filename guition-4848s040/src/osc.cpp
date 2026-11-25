#include "osc.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace osc {

bool Arg::isNumeric() const { return strchr("fidhTF", type) != nullptr; }

float Arg::asFloat() const {
  switch (type) {
    case 'f': case 'd': return f;
    case 'i': case 'h': return (float)i;
    case 'T': return 1.0f;
    case 'F': case 'N': return 0.0f;
    case 's': return (float)atof(s.c_str());
  }
  return 0.0f;
}

std::string Arg::asString() const {
  char b[32];
  switch (type) {
    case 's': return s;
    case 'f': case 'd': snprintf(b, sizeof b, "%g", f); return b;
    case 'i': case 'h': snprintf(b, sizeof b, "%ld", (long)i); return b;
    case 'T': return "true";
    case 'F': return "false";
  }
  return "";
}

namespace {

struct Writer {
  uint8_t *buf;
  size_t cap, pos = 0;
  bool ok = true;

  void raw(const void *d, size_t n) {
    if (pos + n > cap) { ok = false; return; }
    memcpy(buf + pos, d, n);
    pos += n;
  }
  void pad() {
    while (ok && (pos & 3)) { uint8_t z = 0; raw(&z, 1); }
  }
  void str(const std::string &s) {
    raw(s.c_str(), s.size() + 1);  // inkl. Nullbyte
    pad();
  }
  void u32(uint32_t v) {
    uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    raw(b, 4);
  }
};

uint32_t rdU32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// Liest einen OSC-String ab pos; setzt pos hinter das Padding.
bool rdStr(const uint8_t *buf, size_t len, size_t &pos, std::string &out) {
  if (pos >= len) return false;
  const void *z = memchr(buf + pos, 0, len - pos);
  if (!z) return false;
  size_t n = (const uint8_t *)z - (buf + pos);
  out.assign((const char *)buf + pos, n);
  pos += (n + 4) & ~(size_t)3;
  return pos <= len;
}

bool decodeMessage(const uint8_t *buf, size_t len, Message &m) {
  size_t pos = 0;
  if (!rdStr(buf, len, pos, m.path) || m.path.empty() || m.path[0] != '/') return false;
  std::string tags;
  if (pos >= len) return true;  // Nachricht ganz ohne Typ-String (alt, erlaubt)
  if (!rdStr(buf, len, pos, tags) || tags.empty() || tags[0] != ',') return false;
  for (size_t t = 1; t < tags.size(); t++) {
    Arg a;
    a.type = tags[t];
    switch (a.type) {
      case 'f': {
        if (pos + 4 > len) return false;
        uint32_t u = rdU32(buf + pos); pos += 4;
        memcpy(&a.f, &u, 4);
        break;
      }
      case 'i': {
        if (pos + 4 > len) return false;
        a.i = (int32_t)rdU32(buf + pos); pos += 4;
        break;
      }
      case 'd': {
        if (pos + 8 > len) return false;
        uint64_t u = ((uint64_t)rdU32(buf + pos) << 32) | rdU32(buf + pos + 4); pos += 8;
        double d; memcpy(&d, &u, 8); a.f = (float)d;
        break;
      }
      case 'h': {
        if (pos + 8 > len) return false;
        uint64_t u = ((uint64_t)rdU32(buf + pos) << 32) | rdU32(buf + pos + 4); pos += 8;
        a.i = (int32_t)(int64_t)u;
        break;
      }
      case 's': case 'S':
        a.type = 's';
        if (!rdStr(buf, len, pos, a.s)) return false;
        break;
      case 'b': {  // Blob: überspringen
        if (pos + 4 > len) return false;
        uint32_t n = rdU32(buf + pos); pos += 4 + ((n + 3) & ~3u);
        if (pos > len) return false;
        a.type = 'N';
        break;
      }
      case 'c': case 'r': case 'm':  // 32-Bit-Typen, als int behandeln
        if (pos + 4 > len) return false;
        a.i = (int32_t)rdU32(buf + pos); pos += 4; a.type = 'i';
        break;
      case 't':  // Zeitstempel: überspringen
        if (pos + 8 > len) return false;
        pos += 8; a.type = 'N';
        break;
      case 'T': case 'F': case 'N': case 'I':
        break;
      default:
        return false;  // unbekannter Typ: Länge unbekannt, abbrechen
    }
    m.args.push_back(std::move(a));
  }
  return true;
}

bool decodeAny(const uint8_t *buf, size_t len, const std::function<void(const Message &)> &cb,
               int depth) {
  if (len < 4 || depth > 8) return false;
  if (len >= 16 && memcmp(buf, "#bundle\0", 8) == 0) {
    size_t pos = 16;  // "#bundle\0" + Zeitstempel
    while (pos + 4 <= len) {
      uint32_t n = rdU32(buf + pos); pos += 4;
      if (pos + n > len) return false;
      if (!decodeAny(buf + pos, n, cb, depth + 1)) return false;
      pos += n;
    }
    return true;
  }
  Message m;
  if (!decodeMessage(buf, len, m)) return false;
  cb(m);
  return true;
}

}  // namespace

size_t encode(const Message &m, uint8_t *buf, size_t cap) {
  Writer w{buf, cap};
  w.str(m.path);
  std::string tags = ",";
  for (auto &a : m.args) tags += a.type;
  w.str(tags);
  for (auto &a : m.args) {
    switch (a.type) {
      case 'f': { uint32_t u; memcpy(&u, &a.f, 4); w.u32(u); break; }
      case 'i': w.u32((uint32_t)a.i); break;
      case 's': w.str(a.s); break;
      default: break;  // T, F, N haben keine Daten
    }
  }
  return w.ok ? w.pos : 0;
}

bool decode(const uint8_t *buf, size_t len, const std::function<void(const Message &)> &cb) {
  return decodeAny(buf, len, cb, 0);
}

std::string toString(const Message &m) {
  std::string out = m.path + " ,";
  for (auto &a : m.args) out += a.type;
  for (auto &a : m.args) {
    out += ' ';
    out += a.type == 's' ? "\"" + a.s + "\"" : a.asString();
  }
  return out;
}

}  // namespace osc
