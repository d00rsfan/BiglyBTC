#include "saved_speed_manager_view.hpp"

#include "file_operation_io.hpp"
#include "profile_file_snapshot.hpp"

#include <array>
#include <new>
#include <utility>

namespace biglybtc::storage {
namespace {
Error cancelled() { return {ErrorCode::Cancelled, "Saved speed estimate read cancelled"}; }
Error format() { return {ErrorCode::InvalidFormat, "Invalid or excessive saved speed estimate data"}; }
Error memory() { return {ErrorCode::Io, "Insufficient memory for saved speed estimates"}; }
}

std::expected<SavedSpeedManagerView, Error> readSavedSpeedManagerView(
    const ProfileLease& lease, std::string_view settingsBytes, std::stop_token stop) {
  try {
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (!lease.valid()) {
      return std::unexpected(Error{ErrorCode::UnsafePath, "Held profile lease required for saved speed estimates"});
    }
    auto settings = core::readSavedSpeedSettings(settingsBytes);
    if (!settings) {
      return std::unexpected(settings.error() == core::SavedSpeedError::Memory ? memory() : format());
    }
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (!settings->historyLeaf) {
      return std::unexpected(Error{ErrorCode::Unsupported, "Saved AS identity has no supported safe mapper filename"});
    }

    SavedSpeedManagerView result;
    result.settings = std::move(*settings);
    const auto root = lease.directoryHandle();
    const auto profile = detail::stateStamp(root);
    if (!profile || !profile->directory) {
      return std::unexpected(Error{ErrorCode::UnsafePath, "Held speed-estimate profile is not a directory"});
    }
    auto net = detail::openStateDirectory(root, "net");
    if (stop.stop_requested()) { return std::unexpected(cancelled()); }
    if (!net) {
      if (net.error().code == ErrorCode::Missing) { return result; }
      return std::unexpected(net.error());
    }
    const auto opened = detail::stateStamp(net->get());
    const auto named = detail::stateStampAt(root, "net");
    if (!opened || !named || !*named || !detail::sameStateDirectory(*opened, **named)
        || !fileop::nonOwnerWritesBlocked(net->get())) {
      return std::unexpected(Error{ErrorCode::UnsafePath, "Saved network-estimate directory is unsafe or changed"});
    }
    const auto finish = [&]() -> std::expected<SavedSpeedManagerView, Error> {
      if (stop.stop_requested()) { return std::unexpected(cancelled()); }
      const auto currentRoot = detail::stateStamp(root);
      const auto currentNet = detail::stateStampAt(root, "net");
      if (!lease.valid() || lease.directoryHandle() != root || !currentRoot || !currentRoot->directory
          || !detail::sameStateIdentity(*profile, *currentRoot) || !currentNet || !*currentNet
          || !detail::sameStateDirectory(*opened, **currentNet)
          || !fileop::nonOwnerWritesBlocked(net->get())) {
        return std::unexpected(Error{ErrorCode::Conflict, "Saved network-estimate directory changed while reading"});
      }
      return std::move(result);
    };

    std::array sources{RecoverySource::Primary, RecoverySource::Saving, RecoverySource::Backup};
    bool anyPresent = false;
    for (std::size_t index = 0; index < sources.size(); ++index) {
      if (stop.stop_requested()) { return std::unexpected(cancelled()); }
      const auto source = sources[index];
      const char* suffix = source == RecoverySource::Primary ? "" : source == RecoverySource::Saving ? ".saving" : ".bak";
      const auto leaf = *result.settings.historyLeaf + suffix;
      const auto candidate = detail::captureProfileFile(net->get(), leaf, std::nullopt);
      if (stop.stop_requested()) { return std::unexpected(cancelled()); }
      if (!candidate) { return std::unexpected(candidate.error()); }
      if (!candidate->stamp) { continue; }
      anyPresent = true;
      if (!fileop::nonOwnerWritesBlocked(candidate->descriptor.get())) {
        return std::unexpected(Error{ErrorCode::UnsafePath, "Saved speed estimate must be an owned regular file"});
      }
      const auto bytes = detail::readStateFile(net->get(), leaf, core::Limits{}.inputBytes);
      if (stop.stop_requested()) { return std::unexpected(cancelled()); }
      if (!bytes) { return std::unexpected(bytes.error()); }
      if (!*bytes) {
        return std::unexpected(Error{ErrorCode::Conflict, "Saved speed estimate disappeared while reading"});
      }
      auto parsed = core::readSavedSpeedHistory(**bytes);
      if (stop.stop_requested()) { return std::unexpected(cancelled()); }
      const auto current = detail::stateStampAt(net->get(), leaf);
      if (!candidate->stillHeld() || !current || !*current
          || !detail::sameStateSnapshot(*candidate->stamp, **current)) {
        return std::unexpected(Error{ErrorCode::Conflict, "Saved speed estimate changed while reading"});
      }
      if (parsed) {
        result.history = std::move(*parsed);
        result.recoverySource = source;
        return finish();
      }
      if (parsed.error() == core::SavedSpeedError::Memory) { return std::unexpected(memory()); }
      if (source == RecoverySource::Primary) { std::swap(sources[1], sources[2]); }
    }
    if (anyPresent) { return std::unexpected(format()); }
    return finish();
  } catch (const std::bad_alloc&) {
    return std::unexpected(memory());
  }
}
} // namespace biglybtc::storage
