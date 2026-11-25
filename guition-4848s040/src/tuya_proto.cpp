#include "tuya_proto.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mbedtls/aes.h>
#include <mbedtls/md5.h>

namespace tuya {

namespace {

constexpr uint32_t PREFIX = 0x000055AA, SUFFIX = 0x0000AA55;
constexpr size_t MAX_FRAME = 4096;

uint32_t crc32(const uint8_t *d, size_t n) {
  uint32_t c = 0xFFFFFFFF;
  for (size_t i = 0; i < n; i++) {
    c ^= d[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320 & (0 - (c & 1)));
  }
  return ~c;
}

void put32(std::string &s, uint32_t v) {
  s += (char)(v >> 24); s += (char)(v >> 16); s += (char)(v >> 8); s += (char)v;
}
uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

// AES-128-ECB mit PKCS7-Auffüllung
std::string encrypt(const std::string &key, const std::string &plain) {
  size_t pad = 16 - plain.size() % 16;
  std::string in = plain + std::string(pad, (char)pad), out(in.size(), '\0');
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_enc(&ctx, (const unsigned char *)key.data(), 128);
  for (size_t i = 0; i < in.size(); i += 16)
    mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, (const unsigned char *)&in[i], (unsigned char *)&out[i]);
  mbedtls_aes_free(&ctx);
  return out;
}

bool decrypt(const std::string &key, const uint8_t *data, size_t len, std::string &out) {
  if (len == 0 || len % 16) return false;
  out.assign(len, '\0');
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_dec(&ctx, (const unsigned char *)key.data(), 128);
  for (size_t i = 0; i < len; i += 16)
    mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_DECRYPT, data + i, (unsigned char *)&out[i]);
  mbedtls_aes_free(&ctx);
  uint8_t pad = (uint8_t)out.back();
  if (pad == 0 || pad > 16) return false;
  out.resize(len - pad);
  return true;
}

// Klartext-JSON beginnt mit '{'; verschlüsselt mit oder ohne "3.3"-Kopf
bool payloadToJson(const std::string &key, const uint8_t *p, size_t n, std::string &json) {
  json.clear();
  if (n == 0) return true;
  if (p[0] == '{') {
    json.assign((const char *)p, n);
    return true;
  }
  if (n >= 15 && memcmp(p, "3.3", 3) == 0) {
    p += 15;
    n -= 15;
  }
  return decrypt(key, p, n, json);
}

std::string hex4(int v) {
  char b[8];
  snprintf(b, sizeof b, "%04x", std::clamp(v, 0, 0xFFFF));
  return b;
}

int scale(float v, int lo, int hi) { return lo + (int)std::lround(std::clamp(v, 0.0f, 1.0f) * (hi - lo)); }

std::string hsv(const State &s) {
  return hex4(scale(s.hue, 0, 360)) + hex4(scale(s.sat, 0, 1000)) + hex4(scale(s.bright, 10, 1000));
}

}  // namespace

std::string pack(const std::string &key, uint32_t seq, uint32_t cmd, const std::string &json) {
  std::string payload = encrypt(key, json);
  if (cmd == CONTROL) payload = std::string("3.3") + std::string(12, '\0') + payload;
  std::string m;
  put32(m, PREFIX);
  put32(m, seq);
  put32(m, cmd);
  put32(m, (uint32_t)payload.size() + 8);
  m += payload;
  put32(m, crc32((const uint8_t *)m.data(), m.size()));
  put32(m, SUFFIX);
  return m;
}

