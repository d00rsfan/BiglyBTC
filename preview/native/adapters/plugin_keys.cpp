// SPDX-License-Identifier: GPL-2.0-or-later
#include "plugin_keys.hpp"
#include <optional>
namespace biglybtc::storage::detail {
namespace {
std::optional<std::string> originalKeyBytes(std::string_view utf8) {
  std::string bytes;
  bytes.reserve(utf8.size());
  for (std::size_t i = 0; i < utf8.size(); ++i) {
    const auto byte = static_cast<unsigned char>(utf8[i]);
    if (byte < 128) { bytes += static_cast<char>(byte); continue; }
    if ((byte != 0xc2 && byte != 0xc3) || i + 1 == utf8.size()) { return std::nullopt; }
    const auto next = static_cast<unsigned char>(utf8[++i]);
    if (next < 0x80 || next > 0xbf) { return std::nullopt; }
    bytes += static_cast<char>(((byte & 31) << 6) | (next & 63));
  }
  return bytes;
}
}
const core::Node* findOriginalKey(const core::Node& dictionary, std::string_view utf8) {
  bool ascii = true;
  for (const unsigned char byte : utf8) { if (byte >= 128) { ascii = false; break; } }
  if (ascii) { return dictionary.find(utf8); }
  const auto encoded = originalKeyBytes(utf8);
  return encoded ? dictionary.find(*encoded) : nullptr;
}
}
