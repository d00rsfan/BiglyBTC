#pragma once
#include "biglybtc/saved_speed_manager.hpp"
#include "profile.hpp"
#include <stop_token>
namespace biglybtc::storage {
struct SavedSpeedManagerView {
  core::SavedSpeedSettings settings;
  std::optional<core::SavedSpeedHistory> history;
  std::optional<RecoverySource> recoverySource;
  bool missing()const noexcept{return !history;}
};
// Borrow held lease +the Host's already-owned settings bytes. Read only the
// selected fixed net/pm_<ASCII-AS>.dat and its .bak/.saving candidates. Never
// scan, reacquire, write, resolve routes/ASN, ping or initialize SpeedManager.
std::expected<SavedSpeedManagerView,Error> readSavedSpeedManagerView(
    const ProfileLease&,std::string_view settingsBytes,std::stop_token stop={});
} // namespace biglybtc::storage
