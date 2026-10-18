// SPDX-License-Identifier: GPL-2.0-or-later
#include "plugin_attributes.hpp"
#include "plugin_settings.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
std::string bytes(const std::string& token) {
  if (token.empty() || token[0] != 'x' || token.size() % 2 != 1) { throw std::runtime_error("Invalid token"); }
  std::string output;
  for (std::size_t i = 1; i < token.size(); i += 2) { output += static_cast<char>(std::stoul(token.substr(i, 2), nullptr, 16)); }
  return output;
}
int main(int argc, char** argv) {
  if (argc != 2) { return 2; }
  const auto path = std::filesystem::canonical(argv[1]);
  if (!path.native().starts_with("/tmp/biglybtc-") || std::filesystem::file_size(path) > 8 * 1024 * 1024) { return 2; }
  std::ifstream stream(path);
  if (!stream.good()) { return 3; }
  std::string line;
  while (std::getline(stream, line)) {
    std::istringstream input(line); char kind = 0; input >> kind;
    if (kind == 'A') {
      std::string p, c, runtime, name; input >> p >> c >> runtime >> name;
      const auto rawPrimary = bytes(p);
      auto primary = biglybtc::core::Document::parse(rawPrimary);
      if (!primary) { return 4; }
      std::optional<biglybtc::core::Document> cache;
      if (c != "-") {
        auto parsed = biglybtc::core::Document::parse(bytes(c));
        if (!parsed) { return 4; }
        cache.emplace(std::move(*parsed));
      }
      const auto value = biglybtc::storage::readOriginalPluginLongAttribute(*primary,
          cache ? &cache->root() : nullptr, bytes(runtime), bytes(name));
      if (!value) { std::cout << "A TYPE_ERROR\n"; }
      else {
        const auto source = value->source == biglybtc::storage::AttributeSource::Cache ? "CACHE"
            : value->source == biglybtc::storage::AttributeSource::Primary ? "PRIMARY" : "DEFAULT";
        std::cout << "A VALUE " << value->value << ' ' << source << '\n';
      }
      if (primary->serialize() != rawPrimary || (cache && cache->serialize() != bytes(c))) { return 5; }
    } else if (kind == 'C') {
      std::string raw, folder; input >> raw >> folder;
      const auto original = bytes(raw);
      auto document = biglybtc::core::Document::parse(original);
      if (!document) { return 4; }
      auto binding = biglybtc::storage::originalPluginBinding("xmwebui", bytes(folder));
      if (!binding) { return 6; }
      biglybtc::storage::OriginalPluginSettings settings(*document, std::move(*binding));
      const auto value = settings.longInteger("xmui.next.id", 1);
      std::cout << "C " << value << ' ' << settings.wrongTypes().size() << '\n';
      if (document->serialize() != original) { return 5; }
    } else { return 7; }
  }
}