bool takeFrame(std::string &buf, const std::string &key, Frame &out, std::string &err) {
  err.clear();
  out = Frame();
  // bis zum nächsten Rahmenanfang vorspulen
  if (buf.size() >= 4 && get32((const uint8_t *)buf.data()) != PREFIX) {
    size_t next = buf.find(std::string("\x00\x00\x55\xAA", 4), 1);
    buf.erase(0, next == std::string::npos ? buf.size() - 3 : next);
    err = "Datenmüll übersprungen";
    return true;
  }
  if (buf.size() < 16) return false;
  const uint8_t *p = (const uint8_t *)buf.data();
  uint32_t len = get32(p + 12);
  if (len < 12 || len > MAX_FRAME) {
    buf.erase(0, 4);
    err = "ungültige Länge";
    return true;
  }
  if (buf.size() < 16 + len) return false;
  size_t total = 16 + len;
  bool ok = get32(p + total - 4) == SUFFIX && get32(p + total - 8) == crc32(p, total - 8);
  out.seq = get32(p + 4);
  out.cmd = get32(p + 8);
  // Antworten vom Gerät haben einen 4-Byte-Rückgabecode vor den Daten
  out.ret = get32(p + 16);
  if (!ok) err = "Prüfsumme falsch";
  else if (!payloadToJson(key, p + 20, len - 12, out.json)) err = "nicht entschlüsselbar (falscher Schlüssel?)";
  buf.erase(0, total);
  return true;
}

bool decodeDiscovery(const uint8_t *data, size_t len, std::string &json) {
  static std::string key;
  if (key.empty()) {
    unsigned char md[16];
    const char *seed = "yGAdlopoPVldABfn";
    mbedtls_md5((const unsigned char *)seed, strlen(seed), md);
    key.assign((const char *)md, 16);
  }
  if (len < 28 || get32(data) != PREFIX) return false;
  uint32_t n = get32(data + 12);
  if (n < 12 || 16 + n > len) return false;
  return payloadToJson(key, data + 20, n - 12, json) && !json.empty();
}

bool jsonValue(const std::string &json, const std::string &key, std::string &out) {
  std::string pat = "\"" + key + "\"";
  size_t i = json.find(pat);
  if (i == std::string::npos) return false;
  i = json.find_first_not_of(" \t", i + pat.size());
  if (i == std::string::npos || json[i] != ':') return false;
  i = json.find_first_not_of(" \t", i + 1);
  if (i == std::string::npos) return false;
  if (json[i] == '"') {
    size_t e = json.find('"', i + 1);
    if (e == std::string::npos) return false;
    out = json.substr(i + 1, e - i - 1);
  } else {
    size_t e = json.find_first_of(",}] \t", i);
    out = json.substr(i, e == std::string::npos ? std::string::npos : e - i);
  }
  return true;
}

bool parsePath(const std::string &path, size_t lampCount, std::vector<int> &lamps,
               std::string &param, std::string &err) {
  lamps.clear();
  param.clear();
  err.clear();
  if (path.compare(0, 6, "/tuya/") != 0) return false;
  size_t slash = path.find('/', 6);
  if (slash == std::string::npos || slash == 6 || slash + 1 >= path.size()) {
    err = "Pfad muss /tuya/<Lampe>/<Wert> sein";
    return true;
  }
  std::string which = path.substr(6, slash - 6);
  param = path.substr(slash + 1);
  if (which == "all") {
    for (size_t i = 0; i < lampCount; i++) lamps.push_back((int)i);
  } else {
    char *end = nullptr;
    long n = strtol(which.c_str(), &end, 10);
    if (!end || *end || n < 1 || (size_t)n > lampCount) {
      err = lampCount ? "Lampe " + which + " gibt es nicht (1–" + std::to_string(lampCount) + ")"
                      : "keine Lampen eingerichtet";
      return true;
    }
    lamps.push_back((int)n - 1);
  }
  if (lamps.empty()) err = "keine Lampen eingerichtet";
  return true;
}

