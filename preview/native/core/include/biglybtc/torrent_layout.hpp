#pragma once

#include "biglybtc/bencode.hpp"

namespace biglybtc::compat {

// Largest original standard piece size, also accepted by the pinned engine
// metadata reader. Torrent creation has its own, independent piece-size bound.
inline constexpr std::int64_t OriginalMaximumPieceLength=256LL*1024*1024;

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
  // True only for the explicit source-qualified Linux conversion overload.
  bool filenameConverted=false;
};

// Original Java v2 wrappers sort tree keys as UTF16 strings and synthesize their
// own indexed padding names. These indices belong to Java state, not the engine.
std::expected<OriginalLayout, LayoutError> originalLayout(const core::Document& document,
    std::size_t maximumFiles = 100000);

// Explicit decoded-component policy. Old overload remains the historical raw
// codec projection for legacy accepted journals and metadata-only validators.
// The held-profile adapter supplies the exact saved UTF8 conversion string;
// this pure function performs no config/filesystem probing or info rewriting.
inline constexpr std::string_view OriginalDefaultFilenameConversions="\"='";
std::expected<OriginalLayout, LayoutError> originalLayout(const core::Document&,
    std::size_t maximumFiles,std::string_view filenameConversions);
std::expected<std::string,LayoutError> originalFilenameComponent(
    std::string_view decodedUtf8,std::string_view filenameConversions=OriginalDefaultFilenameConversions);

} // namespace biglybtc::compat
