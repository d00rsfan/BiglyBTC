// SPDX-License-Identifier: GPL-2.0-or-later
#include "internal_tag_namespace.hpp"
#include <biglybtc/stock_tag_names.hpp>
#include <limits>
#include <new>
namespace biglybtc::app {
namespace it=core::tags::internal;namespace c=core::tags::constraints;namespace r=core::tags::runtime;
struct InternalTagNamespaceState {
 std::thread::id thread=std::this_thread::get_id();
 std::shared_ptr<const storage::ProfileLease> lease;
 std::unique_ptr<it::Owner> owner;
 std::optional<std::string> locale;
 std::uint64_t session=0,generation=1;bool closed=false;
};
struct InternalTagSnapshot::Data {
 std::weak_ptr<InternalTagNamespaceState> source;std::thread::id thread;
 std::uint64_t session=0,generation=0;it::Snapshot roster;std::optional<std::string> locale;
};
namespace {
storage::Error error(storage::ErrorCode code=storage::ErrorCode::Conflict){return {code,"Internal tag namespace is unavailable"};}
std::optional<storage::Error> mutableState(const std::shared_ptr<InternalTagNamespaceState>& state){
 if(!state||state->thread!=std::this_thread::get_id()||state->closed||!state->owner||!state->lease||!state->lease->valid()){return error();}
 if(state->generation==std::numeric_limits<std::uint64_t>::max()){return error(storage::ErrorCode::Unsupported);}return {};
}
}
bool InternalTagSnapshot::live(std::uint64_t session)const noexcept{
 if(!data_||data_->thread!=std::this_thread::get_id()||data_->session!=session){return false;}
 auto source=data_->source.lock();return source&&!source->closed&&source->owner&&source->lease&&source->lease->valid()&&source->session==session&&source->generation==data_->generation;
}
bool InternalTagSnapshot::current(const storage::ProfileLease& lease,std::uint64_t session)const noexcept{
 if(!data_||data_->thread!=std::this_thread::get_id()||data_->session!=session){return false;}auto source=data_->source.lock();return source&&!source->closed&&source->owner&&source->lease&&source->lease->sharesOwnership(lease)&&source->session==session&&source->generation==data_->generation;
}
bool InternalTagSnapshot::sameRevision(const InternalTagSnapshot& other)const noexcept{
 if(!data_||!other.data_||data_->thread!=std::this_thread::get_id()||other.data_->thread!=std::this_thread::get_id()){return false;}auto source=data_->source.lock();auto otherSource=other.data_->source.lock();return source&&source==otherSource&&!source->closed&&source->generation==data_->generation&&data_->session==other.data_->session&&data_->generation==other.data_->generation;
}
c::Result<r::Type> InternalTagSnapshot::project(std::uint64_t session)const{
 if(!data_||data_->thread!=std::this_thread::get_id()){return std::unexpected(c::Problem{c::ProblemKind::Unavailable,"Internal tag namespace source is stale"});}auto source=data_->source.lock();if(!source||!source->lease){return std::unexpected(c::Problem{c::ProblemKind::Unavailable,"Internal tag namespace source is released"});}return project(*source->lease,session);
}
c::Result<r::Type> InternalTagSnapshot::project(const storage::ProfileLease& lease,std::uint64_t session)const{
 try{
  if(!current(lease,session)){return std::unexpected(c::Problem{c::ProblemKind::Unavailable,"Internal tag namespace source is stale"});}
  r::Type type;type.id=5;type.complete=true;type.tags.reserve(data_->roster.entries.size());
  for(const auto& entry:data_->roster.entries){r::Tag tag;tag.id=entry.id;tag.taggableTypes=2;tag.membership=r::Membership::Unknown;tag.groupKnown=false;
   if(!entry.rawName.starts_with(u"tag.")){tag.name=entry.rawName;}else if(data_->locale){if(auto name=core::tags::stockTagName(*data_->locale,entry.rawName)){tag.name=r::Text(*name);}}
   type.tags.push_back(std::move(tag));}
  return type;
 }catch(const std::bad_alloc&){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Internal tag namespace projection resources unavailable"});}
}
std::expected<std::unique_ptr<InternalTagNamespace>,storage::Error> InternalTagNamespace::activate(
 const storage::ProfileLease& lease,std::uint64_t session,bool normal,const storage::TagManagerStartupMode& mode,it::Limits limits){
 try{if(!session||!lease.valid()||!mode.matches(lease)){return std::unexpected(error());}auto state=std::make_shared<InternalTagNamespaceState>();state->session=session;state->locale=mode.stockTagLocale();
  if(normal&&mode.enabled()){auto held=lease.retain();if(!held){return std::unexpected(held.error());}auto owner=it::Owner::create(limits);if(!owner){return std::unexpected(error(storage::ErrorCode::Io));}state->lease=std::move(*held);state->owner=std::move(*owner);}
  return std::unique_ptr<InternalTagNamespace>(new InternalTagNamespace(std::move(state)));
 }catch(const std::bad_alloc&){return std::unexpected(error(storage::ErrorCode::Io));}
}
std::expected<std::optional<InternalTagSnapshot>,storage::Error> InternalTagNamespace::snapshot()const{
 try{if(!state_||state_->thread!=std::this_thread::get_id()||state_->closed){return std::unexpected(error());}if(!state_->owner){return std::optional<InternalTagSnapshot>{};}if(auto failed=mutableState(state_)){return std::unexpected(*failed);}auto roster=state_->owner->snapshot();if(!roster){return std::unexpected(error());}auto data=std::make_shared<InternalTagSnapshot::Data>();data->source=state_;data->thread=state_->thread;data->session=state_->session;data->generation=state_->generation;data->roster=std::move(*roster);data->locale=state_->locale;return std::optional{InternalTagSnapshot(std::move(data))};}catch(const std::bad_alloc&){return std::unexpected(error(storage::ErrorCode::Io));}
}
std::expected<InternalTagNamespace::Token,storage::Error> InternalTagNamespace::createTag(std::u16string_view name,bool autoAdd){if(auto failed=mutableState(state_)){return std::unexpected(*failed);}auto value=state_->owner->createTag(name,autoAdd);if(!value){return std::unexpected(error());}++state_->generation;return std::move(*value);}
std::expected<void,storage::Error> InternalTagNamespace::attach(const Token& token){if(auto failed=mutableState(state_)){return std::unexpected(*failed);}auto value=state_->owner->attach(token);if(!value){return std::unexpected(error());}++state_->generation;return {};}
std::expected<void,storage::Error> InternalTagNamespace::rename(const Token& token,std::u16string_view name){if(auto failed=mutableState(state_)){return std::unexpected(*failed);}auto value=state_->owner->rename(token,name);if(!value){return std::unexpected(error());}++state_->generation;return {};}
std::expected<void,storage::Error> InternalTagNamespace::remove(const Token& token){if(auto failed=mutableState(state_)){return std::unexpected(*failed);}auto value=state_->owner->remove(token);if(!value){return std::unexpected(error());}++state_->generation;return {};}
std::expected<void,storage::Error> InternalTagNamespace::close(){if(!state_||state_->thread!=std::this_thread::get_id()){return std::unexpected(error());}if(state_->closed){return {};}if(state_->owner){auto closed=state_->owner->close();if(!closed){return std::unexpected(error());}state_->owner.reset();}state_->closed=true;state_->lease.reset();return {};}
}
