// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <biglybtc/bencode.hpp>
#include <cstdint>
#include <expected>
namespace biglybtc::services::azureus_db_runtime::survey {
enum class PolicyError {Input};
// Saved startup state only; no VCC request, configuration write or authority.
std::expected<std::int64_t,PolicyError> savedFeatureFlags(const core::Node& config);
bool surveyEnabled(std::int8_t protocol,std::int32_t network,bool storageAdapter,std::int64_t featureFlags)noexcept;
}
