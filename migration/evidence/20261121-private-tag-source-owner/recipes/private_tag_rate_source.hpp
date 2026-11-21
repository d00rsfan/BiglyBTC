// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "manual_tag_store.hpp"
namespace libtorrent {class session_handle;struct info_hash_t;}
namespace biglybtc::engine {
class ProfileTagRates;
// Sealed current private source/SDK preparation. No generic store/CAS grant.
// Match only with the actual private owner session and fresh source capture.
class PrivateTagRateSource final {
public:
 PrivateTagRateSource()=default;
 bool current()const noexcept;
 bool matches(const storage::ProfileLease&,const core::TorrentHash&,
     const storage::ManualTagSnapshot&,const libtorrent::info_hash_t&,
     const libtorrent::session_handle&)const noexcept;
private:
 friend class ProfileTagRates;
 struct Data;
 explicit PrivateTagRateSource(std::shared_ptr<const Data>);
 std::shared_ptr<const Data> data_;
};
}
