// Guition ESP32-S3-4848S040 – OSC-Player für TouchOSC-Layouts
// Display (ST7701S/RGB) + Touch (GT911) + LVGL 9 + OSC über WLAN
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include "config.h"
#include "touch_gt911.h"
#include "tosc.h"
#include "ui_tosc.h"
#include "net_osc.h"
#include "settings.h"
#include "layouts.h"
#include "web.h"
#include "app.h"

// OSC-Verkehr im seriellen Monitor anzeigen (0 = aus)
#ifndef OSC_LOG
#define OSC_LOG 1
#endif

// ---------------------------------------------------------------------------
// Display: Pinbelegung wie in Arduino_GFX (Gerät ESP32_4848S040_86BOX_GUITION)
// ---------------------------------------------------------------------------
#define PIN_BACKLIGHT 38

// 3-Wire-SPI nur für die Init-Befehle des ST7701S
Arduino_DataBus *bus = new Arduino_SWSPI(
    GFX_NOT_DEFINED /* DC */, 39 /* CS */,
    48 /* SCK */, 47 /* MOSI */, GFX_NOT_DEFINED /* MISO */);

// Paralleles RGB-Interface für die eigentlichen Pixeldaten
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
    11 /* R0 */, 12 /* R1 */, 13 /* R2 */, 14 /* R3 */, 0 /* R4 */,
    8 /* G0 */, 20 /* G1 */, 3 /* G2 */, 46 /* G3 */, 9 /* G4 */, 10 /* G5 */,
    4 /* B0 */, 5 /* B1 */, 6 /* B2 */, 7 /* B3 */, 15 /* B4 */,
    1 /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
    1 /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */,
    0 /* pclk_active_neg */, 12000000 /* prefer_speed */, false /* useBigEndian */,
    0 /* de_idle_high */, 0 /* pclk_idle_high */,
    // Bounce-Buffer: Bilddaten laufen über 2 × 10 Zeilen im internen RAM statt direkt aus dem PSRAM.
    // Ohne ihn verliert das Display bei WLAN-Last Pixel und springt horizontal.
    480 * 10 /* bounce_buffer_size_px */);

// Rotation 0 = Touch-Koordinaten passen ohne Umrechnung
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    480 /* width */, 480 /* height */, rgbpanel, 0 /* rotation */, true /* auto_flush */,
    bus, GFX_NOT_DEFINED /* RST */, st7701_type9_init_operations, sizeof(st7701_type9_init_operations));

// ---------------------------------------------------------------------------
// LVGL-Anbindung
// ---------------------------------------------------------------------------
static const uint32_t SCREEN_W = 480;
static const uint32_t SCREEN_H = 480;
static const uint32_t BUF_LINES = 40;  // Teil-Puffer: 480 x 40 Pixel

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map,
                          lv_area_get_width(area), lv_area_get_height(area));
  lv_display_flush_ready(disp);
}

static void touch_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  static int16_t last_x = 0, last_y = 0;
  static bool pressed = false;

  int16_t x, y;
  int r = gt911::read(x, y);
  if (r == 1) {
    last_x = x;
    last_y = y;
    pressed = true;
  } else if (r == 0) {
    pressed = false;
  }
  // r == -1: keine neuen Daten → letzten Zustand beibehalten

  data->point.x = last_x;
  data->point.y = last_y;
  data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static uint32_t tick_cb() { return millis(); }

static void set_backlight(uint8_t percent) {
  analogWrite(PIN_BACKLIGHT, map(percent, 0, 100, 0, 255));
}

// ---------------------------------------------------------------------------
// Layout und Einstellungen
// ---------------------------------------------------------------------------
static std::unique_ptr<tosc::Engine> engine;
static Settings cfg;
static String activeName;
static uint32_t pendingApplyAt = 0;  // verzögertes Übernehmen (WLAN-Wechsel)
static uint32_t restartAt = 0;

static void show_message(const char *title, const String &detail) {
  Serial.printf("%s: %s\n", title, detail.c_str());
  lv_obj_t *scr = lv_screen_active();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
  lv_obj_t *l = lv_label_create(scr);
  lv_label_set_text_fmt(l, "%s\n\n%s", title, detail.c_str());
  lv_obj_set_width(l, 420);
  lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(0xE0E6EC), 0);
  lv_obj_center(l);
}

