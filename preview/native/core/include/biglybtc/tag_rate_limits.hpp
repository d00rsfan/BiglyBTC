// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "bencode.hpp"
#include <expected>
namespace biglybtc::core {
enum class TagRateLimitError {Record,Configuration,Rate};
struct SavedTagRateLimits {
 std::int32_t upload=0,download=0;
 bool unlimited()const noexcept{return upload==0&&download==0;}
};
// Original saved Long.intValue narrowing followed by negative-to-disabled -1.
// Pure values only;neither a source grant nor a live bandwidth-group owner.
std::int32_t normalizeSavedTagRateLimit(std::int64_t)noexcept;
std::expected<SavedTagRateLimits,TagRateLimitError> readSavedTagRateLimits(const Node&)noexcept;
bool savedTagUnlimitedRateProperty(std::string_view,const Node&)noexcept;
// Read-only metadata eligibility;actual source preparation still requires
// exact support by the selected SDK before membership effects.
// This never grants membership edits or live rate effects.
bool savedTagSupportedRateProperty(std::string_view,const Node&)noexcept;
}
