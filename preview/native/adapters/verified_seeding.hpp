#pragma once
#include "profile.hpp"
#include "original_torrent_identity.hpp"
#include "biglybtc/library.hpp"
#include <optional>

namespace biglybtc::storage {
inline constexpr const char* VerifiedSeedKey="BiglyBTC.verifiedSeeding";
enum class VerifiedSeedState { None, Pending, Released };
// Worker-only saved intent. This is NOT a hash-verification certificate.
struct VerifiedSeedIntent {
  core::TorrentHash hash;
  VerifiedSeedState state;
  std::string row;
  bool paused=false,forceStart=false;
  core::StartupState startup=core::StartupState::Stopped;
};
std::expected<std::optional<VerifiedSeedIntent>,Error> readVerifiedSeedIntent(
    const ProfileLease&,const core::TorrentHash&);
// Explicit user retry only. Keeps stopped70 and removes only this hash's pause;
// no payload/engine IO or automatic startup authority is created.
std::expected<void,Error> resetVerifiedSeedForRetry(const ProfileLease&,const core::TorrentHash&);
std::expected<void,Error> validateVerifiedSeedState(const ProfileLease&,const core::TorrentHash&,
    const OriginalTorrentIdentity&,std::string_view mappingScope);
// Owner calls ONCE after its private same-admission all-piece proof + fresh RO
// snapshot. A failure may follow publication; never replay it from later polls.
std::expected<void,Error> publishVerifiedSeed(const ProfileLease&,const VerifiedSeedIntent&,
    void(*observer)(SaveStage)=nullptr);
}
