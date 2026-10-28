#pragma once

#include "profile.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace biglybtc::storage {

using MainlineIdentityKey = std::array<std::uint8_t, 20>;
enum class OriginalMainlineIdentityState { Disabled, Missing, Imported };
// Owned source observation only. This is not a live wire ID or a reservation.
// Missing/malformed original files are never generated, repaired or replaced.
struct OriginalMainlineIdentitySnapshot {
  OriginalMainlineIdentityState state = OriginalMainlineIdentityState::Missing;
  std::string pluginDirectory;
  std::optional<MainlineIdentityKey> root;
  std::string originalBytes;
};
struct OriginalMainlineIdentitySlot {
  MainlineIdentityKey key;
  std::uint32_t index;
};

class OriginalMainlineIdentity final {
public:
  static constexpr std::size_t MaximumBytes = 1024 * 1024;
  // Pass the already authenticated NetworkPolicy mainlineDirectory and
  // mainlineRestoreIdentity. Disabled mode does not open any original path.
  static std::expected<OriginalMainlineIdentity, Error> load(
      const ProfileLease&, std::string pluginDirectory, bool restoreIdentity = true);
  OriginalMainlineIdentity(OriginalMainlineIdentity&&) noexcept;
  OriginalMainlineIdentity& operator=(OriginalMainlineIdentity&&) noexcept;
  OriginalMainlineIdentity(const OriginalMainlineIdentity&) = delete;
  OriginalMainlineIdentity& operator=(const OriginalMainlineIdentity&) = delete;
  ~OriginalMainlineIdentity();

  const OriginalMainlineIdentitySnapshot& snapshot() const noexcept;
  // Recheck the SAME held profile, directory chain and exact regular source
  // file (or captured absence). Ignores directory mtime/ctime, not leaf stats.
  std::expected<void, Error> validate(const ProfileLease&) const;
  // Pure delegation to the qualified plugin, not a freshness check. A future
  // transaction must validate separately. Caller owns reservation, race
  // exclusion and per-original-Node/family retirement. No wire activation.
  std::expected<OriginalMainlineIdentitySlot, Error> select(
      std::span<const MainlineIdentityKey> active) const;

private:
  struct Impl;
  explicit OriginalMainlineIdentity(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};

} // namespace biglybtc::storage
