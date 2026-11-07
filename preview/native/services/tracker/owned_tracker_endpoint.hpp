// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
namespace biglybtc::sharing {class SavedSharingService;}
namespace biglybtc::tracker {
class EmbeddedTrackerService;
struct TrackerEndpointDescription {
  std::string announceUrl,bindAddress;
  std::uint16_t port=0;
  std::uint64_t generation=0;
};
// Genuine accepted owned listener only. No public constructor/URL setter.
// The start request token is a pre-acceptance fence; a finished request/page
// does not own the background listener. Explicit Stop/profile close retires it.
class OwnedTrackerEndpoint final {
public:
  OwnedTrackerEndpoint(OwnedTrackerEndpoint&&)noexcept;
  OwnedTrackerEndpoint& operator=(OwnedTrackerEndpoint&&)noexcept;
  ~OwnedTrackerEndpoint();
  OwnedTrackerEndpoint(const OwnedTrackerEndpoint&)=delete;
  const TrackerEndpointDescription& description()const noexcept;
  std::expected<std::uint64_t,storage::Error> current(
      const storage::ProfileLease& callerHeldSameOFD,std::uint64_t session)const;
private:
  friend class EmbeddedTrackerService;
  friend class sharing::SavedSharingService;
  std::expected<OwnedTrackerEndpoint,storage::Error> retainedCopy(
      const storage::ProfileLease&,std::uint64_t)const;
  struct Impl;
  explicit OwnedTrackerEndpoint(std::shared_ptr<Impl>)noexcept;
  std::shared_ptr<Impl> impl_;
};
} // namespace biglybtc::tracker
