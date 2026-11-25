#include "ui_tosc.h"
#include <algorithm>
#include <cmath>

using tosc::Control;
using tosc::Type;

namespace ui_tosc {

namespace {

tosc::Engine *g_engine = nullptr;
float g_scale = 1.0f;  // Layout-Einheiten -> Pixel

lv_obj_t *g_status = nullptr;
lv_timer_t *g_status_timer = nullptr;

// Touch-Zustand (es gibt nur einen Zeiger)
lv_point_t g_touch_start;
float g_value_start = 0, g_value_start_y = 0;
float g_last_angle = 0;  // Drehregler: letzter Winkel des Fingers

int px(float v) { return (int)std::lround(v * g_scale); }

lv_color_t col(const tosc::Color &c) {
  return lv_color_make((uint8_t)(c.r * 255), (uint8_t)(c.g * 255), (uint8_t)(c.b * 255));
}
lv_opa_t opa(float a) { return (lv_opa_t)std::clamp(a * 255.0f, 0.0f, 255.0f); }

int radiusFor(const Control &c, const lv_area_t &a) {
  if (c.propI("shape", 1) == 2) return LV_RADIUS_CIRCLE;  // Kreis
  return px(c.propF("cornerRadius", 0) * 4.0f);
}

void fill(lv_layer_t *layer, const lv_area_t &a, lv_color_t color, lv_opa_t o, int radius) {
  if (o == 0) return;
  lv_draw_fill_dsc_t d;
  lv_draw_fill_dsc_init(&d);
  d.color = color;
  d.opa = o;
  d.radius = radius;
  lv_draw_fill(layer, &d, &a);
}

void line(lv_layer_t *layer, int x1, int y1, int x2, int y2, lv_color_t color, lv_opa_t o,
          int width) {
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.p1.x = x1; d.p1.y = y1;
  d.p2.x = x2; d.p2.y = y2;
  d.color = color;
  d.opa = o;
  d.width = width;
  lv_draw_line(layer, &d);
}

// Umriss: 0 = voll, 1 = nur Ecken, 2 = nur Kanten (Mitte)
void outline(lv_layer_t *layer, const Control &c, const lv_area_t &a, lv_color_t color,
             lv_opa_t o, int radius) {
  if (!c.propB("outline", false)) return;
  int style = c.propI("outlineStyle", 0);
  int w = std::max(1, px(1.0f));
  if (style == 0) {
    lv_draw_border_dsc_t d;
    lv_draw_border_dsc_init(&d);
    d.color = color;
    d.opa = o;
    d.width = w;
    d.radius = radius;
    lv_draw_border(layer, &d, &a);
    return;
  }
  int x1 = a.x1, y1 = a.y1, x2 = a.x2, y2 = a.y2;
  int len = std::max(4, (int)(std::min<int32_t>(lv_area_get_width(&a), lv_area_get_height(&a)) * 0.15f));
  if (style == 1) {
    line(layer, x1, y1, x1 + len, y1, color, o, w); line(layer, x1, y1, x1, y1 + len, color, o, w);
    line(layer, x2, y1, x2 - len, y1, color, o, w); line(layer, x2, y1, x2, y1 + len, color, o, w);
    line(layer, x1, y2, x1 + len, y2, color, o, w); line(layer, x1, y2, x1, y2 - len, color, o, w);
    line(layer, x2, y2, x2 - len, y2, color, o, w); line(layer, x2, y2, x2, y2 - len, color, o, w);
  } else {
    int mx = (x1 + x2) / 2, my = (y1 + y2) / 2;
    line(layer, mx - len, y1, mx + len, y1, color, o, w);
    line(layer, mx - len, y2, mx + len, y2, color, o, w);
    line(layer, x1, my - len, x1, my + len, color, o, w);
    line(layer, x2, my - len, x2, my + len, color, o, w);
  }
}

// Bogen: Winkel in Grad wie LVGL (0 = 3 Uhr, im Uhrzeigersinn), width = Dicke nach innen
void arc(lv_layer_t *layer, int cx, int cy, int radius, int width, float start, float end,
         lv_color_t color, lv_opa_t o) {
  if (o == 0 || radius <= 0 || end - start < 0.5f) return;
  lv_draw_arc_dsc_t d;
  lv_draw_arc_dsc_init(&d);
  d.center.x = cx;
  d.center.y = cy;
  d.radius = (uint16_t)radius;
  d.width = std::max(1, std::min(width, radius));
  d.color = color;
  d.opa = o;
  if (end - start >= 359.5f) {
    d.start_angle = 0;
    d.end_angle = 360;
  } else {
    auto norm = [](float a) { a = std::fmod(a, 360.0f); return a < 0 ? a + 360.0f : a; };
    d.start_angle = (lv_value_precise_t)std::lround(norm(start));
    d.end_angle = (lv_value_precise_t)std::lround(norm(end));
    if (d.start_angle == d.end_angle) return;
  }
  lv_draw_arc(layer, &d);
}

// Punkt auf einem Kreis (LVGL-Winkel)
lv_point_t polar(int cx, int cy, float r, float deg) {
  float rad = deg * (float)M_PI / 180.0f;
  return {(int32_t)std::lround(cx + r * std::cos(rad)), (int32_t)std::lround(cy + r * std::sin(rad))};
}

void spoke(lv_layer_t *layer, int cx, int cy, float r1, float r2, float deg, lv_color_t color,
           lv_opa_t o, int width) {
  lv_point_t a = polar(cx, cy, r1, deg), b = polar(cx, cy, r2, deg);
  line(layer, a.x, a.y, b.x, b.y, color, o, width);
}

// Quadrat in der Mitte des Bereichs (für runde Elemente)
struct Circle { int cx, cy, r; };
Circle circleIn(const lv_area_t &a) {
  int w = lv_area_get_width(&a), h = lv_area_get_height(&a);
  return {a.x1 + w / 2, a.y1 + h / 2, std::max(1, std::min(w, h) / 2 - 1)};
}

const lv_font_t *fontFor(int size) {
  int s = (int)std::lround(size * g_scale);
  if (s <= 16) return &lv_font_montserrat_14;
  if (s <= 24) return &lv_font_montserrat_20;
  if (s <= 36) return &lv_font_montserrat_28;
  return &lv_font_montserrat_48;
}

// Text im Bereich; valign: 1 = oben, 2 = Mitte, 3 = unten. wrap = Zeilenumbruch erlaubt.
void text(lv_layer_t *layer, const lv_area_t &a, const char *s, const lv_font_t *font,
          lv_color_t color, lv_opa_t o, lv_text_align_t align, int valign = 2, bool wrap = false) {
  lv_draw_label_dsc_t d;
  lv_draw_label_dsc_init(&d);
  d.text = s;
  d.font = font;
  d.color = color;
  d.opa = o;
  d.align = align;
  if (!wrap) d.flag = LV_TEXT_FLAG_EXPAND;
  lv_area_t ta = a;
  int pad = 4;
  ta.x1 += pad;
  ta.x2 -= pad;
  ta.y1 += pad;
  ta.y2 -= pad;
  lv_point_t sz;
  lv_text_get_size(&sz, s, font, 0, 0, wrap ? lv_area_get_width(&ta) : LV_COORD_MAX, d.flag);
  int free = lv_area_get_height(&ta) - sz.y;
  if (free > 0) {
    if (valign == 2) ta.y1 += free / 2;
    else if (valign == 3) ta.y1 += free;
    ta.y2 = ta.y1 + sz.y;
  }
  if (!wrap) ta.x2 = std::max<int32_t>(ta.x2, ta.x1 + 1);
  lv_draw_label(layer, &d, &ta);
}

// Ausrichtung des Faders: 0 = nach oben, 1 = nach rechts, 2 = nach unten, 3 = nach links
int faderDir(const Control &c) {
  int o = c.propI("orientation", 0) & 3;
  return o;
}

// Radial: Ring mit Lücke unten (300°, beginnt bei 7 Uhr). Encoder: voller Ring, 0 = oben.
constexpr float RADIAL_START = 120.0f, RADIAL_SWEEP = 300.0f;
constexpr float RING_INNER = 0.5f;      // Innenradius im Verhältnis zum Außenradius
constexpr float ENCODER_CURSOR = 36.0f;  // Breite der Encoder-Markierung in Grad

// Kantenlänge des XY-Griffs in Pixeln
int xyHandle(const lv_area_t &a) {
  return std::max(6, std::min(px(24), (int)std::min(lv_area_get_width(&a), lv_area_get_height(&a)) / 3));
}

// Tab-Leiste des Pagers (leer, wenn ausgeblendet)
lv_area_t tabbarArea(const Control &c, const lv_area_t &a) {
  lv_area_t t = a;
  t.y2 = c.propB("tabbar", true) ? a.y1 + px(c.propF("tabbarSize", 40)) - 1 : a.y1 - 1;
  return t;
}

// Radio: Bereich von Feld i (Reihenfolge in Richtung der Ausrichtung, wie beim Fader)
lv_area_t radioCell(const Control &c, const lv_area_t &a, int i) {
  int n = std::max(1, c.propI("steps", 1));
  int dir = c.propI("orientation", 0) & 3;
  bool horiz = dir == 1 || dir == 3;
  int len = horiz ? lv_area_get_width(&a) : lv_area_get_height(&a);
  int pos = (dir == 0 || dir == 3) ? n - 1 - i : i;  // nach oben/links: Feld 0 am Ende
  int s0 = len * pos / n, s1 = len * (pos + 1) / n - 1;
  lv_area_t r = a;
  if (horiz) { r.x1 = a.x1 + s0; r.x2 = a.x1 + s1; }
  else { r.y1 = a.y1 + s0; r.y2 = a.y1 + s1; }
  return r;
}

void drawRing(lv_layer_t *layer, const Control &c, const lv_area_t &a) {
  tosc::Color cc = c.color();
  lv_color_t color = col(cc);
  Circle k = circleIn(a);
  int R = k.r, r = (int)(R * RING_INNER), w = R - r;
  bool radial = c.type == Type::RADIAL;
  float start = radial ? RADIAL_START : 0.0f, sweep = radial ? RADIAL_SWEEP : 360.0f;
  float v = std::clamp(c.num("x"), 0.0f, 1.0f);

  if (c.propB("background", true)) arc(layer, k.cx, k.cy, R, w, start, start + sweep, color, opa(cc.a * 0.25f));

  if (c.propB("grid", false)) {
    int steps = c.propI("gridSteps", 10);
    tosc::Color gc = c.color("gridColor");
    if (steps > 1 && steps < 200)
      for (int i = radial ? 1 : 0; i < steps; i++)
        spoke(layer, k.cx, k.cy, r, R, start + sweep * i / steps, col(gc), opa(gc.a), 1);
  }

  if (radial) {
    float from = c.propB("centered", false) ? 0.5f : 0.0f;
    float lo = std::min(from, v), hi = std::max(from, v);
    if (c.propB("inverted", false)) { float t = 1 - hi; hi = 1 - lo; lo = t; }
    arc(layer, k.cx, k.cy, R, w, start + lo * sweep, start + hi * sweep, color, opa(cc.a));
  } else if (c.propB("cursor", true)) {
    float mid = 270.0f + v * 360.0f;
    arc(layer, k.cx, k.cy, R, w, mid - ENCODER_CURSOR / 2, mid + ENCODER_CURSOR / 2, color, opa(cc.a));
    spoke(layer, k.cx, k.cy, r, R, mid, lv_color_mix(lv_color_white(), color, 110), LV_OPA_COVER,
          std::max(1, px(1.5f)));
  }

  if (c.propB("outline", false)) {
    int lw = std::max(1, px(1.0f));
    arc(layer, k.cx, k.cy, R, lw, start, start + sweep, color, opa(cc.a));
    if (radial) {
      spoke(layer, k.cx, k.cy, r, R, start, color, opa(cc.a), lw);
      spoke(layer, k.cx, k.cy, r, R, start + sweep, color, opa(cc.a), lw);
    }
  }
}

void drawXY(lv_layer_t *layer, const Control &c, const lv_area_t &a, int radius) {
  tosc::Color cc = c.color();
  lv_color_t color = col(cc);
  if (c.propB("background", true)) fill(layer, a, color, opa(cc.a * 0.25f), radius);
  tosc::Color gc = c.color("gridColor");
  int W = lv_area_get_width(&a), H = lv_area_get_height(&a);
  int nx = c.propI("gridStepsX", 10), ny = c.propI("gridStepsY", 10);
  if (c.propB("gridX", false) && nx > 1 && nx < 200)
    for (int i = 1; i < nx; i++) {
      int x = a.x1 + W * i / nx;
      line(layer, x, a.y1, x, a.y2, col(gc), opa(gc.a), 1);
    }
  if (c.propB("gridY", false) && ny > 1 && ny < 200)
    for (int i = 1; i < ny; i++) {
      int y = a.y1 + H * i / ny;
      line(layer, a.x1, y, a.x2, y, col(gc), opa(gc.a), 1);
    }
  int S = xyHandle(a);
  float x = std::clamp(c.num("x"), 0.0f, 1.0f), y = std::clamp(c.num("y"), 0.0f, 1.0f);
  int cx = a.x1 + S / 2 + (int)std::lround(x * (W - S));
  int cy = a.y2 - S / 2 - (int)std::lround(y * (H - S));
  if (c.propB("lines", true)) {
    line(layer, cx, a.y1, cx, a.y2, color, opa(cc.a), std::max(1, px(1.0f)));
    line(layer, a.x1, cy, a.x2, cy, color, opa(cc.a), std::max(1, px(1.0f)));
  }
  if (c.propB("cursor", true)) {
    lv_area_t h = {cx - S / 2, cy - S / 2, cx - S / 2 + S - 1, cy - S / 2 + S - 1};
    fill(layer, h, color, opa(cc.a), std::max(1, px(2)));
  }
  outline(layer, c, a, color, opa(cc.a), radius);
}

// Radar: x = Winkel (0 = 3 Uhr, gegen den Uhrzeigersinn), y = Abstand von der Mitte
void drawRadar(lv_layer_t *layer, const Control &c, const lv_area_t &a) {
  tosc::Color cc = c.color();
  lv_color_t color = col(cc);
  Circle k = circleIn(a);
  lv_area_t sq = {k.cx - k.r, k.cy - k.r, k.cx + k.r, k.cy + k.r};
  if (c.propB("background", true)) fill(layer, sq, color, opa(cc.a * 0.25f), LV_RADIUS_CIRCLE);
  tosc::Color gc = c.color("gridColor");
  int nx = c.propI("gridStepsX", 10), ny = c.propI("gridStepsY", 10);
  if (c.propB("gridY", false) && ny > 1 && ny < 200)
    for (int i = 1; i < ny; i++) arc(layer, k.cx, k.cy, k.r * i / ny, 1, 0, 360, col(gc), opa(gc.a));
  if (c.propB("gridX", false) && nx > 1 && nx < 200)
    for (int i = 0; i < nx; i++) spoke(layer, k.cx, k.cy, 0, k.r, -360.0f * i / nx, col(gc), opa(gc.a), 1);
  float ang = -360.0f * std::clamp(c.num("x"), 0.0f, 1.0f);
  float dist = k.r * std::clamp(c.num("y"), 0.0f, 1.0f);
  if (c.propB("lines", true)) {
    int lw = std::max(1, px(1.0f));
    spoke(layer, k.cx, k.cy, 0, k.r, ang, color, opa(cc.a), lw);
    if (dist >= 1) arc(layer, k.cx, k.cy, (int)dist, lw, 0, 360, color, opa(cc.a));
  }
  if (c.propB("cursor", true)) {
    lv_point_t p = polar(k.cx, k.cy, dist, ang);
    int d = std::max(3, px(5));
    lv_area_t dot = {p.x - d, p.y - d, p.x + d, p.y + d};
    fill(layer, dot, color, opa(cc.a), LV_RADIUS_CIRCLE);
  }
  if (c.propB("outline", false)) arc(layer, k.cx, k.cy, k.r, std::max(1, px(1.0f)), 0, 360, color, opa(cc.a));
}

void drawRadio(lv_layer_t *layer, const Control &c, const lv_area_t &a, int radius) {
  tosc::Color cc = c.color();
  lv_color_t color = col(cc);
  int n = std::max(1, c.propI("steps", 1));
  int sel = (int)std::lround(c.num("x", -1));
  int gap = std::max(1, px(2));
  for (int i = 0; i < n; i++) {
    lv_area_t r = radioCell(c, a, i);
    lv_area_increase(&r, -gap, -gap);
    if (c.propB("background", true)) fill(layer, r, color, opa(cc.a * 0.25f), radius);
    if (i == sel) fill(layer, r, color, opa(cc.a), radius);
  }
  outline(layer, c, a, color, opa(cc.a), radius);
}

void drawPager(lv_layer_t *layer, const Control &c, const lv_area_t &a) {
  tosc::Color cc = c.color();
  lv_color_t color = col(cc);
  lv_area_t bar = tabbarArea(c, a);
  int n = (int)c.children.size();
  if (n > 0 && bar.y2 >= bar.y1) {
    int page = (int)std::lround(c.num("page"));
    int W = lv_area_get_width(&bar);
    for (int i = 0; i < n; i++) {
      const Control &pg = *c.children[i];
      bool on = i == page;
      lv_area_t t = bar;
      t.x1 = bar.x1 + W * i / n;
      t.x2 = bar.x1 + W * (i + 1) / n - 1;
      lv_area_increase(&t, -1, -1);
      tosc::Color bc = pg.color(on ? "tabColorOn" : "tabColorOff");
      fill(layer, t, col(bc), opa(bc.a), 0);
      if (!c.propB("tabLabels", true)) continue;
      std::string label = pg.prop("tabLabel");
      if (label.empty()) label = std::to_string(i + 1);
      tosc::Color tc = pg.colors.count(on ? "textColorOn" : "textColorOff")
                           ? pg.color(on ? "textColorOn" : "textColorOff")
                           : tosc::Color{1, 1, 1, 1};
      text(layer, t, label.c_str(), fontFor(c.propI(on ? "textSizeOn" : "textSizeOff", 14)), col(tc),
           opa(tc.a), LV_TEXT_ALIGN_CENTER);
    }
  }
  outline(layer, c, a, color, opa(cc.a), 0);
}

// Pager: nur die aktive Seite anzeigen
void showPage(Control &c) {
  int page = (int)std::lround(c.num("page"));
  for (int i = 0; i < (int)c.children.size(); i++) {
    Control &pg = *c.children[i];
    if (!pg.ui) continue;
    bool vis = i == page && pg.propB("visible", true);
    if (vis) lv_obj_remove_flag((lv_obj_t *)pg.ui, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag((lv_obj_t *)pg.ui, LV_OBJ_FLAG_HIDDEN);
  }
}

// ---------------------------------------------------------------------------
// Zeichnen
// ---------------------------------------------------------------------------
void drawControl(lv_event_t *e) {
  auto *c = (Control *)lv_event_get_user_data(e);
  lv_obj_t *obj = lv_event_get_current_target_obj(e);
  lv_layer_t *layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(obj, &a);

  tosc::Color cc = c->color();
  lv_color_t color = col(cc);
  int radius = radiusFor(*c, a);
  bool bg = c->propB("background", true);

  switch (c->type) {
    case Type::GROUP:
    case Type::BOX:
      if (bg) fill(layer, a, color, opa(cc.a), radius);
      outline(layer, *c, a, color, opa(cc.a), radius);
      break;

    case Type::BUTTON: {
      bool on = c->num("x") > 0.5f;
      if (bg) fill(layer, a, color, opa(cc.a * 0.25f), radius);
      if (on) fill(layer, a, color, opa(cc.a), radius);
      outline(layer, *c, a, color, opa(cc.a), radius);
      break;
    }

    case Type::FADER: {
      float v = std::clamp(c->num("x"), 0.0f, 1.0f);
      int dir = faderDir(*c);
      bool vertical = dir == 0 || dir == 2;
      if (bg) fill(layer, a, color, opa(cc.a * 0.25f), radius);

      // Raster
      if (c->propB("grid", false)) {
        int steps = c->propI("gridSteps", 10);
        tosc::Color gc = c->color("gridColor");
        if (steps > 1 && steps < 200) {
          for (int i = 1; i < steps; i++) {
            float f = (float)i / steps;
            if (vertical) {
              int y = a.y1 + (int)std::lround(f * lv_area_get_height(&a));
              line(layer, a.x1, y, a.x2, y, col(gc), opa(gc.a), 1);
            } else {
              int x = a.x1 + (int)std::lround(f * lv_area_get_width(&a));
              line(layer, x, a.y1, x, a.y2, col(gc), opa(gc.a), 1);
            }
          }
        }
      }

      // Balken
      bool centered = c->propB("centered", false);
      float from = centered ? 0.5f : 0.0f;
      float lo = std::min(from, v), hi = std::max(from, v);
      lv_area_t bar = a;
      int W = lv_area_get_width(&a), H = lv_area_get_height(&a);
      switch (dir) {
        case 0: bar.y1 = a.y2 - (int)(hi * H); bar.y2 = a.y2 - (int)(lo * H); break;
        case 2: bar.y1 = a.y1 + (int)(lo * H); bar.y2 = a.y1 + (int)(hi * H); break;
        case 1: bar.x1 = a.x1 + (int)(lo * W); bar.x2 = a.x1 + (int)(hi * W); break;
        case 3: bar.x1 = a.x2 - (int)(hi * W); bar.x2 = a.x2 - (int)(lo * W); break;
      }
      if (c->propB("bar", true) && lv_area_get_size(&bar) > 0)
        fill(layer, bar, color, opa(cc.a), std::min(radius, 1000));

      // Cursor (Linie an der aktuellen Position)
      if (c->propB("cursor", true)) {
        int w = std::max(2, px(3));
        lv_color_t cur = lv_color_mix(lv_color_white(), color, 110);
        switch (dir) {
          case 0: { int y = a.y2 - (int)(v * H); line(layer, a.x1, y, a.x2, y, cur, LV_OPA_COVER, w); break; }
          case 2: { int y = a.y1 + (int)(v * H); line(layer, a.x1, y, a.x2, y, cur, LV_OPA_COVER, w); break; }
          case 1: { int x = a.x1 + (int)(v * W); line(layer, x, a.y1, x, a.y2, cur, LV_OPA_COVER, w); break; }
          case 3: { int x = a.x2 - (int)(v * W); line(layer, x, a.y1, x, a.y2, cur, LV_OPA_COVER, w); break; }
        }
      }
      outline(layer, *c, a, color, opa(cc.a), radius);
      break;
    }

    case Type::GRID:
      // Raster: die Kind-Elemente zeichnen sich selbst, hier nur der Rahmen
      outline(layer, *c, a, color, opa(cc.a), radius);
      break;

    case Type::RADIAL:
    case Type::ENCODER:
      drawRing(layer, *c, a);
      break;

    case Type::XY:
      drawXY(layer, *c, a, radius);
      break;

    case Type::RADAR:
      drawRadar(layer, *c, a);
      break;

    case Type::RADIO:
      drawRadio(layer, *c, a, radius);
      break;

    case Type::PAGER:
      drawPager(layer, *c, a);
      break;

    case Type::LABEL:
    case Type::TEXT: {
      if (bg) fill(layer, a, color, opa(cc.a), radius);
      outline(layer, *c, a, color, opa(cc.a), radius);
      const tosc::Value *v = c->value("text");
      std::string s = v ? (v->isString ? v->str : std::to_string(v->num)) : c->prop("text");
      tosc::Color tc = c->colors.count("textColor") ? c->color("textColor")
                                                     : tosc::Color{1, 1, 1, 1};
      int alignH = c->propI("textAlignH", 2);
      lv_text_align_t al = alignH == 1 ? LV_TEXT_ALIGN_LEFT
                           : alignH == 3 ? LV_TEXT_ALIGN_RIGHT
                                         : LV_TEXT_ALIGN_CENTER;
      text(layer, a, s.c_str(), fontFor(c->propI("textSize", 14)), col(tc), opa(tc.a), al,
           c->propI("textAlignV", 2), c->type == Type::TEXT && c->propB("textWrap", false));
      break;
    }

    case Type::OTHER: {
      // Noch nicht unterstützter Typ: gestrichelter Rahmen mit Typname
      lv_draw_rect_dsc_t d;
      lv_draw_rect_dsc_init(&d);
      d.bg_opa = LV_OPA_TRANSP;
      d.border_color = lv_color_hex(0x606870);
      d.border_width = 1;
      d.border_opa = LV_OPA_COVER;
      lv_draw_rect(layer, &d, &a);
      text(layer, a, c->typeName.c_str(), &lv_font_montserrat_14, lv_color_hex(0x8890A0),
           LV_OPA_COVER, LV_TEXT_ALIGN_CENTER);
      break;
    }
  }
}

// ---------------------------------------------------------------------------
// Touch
// ---------------------------------------------------------------------------
float faderValueAt(const Control &c, const lv_area_t &a, const lv_point_t &p) {
  float W = (float)std::max<int32_t>(1, lv_area_get_width(&a) - 1);
  float H = (float)std::max<int32_t>(1, lv_area_get_height(&a) - 1);
  switch (faderDir(c)) {
    case 0: return (a.y2 - p.y) / H;
    case 2: return (p.y - a.y1) / H;
    case 1: return (p.x - a.x1) / W;
    default: return (a.x2 - p.x) / W;
  }
}

// Winkel des Fingers um die Mitte (LVGL-Grad 0..360) und Abstand
float touchAngle(const Circle &k, const lv_point_t &p, float *dist = nullptr) {
  float dx = (float)(p.x - k.cx), dy = (float)(p.y - k.cy);
  if (dist) *dist = std::sqrt(dx * dx + dy * dy);
  float deg = std::atan2(dy, dx) * 180.0f / (float)M_PI;
  return deg < 0 ? deg + 360.0f : deg;
}

bool inArea(const lv_area_t &a, const lv_point_t &p) {
  return p.x >= a.x1 && p.x <= a.x2 && p.y >= a.y1 && p.y <= a.y2;
}

float wrapDelta(float d) {
  while (d > 180) d -= 360;
  while (d <= -180) d += 360;
  return d;
}

// Radial/Encoder: Drehen um die Mitte
void touchRing(Control &c, const lv_area_t &a, const lv_point_t &p, bool down) {
  Circle k = circleIn(a);
  float dist;
  float ang = touchAngle(k, p, &dist);
  float old = c.num("x");
  bool radial = c.type == Type::RADIAL;
  bool inverted = radial && c.propB("inverted", false);
  float factor = c.propF("responseFactor", 100) / 100.0f;

  if (radial && c.propI("response", 0) == 0) {  // absolut: Wert folgt dem Finger
    if (dist < k.r * 0.15f) return;           // zu nah an der Mitte: Winkel unbestimmt
    float rel = std::fmod(ang - RADIAL_START + 720.0f, 360.0f);
    float v = rel <= RADIAL_SWEEP ? rel / RADIAL_SWEEP : (rel > RADIAL_SWEEP + (360 - RADIAL_SWEEP) / 2 ? 0 : 1);
    if (inverted) v = 1 - v;
    if (!down && std::fabs(v - old) > 0.5f) v = old > 0.5f ? 1 : 0;  // nicht über die Lücke springen
    g_engine->setNum(c, "x", v);
    return;
  }
  // relativ (Encoder immer): Winkeländerung seit dem letzten Ereignis
  if (down) {
    g_last_angle = ang;
    return;
  }
  if (dist < k.r * 0.15f) return;
  float d = wrapDelta(ang - g_last_angle);
  g_last_angle = ang;
  float sweep = radial ? RADIAL_SWEEP : 360.0f;
  float v = old + (inverted ? -d : d) / sweep * factor;
  if (!radial) {
    v = std::fmod(v, 1.0f);  // Encoder dreht endlos
    if (v < 0) v += 1.0f;
  }
  g_engine->setNum(c, "x", v);
}

void touchXY(Control &c, const lv_area_t &a, const lv_point_t &p, bool down) {
  int S = xyHandle(a);
  float W = (float)std::max<int32_t>(1, lv_area_get_width(&a) - S);
  float H = (float)std::max<int32_t>(1, lv_area_get_height(&a) - S);
  float x, y;
  if (down) {
    g_touch_start = p;
    g_value_start = c.num("x");
    g_value_start_y = c.num("y");
  }
  if (c.propI("response", 0) == 1) {
    float f = c.propF("responseFactor", 100) / 100.0f;
    x = g_value_start + (p.x - g_touch_start.x) / W * f;
    y = g_value_start_y - (p.y - g_touch_start.y) / H * f;
  } else {
    x = (p.x - a.x1 - S / 2) / W;
    y = (a.y2 - S / 2 - p.y) / H;
  }
  if (!c.propB("lockX", false)) g_engine->setNum(c, "x", x);
  if (!c.propB("lockY", false)) g_engine->setNum(c, "y", y);
}

void touchRadar(Control &c, const lv_area_t &a, const lv_point_t &p) {
  Circle k = circleIn(a);
  float dist;
  float ang = touchAngle(k, p, &dist);
  if (!c.propB("lockX", false) && dist >= k.r * 0.05f)
    g_engine->setNum(c, "x", std::fmod(360.0f - ang, 360.0f) / 360.0f);
  if (!c.propB("lockY", false)) g_engine->setNum(c, "y", dist / k.r);
}

void touchRadio(Control &c, const lv_area_t &a, const lv_point_t &p) {
  int n = std::max(1, c.propI("steps", 1));
  for (int i = 0; i < n; i++) {
    lv_area_t r = radioCell(c, a, i);
    if (inArea(r, p)) {
      g_engine->setNum(c, "x", (float)i);
      return;
    }
  }
}

void touchPager(Control &c, const lv_area_t &a, const lv_point_t &p) {
  lv_area_t bar = tabbarArea(c, a);
  int n = (int)c.children.size();
  if (n == 0 || !inArea(bar, p)) return;
  int i = std::clamp((int)((p.x - bar.x1) * n / std::max<int32_t>(1, lv_area_get_width(&bar))), 0, n - 1);
  g_engine->setNum(c, "page", (float)i);
  showPage(c);
}

void touchControl(lv_event_t *e) {
  auto *c = (Control *)lv_event_get_user_data(e);
  lv_obj_t *obj = lv_event_get_current_target_obj(e);
  lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t *indev = lv_indev_active();
  if (!indev || !g_engine) return;
  lv_point_t p;
  lv_indev_get_point(indev, &p);
  lv_area_t a;
  lv_obj_get_coords(obj, &a);

  bool down = code == LV_EVENT_PRESSED;
  bool up = code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST;
  bool move = code == LV_EVENT_PRESSING;
  float oldX = c->num("x"), oldY = c->num("y"), oldPage = c->num("page");

  if (down) g_engine->setNum(*c, "touch", 1);

  switch (c->type) {
    case Type::BUTTON: {
      int bt = c->propI("buttonType", 0);
      bool onPress = c->propB("press", true), onRelease = c->propB("release", true);
      if (bt == 0) {  // Taster
        if (down && onPress) g_engine->setNum(*c, "x", 1);
        if (up && onRelease) g_engine->setNum(*c, "x", 0);
      } else if (bt == 1) {  // Schalter, beim Loslassen
        if (up && code == LV_EVENT_RELEASED) g_engine->setNum(*c, "x", oldX > 0.5f ? 0 : 1);
      } else {  // Schalter, beim Drücken
        if (down) g_engine->setNum(*c, "x", oldX > 0.5f ? 0 : 1);
      }
      break;
    }
    case Type::FADER: {
      bool relative = c->propI("response", 0) == 1;
      if (down) {
        g_touch_start = p;
        g_value_start = oldX;
      }
      if (down || move) {
        float v;
        if (relative) {
          lv_point_t s = g_touch_start;
          float factor = c->propF("responseFactor", 100) / 100.0f;
          v = g_value_start + (faderValueAt(*c, a, p) - faderValueAt(*c, a, s)) * factor;
        } else {
          v = faderValueAt(*c, a, p);
        }
        g_engine->setNum(*c, "x", std::clamp(v, 0.0f, 1.0f));
      }
      break;
    }
    case Type::RADIAL:
    case Type::ENCODER:
      if (down || move) touchRing(*c, a, p, down);
      break;
    case Type::XY:
      if (down || move) touchXY(*c, a, p, down);
      break;
    case Type::RADAR:
      if (down || move) touchRadar(*c, a, p);
      break;
    case Type::RADIO:
      if (down || move) touchRadio(*c, a, p);
      break;
    case Type::PAGER:
      if (down) touchPager(*c, a, p);
      break;
    default:
      break;
  }

  if (up) g_engine->setNum(*c, "touch", 0);
  if (c->num("x") != oldX || c->num("y") != oldY || c->num("page") != oldPage) lv_obj_invalidate(obj);
}

// ---------------------------------------------------------------------------
// Aufbau
// ---------------------------------------------------------------------------
void createObj(Control &c, lv_obj_t *parent, bool isRoot, int ox, int oy) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  int x = isRoot ? ox : px(c.frame.x), y = isRoot ? oy : px(c.frame.y);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, std::max(1, px(c.frame.w)), std::max(1, px(c.frame.h)));
  if (!c.propB("visible", true)) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  c.ui = o;

