#include "web.h"
#include <WebServer.h>
#include <LittleFS.h>
#include <WiFi.h>
#include "app.h"
#include "config.h"
#include "layouts.h"
#include "net_osc.h"
#include "web_page.h"

namespace web {

namespace {

WebServer server(80);

// ---------------------------------------------------------------------------
// Monitor-Puffer
// ---------------------------------------------------------------------------
constexpr size_t LOG_SIZE = 200;
struct LogLine {
  uint32_t seq;
  uint32_t ms;
  char dir;
  String text;
};
LogLine logBuf[LOG_SIZE];
uint32_t logSeq = 0;  // Nummer der nächsten Zeile

// ---------------------------------------------------------------------------
// JSON-Hilfen
// ---------------------------------------------------------------------------
String jsonStr(const String &s) {
  String o = "\"";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if ((unsigned char)c < 0x20) {
          char b[8];
          snprintf(b, sizeof b, "\\u%04x", c);
          o += b;
        } else {
          o += c;
        }
    }
  }
  return o + "\"";
}

void sendJson(int code, const String &body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

void sendOk() { sendJson(200, "{\"ok\":true}"); }

void sendErr(int code, const String &msg) {
  sendJson(code, "{\"ok\":false,\"error\":" + jsonStr(msg) + "}");
}

// ---------------------------------------------------------------------------
// Handler
// ---------------------------------------------------------------------------
void handleIndex() {
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html; charset=utf-8", WEB_PAGE, sizeof(WEB_PAGE) - 1);
}

void handleStatus() {
  const Settings &s = app::settings();
  String j = "{";
  j += "\"connected\":" + String(net_osc::connected() ? "true" : "false");
  j += ",\"ap\":" + String(net_osc::apActive() ? "true" : "false");
  j += ",\"apSsid\":" + jsonStr(SETUP_AP_SSID);
  j += ",\"ip\":" + jsonStr(net_osc::ip());
  j += ",\"hostname\":" + jsonStr(PANEL_HOSTNAME);
  j += ",\"ssid\":" + jsonStr(s.ssid);
  j += ",\"hasPassword\":" + String(s.password.isEmpty() ? "false" : "true");
  j += ",\"rssi\":" + String(net_osc::rssi());
  j += ",\"host\":" + jsonStr(s.targetHost);
  j += ",\"tport\":" + String(s.targetPort);
  j += ",\"lport\":" + String(s.listenPort);
  j += ",\"resolved\":" + String(net_osc::targetResolved() ? "true" : "false");
  j += ",\"sent\":" + String(net_osc::sentCount());
  j += ",\"recv\":" + String(net_osc::receivedCount());
  j += ",\"layout\":" + jsonStr(app::activeLayout());
  j += ",\"controls\":" + String((unsigned)app::controlCount());
  j += ",\"layouts\":[";
  bool first = true;
  for (auto &l : layouts::list()) {
    if (!first) j += ',';
    first = false;
    j += "{\"name\":" + jsonStr(l.name) + ",\"size\":" + String((unsigned)l.size) + "}";
  }
  j += "]";
  j += ",\"fsFree\":" + String((unsigned)layouts::freeBytes());
  j += ",\"heap\":" + String(ESP.getFreeHeap());
  j += ",\"psram\":" + String(ESP.getFreePsram());
  j += ",\"uptime\":" + String(millis() / 1000);
  j += "}";
  sendJson(200, j);
}

bool parsePort(const String &v, uint16_t &out) {
  long p = v.toInt();
  if (p < 1 || p > 65535) return false;
  out = (uint16_t)p;
  return true;
}

void handleSettings() {
  Settings s = app::settings();
  if (server.hasArg("host")) {
    String h = server.arg("host");
    h.trim();
    if (h.isEmpty() || h.length() > 63) return sendErr(400, "Ziel-Adresse fehlt oder ist zu lang");
    s.targetHost = h;
  }
  if (server.hasArg("tport") && !parsePort(server.arg("tport"), s.targetPort))
    return sendErr(400, "Ziel-Port muss zwischen 1 und 65535 liegen");
  if (server.hasArg("lport") && !parsePort(server.arg("lport"), s.listenPort))
    return sendErr(400, "Empfangs-Port muss zwischen 1 und 65535 liegen");
  if (server.hasArg("ssid")) {
    String ssid = server.arg("ssid");
    if (ssid.length() > 32) return sendErr(400, "WLAN-Name ist zu lang (max. 32 Zeichen)");
    String pass = server.arg("password");
    if (!pass.isEmpty() && (pass.length() < 8 || pass.length() > 63))
      return sendErr(400, "WLAN-Passwort muss 8 bis 63 Zeichen haben");
    if (!pass.isEmpty()) s.password = pass;
    else if (ssid != s.ssid) s.password = "";  // anderes Netz ohne Passwort = offenes WLAN
    s.ssid = ssid;
  }
  app::updateSettings(s);
  sendOk();
}

// Upload: Datei landet erst in einer Zwischendatei und wird nach der Prüfung umbenannt.
const char *TMP_UPLOAD = LAYOUT_DIR "/.upload.tmp";
File uploadFile;
String uploadError;
size_t uploadSize = 0;

void handleUploadData() {
  HTTPUpload &up = server.upload();
  switch (up.status) {
    case UPLOAD_FILE_START:
      uploadError = "";
      uploadSize = 0;
      if (LittleFS.exists(TMP_UPLOAD)) LittleFS.remove(TMP_UPLOAD);
      uploadFile = LittleFS.open(TMP_UPLOAD, "w");
      if (!uploadFile) uploadError = "Datei konnte nicht angelegt werden";
      break;
    case UPLOAD_FILE_WRITE:
      if (!uploadFile) break;
      uploadSize += up.currentSize;
      if (uploadSize > LAYOUT_MAX_BYTES) {
        uploadError = "Datei zu groß (max. 2 MB)";
        uploadFile.close();
      } else if (uploadFile.write(up.buf, up.currentSize) != up.currentSize) {
        uploadError = "Speicher voll";
        uploadFile.close();
      }
      break;
    case UPLOAD_FILE_END:
      if (uploadFile) uploadFile.close();
      break;
    case UPLOAD_FILE_ABORTED:
      if (uploadFile) uploadFile.close();
      uploadError = "Upload abgebrochen";
      break;
  }
}

void handleUploadDone() {
  auto fail = [](int code, const String &msg) {
    if (LittleFS.exists(TMP_UPLOAD)) LittleFS.remove(TMP_UPLOAD);
    web::log('!', "Upload abgelehnt: " + msg);
    sendErr(code, msg);
  };
  if (!server.hasUpload()) return fail(400, "Keine Datei empfangen");
  if (!uploadError.isEmpty()) return fail(400, uploadError);
  String name = layouts::sanitize(server.upload().filename);
  if (name.isEmpty()) return fail(400, "Ungültiger Dateiname");

  // Prüfen, ob die Datei ein lesbares TouchOSC-Layout ist
  String err;
  auto check = layouts::load(TMP_UPLOAD, err);
  if (!check) return fail(400, "Kein gültiges TouchOSC-Layout: " + err);
  size_t count = check->controlCount();
  check.reset();

  bool replacing = layouts::exists(name);
  if (replacing) LittleFS.remove(layouts::path(name));
  if (!LittleFS.rename(TMP_UPLOAD, layouts::path(name))) return fail(500, "Speichern fehlgeschlagen");
  web::log('!', "Layout gespeichert: " + name + " (" + String((unsigned)count) + " Elemente)");

  bool activate = server.arg("activate") == "1" || (replacing && name == app::activeLayout());
  if (activate && !app::activateLayout(name, err)) return sendErr(500, err);
  sendJson(200, "{\"ok\":true,\"name\":" + jsonStr(name) + ",\"controls\":" + String((unsigned)count) +
                    ",\"active\":" + (activate ? "true" : "false") + "}");
}

void handleActivate() {
  String name = server.arg("name");
  if (!layouts::exists(name)) return sendErr(404, "Layout nicht gefunden");
  String err;
  if (!app::activateLayout(name, err)) return sendErr(400, err);
  sendOk();
}

void handleDelete() {
  String name = server.arg("name");
  if (!layouts::exists(name)) return sendErr(404, "Layout nicht gefunden");
  if (name == app::activeLayout()) return sendErr(400, "Das aktive Layout kann nicht gelöscht werden");
  if (!layouts::remove(name)) return sendErr(500, "Löschen fehlgeschlagen");
  web::log('!', "Layout gelöscht: " + name);
  sendOk();
}

void handleDownload() {
  String name = server.arg("name");
  if (!layouts::exists(name)) return sendErr(404, "Layout nicht gefunden");
  File f = LittleFS.open(layouts::path(name), "r");
  String safe = name;
  safe.replace("\"", "");
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + safe + "\"");
  server.streamFile(f, "application/octet-stream");
  f.close();
}

