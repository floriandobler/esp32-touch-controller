// Minimale LVGL-9-Konfiguration – alles nicht Gesetzte nutzt LVGL-Defaults
// (Farbformat ist standardmäßig RGB565 = passend zum Display).
#ifndef LV_CONF_H
#define LV_CONF_H


// Speicher über malloc (der ESP32-Core legt große Blöcke automatisch ins PSRAM)
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_DEF_REFR_PERIOD 16   // ~60 fps Ziel

// Schriften
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_20

#define LV_USE_LOG 0

#endif
