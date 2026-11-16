// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "azureus_key_block_area.hpp"
#include "../services/azureus_key_block/state.hpp"
#include <QHostAddress>
#include <stop_token>
namespace biglybtc::app {
struct AzureusIdentityProfile {
 std::shared_ptr<services::azureus_key_block::State> blocks;
 storage::AzureusKeyBlockArea blockArea;
 bool addressChanged=false,directBlocksPurged=false,addressRecorded=false;
 std::optional<storage::Error> warning;
};
// Profile-worker operation, before handing the new State to its UDP actor.
// The normal caller supplies its actually observed/bound local address.
// A real transport replacement also purges direct blocks on the same host,
// independently of whether optional saved address history can be read.
std::expected<AzureusIdentityProfile,storage::Error> prepareAzureusIdentityProfile(
 const storage::AzureusValueVersions&,const storage::AzureusKeyBlockArea&,
 std::span<const services::azureus_key_block::Record>,const QHostAddress& actual,
 std::int64_t wallMilliseconds,services::azureus_key_block::Verifier=services::azureus_key_block::verifyOfficialStatus,std::stop_token={},bool actualContactChanged=false);
}
