#pragma once

#include "biglybtc/library.hpp"
#include "biglybtc/saved_dnd.hpp"

#include <map>

namespace biglybtc::core {

enum class CacheError { Root, State, Limit, Entry, Hash, Text, Integer };
struct CachedTorrent {
  TorrentHash hash;
  std::optional<std::string> name;
  std::optional<std::string> utf8Name;
  std::optional<std::string> displayName;
  std::optional<std::int64_t> bytes;
  std::optional<std::int64_t> files;
  std::optional<std::int64_t> type;
  bool privateTorrent = false;
  SavedDndFacts savedDnd{};
};
struct CacheIssue { std::size_t sourceIndex; CacheError error; };
struct StateCache {
  std::map<std::string, CachedTorrent, std::less<>> torrents;
  std::vector<CacheIssue> issues;
};

// Original cached-state wrappers use the last entry for each binary identity.
// All returned strings/identities are owned; the source document stays untouched.
std::expected<StateCache, CacheError> readStateCache(const Document& document,
    std::size_t maximumEntries = 100000, std::size_t maximumNameBytes = 65536);

} // namespace biglybtc::core
