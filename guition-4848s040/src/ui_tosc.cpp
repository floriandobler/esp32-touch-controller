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
float g_value_start = 0;

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

const lv_font_t *fontFor(int size) {
  int s = (int)std::lround(size * g_scale);
  if (s <= 16) return &lv_font_montserrat_14;
  if (s <= 24) return &lv_font_montserrat_20;
  if (s <= 36) return &lv_font_montserrat_28;
  return &lv_font_montserrat_48;
}

void text(lv_layer_t *layer, const lv_area_t &a, const char *s, const lv_font_t *font,
          lv_color_t color, lv_opa_t o, lv_text_align_t align) {
  lv_draw_label_dsc_t d;
  lv_draw_label_dsc_init(&d);
  d.text = s;
  d.font = font;
  d.color = color;
  d.opa = o;
  d.align = align;
  lv_area_t ta = a;
  int pad = 4;
  ta.x1 += pad;
  ta.x2 -= pad;
  int h = lv_font_get_line_height(font);
  int free = lv_area_get_height(&a) - h;
  if (free > 0) {
    ta.y1 += free / 2;
    ta.y2 = ta.y1 + h;
  }
  lv_draw_label(layer, &d, &ta);
}

// Ausrichtung des Faders: 0 = nach oben, 1 = nach rechts, 2 = nach unten, 3 = nach links
int faderDir(const Control &c) {
  int o = c.propI("orientation", 0) & 3;
  return o;
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
      text(layer, a, s.c_str(), fontFor(c->propI("textSize", 14)), col(tc), opa(tc.a), al);
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
  float oldX = c->num("x");

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
    default:
      break;
  }

  if (up) g_engine->setNum(*c, "touch", 0);
  if (c->num("x") != oldX) lv_obj_invalidate(obj);
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
                     (c.type == Type::BUTTON || c.type == Type::FADER);
  if (interactive) {
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o, touchControl, LV_EVENT_PRESSED, &c);
    lv_obj_add_event_cb(o, touchControl, LV_EVENT_PRESSING, &c);
    lv_obj_add_event_cb(o, touchControl, LV_EVENT_RELEASED, &c);
    lv_obj_add_event_cb(o, touchControl, LV_EVENT_PRESS_LOST, &c);
  }

  for (auto &ch : c.children) createObj(*ch, o, false, 0, 0);
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
  if (c.ui) lv_obj_invalidate((lv_obj_t *)c.ui);
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
