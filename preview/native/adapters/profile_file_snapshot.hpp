// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "state_file_io.hpp"

namespace biglybtc::storage::detail {

struct ProfileFileSnapshot {
  StateDescriptor descriptor;
  std::optional<StateStamp> stamp;
  std::optional<std::string> bytes;
  bool witnessRequired = true;

  bool stillHeld() const;
  bool matches(const ProfileFileSnapshot& observed) const;
  void releaseWitness() noexcept;
};

std::expected<ProfileFileSnapshot, Error> captureProfileFile(StateHandle directory,
    const std::string& leaf, std::size_t maximum);

struct HistorySource : ProfileFileSnapshot {
  std::string name;
};

std::expected<HistorySource, Error> captureHistorySource(StateHandle root, std::string name);

} // namespace biglybtc::storage::detail
