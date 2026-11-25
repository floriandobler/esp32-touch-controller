// Darstellung eines TouchOSC-Layouts mit LVGL und Touch-Bedienung.
#pragma once
#include <lvgl.h>
#include "tosc.h"

namespace ui_tosc {

// Baut die Oberfläche für das geladene Layout auf dem aktiven Bildschirm auf.
void build(tosc::Engine &engine, int screenW, int screenH);

// Element neu zeichnen (z. B. nach eingehendem OSC).
void refresh(tosc::Control &c);

// Statuszeile am unteren Rand einblenden; ms = 0 bleibt stehen.
void status(const char *text, uint32_t ms = 4000);

}  // namespace ui_tosc
