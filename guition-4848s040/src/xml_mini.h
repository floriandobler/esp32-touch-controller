// Minimaler XML-Parser (DOM) für TouchOSC-Layouts (lexml).
// Unterstützt Elemente, Attribute, Text, CDATA, Kommentare und die <?xml?>-Deklaration.
// Kein Namespace-, DTD- oder Entity-Support außer den fünf Standard-Entities.
// Plattformunabhängig, damit er auch am Rechner getestet werden kann.
#pragma once
#include <string>
#include <vector>
#include <memory>

namespace xml {

struct Node {
  std::string name;
  std::vector<std::pair<std::string, std::string>> attrs;
  std::string text;  // zusammengefügter Text- und CDATA-Inhalt
  std::vector<std::unique_ptr<Node>> children;

  const Node *child(const char *n) const;
  std::string attr(const char *n) const;
  // Text des Kindelements n, oder def wenn nicht vorhanden
  std::string childText(const char *n, const char *def = "") const;
};

// Gibt das Wurzelelement zurück, oder nullptr bei Fehler (err wird gesetzt).
std::unique_ptr<Node> parse(const char *data, size_t len, std::string *err = nullptr);

}  // namespace xml
