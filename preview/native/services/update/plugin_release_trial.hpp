// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "plugin_release_bundle.hpp"

#include <chrono>
#include <cstdint>
#include <stop_token>

namespace biglybtc::update {

struct PluginTrialProgram {
  QString executable;
  QByteArray sha256;
};

enum class PluginTrialError { Input, ProgramIdentity, Storage, Start, Rejected, Deadline, Cancelled };

// The installation owner supplies the exact built-in trial executable identity.
// The process receives only a generated working directory and the verified module.
std::expected<void, PluginTrialError> trialPluginRelease(const VerifiedPluginBundle&,
    const PluginTrialProgram&, std::uint64_t capabilities, std::chrono::milliseconds deadline,
    std::stop_token = {});

} // namespace biglybtc::update
