// Layout-Dateien im Dateisystem (/layouts/*.tosc) verwalten und laden.
#pragma once
#include <Arduino.h>
#include <memory>
#include <vector>
#include "tosc.h"

namespace layouts {

struct Info {
  String name;
  size_t size;
};

bool begin();                       // Dateisystem einhängen, alte /layout.tosc übernehmen
std::vector<Info> list();           // alphabetisch
bool exists(const String &name);
bool remove(const String &name);
String path(const String &name);
String sanitize(const String &name);  // sicherer Dateiname mit Endung .tosc, "" wenn ungültig
size_t freeBytes();

// Datei laden und prüfen. Gibt nullptr und err zurück, wenn sie nicht lesbar ist.
std::unique_ptr<tosc::Engine> load(const String &filePath, String &err);

}  // namespace layouts
