// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "profile_file_snapshot.hpp"

namespace biglybtc::storage::detail::azureusFiles {

inline bool currentDirectory(StateHandle parent, const std::string& name, StateHandle directory) {
  const auto held = stateStamp(directory);
  const auto named = stateStampAt(parent, name);
  return held && held->owned && named && *named && sameStateDirectory(*held, **named);
}

inline std::expected<StateDescriptor, Error> openDirectory(StateHandle parent, const std::string& name,
    bool create = false) {
  auto directory = openStateDirectory(parent, name);
  if (!directory && create && directory.error().code == ErrorCode::Missing) {
    directory = createStateDirectory(parent, name);
    if (!directory && directory.error().code == ErrorCode::Conflict) {
      directory = openStateDirectory(parent, name);
    }
  }
  if (!directory) { return directory; }
  if (!currentDirectory(parent, name, directory->get())) {
    return std::unexpected(Error{ErrorCode::Conflict, "Azureus profile directory changed while opening"});
  }
  return directory;
}

inline std::expected<ProfileFileSnapshot, Error> capture(StateHandle directory, const std::string& name,
    std::size_t maximum) {
  auto result = captureProfileFile(directory, name, maximum);
  // A Win32 replacement cannot retire its old destination while it is held.
  // Persist the verified stamp and bytes for CAS and close the witness.
  if (result) { result->releaseWitness(); }
  return result;
}

inline std::expected<ProfileFileSnapshot, Error> stage(StateHandle directory, const std::string& name,
    std::string_view bytes, std::size_t maximum) {
  auto writer = stageStateFile(directory, name, bytes, 0600);
  if (!writer) { return std::unexpected(writer.error()); }
  auto synced = syncStateFile(writer->get());
  if (!synced) { return std::unexpected(synced.error()); }
  auto stamp = stateStamp(writer->get());
  if (!stamp) { return std::unexpected(stamp.error()); }
  *writer = StateDescriptor{};
  auto staged = captureProfileFile(directory, name, maximum);
  if (!staged) { return staged; }
  if (!staged->stamp || !sameStateIdentity(*stamp, *staged->stamp)
      || !staged->bytes || *staged->bytes != bytes) {
    return std::unexpected(Error{ErrorCode::Conflict, "Azureus profile stage changed while writing"});
  }
  return staged;
}

inline std::expected<ProfileFileSnapshot, Error> publish(StateHandle directory, const std::string& from,
    const std::string& to, const ProfileFileSnapshot& staged, bool replace, std::size_t maximum) {
  if (!staged.stamp || !staged.bytes || (staged.witnessRequired && !staged.stillHeld())) {
    return std::unexpected(Error{ErrorCode::Conflict, "Azureus profile stage no longer matches its witness"});
  }
  auto published = publishStateFile(directory, from, to, InvalidStateHandle, replace, &*staged.stamp);
  if (!published) { return std::unexpected(published.error()); }
  auto synced = syncStateDirectory(directory);
  if (!synced) { return std::unexpected(synced.error()); }
  auto current = capture(directory, to, maximum);
  if (!current) { return current; }
  if (!current->stamp || !sameStateIdentity(*staged.stamp, *current->stamp) || current->bytes != staged.bytes) {
    return std::unexpected(Error{ErrorCode::Conflict, "Published Azureus profile file changed"});
  }
  return current;
}

} // namespace biglybtc::storage::detail::azureusFiles
