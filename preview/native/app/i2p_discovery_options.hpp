// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "owned_i2p_service.hpp"
#include "../services/i2p_dht/actor.hpp"
namespace biglybtc::app {
// Host worker only. Routing hints are read under the held profile lease;
// neither original properties nor Java routing contacts are rewritten.
std::expected<std::optional<services::i2p::dht::Actor::Options>,storage::Error>
readI2pDiscoveryOptions(const storage::ProfileLease&,bool mixed,const I2pServiceOptions&);
}
