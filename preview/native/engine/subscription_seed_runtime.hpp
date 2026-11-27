// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "state_file_io.hpp"
#include "biglybtc/library.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace biglybtc::storage { class ProfileSourceAccess; }
namespace biglybtc::engine {

using SubscriptionSeedHash = std::array<char, 20>;
inline constexpr std::string_view SubscriptionSeedTransferType="org.gudy.azureus2.pluginsimpl.local.ddb.DDBaseTTTorrent";
inline constexpr std::size_t PrivateSubscriptionCandidateLimit = 16;

inline std::size_t nextSubscriptionSeedCandidate(std::span<const core::TorrentHash> candidates,
    const std::optional<core::TorrentHash>& lastAttempt) {
  if (!lastAttempt || candidates.empty()) { return 0; }
  const auto found = std::ranges::find(candidates, *lastAttempt);
  return found == candidates.end() ? 0 : (static_cast<std::size_t>(found - candidates.begin()) + 1) % candidates.size();
}

enum class SubscriptionSeedRead { Prepare, Revalidate };

struct SubscriptionSeedPresence {
  SubscriptionSeedHash dhtKey{};
  std::string wire;
  std::int32_t version=0;
  bool operator==(const SubscriptionSeedPresence&) const=default;
};

struct SubscriptionSeedMetainfo {
  SubscriptionSeedHash lookupKey{};
  std::string encoded;
  bool operator==(const SubscriptionSeedMetainfo&) const=default;
};

struct SubscriptionSeedSource {
  std::string metainfo;
  std::filesystem::path directory;
  std::shared_ptr<const storage::detail::StateDescriptor> directoryHandle{};
  std::optional<SubscriptionSeedPresence> presence{};
  std::optional<SubscriptionSeedMetainfo> ddbMetainfo{};

  bool sameVersion(const SubscriptionSeedSource& other) const {
    return metainfo == other.metainfo && directory == other.directory;
  }
};

// The retained profile worker resolves the signed subscription on its own
// route. Preparation retains the verified parent directory. Revalidation
// checks the staged file and returns its metainfo and path without a grant.
using SubscriptionSeedResolver = std::function<std::optional<SubscriptionSeedSource>(
    std::string_view, SubscriptionSeedRead)>;
using HeldSubscriptionSeedResolver = std::function<std::optional<SubscriptionSeedSource>(
    const storage::ProfileSourceAccess&, std::string_view, SubscriptionSeedRead)>;

} // namespace biglybtc::engine
