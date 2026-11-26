// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "profile_file_snapshot.hpp"
#include "saved_rss_source.hpp"

namespace biglybtc::search::profile_files {
namespace state = storage::detail;
using Error = SavedRssSourceError;
using state::StateHandle;
using state::StateStamp;

struct File {
  std::string name;
  std::string bytes;
  state::ProfileFileSnapshot witness;
  std::size_t maximum;
  bool present;
};

inline std::expected<File, Error> named(StateHandle directory, std::string name, std::size_t maximum) {
  std::optional<std::size_t> byteLimit = maximum;
#ifdef _WIN32
  // The interoperable Java byte lock covers the contents of .azlock.
  if (name == ".azlock") { byteLimit.reset(); }
#endif
  auto snapshot = state::captureProfileFile(directory, name, byteLimit);
  if (!snapshot) {
    switch (snapshot.error().code) {
      case storage::ErrorCode::Conflict: return std::unexpected(Error::SourceChanged);
      case storage::ErrorCode::InvalidFormat: return std::unexpected(Error::Limit);
      default: return std::unexpected(Error::Malformed);
    }
  }
  const bool present = snapshot->stamp.has_value();
  if (present && (snapshot->stamp->size < 0 || static_cast<std::uint64_t>(snapshot->stamp->size) > maximum)) {
    return std::unexpected(Error::Limit);
  }
  auto bytes = snapshot->bytes ? std::move(*snapshot->bytes) : std::string{};
  snapshot->bytes.reset();
  return File{std::move(name), std::move(bytes), std::move(*snapshot), maximum, present};
}

inline std::expected<void, Error> unchanged(StateHandle directory, const std::vector<File>& files) {
  for (const auto& before : files) {
    auto after = named(directory, before.name, before.maximum);
    if (!after) { return std::unexpected(after.error()); }
    if (!before.witness.matches(after->witness) || before.bytes != after->bytes) {
      return std::unexpected(Error::SourceChanged);
    }
  }
  return {};
}

inline std::string scope(const StateStamp& value) {
  return std::to_string(value.device) + ':' + std::to_string(value.inode) + ':' + std::to_string(value.owner);
}

inline std::string stamp(const StateStamp& value) {
  return std::to_string(value.device) + ':' + std::to_string(value.inode) + ':' + std::to_string(value.mode)
      + ':' + std::to_string(value.owner) + ':' + std::to_string(value.group) + ':' + std::to_string(value.links)
      + ':' + std::to_string(value.size) + ':' + std::to_string(value.modified.tv_sec)
      + ':' + std::to_string(value.modified.tv_nsec) + ':' + std::to_string(value.changed.tv_sec)
      + ':' + std::to_string(value.changed.tv_nsec);
}

inline bool sameDirectory(const StateStamp& a, const StateStamp& b) {
  return a.directory && b.directory && a.owned && b.owned && state::sameStateIdentity(a, b);
}

inline std::expected<StateStamp, Error> directory(const storage::ProfileLease& lease,
    const std::filesystem::path& path) {
  if (!lease.valid()) { return std::unexpected(Error::Lease); }
  const auto opened = state::openStatePathDirectory(path);
  if (!opened) { return std::unexpected(Error::Lease); }
  const auto held = state::stateStamp(lease.directoryHandle());
  const auto label = state::stateStamp(opened->get());
  if (!held || !label || !sameDirectory(*held, *label)) { return std::unexpected(Error::Lease); }
  return *held;
}
} // namespace biglybtc::search::profile_files
