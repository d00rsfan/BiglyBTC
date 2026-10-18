#include "plugin_settings.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
std::string hex(std::string_view value) {
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (const unsigned char byte : value) { out << std::setw(2) << static_cast<unsigned>(byte); }
  return out.str();
}
int main(int argc, char** argv) {
  if (argc != 3) { return 2; }
  const auto path = std::filesystem::canonical(argv[1]);
  if (!path.native().starts_with("/tmp/biglybtc-") || std::filesystem::file_size(path) > 4 * 1024 * 1024) { return 2; }
  std::ifstream stream(path, std::ios::binary);
  if (!stream.good()) { return 3; }
  const std::string bytes((std::istreambuf_iterator<char>(stream)), {});
  auto document = biglybtc::core::Document::parse(bytes);
  if (!document) { std::cout << "REJECT\n"; return 0; }
  auto settings = biglybtc::storage::readOriginalMldhtSettings(*document, argv[2]);
  if (!settings) { std::cout << "REJECT\n"; return 0; }
  const auto& s = *settings;
  const bool flags[] = {s.loadedAtStartup, s.enabled, s.autostart4, s.autostart6, s.autoopen4, s.autoopen6,
      s.backupOnly, s.onlyPeerBootstrap, s.alwaysRestoreId, s.showStatusEntry, s.multihoming4, s.multihoming6};
  unsigned mask = 0;
  for (unsigned i = 0; i < 12; ++i) { if (flags[i]) { mask |= 1U << i; } }
  if (document->serialize() != bytes) { return 4; }
  std::cout << "CONFIG " << hex(s.binding.configPrefix) << ' ' << hex(s.binding.directoryName)
      << ' ' << s.port << ' ' << mask << ' ' << s.wrongTypes.size() << '\n';
}