bool apply(State &s, const std::string &param, const osc::Message &m, std::string &err) {
  auto arg = [&](size_t i, float &v) {
    if (i >= m.args.size()) return false;
    const osc::Arg &a = m.args[i];
    if (a.type == 'T' || a.type == 'F') v = a.type == 'T' ? 1.0f : 0.0f;
    else if (a.isNumeric()) v = a.asFloat();
    else return false;
    if (std::isnan(v)) v = 0;
    v = std::clamp(v, 0.0f, 1.0f);
    return true;
  };
  float a = 0, b = 0;
  if (!arg(0, a)) {
    err = "kein Wert";
    return false;
  }
  if (param == "on") s.on = a > 0.5f;
  else if (param == "bright") s.bright = a;
  else if (param == "temp") { s.temp = a; s.colour = false; }
  else if (param == "hue") { s.hue = a; s.colour = true; }
  else if (param == "sat") { s.sat = a; s.colour = true; }
  else if (param == "color" || param == "colour") {
    s.hue = a;
    if (arg(1, b)) s.sat = b;
    s.colour = true;
  } else {
    err = "unbekannter Wert \"" + param + "\" (on, bright, temp, hue, sat, color)";
    return false;
  }
  return true;
}

std::string controlData(const State &s) {
  // Modus 0 (direkt) + H S V + Helligkeit + Weißton, je 4 Hex-Stellen
  std::string d = "0";
  if (s.colour) d += hsv(s) + "00000000";
  else d += "000000000000" + hex4(scale(s.bright, 10, 1000)) + hex4(scale(s.temp, 0, 1000));
  return "\"28\":\"" + d + "\"";
}

std::string modeDps(const State &s) { return std::string("\"21\":\"") + (s.colour ? "colour" : "white") + "\""; }

std::string persistDps(const State &s) {
  if (s.colour) return "\"24\":\"" + hsv(s) + "\"";
  return "\"22\":" + std::to_string(scale(s.bright, 10, 1000)) + ",\"23\":" + std::to_string(scale(s.temp, 0, 1000));
}

std::string switchDps(const State &s) { return std::string("\"20\":") + (s.on ? "true" : "false"); }

bool fromStatus(const std::string &json, State &s) {
  bool any = false;
  std::string v;
  if (jsonValue(json, "20", v)) { s.on = v == "true"; any = true; }
  if (jsonValue(json, "21", v)) { s.colour = v == "colour"; any = true; }
  if (!s.colour && jsonValue(json, "22", v)) { s.bright = (atoi(v.c_str()) - 10) / 990.0f; any = true; }
  if (jsonValue(json, "23", v)) { s.temp = atoi(v.c_str()) / 1000.0f; any = true; }
  if (jsonValue(json, "24", v) && v.size() == 12) {
    int h = (int)strtol(v.substr(0, 4).c_str(), nullptr, 16);
    int sa = (int)strtol(v.substr(4, 4).c_str(), nullptr, 16);
    int va = (int)strtol(v.substr(8, 4).c_str(), nullptr, 16);
    s.hue = h / 360.0f;
    s.sat = sa / 1000.0f;
    if (s.colour) s.bright = (va - 10) / 990.0f;
    any = true;
  }
  s.bright = std::clamp(s.bright, 0.0f, 1.0f);
  s.temp = std::clamp(s.temp, 0.0f, 1.0f);
  s.hue = std::clamp(s.hue, 0.0f, 1.0f);
  s.sat = std::clamp(s.sat, 0.0f, 1.0f);
  return any;
}

std::vector<osc::Message> feedback(int n, const State &s) {
  std::string base = "/tuya/" + std::to_string(n) + "/";
  return {
      {base + "on", {osc::Arg::Float(s.on ? 1.0f : 0.0f)}},
      {base + "bright", {osc::Arg::Float(s.bright)}},
      {base + "temp", {osc::Arg::Float(s.temp)}},
      {base + "hue", {osc::Arg::Float(s.hue)}},
      {base + "sat", {osc::Arg::Float(s.sat)}},
      {base + "color", {osc::Arg::Float(s.hue), osc::Arg::Float(s.sat)}},
  };
}

}  // namespace tuya