void handleLog() {
  uint32_t since = server.arg("since").toInt();
  uint32_t oldest = logSeq > LOG_SIZE ? logSeq - LOG_SIZE : 0;
  if (since < oldest) since = oldest;
  String j = "{\"next\":" + String(logSeq) + ",\"now\":" + String(millis()) + ",\"lines\":[";
  bool first = true;
  for (uint32_t s = since; s < logSeq; s++) {
    const LogLine &l = logBuf[s % LOG_SIZE];
    if (!first) j += ',';
    first = false;
    j += "[" + String(l.ms) + ",\"" + String(l.dir) + "\"," + jsonStr(l.text) + "]";
  }
  j += "]}";
  sendJson(200, j);
}

void handleRestart() {
  sendOk();
  app::restart();
}

}  // namespace

void log(char dir, const String &text) {
  LogLine &l = logBuf[logSeq % LOG_SIZE];
  l.seq = logSeq++;
  l.ms = millis();
  l.dir = dir;
  l.text = text;
  l.text.replace("\n", "  · ");
}

void begin() {
  server.on("/", HTTP_GET, handleIndex);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/layouts", HTTP_POST, handleUploadDone, handleUploadData);
  server.on("/api/layouts/activate", HTTP_POST, handleActivate);
  server.on("/api/layouts/delete", HTTP_POST, handleDelete);
  server.on("/api/layouts/download", HTTP_GET, handleDownload);
  server.on("/api/log", HTTP_GET, handleLog);
  server.on("/api/restart", HTTP_POST, handleRestart);
  server.onNotFound([]() {
    // Einrichtungs-WLAN: alle Seiten auf die Startseite umleiten
    if (net_osc::apActive()) {
      server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
      server.send(302, "text/plain", "");
    } else {
      server.send(404, "text/plain", "Nicht gefunden");
    }
  });
  server.begin();
}

void loop() { server.handleClient(); }

}  // namespace web
