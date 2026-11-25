# Projekt: Guition ESP32-S3-4848S040 (PlatformIO)

## Hardware
- ESP32-S3, 16 MB Flash (QIO), 8 MB PSRAM (Octal)
- Display 4", 480×480, ST7701S: Init über 3-Wire-SPI (CS 39, SCK 48, MOSI 47), Pixel über RGB-Parallel
  (DE 18, VSYNC 17, HSYNC 16, PCLK 21; R 11,12,13,14,0; G 8,20,3,46,9,10; B 4,5,6,7,15)
- Backlight GPIO 38 (PWM, active high)
- Touch GT911 über I2C (SDA 19, SCL 45), kein INT/RST; Adresse 0x5D oder 0x14
- USB-C läuft über einen USB-Seriell-Chip (/dev/cu.usbserial-*), NICHT über natives USB.
  GPIO 19/20 sind für Touch/Display belegt.

## Bewährt (auf dem Panel getestet)
- Display, Touch und Helligkeitsregler funktionieren mit Rotation 0, Touch-Koordinaten ohne Umrechnung.
- Upload nur mit `upload_speed = 115200` stabil (921600 bricht ab: "Unable to verify flash chip connection").
- OSC-Player Stufe 1: Layout, Touch, WLAN und OSC in beide Richtungen getestet (84/84 Nachrichten
  Mac → Panel, Fader folgt). Panel bekommt per DHCP z. B. 10.10.1.101, Mac ist 10.10.1.120.
- Bild springt bei WLAN-Last horizontal (PSRAM-Bandbreite) → Bounce-Buffer 480×10 px im
  `Arduino_ESP32RGBPanel`-Konstruktor behebt es (getestet mit 100 OSC/s + Webabrufen).
  `CONFIG_LCD_RGB_RESTART_IN_VSYNC` ist im Framework an, daher nur kurzes Springen statt Dauerversatz.
- Tests vom Mac ins lokale Netz brauchen die macOS-Freigabe „Lokales Netzwerk“ für die Claude-App
  (sonst „No route to host“, obwohl das Gerät in `arp -an` auftaucht).

## Toolchain-Festlegungen – nicht ohne Grund ändern
- Plattform pioarduino `55.03.313` (Arduino-Core 3.3.13). Die Plattform mit Arduino-Core 4.0
  (61.x / "stable") bricht Arduino_GFX 1.6.x beim Kompilieren (int8_t → gpio_num_t).
- Arduino_GFX ~1.6.9 (Display: `st7701_type9_init_operations`), LVGL ~9.6.0 mit `include/lv_conf.h`
  (`LV_CONF_INCLUDE_SIMPLE`). `LV_COLOR_DEPTH` nicht setzen (in 9.6 veraltet, Standard ist RGB565).

## Ziel
OSC-Player: Panel zeigt TouchOSC-Layouts (.tosc, lexml v6) an und spricht OSC über WLAN/UDP.
Stufe 1 (fertig): Group/Button/Fader/Label/Text/Box, OSC senden+empfangen.
Stufe 2 (fertig): Weboberfläche (http://osc-panel.local): Layouts hochladen/wechseln/löschen (live,
ohne Neustart), OSC-Ziel/Ports, WLAN, Live-Monitor; Einstellungen in NVS; Einrichtungs-WLAN
"OSC-Panel"/"oscpanel" nach 20 s ohne Verbindung.
Stufe 3: weitere Typen (Radio, XY, Encoder, Pager …).

## Struktur (Projekt in `guition-4848s040/`)
- `src/main.cpp`: Display-Init, LVGL-Anbindung (Partial-Buffer 480×40), `app::`-Funktionen (app.h)
  für die Weboberfläche. Layout-Wechsel: erst `lv_obj_clean`, dann altes Engine freigeben
  (LVGL-Objekte zeigen auf die Controls).
- `src/layouts.*`: Layouts in LittleFS unter `/layouts/*.tosc`; Upload erst nach `.upload.tmp`,
  prüfen, dann umbenennen
- `src/web.*`, `src/web_page.h`: Arduino-`WebServer` (läuft in `loop()`, gleicher Thread wie LVGL)
- `src/settings.*`: NVS-Namespace `oscpanel`; Startwerte aus config.h/secrets.h
- `src/tosc.*`, `src/xml_mini.*`, `src/osc.*`: plattformunabhängig (kein Arduino/LVGL) –
  am Mac testbar mit `./tools/host_test/run.sh "data/layouts/Panel Test 01.tosc"` (ASan/UBSan)
- `src/tosc_inflate_esp.cpp`: entpackt .tosc mit ROM-`tinfl_decompress` (ROM-miniz hat kein malloc:
  `*_mem_to_heap` funktioniert nicht, Dekompressor-Zustand ~11 KB auf den Heap, nicht Stack)
- `src/ui_tosc.*`: zeichnet Elemente selbst per `LV_EVENT_DRAW_MAIN`, Touch über LVGL-Events
- `src/net_osc.*`: WLAN (Sleep aus), Einrichtungs-WLAN, UDP, mDNS `osc-panel.local` (+ `_osc._udp`)
- `include/config.h` (Standardwerte), `include/secrets.h` (WLAN, Ziel-IP; nicht weitergeben)
- `src/touch_gt911.h`: Eigener minimaler GT911-Treiber
- `include/lv_conf.h`: LVGL-Konfiguration (Schriften Montserrat 14/20/28/48)

## Hinweise
- LVGL 9.6: öffentliche Header liegen unter `lvgl/include/lvgl/…`, nicht `lvgl/src/…`.
- `int32_t` ist auf dem ESP32 `long` → bei `std::min/max` mit LVGL-Werten Typ explizit angeben.
- Die Shell-Funktion `grep` verhält sich anders – für Suchen `/usr/bin/grep` verwenden.
- Upload: Firmware ~2 min, Dateisystem ~15 s (bei 115200 Baud). `uploadfs` löscht im Browser
  hochgeladene Layouts.
- API zum Testen: `curl http://10.10.1.101/api/status`, Upload `curl -F file=@x.tosc
  "http://10.10.1.101/api/layouts?activate=1"`, Log `/api/log?since=0`.
- Auf dem Mac läuft Resolume Arena (meldet sich per mDNS als `_osc._udp`) – Port 7000 ist dann belegt.
