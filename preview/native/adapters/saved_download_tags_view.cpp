#include "saved_download_tags_view.hpp"
#include "file_io.hpp"
#include <new>

namespace biglybtc::storage {
namespace {
Error cancelled() { return {ErrorCode::Cancelled, "Saved download-tag read cancelled"}; }
Error projectionError(core::SavedTagError error) {
  switch (error) {
    case core::SavedTagError::Limit: return {ErrorCode::Unsupported, "Saved download tags exceed projection limits"};
    case core::SavedTagError::Memory: return {ErrorCode::Io, "Insufficient memory for saved download tags"};
    default: return {ErrorCode::InvalidFormat, "Invalid saved manual download-tag data"};
  }
}
}
std::expected<SavedDownloadTagsView, Error> readSavedDownloadTagsView(const ProfileLease& lease, std::stop_token stop) {
  try {
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    auto file = ConfigFile::load(lease, "tag.config", core::Limits{});
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (!file) {
      if (file.error().code != ErrorCode::Missing) { return std::unexpected(file.error()); }
      for (const char* name : {"tag.config", "tag.config.bak", "tag.config.saving"}) {
        if (stop.stop_requested()) { return std::unexpected(cancelled()); }
        auto bytes = detail::readFile(lease.directoryHandle(), name, core::Limits{}.inputBytes);
        if (stop.stop_requested()) { return std::unexpected(cancelled()); }
        if (!bytes) { return std::unexpected(bytes.error()); }
        if (bytes->has_value()) { return std::unexpected(Error{ErrorCode::InvalidFormat, "A tag configuration candidate exists but none was read completely"}); }
      }
      return SavedDownloadTagsView{};
    }
    auto tags = core::readSavedDownloadTags(file->document().originalBytes());
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (!tags) { return std::unexpected(projectionError(tags.error())); }
    SavedDownloadTagsView result{std::move(*tags), file->recoverySource()};
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    return result;
  } catch (const std::bad_alloc&) {
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    return std::unexpected(Error{ErrorCode::Io, "Insufficient memory for saved download tags"});
  }
}
} // namespace biglybtc::storage
