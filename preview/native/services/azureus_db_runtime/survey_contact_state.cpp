// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHTDBImpl.SurveyContactState: Copyright (C) Azureus Software, Inc.
#include "survey_contact_state.hpp"
#include <bit>
#include <new>
namespace biglybtc::services::azureus_db_runtime::survey {
namespace {
std::int64_t wrapAdd(std::int64_t a,std::int64_t b)noexcept{return std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(a)+std::bit_cast<std::uint64_t>(b));}
std::int64_t wrapSub(std::int64_t a,std::int64_t b)noexcept{return std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(a)-std::bit_cast<std::uint64_t>(b));}
}
ContactState::ContactState(std::int32_t instance,std::int64_t now,std::int64_t jitter,std::size_t maximum):instance_(instance),created_(now),deadline_(wrapAdd(wrapAdd(now,12600000),jitter)),lastUse_(now),maximum_(maximum){}
std::expected<ContactState,StateError> ContactState::create(std::int32_t instance,std::int64_t now,std::int64_t jitter,std::size_t maximum){
 if(jitter<0||jitter>=3600000||maximum==0||maximum>8192){return std::unexpected(StateError::Input);}return ContactState(instance,now,jitter,maximum);
}
std::expected<bool,StateError> ContactState::add(MappingId id){
 if(id==0){return std::unexpected(StateError::Input);}if(known_.contains(id)){return false;}if(known_.size()>=maximum_){return std::unexpected(StateError::Capacity);}
 try{return known_.insert(id).second;}catch(const std::bad_alloc&){return std::unexpected(StateError::Memory);}
}
void ContactState::updateInstance(std::int32_t instance)noexcept{if(instance_!=instance){known_.clear();}instance_=instance;}
void ContactState::failure()noexcept{failures_=std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(failures_)+1);if(failures_>=2){known_.clear();}}
bool ContactState::expired(std::int64_t now)const noexcept{return wrapSub(now,lastUse_)>3600000||now>deadline_;}
}
