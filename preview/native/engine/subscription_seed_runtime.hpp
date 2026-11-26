// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace biglybtc::engine {

using SubscriptionSeedHash = std::array<char, 20>;

enum class SubscriptionSeedRead { Prepare, Revalidate };

struct SubscriptionSeedSource {
  std::string metainfo;
  std::filesystem::path directory;
};

// The retained profile worker resolves the signed subscription on its own
// route. Revalidation returns the same metainfo and performs no writes.
using SubscriptionSeedResolver = std::function<std::optional<SubscriptionSeedSource>(
    std::string_view, SubscriptionSeedRead)>;

} // namespace biglybtc::engine
