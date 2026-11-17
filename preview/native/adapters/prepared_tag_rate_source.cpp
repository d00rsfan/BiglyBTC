// SPDX-License-Identifier: GPL-2.0-or-later
#include "prepared_tag_rate_source.hpp"
#include <algorithm>
namespace biglybtc::storage {
struct PreparedTagRateSource::Data {
 std::weak_ptr<const ProfileLease> lease;
 std::weak_ptr<const std::byte> store,owner;
 ManualTagSnapshot source;std::vector<Entry> entries;std::uint64_t assignmentSession=0;
};
PreparedTagRateSource::PreparedTagRateSource(std::weak_ptr<const ProfileLease> lease,const ManualTagStore& store,
    ManualTagSnapshot source,std::weak_ptr<const std::byte> owner,std::vector<Entry> entries){
 auto data=std::make_shared<Data>();data->lease=std::move(lease);data->store=store.assignmentIdentity_;
 data->owner=std::move(owner);data->source=std::move(source);data->entries=std::move(entries);data_=std::move(data);
}
PreparedTagRateSource PreparedTagRateSource::forAssignmentSession(std::uint64_t session)const{
 PreparedTagRateSource result;if(!data_||!session){return result;}auto copy=std::make_shared<Data>(*data_);copy->assignmentSession=session;result.data_=std::move(copy);return result;
}
bool PreparedTagRateSource::matchesAssignment(const ProfileLease& lease,const ManualTagStore& store,
    const ManualTagSnapshot& source,std::int32_t tag,std::uint64_t session)const noexcept{
 return session&&data_&&data_->assignmentSession==session&&matches(lease,store,source,tag);
}
bool PreparedTagRateSource::matches(const ProfileLease& lease,const ManualTagStore& store,
    const ManualTagSnapshot& source,std::int32_t tag)const noexcept{
 if(!data_||data_->owner.expired()){return false;}const auto held=data_->lease.lock();const auto ownerStore=data_->store.lock();
 if(!held||!ownerStore||!held->sharesOwnership(lease)||ownerStore!=store.assignmentIdentity_
     ||data_->source.generation()!=source.generation()||!store.matchesSnapshotLineage(source,lease)
     ||!data_->source.sameSource(source)){return false;}
 const auto at=std::ranges::find_if(data_->entries,[&](const auto& entry){return entry.tag==tag;});
 return at!=data_->entries.end()&&at->ready.currentFor(at->uid,at->limits);
}
}
