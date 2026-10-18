#include "mldht_cache.hpp"
#include "file_io.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <span>
#include <sstream>
#include <fcntl.h>
namespace fs = std::filesystem;
std::string hex(std::span<const std::uint8_t> bytes) {
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (const auto byte : bytes) { out << std::setw(2) << static_cast<unsigned>(byte); }
  return out.str();
}
int main(int argc, char** argv) {
  namespace store = biglybtc::storage;
  if (argc != 2 && argc != 3) { return 2; }
  const auto input = fs::canonical(argv[1]);
  if (!input.native().starts_with("/tmp/biglybtc-") || fs::file_size(input) > 50 * 1024 * 1024) { return 2; }
  std::ifstream stream(input, std::ios::binary);
  if (!stream.good()) { return 3; }
  const std::string bytes((std::istreambuf_iterator<char>(stream)), {});
  auto decoded = store::OriginalMldhtCache::parse(bytes, 1234567890123);
  if (!decoded) { std::cout << "REJECT\n"; return 0; }
  // Exercise ownership after moving a cache with borrowed version strings.
  auto cache = std::move(*decoded);
  if (cache.originalBytes() != bytes) { return 4; }
  if (argc == 3) {
    const fs::path output(argv[2]);
    const auto parent = fs::canonical(output.parent_path());
    if (!parent.native().starts_with("/tmp/biglybtc-")) { return 2; }
    store::detail::Descriptor directory(::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (directory.get() < 0 || !store::detail::writeExclusive(directory.get(), output.filename().string(), bytes, 0600)) { return 5; }
  }
  std::cout << "CACHE " << (cache.oldKey() ? hex(*cache.oldKey()) : "-") << ' ' << cache.timestamp() << ' ';
  if (cache.estimateBits()) {
    std::cout << std::hex << std::setfill('0') << std::setw(16) << *cache.estimateBits() << std::dec;
  } else { std::cout << '-'; }
  std::cout << '\n';
  for (const auto& entry : cache.entries()) {
    std::vector<std::uint8_t> packed(entry.address.begin(), entry.address.begin() + (entry.family == 4 ? 4 : 16));
    packed.push_back(static_cast<std::uint8_t>(entry.port >> 8)); packed.push_back(static_cast<std::uint8_t>(entry.port));
    const auto version = entry.version ? "v" + hex({reinterpret_cast<const std::uint8_t*>(entry.version->data()), entry.version->size()}) : "-";
    std::cout << "ENTRY " << (entry.replacement ? 'R' : 'M') << ' ' << entry.sourceIndex << ' '
        << hex(packed) << ' ' << hex(entry.id) << ' ' << entry.created << ' ' << entry.lastSeen << ' '
        << entry.lastSend << ' ' << entry.failedCountRaw << ' ' << entry.failedCount << ' ' << entry.verified << ' ' << version << '\n';
  }
}
