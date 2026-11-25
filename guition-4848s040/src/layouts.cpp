#include "layouts.h"
#include <LittleFS.h>
#include <algorithm>
#include "config.h"
#include "tosc_inflate.h"

namespace layouts {

bool begin() {
  // Beim allerersten Start ohne Dateisystem formatieren
  if (!LittleFS.begin(true)) return false;
  if (!LittleFS.exists(LAYOUT_DIR)) LittleFS.mkdir(LAYOUT_DIR);
  // Stufe 1 legte das Layout als /layout.tosc ab
  if (LittleFS.exists("/layout.tosc") && !LittleFS.exists(path("layout.tosc")))
    LittleFS.rename("/layout.tosc", path("layout.tosc"));
  return true;
}

String path(const String &name) { return String(LAYOUT_DIR) + "/" + name; }

std::vector<Info> list() {
  std::vector<Info> out;
  File dir = LittleFS.open(LAYOUT_DIR);
  if (!dir || !dir.isDirectory()) return out;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (f.isDirectory()) continue;
    String n = f.name();
    if (n.endsWith(".tosc")) out.push_back({n, f.size()});
  }
  std::sort(out.begin(), out.end(), [](const Info &a, const Info &b) {
    return a.name.compareTo(b.name) < 0;
  });
  return out;
}

bool exists(const String &name) { return !name.isEmpty() && LittleFS.exists(path(name)); }

bool remove(const String &name) { return exists(name) && LittleFS.remove(path(name)); }

String sanitize(const String &name) {
  String base = name;
  int slash = std::max(base.lastIndexOf('/'), base.lastIndexOf('\\'));
  if (slash >= 0) base = base.substring(slash + 1);
  if (base.endsWith(".tosc")) base = base.substring(0, base.length() - 5);
  String out;
  for (size_t i = 0; i < base.length() && out.length() < 48; i++) {
    char c = base[i];
    if (isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_' || c == '.' || c == '(' ||
        c == ')')
      out += c;
    else if ((unsigned char)c >= 0x80)
      continue;  // Umlaute usw. weglassen
    else
      out += '_';
  }
  out.trim();
  while (out.startsWith(".")) out.remove(0, 1);
  if (out.isEmpty()) return "";
  return out + ".tosc";
}

size_t freeBytes() { return LittleFS.totalBytes() - LittleFS.usedBytes(); }

std::unique_ptr<tosc::Engine> load(const String &filePath, String &err) {
  File f = LittleFS.open(filePath, "r");
  if (!f) {
    err = "Datei nicht gefunden";
    return nullptr;
  }
  size_t size = f.size();
  if (size == 0 || size > LAYOUT_MAX_BYTES) {
    err = size ? "Datei zu groß" : "Datei ist leer";
    return nullptr;
  }
  std::string raw(size, '\0');
  if (f.read((uint8_t *)&raw[0], size) != size) {
    err = "Lesefehler";
    return nullptr;
  }
  f.close();

  uint32_t t0 = millis();
  std::string xmlText, e;
  if (!tosc::inflateFile((const uint8_t *)raw.data(), raw.size(), xmlText, &e)) {
    err = e.c_str();
    return nullptr;
  }
  raw.clear();
  raw.shrink_to_fit();
  auto engine = std::make_unique<tosc::Engine>();
  if (!engine->load(xmlText.data(), xmlText.size(), &e)) {
    err = e.c_str();
    return nullptr;
  }
  Serial.printf("Layout %s: %u Bytes (entpackt %u), %u Elemente, %lu ms\n", filePath.c_str(),
                (unsigned)size, (unsigned)xmlText.size(), (unsigned)engine->controlCount(),
                (unsigned long)(millis() - t0));
  return engine;
}

}  // namespace layouts
