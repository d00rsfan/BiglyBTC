// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include "biglybtc/library.hpp"
#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace biglybtc::app {
struct I2pServiceOptions {
  std::uint16_t routerPort=7654;
  std::vector<std::pair<std::string,std::string>> sessionOptions;
  std::uint16_t peerPort=6881;
};
// All calls and destruction belong to the external Host worker that called
// start(). Blocking Qt/provider observations and joins must never run on GUI.
class OwnedI2pService final {
public:
  enum class Phase {Prepared,Starting,Active,Withdrawn,Retiring};
  struct Observation {
    Phase phase=Phase::Prepared;
    std::array<std::uint8_t,32> publicIdentity{};
    std::uint64_t admission=0,generation=0;
    std::int64_t verifiedBytes=0,payloadDownloaded=-1,payloadUploaded=-1;
    int downloadRate=0,uploadRate=0,peers=0,seeds=0;
    bool checked=false,seeding=false,current=false,joined=false;
    std::uint64_t trackerAnnounces=0,trackerReplies=0,trackerErrors=0;
    std::string failure;
  };
  static bool available()noexcept;
  static std::expected<std::unique_ptr<OwnedI2pService>,storage::Error> start(
      const storage::ProfileLease&,const core::TorrentHash&,I2pServiceOptions={});
  ~OwnedI2pService();
  OwnedI2pService(const OwnedI2pService&)=delete;
  OwnedI2pService& operator=(const OwnedI2pService&)=delete;
  Observation observation()const;
  // Five-second actual retirement, external join, then exact saved checkpoint.
  // Failure retains recheck state; callers must not change saved intent.
  // Provider/source joins and checkpoint IO may block on this Host worker.
  std::expected<void,storage::Error> close();
private:
  struct Impl;explicit OwnedI2pService(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};
}
