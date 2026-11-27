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
  bool operator==(const SubscriptionSeedSource&) const = default;
};

// The retained profile worker resolves the signed subscription on its own
// route. Revalidation checks the staged file and returns its metainfo and path.
using SubscriptionSeedResolver = std::function<std::optional<SubscriptionSeedSource>(
    std::string_view, SubscriptionSeedRead)>;

} // namespace biglybtc::engine
