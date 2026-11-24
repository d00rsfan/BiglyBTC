// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "plugin_release_catalogue.hpp"

#include <optional>

namespace biglybtc::update {

enum class PluginActivationError { Format, Proof, Replay, Incompatible, Conflict, NoPrevious };

struct PluginActivationEntry {
  VerifiedPluginRelease release;
  qint64 acceptedAt;
};

struct PluginActivationState {
  qint64 highestSequence = 0;
  std::optional<PluginActivationEntry> active;
  std::optional<PluginActivationEntry> previous;
};

// An installed version's preserved signature is checked at the recorded
// acceptance time. New release admission is checked against the current time
// and the original highest sequence, including after a local rollback.
std::expected<PluginActivationState, PluginActivationError> readPluginActivationState(
    QByteArrayView, QByteArrayView approvedPublicKey, const PluginReleaseTarget&, qint64 now);
QByteArray encodePluginActivationState(const PluginActivationState&);

std::expected<PluginActivationState, PluginActivationError> selectPluginActivation(
    const PluginActivationState&, const VerifiedPluginRelease&, qint64 now);
std::expected<PluginActivationState, PluginActivationError> selectPluginRollback(const PluginActivationState&);

} // namespace biglybtc::update
