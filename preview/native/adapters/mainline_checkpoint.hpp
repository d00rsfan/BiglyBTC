#pragma once

#include "profile.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace biglybtc::storage {

// Disposable native LT state only. Never replaces original settings, IDs or
// routing caches. All access requires a held lease; load never creates files.
class MainlineCheckpoint final {
public:
  static constexpr std::size_t MaximumStateBytes = 1024 * 1024;
  static constexpr std::size_t MaximumWrapperBytes = 4 * 1024 * 1024;
  static std::expected<MainlineCheckpoint, Error> load(const ProfileLease&, std::string pluginDirectory);
  MainlineCheckpoint(MainlineCheckpoint&&) noexcept;
  MainlineCheckpoint& operator=(MainlineCheckpoint&&) noexcept;
  MainlineCheckpoint(const MainlineCheckpoint&) = delete;
  MainlineCheckpoint& operator=(const MainlineCheckpoint&) = delete;
  ~MainlineCheckpoint();

  const std::optional<std::string>& state() const noexcept;
  // Same-state saves are read-only no-ops after CAS. Any attempted-publication
  // failure fences this object; explicit reload/recovery is required to retry.
  std::expected<void, Error> save(const ProfileLease&, std::string_view state);

private:
  struct Impl;
  explicit MainlineCheckpoint(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};

struct MainlineLegacyHint {
  std::string address; // Owned numeric address, never a DNS name or node ID.
  std::uint16_t port = 0;
  bool verified = false, replacement = false;
  std::int64_t lastSeen = 0; // Original decoder projection, not live freshness.
};
struct MainlineLegacyHints {
  std::vector<MainlineLegacyHint> endpoints;
  std::size_t decoded = 0, rejected = 0; // rejected includes duplicate/capacity omissions.
  bool ipv4Missing = false, ipv6Missing = false;
};
// At most64 hints, main entries before replacements, then verified/recent.
// Only plugins/<exact original basename>/IPv{4,6}-table.cache is read. Missing
// paths are empty; unsafe/corrupt inputs fail unchanged. No baseID access.
std::expected<MainlineLegacyHints, Error> readLegacyMldhtHints(
    const ProfileLease&, std::string pluginDirectory, std::int64_t nowMillis);

} // namespace biglybtc::storage
