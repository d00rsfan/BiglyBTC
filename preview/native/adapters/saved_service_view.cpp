#include "saved_service_view.hpp"

#include "file_io.hpp"

#include <new>
#include <utility>

namespace biglybtc::storage {
namespace {
// Saved-field source contracts: BiglyBT cb8c6cb9bb97916cbc7e80338a017200ffc3e71f,
// TRHostConfigImpl and ShareConfigImpl. File handling stays in the existing
// read-only ConfigFile/directory-fd reader; no original managers are initialized.
Error cancelled() { return {ErrorCode::Cancelled, "Saved-service read cancelled"}; }
Error projectionError(core::SavedServiceError error) {
  switch (error) {
    case core::SavedServiceError::Limit:
      return {ErrorCode::Unsupported, "Saved-service data exceeds the bounded preview projection limits"};
    case core::SavedServiceError::Memory:
      return {ErrorCode::Io, "Insufficient memory for saved-service data"};
    case core::SavedServiceError::Root:
    case core::SavedServiceError::List:
      return {ErrorCode::InvalidFormat, "Invalid saved-service configuration structure"};
    case core::SavedServiceError::EditedDocument:
      return {ErrorCode::InvalidFormat, "Edited saved-service document is not an original saved snapshot"};
  }
  return {ErrorCode::InvalidFormat, "Unsupported saved-service projection result"};
}

// ConfigFile::load historically reports Missing if the primary is absent even
// when all present recovery candidates are malformed. Never present that case
// as an absent/empty service. A race may make a refresh necessary; no sequence
// of these separate reads is claimed to be an atomic profile snapshot.
std::expected<bool, Error> confirmAbsent(const ProfileLease& lease,
    const char* filename, std::stop_token stop) {
  for (const char* suffix : {"", ".bak", ".saving"}) {
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    auto bytes = detail::readFile(lease.directoryHandle(), std::string(filename) + suffix,
        core::Limits{}.inputBytes);
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (!bytes) { return std::unexpected(bytes.error()); }
    if (bytes->has_value()) { return false; }
  }
  return true;
}

template<typename View, typename Project>
std::expected<View, Error> readView(const ProfileLease& lease, const char* filename,
    std::stop_token stop, Project project) {
  if (stop.stop_requested()) { return std::unexpected(cancelled()); }
  try {
    auto file = ConfigFile::load(lease, filename, core::Limits{});
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (!file) {
      if (file.error().code != ErrorCode::Missing) { return std::unexpected(file.error()); }
      const auto absent = confirmAbsent(lease, filename, stop);
      if (!absent) { return std::unexpected(absent.error()); }
      if (!*absent) {
        return std::unexpected(Error{ErrorCode::InvalidFormat,
            "A saved-service candidate exists but no complete compatible document was read"});
      }
      if (stop.stop_requested()) { return std::unexpected(cancelled()); }
      return View{};
    }
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    auto snapshot = project(file->document());
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (!snapshot) { return std::unexpected(projectionError(snapshot.error())); }
    View result;
    result.recoverySource = file->recoverySource();
    result.snapshot.emplace(std::move(*snapshot));
    return result;
  } catch (const std::bad_alloc&) {
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    return std::unexpected(Error{ErrorCode::Io, "Insufficient memory for saved-service data"});
  }
}
} // namespace

std::expected<SavedHostingView, Error> readSavedHostingView(
    const ProfileLease& lease, std::stop_token stop) {
  return readView<SavedHostingView>(lease, "tracker.config", stop,
      [](const core::Document& document) { return core::readSavedHosting(document); });
}
std::expected<SavedSharesView, Error> readSavedSharesView(
    const ProfileLease& lease, std::stop_token stop) {
  return readView<SavedSharesView>(lease, "sharing.config", stop,
      [](const core::Document& document) { return core::readSavedShares(document); });
}

} // namespace biglybtc::storage
