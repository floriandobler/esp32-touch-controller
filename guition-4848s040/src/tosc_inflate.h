// Entpackt den Inhalt einer .tosc-Datei (zlib-komprimiertes XML) nach out.
// Unkomprimiertes XML (beginnt mit '<') wird unverändert übernommen.
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

namespace tosc {
bool inflateFile(const uint8_t *data, size_t len, std::string &out, std::string *err);
}
