// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_file_snapshot.hpp"

namespace biglybtc::storage::detail {
namespace {
constexpr auto MaximumHistorySource = 64U * 1024U * 1024U;
std::unexpected<Error> changed() {
  return std::unexpected(Error{ErrorCode::Conflict,"Profile file changed during snapshot"});
}
}

bool ProfileFileSnapshot::stillHeld() const {
  if (!stamp) { return descriptor.get() == InvalidStateHandle; }
  const auto held = stateStamp(descriptor.get());
  return held && sameStateSnapshot(*stamp, *held);
}

bool ProfileFileSnapshot::matches(const ProfileFileSnapshot& observed) const {
  return (!witnessRequired || stillHeld()) && bool(stamp) == bool(observed.stamp)
      && (!stamp || sameStateSnapshot(*stamp, *observed.stamp)) && bytes == observed.bytes;
}

void ProfileFileSnapshot::releaseWitness() noexcept {
  descriptor = StateDescriptor{};
  witnessRequired = false;
}

std::expected<ProfileFileSnapshot, Error> captureProfileFile(StateHandle directory,
    const std::string& leaf, std::optional<std::size_t> maximum) {
  const auto before = stateStampAt(directory, leaf);
  if (!before) { return std::unexpected(before.error()); }
  if (!*before) { return ProfileFileSnapshot{}; }
  const auto& named = **before;
  if (!named.regular || !named.owned || named.links != 1) {
    return std::unexpected(Error{ErrorCode::UnsafePath,"Profile snapshot requires an owned single-link regular file"});
  }
  auto witness = openStateWitness(directory, leaf);
  if (!witness) { return changed(); }
  const auto held = stateStamp(witness->get());
  if (!held || !sameStateSnapshot(named, *held)) { return changed(); }
  ProfileFileSnapshot snapshot{std::move(*witness), *held, {}};
  if (maximum) {
    auto bytes = readStateFile(directory, leaf, *maximum);
    if (!bytes) { return std::unexpected(bytes.error()); }
    if (!*bytes) { return changed(); }
    snapshot.bytes = std::move(*bytes);
  }
  const auto after = stateStampAt(directory, leaf);
  if (!snapshot.stillHeld() || !after || !*after
      || !sameStateSnapshot(*held, **after)) { return changed(); }
  return snapshot;
}

std::expected<HistorySource, Error> captureHistorySource(StateHandle root, std::string name) {
  const auto slash = name.find('/');
  StateDescriptor directory;
  auto parent = root;
  auto leaf = name;
  if (slash != std::string::npos) {
    auto opened = openDirectoryAt(root, name.substr(0, slash));
    if (!opened) { return std::unexpected(opened.error()); }
    directory = std::move(*opened);
    parent = directory.get();
    leaf = name.substr(slash + 1);
  }
  auto snapshot = captureProfileFile(parent, leaf, MaximumHistorySource);
  if (!snapshot) { return std::unexpected(snapshot.error()); }
  return HistorySource{std::move(*snapshot), std::move(name)};
}

} // namespace biglybtc::storage::detail
