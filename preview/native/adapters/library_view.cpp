#include "library_view.hpp"
#include "encoded_document.hpp"
#include "file_io.hpp"

#include <cerrno>
#include <fcntl.h>

namespace biglybtc::storage {

std::expected<LibrarySourceSnapshot, Error> readLibrarySource(const ProfileLease& lease) {
  auto file = ConfigFile::load(lease, "downloads.config");
  if (!file) { return std::unexpected(file.error()); }
  auto saved = core::readLibrary(file->document());
  if (!saved) { return std::unexpected(Error{ErrorCode::InvalidFormat, "Invalid original saved-download inventory"}); }
  LibrarySourceSnapshot result;
  auto& view = result.view;
  result.legacyInventory = saved->legacyMap;
  std::optional<core::StateCache> cache;
  detail::Descriptor active(::openat(lease.directoryHandle(), "active", O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_DIRECTORY));
  if (active.get() < 0) {
    if (errno != ENOENT) { view.cacheWarning = Error{ErrorCode::UnsafePath, "Original metadata cache directory is unavailable"}; }
  } else {
    auto bytes = detail::readFile(active.get(), "cache.dat", compatibleConfigLimits().inputBytes);
    if (!bytes) { view.cacheWarning = bytes.error(); }
    else if (*bytes) {
      auto document = EncodedDocument::parse(std::move(**bytes), compatibleConfigLimits());
      if (document && document->compressed()) {
        auto values = core::readStateCache(document->document());
        if (values) {
          cache = std::move(*values);
          result.cacheDocument = std::make_shared<const EncodedDocument>(std::move(*document));
        }
        else { view.cacheWarning = Error{ErrorCode::InvalidFormat, "Original metadata cache has an invalid structure"}; }
      } else { view.cacheWarning = Error{ErrorCode::InvalidFormat, "Original metadata cache is not a complete compatible gzip document"}; }
    }
  }
  view.items.reserve(saved->downloads.size());
  for (auto& item : saved->downloads) {
    std::optional<core::CachedTorrent> metadata;
    if (item.hash && cache) {
      const auto found = cache->torrents.find(item.hash->hex());
      if (found != cache->torrents.end()) { metadata = found->second; }
    }
    view.items.push_back({std::move(item), std::move(metadata)});
  }
  view.pauses = std::move(saved->paused);
  view.inventoryIssues = std::move(saved->issues);
  if (cache) { view.cacheIssues = std::move(cache->issues); }
  return result;
}
std::expected<LibraryView, Error> readLibraryView(const ProfileLease& lease) {
  auto result = readLibrarySource(lease);
  if (!result) { return std::unexpected(result.error()); }
  return std::move(result->view);
}
} // namespace biglybtc::storage
