#include "library_view.hpp"

#include <iostream>

namespace {
std::string hex(std::string_view value) {
  constexpr char digits[] = "0123456789ABCDEF";
  std::string result;
  for (unsigned char byte : value) { result += digits[byte >> 4]; result += digits[byte & 15]; }
  return result;
}
std::string number(std::optional<std::int64_t> value) { return value ? std::to_string(*value) : "-"; }
}

int main(int argc, char** argv) {
  if (argc != 2) { return 2; }
  auto lease = biglybtc::storage::ProfileLease::acquire(argv[1]);
  if (!lease) { std::cerr << lease.error().message << '\n'; return 2; }
  auto view = biglybtc::storage::readLibraryView(*lease);
  if (!view) { std::cerr << view.error().message << '\n'; return 2; }
  std::cout << "VIEW " << view->items.size() << ' ' << view->inventoryIssues.size() << ' '
      << view->cacheIssues.size() << ' ' << view->cacheWarning.has_value() << '\n';
  for (const auto& row : view->items) {
    std::cout << "ITEM " << (row.saved.hash ? row.saved.hash->hex() : "-") << ' ';
    if (!row.cached) { std::cout << "- - - - -\n"; continue; }
    const auto& metadata = *row.cached;
    std::cout << number(metadata.bytes) << ' ' << number(metadata.type) << ' ' << number(metadata.files)
        << ' ' << metadata.privateTorrent << ' ' << hex(metadata.name.value_or("")) << '\n';
  }
}
