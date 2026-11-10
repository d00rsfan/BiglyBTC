// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "azureus_value_versions.hpp"
#include "biglybtc/shared_udp_witness.hpp"
#include "chat_identity_stamp.hpp"
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
  // Shared SDK socket: local may be behind NAT. The advertised address must
  // be a real observation of that qualified listener. Only relevant network
  // settings are frozen; unrelated profile/statistics writes do not revoke it.
  static std::expected<std::shared_ptr<AzureusPublicPolicy>,Error> openShared(
      const ProfileLease&,std::shared_ptr<AzureusValueVersions>,AzureusPublicBind advertised,
      core::SharedUdpEndpoint local,std::shared_ptr<const core::SharedUdpWitness>);
  static std::expected<std::shared_ptr<AzureusPublicPolicy>,Error> openBootstrapShared(
      const ProfileLease&,std::shared_ptr<AzureusValueVersions>,std::int32_t network,
      core::SharedUdpEndpoint local,std::shared_ptr<const core::SharedUdpWitness>);
  // Generated/owned network4 only; cannot authorize Public endpoints.
  static std::expected<std::shared_ptr<AzureusPublicPolicy>,Error> openOwnedShared(
      const ProfileLease&,std::shared_ptr<AzureusValueVersions>,AzureusPublicBind,
      std::shared_ptr<const core::SharedUdpWitness>);
  std::expected<void,Error> validateChatIdentity(std::string_view key,const ChatIdentityStamp&)const;
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
  static std::expected<std::shared_ptr<AzureusPublicPolicy>,Error> openSharedImpl(
      const ProfileLease&,std::shared_ptr<AzureusValueVersions>,AzureusPublicBind,
      core::SharedUdpEndpoint,std::shared_ptr<const core::SharedUdpWitness>,bool owned,bool bootstrap=false);
  struct Impl;explicit AzureusPublicPolicy(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};
}
