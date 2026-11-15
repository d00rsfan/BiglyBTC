// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "azureus_value_versions.hpp"
#include <biglybtc/azureus_local_diversification.hpp>
#include <span>
#include <vector>
namespace biglybtc::storage {
std::expected<std::vector<core::AzureusLocalDiversification>,Error> readAzureusLocalDiversifications(const AzureusValueVersions&,std::int64_t now);
// Patch only known local records, preserving remote and unrelated raw fields.
// Type1 clears every recognised duplicate for that key. Ambiguous rows refuse.
std::expected<void,Error> saveAzureusLocalDiversifications(const AzureusValueVersions&,std::span<const core::AzureusLocalDiversification>);
}
