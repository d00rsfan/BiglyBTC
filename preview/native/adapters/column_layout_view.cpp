#include "column_layout_view.hpp"

#include "file_io.hpp"

#include <new>
#include <utility>

namespace biglybtc::storage {
namespace {
// Original saved-layout source: BiglyBT
// cb8c6cb9bb97916cbc7e80338a017200ffc3e71f, TableColumnManager.
// This adapter uses only the existing read-only directory-fd ConfigFile reader;
// it does not initialize the original manager or perform its recovery writes.
Error cancelled() { return {ErrorCode::Cancelled, "Column-layout read cancelled"}; }
Error projectionError(core::ColumnLayoutError error) {
  switch (error) {
    case core::ColumnLayoutError::Limit:
      return {ErrorCode::Unsupported, "Column-layout data exceeds the bounded preview projection limits"};
    case core::ColumnLayoutError::Memory:
      return {ErrorCode::Io, "Insufficient memory for column-layout data"};
    case core::ColumnLayoutError::Root:
      return {ErrorCode::InvalidFormat, "Invalid column-layout document structure"};
    case core::ColumnLayoutError::EditedDocument:
      return {ErrorCode::InvalidFormat, "Edited column-layout document is not an original saved snapshot"};
  }
  return {ErrorCode::InvalidFormat, "Unsupported column-layout projection result"};
}

// The existing loader can report Missing when the primary is absent but its
// recovery candidates exist and are malformed. Confirm true absence instead
// of presenting malformed saved settings as an empty/missing layout. These
// separate observations do not promise an atomic view against external edits.
std::expected<bool, Error> confirmAbsent(const ProfileLease& lease, std::stop_token stop) {
  for (const char* name : {"tables.config", "tables.config.bak", "tables.config.saving"}) {
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    auto bytes = detail::readFile(lease.directoryHandle(), name, core::Limits{}.inputBytes);
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (!bytes) { return std::unexpected(bytes.error()); }
    if (bytes->has_value()) { return false; }
  }
  return true;
}
} // namespace

std::expected<ColumnLayoutsView, Error> readColumnLayoutsView(
    const ProfileLease& lease, std::stop_token stop) {
  try {
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    auto file = ConfigFile::load(lease, "tables.config", core::Limits{});
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (!file) {
      if (file.error().code != ErrorCode::Missing) { return std::unexpected(file.error()); }
      const auto absent = confirmAbsent(lease, stop);
      if (stop.stop_requested()) { return std::unexpected(cancelled()); }
      if (!absent) { return std::unexpected(absent.error()); }
      if (!*absent) {
        return std::unexpected(Error{ErrorCode::InvalidFormat,
            "A column-layout candidate exists but no complete compatible document was read"});
      }
      return ColumnLayoutsView{};
    }
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    auto snapshot = core::readColumnLayouts(file->document());
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (!snapshot) { return std::unexpected(projectionError(snapshot.error())); }
    ColumnLayoutsView result;
    result.recoverySource = file->recoverySource();
    result.snapshot.emplace(std::move(*snapshot));
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    return result;
  } catch (const std::bad_alloc&) {
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    return std::unexpected(Error{ErrorCode::Io, "Insufficient memory for column-layout data"});
  }
}

} // namespace biglybtc::storage
