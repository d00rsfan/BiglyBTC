// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "manual_tag_store.hpp"
#include "biglybtc/tag_rate_readiness.hpp"
namespace biglybtc::engine {class ProfileTagRates;}
namespace biglybtc::storage {
// Read-only exact current-source/SDK preparation,NOT a generic writer grant.
// Actor CAS and actual target attachment/post-commit effects stay mandatory.
// Owns bytes and weak lifetime witnesses only;no profile lease/SDK dependency.
class PreparedTagRateSource final {
public:
 PreparedTagRateSource()=default;
 bool matches(const ProfileLease&,const ManualTagStore&,const ManualTagSnapshot&,std::int32_t tag)const noexcept;
private:
 friend class biglybtc::engine::ProfileTagRates;
 struct Entry{std::int32_t tag;std::uint64_t uid;core::SavedTagRateLimits limits;core::TagRateReadiness ready;};
 struct Data;std::shared_ptr<const Data> data_;
 PreparedTagRateSource(std::weak_ptr<const ProfileLease>,const ManualTagStore&,ManualTagSnapshot,
     std::weak_ptr<const std::byte>,std::vector<Entry>);
};
}
