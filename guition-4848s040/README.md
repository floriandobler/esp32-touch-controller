# Guition ESP32-S3-4848S040 – OSC-Player für TouchOSC-Layouts

4"-Touchpanel (480×480, ST7701S über RGB, GT911-Touch) mit Arduino-Core 3.x, Arduino_GFX und LVGL 9.
Das Panel zeigt ein TouchOSC-Layout (`.tosc`) an und sendet/empfängt OSC über WLAN.

## Loslegen

1. VS Code + Erweiterung **PlatformIO IDE** installieren und diesen Ordner öffnen.
2. Optional `include/secrets.h` ausfüllen (WLAN, Ziel-IP; Vorlage `include/secrets.example.h`).
   Das sind nur die Startwerte – alles lässt sich später im Browser ändern.
3. Panel per USB-C anschließen (`/dev/cu.usbserial-*`) und die Firmware hochladen (~2 min):

```bash
pio run -t upload
```

4. Im Browser **http://osc-panel.local** öffnen (oder die IP, die unten auf dem Display steht).

## Weboberfläche

- **Layouts:** `.tosc`-Datei in das Feld ziehen. Sie wird geprüft, gespeichert und sofort angezeigt –
  ohne Neustart. Mehrere Layouts können gespeichert sein, eins davon wird angezeigt.
  Gleicher Dateiname ersetzt das vorhandene Layout.
- **OSC:** Ziel-Adresse, Ziel-Port und Empfangs-Port – gelten sofort.
- **WLAN:** Netzwerk wechseln. Passwortfeld leer lassen = Passwort bleibt.
- **DMX (Art-Net):** Art-Net-Ausgang an/aus, Ziel-IP (leer = Broadcast ins eigene Netz), Universe.
- **Monitor:** Live-Anzeige aller gesendeten (→) und empfangenen (←) OSC-Nachrichten.

Einstellungen und das angezeigte Layout bleiben nach einem Neustart erhalten.

### Einrichtungs-WLAN

Kommt 20 s lang keine WLAN-Verbindung zustande (oder ist keins eingetragen), öffnet das Panel
das WLAN **OSC-Panel** (Passwort `oscpanel`). Damit verbinden, **http://192.168.4.1** öffnen und
das richtige WLAN eintragen. Das Panel versucht im Hintergrund weiter, sich zu verbinden, und
schließt das Einrichtungs-WLAN eine Minute nach erfolgreicher Verbindung.

### Layouts ohne Browser aufspielen

Dateien in `data/layouts/` legen und `pio run -t uploadfs` ausführen.
**Achtung:** Das ersetzt das ganze Dateisystem – im Browser hochgeladene Layouts sind danach weg.

## DMX über Art-Net

Der TouchOSC-Editor kann kein Art-Net – deshalb übernimmt das Panel OSC-Nachrichten mit
bestimmten Pfaden und schickt sie als Art-Net (UDP-Port 6454) statt als OSC:

| OSC-Nachricht | DMX |
|---|---|
| `/dmx/31` mit Float `0.0`–`1.0` | Kanal 31 = 0–255 (im eingestellten Universe) |
| `/dmx/31` mit Integer `0`–`255` | Kanal 31 = Wert direkt |
| `/dmx/2/31 …` | Universe 2, Kanal 31 |
| `/dmx/31` mit mehreren Werten | Kanal 31, 32, 33 … (z. B. XY-Pad → zwei Kanäle) |

Im TouchOSC-Editor also einfach als OSC-Adresse `/dmx/<kanal>` eintragen.

- Ein Universe wird erst gesendet, wenn es eine `/dmx`-Nachricht dafür gab; danach jede Sekunde
  wiederholt (Änderungen höchstens 40-mal pro Sekunde). Nach einem Neustart sendet das Panel nichts,
  bis wieder ein Regler bewegt wird.
- Auch `/dmx/…`-Nachrichten, die von außen per OSC ans Panel kommen, gehen als Art-Net raus.
- **Achtung:** Sendet schon ein anderes Pult oder Resolume auf demselben Universe, kämpfen beide
  um die Kanäle. Für das Panel ein eigenes Universe verwenden.
- Über WLAN ist die feste IP des Art-Net-Knotens zuverlässiger als Broadcast.

## Ports

| | Standard | Einstellung |
|---|---|---|
| Panel sendet an | Ziel:7000 | Resolume: *OSC Input Port* |
| Panel empfängt auf | 7001 | Resolume: *OSC Output Port* |
| Art-Net (DMX) | 6454 | fest |

