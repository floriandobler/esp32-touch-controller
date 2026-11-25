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
#include "../../src/dmx.h"

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

static void dump(tosc::Engine &e, tosc::Control &c, int depth) {
  auto col = c.color();
  printf("%*s%s '%s' [%s] frame=(%g,%g %gx%g) color=(%g,%g,%g,%g)\n", depth * 2, "",
         tosc::typeName(c.type), c.name.c_str(), c.typeName.c_str(), c.frame.x, c.frame.y, c.frame.w,
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
  // Elemente aus "ESP32 Panel Test 02" (Stufe 3)
  if (auto *g = find(*e.root(), "grid1")) {
    printf("grid1: Fader 3 auf 0.5 (Pfad aus parent.name + Index ab 1)\n");
    e.setNum(*g->children[2], "x", 0.5f);
    assert(!sent.empty() && sent.back().rfind("/grid1/3 ", 0) == 0);
    osc::Message in{"/grid1/4", {osc::Arg::Float(0.8f)}};
    assert(e.handleIncoming(in) && g->children[3]->num("x") > 0.79f);
  }
  if (auto *r = find(*e.root(), "radio2")) {
    printf("radio2: Index 3, dann 9 (wird auf 4 begrenzt)\n");
    e.setNum(*r, "x", 3);
    assert(sent.back() == "/radio2 ,i 3");
    osc::Message in{"/radio2", {osc::Arg::Int(9)}};
    e.handleIncoming(in);
    assert(r->num("x") == 4);
  }
  if (auto *p = find(*e.root(), "pager1")) {
    printf("pager1: Seite 2 empfangen, dann Seite 1 senden\n");
    osc::Message in{"/pager1", {osc::Arg::Int(2)}};
    assert(e.handleIncoming(in) && p->num("page") == 2);
    e.setNum(*p, "page", 1);
    assert(sent.back() == "/pager1 ,i 1");
  }
  if (auto *xy = find(*e.root(), "xy2")) {
    printf("xy2: x=0.25, y=2 (wird auf 1 begrenzt)\n");
    e.setNum(*xy, "x", 0.25f);
    e.setNum(*xy, "y", 2.0f);
    assert(xy->num("y") == 1.0f);
  }
  if (auto *b4 = find(*e.root(), "button4")) {
    e.setNum(*b4, "x", 1);
    assert(sent.back().rfind("/3/button4", 0) == 0);
  }

  // DMX über Art-Net (unabhängig vom Layout)
  {
    printf("\nDMX:\n");
    std::vector<dmx::Change> ch;
    std::string err;
    auto P = [&](const osc::Message &m) { return dmx::parse(m, 0, ch, err); };
    assert(!P({"/composition/master/", {osc::Arg::Float(1)}}));
    assert(!P({"/dmxfoo/1", {osc::Arg::Float(1)}}));
    assert(P({"/dmx/31", {osc::Arg::Float(0.5f)}}) && ch.size() == 1 && ch[0].channel == 31 &&
           ch[0].universe == 0 && ch[0].value == 128);
    printf("  %s\n", dmx::describe(ch).c_str());
    assert(P({"/DMX/2/511", {osc::Arg::Float(1), osc::Arg::Int(300), osc::Arg::Int(7)}}) &&
           ch.size() == 2 && ch[0].universe == 2 && ch[0].value == 255 && ch[1].value == 255);
    printf("  %s (dritter Wert läge hinter Kanal 512)\n", dmx::describe(ch).c_str());
    assert(P({"/dmx/1", {osc::Arg::Int(-5), osc::Arg::Bool(true), osc::Arg::Float(-1)}}) &&
           ch[0].value == 0 && ch[1].value == 255 && ch[2].value == 0);
    assert(P({"/dmx/0", {osc::Arg::Float(1)}}) && ch.empty() && !err.empty());
    printf("  /dmx/0 -> %s\n", err.c_str());
    assert(P({"/dmx/513", {osc::Arg::Float(1)}}) && ch.empty());
    assert(P({"/dmx/31", {}}) && ch.empty());
    assert(P({"/dmx/1/2/3", {osc::Arg::Float(1)}}) && ch.empty());
    assert(P({"/dmx/x", {osc::Arg::Float(1)}}) && ch.empty());

    // Paketaufbau und Sendezeitpunkte
    dmx::Output out;
    std::vector<std::vector<uint8_t>> pk;
    auto send = [&](const uint8_t *d, size_t n) { pk.emplace_back(d, d + n); };
    out.poll(0, send);
    assert(pk.empty());  // ohne Nachricht wird nichts gesendet
    P({"/dmx/31", {osc::Arg::Float(1)}});
    out.apply(ch);
    out.poll(1000, send);
    assert(pk.size() == 1 && pk[0].size() == 18 + 512);
    const uint8_t *a = pk[0].data();
    assert(memcmp(a, "Art-Net\0", 8) == 0 && a[8] == 0x00 && a[9] == 0x50 && a[11] == 14);
    assert(a[12] == 1 && a[14] == 0 && a[15] == 0 && a[16] == 2 && a[17] == 0);
    assert(a[18 + 30] == 255 && a[18 + 29] == 0);
    out.poll(1010, send);  // nichts geändert
    assert(pk.size() == 1);
    P({"/dmx/31", {osc::Arg::Float(0)}});
    out.apply(ch);
    out.poll(1010, send);  // geändert, aber zu früh (25 ms)
    assert(pk.size() == 1);
    out.poll(1025, send);
    assert(pk.size() == 2 && pk[1][18 + 30] == 0 && pk[1][12] == 2);
    out.poll(2025, send);  // Wiederholung nach 1 s
    assert(pk.size() == 3);
    P({"/dmx/300/1", {osc::Arg::Float(1)}});  // Universe 300 = Net 1, SubUni 44
    out.apply(ch);
    out.poll(2030, send);
    assert(pk.size() == 4 && pk[3][14] == 44 && pk[3][15] == 1);
    for (int u = 0; u < 20; u++) {
      P({"/dmx/" + std::to_string(1000 + u) + "/1", {osc::Arg::Float(1)}});
      out.apply(ch);
    }
    assert(out.universeCount() == dmx::MAX_UNIVERSES);
    printf("  Art-Net-Pakete ok (%zu gesendet)\n", pk.size());
  }

  printf("\nAlles in Ordnung.\n");
  return 0;
}