  lv_obj_add_event_cb(o, drawControl, LV_EVENT_DRAW_MAIN, &c);

  bool interactive = c.propB("interactive", true) &&
                     (c.type == Type::BUTTON || c.type == Type::FADER || c.type == Type::XY ||
                      c.type == Type::RADIAL || c.type == Type::ENCODER || c.type == Type::RADAR ||
                      c.type == Type::RADIO || c.type == Type::PAGER);
  if (interactive) {
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o, touchControl, LV_EVENT_PRESSED, &c);
    lv_obj_add_event_cb(o, touchControl, LV_EVENT_PRESSING, &c);
    lv_obj_add_event_cb(o, touchControl, LV_EVENT_RELEASED, &c);
    lv_obj_add_event_cb(o, touchControl, LV_EVENT_PRESS_LOST, &c);
  }

  for (auto &ch : c.children) createObj(*ch, o, false, 0, 0);
  if (c.type == Type::PAGER) showPage(c);
}

}  // namespace

void build(tosc::Engine &engine, int screenW, int screenH) {
  g_engine = &engine;
  Control *root = engine.root();
  if (!root) return;

  float lw = root->frame.w > 0 ? root->frame.w : (float)screenW;
  float lh = root->frame.h > 0 ? root->frame.h : (float)screenH;
  g_scale = std::min(screenW / lw, screenH / lh);
  int ox = (screenW - px(lw)) / 2, oy = (screenH - px(lh)) / 2;

  lv_obj_t *scr = lv_screen_active();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  createObj(*root, scr, true, ox, oy);
}

