// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "azureus_value_versions.hpp"
#include <array>
#include <memory>
namespace biglybtc::storage {
struct AzureusPublicBind {
  std::int32_t network=0;std::uint8_t family=4;
  std::array<std::uint8_t,16> address{};std::uint16_t port=0;
  bool operator==(const AzureusPublicBind&)const=default;
};
// SDK-neutral, profile-bound direct numeric policy. Synchronous reads belong
// to external initialization or the existing profile worker, never the actor.
// A failed validation latches withdrawal; explicit recreation is required.
class AzureusPublicPolicy final {
public:
  static std::expected<std::shared_ptr<AzureusPublicPolicy>,Error> open(
      const ProfileLease&,std::shared_ptr<AzureusValueVersions>,AzureusPublicBind);
  ~AzureusPublicPolicy();
  const AzureusPublicBind& binding()const noexcept;
  bool matchesVersions(const AzureusValueVersions&)const noexcept;
  std::shared_ptr<const ProfileLease> profileOwnership()const noexcept;
  // Revocation only. True is NOT a fresh permit or a live route observation.
  bool current()const noexcept;
  // Monotone revocation when a same-profile dependent source also fails.
  void revoke()noexcept;
  std::expected<void,Error> validate()const;
private:
  struct Impl;explicit AzureusPublicPolicy(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};
}
