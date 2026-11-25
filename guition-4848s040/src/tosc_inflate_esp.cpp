// Entpacken auf dem ESP32 mit dem miniz/tinfl aus dem ROM (kein zusätzlicher Code nötig).
#if defined(ESP_PLATFORM)
#include "tosc_inflate.h"
#include <cstdlib>
#include <esp_heap_caps.h>

extern "C" {
#include "miniz.h"
}

namespace tosc {

bool inflateFile(const uint8_t *data, size_t len, std::string &out, std::string *err) {
  if (len > 0 && data[0] == '<') {
    out.assign((const char *)data, len);
    return true;
  }
  if (len < 2 || (data[0] & 0x0F) != 8) {
    if (err) *err = "Keine gültige .tosc-Datei (weder zlib noch XML)";
    return false;
  }

  // Der Dekompressor-Zustand ist ~11 KB groß: auf den Heap, nicht auf den Stack.
  auto *dec = (tinfl_decompressor *)heap_caps_malloc(sizeof(tinfl_decompressor),
                                                     MALLOC_CAP_8BIT);
  if (!dec) {
    if (err) *err = "Kein Speicher für den Dekompressor";
    return false;
  }

  // Ausgabepuffer schätzen und bei Bedarf vergrößern (XML komprimiert etwa 10:1).
  size_t cap = len * 12 + 4096;
  bool ok = false;
  for (int attempt = 0; attempt < 6 && !ok; attempt++, cap *= 2) {
    uint8_t *buf = (uint8_t *)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = (uint8_t *)malloc(cap);
    if (!buf) break;

    tinfl_init(dec);
    size_t inLen = len, outLen = cap;
    tinfl_status st = tinfl_decompress(dec, data, &inLen, buf, buf, &outLen,
                                       TINFL_FLAG_PARSE_ZLIB_HEADER |
                                           TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    if (st == TINFL_STATUS_DONE) {
      out.assign((const char *)buf, outLen);
      ok = true;
    } else if (st != TINFL_STATUS_HAS_MORE_OUTPUT) {
      if (err) *err = "Entpacken fehlgeschlagen (Status " + std::to_string((int)st) + ")";
      free(buf);
      break;
    }
    free(buf);
  }
  free(dec);
  if (!ok && err && err->empty()) *err = "Entpacken fehlgeschlagen (Datei zu groß?)";
  return ok;
}

}  // namespace tosc
#endif
