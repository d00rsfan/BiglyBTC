// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "manual_tag_store.hpp"
#include "original_torrent_identity.hpp"
namespace biglybtc::storage {
enum class TagIdentityPolicy { OriginalDownload, ExplicitFullV2Reader };
class ManualTagIdentityScope final {
public:
 ManualTagIdentityScope()=default;
 std::optional<core::TorrentHash> runtimeHash()const;
 std::optional<core::TorrentHash> canonicalHash()const;
 // Identity equality only, not a live manager, membership or write permission.
 bool matchesOriginalDownloadKey()const noexcept;
 // Passive cached lineage only. The value retains no new profile lease.
 // Revalidate explicitly before treating filesystem observations as current.
 bool current(const ManualTagStore&,const ProfileLease&,const ManualTagSnapshot&)const noexcept;
 std::expected<void,Error> revalidate(const ManualTagStore&,const ProfileLease&,const ManualTagSnapshot&)const;
private:
 struct Data;std::shared_ptr<const Data> data_;
 explicit ManualTagIdentityScope(std::shared_ptr<const Data> data):data_(std::move(data)){}
 friend std::expected<ManualTagIdentityScope,Error> bindManualTagIdentity(
   const ManualTagStore&,const ProfileLease&,const ManualTagSnapshot&,const core::TorrentHash&,TagIdentityPolicy);
};
// Source-bound read value,not a membership or writer permission. Explicit full32
// reader aliases require explicit opt-in; the default enforces the original
// DownloadManager identity comparison. Reader policy is independent of new-record encoding. The
// bounded supported inventory has only one full32 owner and no key collisions.
std::expected<ManualTagIdentityScope,Error> bindManualTagIdentity(
 const ManualTagStore&,const ProfileLease&,const ManualTagSnapshot&,const core::TorrentHash&,
 TagIdentityPolicy=TagIdentityPolicy::OriginalDownload);
}
