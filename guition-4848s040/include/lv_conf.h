// Minimale LVGL-9-Konfiguration – alles nicht Gesetzte nutzt LVGL-Defaults
// (Farbformat ist standardmäßig RGB565 = passend zum Display).
#ifndef LV_CONF_H
#define LV_CONF_H


// Speicher über malloc (der ESP32-Core legt große Blöcke automatisch ins PSRAM)
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_DEF_REFR_PERIOD 16   // ~60 fps Ziel

// Schriften: eigene Montserrat-Medium mit Latin-1 (Umlaute, ß) in src/fonts/ statt der
// eingebauten (nur ASCII). Erzeugt mit lv_font_conv, Befehl im Kopf der .c-Dateien.
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_CUSTOM_DECLARE                                                          \
  LV_FONT_DECLARE(font_latin1_14) LV_FONT_DECLARE(font_latin1_20)                     \
  LV_FONT_DECLARE(font_latin1_28) LV_FONT_DECLARE(font_latin1_48)
#define LV_USE_CUSTOM_FONT_DEFAULT 1
#define LV_FONT_DEFAULT &font_latin1_20

#define LV_USE_LOG 0

#endif
