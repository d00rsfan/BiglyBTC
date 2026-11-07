// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
namespace biglybtc::core {class Document;}
namespace biglybtc::search::detail {
enum class SubscriptionAutomaticValueError {Malformed,Unsupported,Limit,Memory};
// Passive values only. Actual private source/plan owners supply/revalidate them;
// constructing these structures never grants profile/network/intake authority.
struct SubscriptionAutomaticPolicyValues {
  bool autoStart=true,addHashNames=false,saveTorrentFiles=true;
  bool defaultStartStopped=false,defaultStartAutoPause=false;
  bool deleteSavedTorrentFiles=false;
  std::int32_t minStartMiB=0,maxStartMiB=0,markReadAfterDays=0;
  std::string defaultSaveDirectory,torrentSaveDirectory;
  bool operator==(const SubscriptionAutomaticPolicyValues&)const=default;
};
enum class AutomaticDataLocationKind {Absent,RegularFile,Directory,Unsupported};
struct SubscriptionAutomaticGeometry {
  std::string destinationDirectory,saveName,dataLocation,torrentFile;
  bool multifile=false,autoStart=false,queued=false,paused=false;
  bool operator==(const SubscriptionAutomaticGeometry&)const=default;
};
// No IO. Explicit Default save path is required; Original's platform-derived
// missing default is deliberately not guessed. Save Torrent Files=false and
// automatic failure-age mark-read are explicitly unsupported in this chapter.
std::expected<SubscriptionAutomaticPolicyValues,SubscriptionAutomaticValueError>
readSubscriptionAutomaticPolicy(const core::Document& actualPrimarySettings,
    std::string_view actualProfileDirectory);
bool subscriptionAutomaticShouldStart(const SubscriptionAutomaticPolicyValues&,
    std::int64_t actualTorrentDataBytes)noexcept;
// shouldStart is the Original manager's pure size decision. Geometry.queued
// additionally honours global Default Start Torrents Stopped; neither grants
// runtime admission, and neither restarts an already present download.
// actualOriginalLayoutName is already converted ONCE by originalLayout. The
// plugin getName()+Scheduler/PIF conversion applies once more to torrent-copy
// and hash-container names. Actual nofollow kind comes from the storage owner,
// never a caller permission; unsupported/symlink/special shapes refuse.
std::expected<SubscriptionAutomaticGeometry,SubscriptionAutomaticValueError>
subscriptionAutomaticGeometry(const SubscriptionAutomaticPolicyValues&,
    std::string_view actualOriginalLayoutName,const std::array<std::byte,20>& actualV1Hash,
    bool multifile,std::int64_t actualTorrentDataBytes,
    std::string_view actualFilenameConversions,AutomaticDataLocationKind observedLocationKind);
} // namespace biglybtc::search::detail
