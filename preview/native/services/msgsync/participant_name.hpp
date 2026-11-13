// SPDX-License-Identifier: GPL-2.0-or-later
// Original BuddyPluginBeta.pkToString: bytes8..10, uppercase hex.
#pragma once
#include <optional>
#include <string>
#include <string_view>
namespace biglybtc::services::msgsync {
// null reproduces the original zero ID; short non-null keys are rejected.
// Display only: never use the short name as an identity or persistence key.
std::optional<std::string> participantShortName(std::optional<std::string_view>);
}