static void osc_out(const osc::Message &m) {
  String s = osc::toString(m).c_str();
  if (OSC_LOG) Serial.printf("OSC -> %s\n", s.c_str());
  web::log('>', s);
  net_osc::send(m);
}

namespace app {

const Settings &settings() { return cfg; }

void updateSettings(const Settings &s) {
  bool wifiChanged = s.ssid != cfg.ssid || s.password != cfg.password;
  cfg = s;
  settings::save(cfg);
  web::log('!', "Einstellungen gespeichert");
  if (wifiChanged) pendingApplyAt = millis() + 500;  // Antwort an den Browser noch abschicken
  else net_osc::apply(cfg);
}

bool activateLayout(const String &name, String &err) {
  auto e = layouts::load(layouts::path(name), err);
  if (!e) {
    web::log('!', "Layout " + name + " nicht lesbar: " + err);
    return false;
  }
  // Erst die alte Oberfläche löschen, dann das alte Modell freigeben (Objekte zeigen darauf)
  lv_obj_clean(lv_screen_active());
  engine = std::move(e);
  engine->onSend = osc_out;
  engine->onRemoteChange = [](tosc::Control &c) { ui_tosc::refresh(c); };
  ui_tosc::build(*engine, SCREEN_W, SCREEN_H);
  activeName = name;
  if (cfg.layout != name) {
    cfg.layout = name;
    settings::save(cfg);
  }
  web::log('!', "Layout angezeigt: " + name + " (" + String((unsigned)engine->controlCount()) +
                    " Elemente)");
  return true;
}

const String &activeLayout() { return activeName; }
size_t controlCount() { return engine ? engine->controlCount() : 0; }
void restart() { restartAt = millis() + 500; }

}  // namespace app

static void load_initial_layout() {
  if (!layouts::begin()) {
    show_message("Dateisystem fehlt", "Bitte neu flashen.");
    return;
  }
  auto list = layouts::list();
  if (list.empty()) {
    show_message("Noch kein Layout", "Im Browser die Adresse unten öffnen\nund eine .tosc-Datei hochladen.");
    return;
  }
  // Gespeichertes Layout, sonst das erste in der Liste
  String err;
  if (layouts::exists(cfg.layout) && app::activateLayout(cfg.layout, err)) return;
  for (auto &l : list)
    if (app::activateLayout(l.name, err)) return;
  show_message("Layout nicht lesbar", err);
}

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\nGuition 4848S040 OSC-Player – LVGL %d.%d.%d\n",
                lv_version_major(), lv_version_minor(), lv_version_patch());
  Serial.printf("PSRAM: %u Bytes frei\n", ESP.getFreePsram());

  if (!gfx->begin()) {
    Serial.println("gfx->begin() fehlgeschlagen!");
  }
  gfx->fillScreen(RGB565_BLACK);
  pinMode(PIN_BACKLIGHT, OUTPUT);
  set_backlight(100);

  gt911::begin();

  lv_init();
  lv_tick_set_cb(tick_cb);

  size_t buf_bytes = SCREEN_W * BUF_LINES * sizeof(uint16_t);
  void *buf = heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!buf) buf = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);

  lv_display_t *disp = lv_display_create(SCREEN_W, SCREEN_H);
  lv_display_set_flush_cb(disp, flush_cb);
  lv_display_set_buffers(disp, buf, NULL, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_cb);

  cfg = settings::load();
  load_initial_layout();

  net_osc::begin(cfg, [](const char *msg, bool sticky) {
    ui_tosc::status(msg, sticky ? 0 : 8000);
    web::log('!', String(msg));
  });
  web::begin();
  Serial.printf("Setup fertig. Freier Heap: %u, PSRAM: %u\n", ESP.getFreeHeap(),
                ESP.getFreePsram());
}

void loop() {
  net_osc::loop([](const osc::Message &m) {
    bool hit = engine && engine->handleIncoming(m);
    String s = osc::toString(m).c_str();
    if (!hit) s += "  (kein Element)";
    if (OSC_LOG) Serial.printf("OSC <- %s\n", s.c_str());
    web::log('<', s);
  });
  web::loop();
  lv_timer_handler();

  if (pendingApplyAt && (int32_t)(millis() - pendingApplyAt) >= 0) {
    pendingApplyAt = 0;
    net_osc::apply(cfg);
  }
  if (restartAt && (int32_t)(millis() - restartAt) >= 0) ESP.restart();
  delay(2);
}