void refresh(tosc::Control &c) {
  if (!c.ui) return;
  if (c.type == Type::PAGER) showPage(c);
  lv_obj_invalidate((lv_obj_t *)c.ui);
}

void status(const char *msg, uint32_t ms) {
  if (!g_status) {
    g_status = lv_label_create(lv_layer_top());
    lv_obj_set_style_bg_color(g_status, lv_color_hex(0x202830), 0);
    lv_obj_set_style_bg_opa(g_status, LV_OPA_90, 0);
    lv_obj_set_style_text_color(g_status, lv_color_hex(0xE0E6EC), 0);
    lv_obj_set_style_text_font(g_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_hor(g_status, 10, 0);
    lv_obj_set_style_pad_ver(g_status, 6, 0);
    lv_obj_set_style_radius(g_status, 6, 0);
    lv_obj_set_style_text_align(g_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(g_status, LV_SIZE_CONTENT);
    lv_obj_align(g_status, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_remove_flag(g_status, LV_OBJ_FLAG_CLICKABLE);
  }
  lv_label_set_text(g_status, msg);
  lv_obj_remove_flag(g_status, LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(g_status, LV_ALIGN_BOTTOM_MID, 0, -8);

  if (g_status_timer) {
    lv_timer_delete(g_status_timer);
    g_status_timer = nullptr;
  }
  if (ms > 0) {
    g_status_timer = lv_timer_create(
        [](lv_timer_t *) {
          lv_obj_add_flag(g_status, LV_OBJ_FLAG_HIDDEN);
          lv_timer_delete(g_status_timer);
          g_status_timer = nullptr;
        },
        ms, nullptr);
  }
}

}  // namespace ui_tosc
