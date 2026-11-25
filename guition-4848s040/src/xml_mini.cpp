#include "xml_mini.h"
#include <cstring>

namespace xml {

const Node *Node::child(const char *n) const {
  for (auto &c : children)
    if (c->name == n) return c.get();
  return nullptr;
}

std::string Node::attr(const char *n) const {
  for (auto &a : attrs)
    if (a.first == n) return a.second;
  return "";
}

std::string Node::childText(const char *n, const char *def) const {
  const Node *c = child(n);
  return c ? c->text : std::string(def);
}

namespace {

struct Parser {
  const char *p, *end;
  std::string err;

  bool startsWith(const char *s) const {
    size_t n = strlen(s);
    return (size_t)(end - p) >= n && memcmp(p, s, n) == 0;
  }
  bool skipPast(const char *s) {
    size_t n = strlen(s);
    while ((size_t)(end - p) >= n) {
      if (memcmp(p, s, n) == 0) { p += n; return true; }
      p++;
    }
    p = end;
    return false;
  }
  void skipWs() {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
  }
  static bool isNameChar(char c) {
    return c && !strchr(" \t\r\n/>=<'\"", c);
  }
  std::string readName() {
    const char *s = p;
    while (p < end && isNameChar(*p)) p++;
    return std::string(s, p - s);
  }

  static void appendDecoded(std::string &out, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
      if (s[i] != '&') { out += s[i]; continue; }
      const char *semi = (const char *)memchr(s + i, ';', n - i);
      if (!semi) { out += s[i]; continue; }
      std::string ent(s + i + 1, semi - s - i - 1);
      if (ent == "lt") out += '<';
      else if (ent == "gt") out += '>';
      else if (ent == "amp") out += '&';
      else if (ent == "quot") out += '"';
      else if (ent == "apos") out += '\'';
      else if (!ent.empty() && ent[0] == '#') {
        unsigned long cp = (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X'))
                               ? strtoul(ent.c_str() + 2, nullptr, 16)
                               : strtoul(ent.c_str() + 1, nullptr, 10);
        // UTF-8 kodieren
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
          out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F));
          out += (char)(0x80 | (cp & 0x3F));
        } else {
          out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F));
          out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
        }
      } else { out.append(s + i, semi - s - i + 1); }
      i = semi - s;
    }
  }

  // Überspringt Deklarationen, Kommentare, DOCTYPE vor dem Wurzelelement
  void skipMisc() {
    for (;;) {
      skipWs();
      if (startsWith("<?")) skipPast("?>");
      else if (startsWith("<!--")) skipPast("-->");
      else if (startsWith("<!")) skipPast(">");
      else return;
    }
  }

  std::unique_ptr<Node> parseElement(int depth) {
    if (depth > 64) { err = "zu tief verschachtelt"; return nullptr; }
    if (p >= end || *p != '<') { err = "'<' erwartet"; return nullptr; }
    p++;
    auto node = std::make_unique<Node>();
    node->name = readName();
    if (node->name.empty()) { err = "Elementname fehlt"; return nullptr; }

    // Attribute
    for (;;) {
      skipWs();
      if (p >= end) { err = "Dateiende im Tag"; return nullptr; }
      if (*p == '/') {
        if (p + 1 < end && p[1] == '>') { p += 2; return node; }
        err = "'/>' erwartet"; return nullptr;
      }
      if (*p == '>') { p++; break; }
      std::string an = readName();
      if (an.empty()) { err = "Attributname fehlt"; return nullptr; }
      skipWs();
      if (p >= end || *p != '=') { err = "'=' erwartet"; return nullptr; }
      p++;
      skipWs();
      if (p >= end || (*p != '"' && *p != '\'')) { err = "Anführungszeichen erwartet"; return nullptr; }
      char q = *p++;
      const char *s = p;
      while (p < end && *p != q) p++;
      if (p >= end) { err = "Attributwert nicht beendet"; return nullptr; }
      std::string val;
      appendDecoded(val, s, p - s);
      p++;
      node->attrs.emplace_back(std::move(an), std::move(val));
    }

    // Inhalt
    for (;;) {
      if (p >= end) { err = "Dateiende in <" + node->name + ">"; return nullptr; }
      if (*p != '<') {
        const char *s = p;
        while (p < end && *p != '<') p++;
        appendDecoded(node->text, s, p - s);
        continue;
      }
      if (startsWith("<![CDATA[")) {
        p += 9;
        const char *s = p;
        if (!skipPast("]]>")) { err = "CDATA nicht beendet"; return nullptr; }
        node->text.append(s, p - 3 - s);
      } else if (startsWith("<!--")) {
        skipPast("-->");
      } else if (startsWith("<?")) {
        skipPast("?>");
      } else if (startsWith("</")) {
        p += 2;
        std::string cn = readName();
        if (cn != node->name) { err = "</" + cn + "> passt nicht zu <" + node->name + ">"; return nullptr; }
        skipWs();
        if (p >= end || *p != '>') { err = "'>' erwartet"; return nullptr; }
        p++;
        return node;
      } else {
        auto c = parseElement(depth + 1);
        if (!c) return nullptr;
        node->children.push_back(std::move(c));
      }
    }
  }
};

}  // namespace

std::unique_ptr<Node> parse(const char *data, size_t len, std::string *err) {
  Parser ps{data, data + len, {}};
  // UTF-8-BOM überspringen
  if (len >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB &&
      (unsigned char)data[2] == 0xBF)
    ps.p += 3;
  ps.skipMisc();
  auto root = ps.parseElement(0);
  if (!root && err) *err = ps.err;
  return root;
}

}  // namespace xml
