// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "state_file_io.hpp"

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace biglybtc::engine {

using SubscriptionSeedHash = std::array<char, 20>;

enum class SubscriptionSeedRead { Prepare, Revalidate };

struct SubscriptionSeedSource {
  std::string metainfo;
  std::filesystem::path directory;
  std::shared_ptr<const storage::detail::StateDescriptor> directoryHandle{};

  bool sameVersion(const SubscriptionSeedSource& other) const {
    return metainfo == other.metainfo && directory == other.directory;
  }
};

// The retained profile worker resolves the signed subscription on its own
// route. Preparation retains the verified parent directory. Revalidation
// checks the staged file and returns its metainfo and path without a grant.
using SubscriptionSeedResolver = std::function<std::optional<SubscriptionSeedSource>(
    std::string_view, SubscriptionSeedRead)>;

} // namespace biglybtc::engine