## Was vom TouchOSC-Layout unterstützt wird

- **Elemente:** Group, Button (Taster, Schalter beim Drücken/Loslassen), Fader (alle vier
  Richtungen, absolut/relativ, Balken, Cursor, Raster, zentriert), Label, Text, Box,
  XY-Pad, Radar, Radial (Drehregler mit Anschlag), Encoder (endlos), Radio, Grid und Pager
  (Seiten mit Tab-Leiste). Andere Typen erscheinen als grauer Rahmen mit ihrem Namen.
- **OSC:** Pfade und Argumente aus Konstanten, Werten, Eigenschaften und Index; Umwandlung in
  Float/Integer/Boolean/String mit Skalierung; Trigger ANY/RISE/FALL; Senden/Empfangen,
  Feedback, „keine Duplikate“. Empfangene Werte bewegen die Elemente mit.
- **Nicht unterstützt:** Lua-Skripte, MIDI, mehrere OSC-Verbindungen (es gibt genau ein Ziel),
  Multitouch (der Touch-Treiber liest nur einen Finger).

## Aufbau

| Datei | Inhalt |
|---|---|
| `platformio.ini` | Board-Setup: 16 MB Flash, Octal-PSRAM, LittleFS, Bibliotheken |
| `include/config.h` | Ports, Hostname, Layout-Pfad (Standardwerte) |
| `include/secrets.h` | WLAN-Zugang und Ziel-IP (nicht weitergeben) |
| `include/lv_conf.h` | LVGL-Konfiguration (Schriften, Speicher) |
| `src/main.cpp` | Display-Init, LVGL-Anbindung, Layout wechseln, Ablauf |
| `src/settings.*` | Einstellungen dauerhaft speichern (NVS) |
| `src/layouts.*` | Layout-Dateien in `/layouts` verwalten, laden und prüfen |
| `src/web.*`, `src/web_page.h` | Webserver, API und Weboberfläche |
| `src/tosc.*` | TouchOSC-Modell: Eigenschaften, Werte, OSC-Nachrichten bauen/zuordnen |
| `src/ui_tosc.*` | Zeichnen der Elemente und Touch-Bedienung |
| `src/net_osc.*` | WLAN, Einrichtungs-WLAN, mDNS und OSC über UDP |
| `src/dmx.*` | `/dmx/…`-Pfade auswerten, Art-Net-Pakete bauen (am Rechner testbar) |
| `src/artnet.*` | Art-Net senden (Ziel, Universe, Takt) |
| `src/osc.*` | OSC-Kodierung (Nachrichten, Bundles) |
| `src/xml_mini.*` | Kleiner XML-Parser |
| `src/tosc_inflate*` | Entpacken der `.tosc`-Datei (zlib, ROM-Funktion des ESP32) |
| `src/touch_gt911.h` | Kleiner GT911-Treiber (I2C: SDA 19, SCL 45) |
| `tools/host_test/` | Testprogramm für den Rechner, nutzt denselben Code wie das Panel |

Layout am Rechner prüfen, ohne zu flashen:

```bash
./tools/host_test/run.sh "data/layouts/ESP32 Panel Test 02.tosc"
```

## Wenn etwas nicht passt

- **Upload bricht ab:** `upload_speed` in `platformio.ini` muss auf `115200` bleiben – schnellere
  Werte (z. B. 921600) brechen mit „Unable to verify flash chip connection“ ab.
- **„Noch kein Layout“ auf dem Display:** im Browser eine `.tosc`-Datei hochladen.
- **osc-panel.local geht nicht:** die IP-Adresse vom Display verwenden (manche Netze/Geräte
  unterstützen kein mDNS).
- **Nachrichten kommen nicht an:** Ziel-IP und Port prüfen, am Rechner die Firewall für das
  Empfangsprogramm erlauben. Im Monitor der Weboberfläche müssen die Nachrichten erscheinen.
- **Touch gespiegelt oder vertauscht:** in `touch_cb()` umrechnen, z. B. `x = 479 - x;`
  oder `std::swap(x, y);`.
- **Plattform-Version:** Absichtlich auf pioarduino `55.03.313` (Arduino-Core 3.3.13) festgelegt.
  Die neue Plattform mit Arduino-Core 4.0 bricht Arduino_GFX 1.6.x beim Kompilieren.
