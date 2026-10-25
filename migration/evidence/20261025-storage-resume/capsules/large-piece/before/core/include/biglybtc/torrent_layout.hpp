#pragma once

#include "biglybtc/bencode.hpp"

namespace biglybtc::compat {

struct LayoutError { std::string message; };
struct OriginalFile {
  std::string path;
  std::int64_t offset;
  std::int64_t bytes;
  bool padding;
};
struct OriginalLayout {
  std::string name;
  bool multifile;
  std::int64_t pieceLength;
  std::int64_t bytes;
  std::vector<OriginalFile> files;
};

// Original Java v2 wrappers sort tree keys as UTF16 strings and synthesize their
// own indexed padding names. These indices belong to Java state, not the engine.
std::expected<OriginalLayout, LayoutError> originalLayout(const core::Document& document,
    std::size_t maximumFiles = 100000);

} // namespace biglybtc::compat
