// Testprogramm für den Rechner: lädt eine .tosc-Datei mit demselben Code wie das Panel,
// gibt den Elementbaum aus und simuliert Touch-Eingaben und eingehendes OSC.
//
//   ./tools/host_test/run.sh data/layout.tosc
#include <cstdio>
#include <fstream>
#include <iterator>
#include <cassert>
#include <zlib.h>
#include "../../src/tosc.h"
#include "../../src/tosc_inflate.h"

// zlib-Variante von inflateFile (auf dem Panel: ROM-tinfl)
bool tosc::inflateFile(const uint8_t *data, size_t len, std::string &out, std::string *err) {
  if (len && data[0] == '<') { out.assign((const char *)data, len); return true; }
  uLongf cap = len * 12 + 4096;
  for (int i = 0; i < 6; i++, cap *= 2) {
    out.resize(cap);
    uLongf n = cap;
    int r = uncompress((Bytef *)&out[0], &n, data, len);
    if (r == Z_OK) { out.resize(n); return true; }
    if (r != Z_BUF_ERROR) break;
  }
  if (err) *err = "zlib-Fehler";
  return false;
}

static const char *typeStr(tosc::Type t) {
  switch (t) {
    case tosc::Type::GROUP: return "GROUP";
    case tosc::Type::BUTTON: return "BUTTON";
    case tosc::Type::FADER: return "FADER";
    case tosc::Type::LABEL: return "LABEL";
    case tosc::Type::TEXT: return "TEXT";
    case tosc::Type::BOX: return "BOX";
    default: return "OTHER";
  }
}

static void dump(tosc::Engine &e, tosc::Control &c, int depth) {
  auto col = c.color();
  printf("%*s%s '%s' [%s] frame=(%g,%g %gx%g) color=(%g,%g,%g,%g)\n", depth * 2, "",
         typeStr(c.type), c.name.c_str(), c.typeName.c_str(), c.frame.x, c.frame.y, c.frame.w,
         c.frame.h, col.r, col.g, col.b, col.a);
  for (auto &v : c.values)
    printf("%*s  value %s = %s\n", depth * 2, "", v.key.c_str(),
           v.isString ? ("\"" + v.str + "\"").c_str() : std::to_string(v.num).c_str());
  for (auto &m : c.osc)
    printf("%*s  osc %s send=%d recv=%d args=%zu\n", depth * 2, "", e.buildPath(c, m).c_str(),
           m.send, m.receive, m.args.size());
  for (auto &ch : c.children) dump(e, *ch, depth + 1);
}

static tosc::Control *find(tosc::Control &c, const std::string &name) {
  if (c.name == name) return &c;
  for (auto &ch : c.children)
    if (auto *r = find(*ch, name)) return r;
  return nullptr;
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "Aufruf: host_test <datei.tosc>\n"); return 2; }
  std::ifstream f(argv[1], std::ios::binary);
  std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (raw.empty()) { fprintf(stderr, "Datei leer/nicht lesbar\n"); return 1; }

  std::string xmlText, err;
  if (!tosc::inflateFile((const uint8_t *)raw.data(), raw.size(), xmlText, &err)) {
    fprintf(stderr, "Entpacken: %s\n", err.c_str());
    return 1;
  }
  tosc::Engine e;
  if (!e.load(xmlText.data(), xmlText.size(), &err)) {
    fprintf(stderr, "Laden: %s\n", err.c_str());
    return 1;
  }
  printf("%zu Elemente, %zu Bytes XML\n\n", e.controlCount(), xmlText.size());
  dump(e, *e.root(), 0);

  // OSC-Ausgang mitschneiden, dabei Kodierung/Dekodierung im Kreis prüfen
  std::vector<std::string> sent;
  e.onSend = [&](const osc::Message &m) {
    uint8_t buf[512];
    size_t n = osc::encode(m, buf, sizeof buf);
    assert(n > 0 && n % 4 == 0);
    osc::decode(buf, n, [&](const osc::Message &back) {
      assert(osc::toString(back) == osc::toString(m));
    });
    sent.push_back(osc::toString(m));
    printf("  -> %s  (%zu Bytes)\n", sent.back().c_str(), n);
  };
  int redraws = 0;
  e.onRemoteChange = [&](tosc::Control &c) {
    redraws++;
    printf("  <- neu zeichnen: %s x=%g\n", c.name.c_str(), c.num("x"));
  };

  printf("\nSimulation:\n");
  if (auto *b = find(*e.root(), "button1")) {
    printf("button1 drücken/loslassen\n");
    e.setNum(*b, "x", 1);
    e.setNum(*b, "x", 0);
  }
  if (auto *fd = find(*e.root(), "fader1")) {
    printf("fader1 auf 0.75, dann nochmal 0.75 (darf nichts senden)\n");
    size_t before = sent.size();
    e.setNum(*fd, "x", 0.75f);
    e.setNum(*fd, "x", 0.75f);
    assert(sent.size() == before + 1);

    printf("Eingang /composition/master/ 0.25\n");
    osc::Message in{"/composition/master/", {osc::Arg::Float(0.25f)}};
    before = sent.size();
    bool hit = e.handleIncoming(in);
    assert(hit && fd->num("x") == 0.25f && redraws == 1);
    assert(sent.size() == before);  // feedback=0: nicht zurücksenden

    // Bundle mit zwei Nachrichten dekodieren
    uint8_t pkt[256];
    size_t pos = 0;
    memcpy(pkt, "#bundle\0", 8); pos = 8;
    memset(pkt + pos, 0, 8); pos += 8;
    for (float v : {0.1f, 0.9f}) {
      osc::Message m{"/composition/master/", {osc::Arg::Float(v)}};
      size_t n = osc::encode(m, pkt + pos + 4, sizeof pkt - pos - 4);
      pkt[pos] = 0; pkt[pos + 1] = 0; pkt[pos + 2] = 0; pkt[pos + 3] = (uint8_t)n;
      pos += 4 + n;
    }
    int count = 0;
    bool ok = osc::decode(pkt, pos, [&](const osc::Message &m) { count++; e.handleIncoming(m); });
    assert(ok && count == 2 && fd->num("x") > 0.89f);
    printf("Bundle ok, fader1 x=%g\n", fd->num("x"));
  }
  printf("\nAlles in Ordnung.\n");
  return 0;
}
