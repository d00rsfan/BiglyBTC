#pragma once
#include "profile.hpp"
#include "biglybtc/library.hpp"
#include "biglybtc/torrent_intake_options.hpp"
#include <optional>
#include <string>
#include <span>
#include <filesystem>
namespace biglybtc::storage {
inline constexpr const char* AllocationModeKey="BiglyBTC.allocationMode";
inline constexpr const char* AllocationStateKey="BiglyBTC.allocationState";
inline constexpr const char* AllocationSnapshotKey="BiglyBTC.allocationSnapshot";
enum class AllocationState { Pending=1,Complete=2,Cancelled=3 };
struct AllocationIntent {
  core::TorrentHash hash;
  core::TorrentAllocationMode mode;
  AllocationState state;
  std::string row;
  bool paused=false;
  std::string targetSnapshot;
};
// No marker means legacy. Historical Complete does not authorize zeroing.
std::expected<std::optional<AllocationIntent>,Error> readAllocationIntent(const ProfileLease&,const core::TorrentHash&);
std::expected<void,Error> validateAllocationTargets(const ProfileLease&,const AllocationIntent&,const core::Document&,std::span<const std::filesystem::path>);
// Single held-row/pause CAS after the owner has synced/settled all real work.
// A save-attempt failure is uncertain; caller must not replay this owner attempt.
std::expected<void,Error> publishAllocationState(const ProfileLease&,const AllocationIntent&,AllocationState,void(*)(SaveStage)=nullptr);
} // namespace biglybtc::storage
