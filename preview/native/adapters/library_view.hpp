#pragma once

#include "biglybtc/state_cache.hpp"
#include "profile.hpp"
#include "encoded_document.hpp"
#include <memory>

namespace biglybtc::storage {

struct LibraryItem {
  core::SavedDownload saved;
  std::optional<core::CachedTorrent> cached;
};
struct LibraryView {
  std::vector<LibraryItem> items;
  std::vector<core::SavedPause> pauses;
  std::vector<core::LibraryIssue> inventoryIssues;
  std::vector<core::CacheIssue> cacheIssues;
  std::optional<Error> cacheWarning;
};
struct LibrarySourceSnapshot {
  LibraryView view;
  std::shared_ptr<const EncodedDocument> cacheDocument;
  bool legacyInventory = false;
};

std::expected<LibraryView, Error> readLibraryView(const ProfileLease& lease);
// Same read, with immutable decoded cache ownership for host/plugin consumers.
// Callers can index it once and avoid loading newer cached attributes from disk.
std::expected<LibrarySourceSnapshot, Error> readLibrarySource(const ProfileLease& lease);

} // namespace biglybtc::storage
