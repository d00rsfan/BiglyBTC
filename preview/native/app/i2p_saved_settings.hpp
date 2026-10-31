// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "owned_i2p_service.hpp"
#include "biglybtc/bencode.hpp"
namespace biglybtc::app {
// Read-only original pure-destination tunnel settings; explicit fixture/client
// options override values without writing or registering Java configuration.
std::expected<I2pServiceOptions,storage::Error> readSavedI2pOptions(const core::Document&,I2pServiceOptions);
// Original mixed index0 tunnel keys/defaults; no pure-identity fallback.
std::expected<I2pServiceOptions,storage::Error> readSavedMixedI2pOptions(const core::Document&,I2pServiceOptions);
}
