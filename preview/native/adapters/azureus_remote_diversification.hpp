// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "azureus_value_versions.hpp"
#include <biglybtc/azureus_diversification.hpp>
#include <span>
#include <vector>
namespace biglybtc::storage {
std::expected<std::vector<core::AzureusRemoteDiversification>,Error> readAzureusRemoteDiversifications(const AzureusValueVersions&,std::int64_t now);
// Original dht/[netN]/diverse.dat + .saving. Preserve local/unrelated records.
std::expected<void,Error> saveAzureusRemoteDiversifications(const AzureusValueVersions&,std::span<const core::AzureusRemoteDiversification>);
}
