std::expected<PrivateTagRateSource,storage::Error> ProfileTagRates::preparePrivateSource(
    const core::TorrentHash& hash,libtorrent::session_handle session,const ProfilePreparedTagAttachment& pending){
 auto live=impl_->current();if(!live){return std::unexpected(live.error());}
 if(!impl_->enabled||!SdkSharedQuotaScope::supported()||!pending.impl_){
  return std::unexpected(unavailable("Private tag sources require the actual common SDK quota runtime"));
 }
 const auto state=pending.impl_;const auto owner=impl_->ownerId(session);
 if(!owner||!impl_->scope||state->session.lock()!=session.native_handle()
     ||state->source.lock()!=impl_->sourceIdentity||!state->current()){
  return std::unexpected(unavailable("Private tag source requires its actual current SDK target"));
 }
 const auto reserved=impl_->bindings.find(std::pair{*owner,hash.hex()});
 if(reserved==impl_->bindings.end()||reserved->second.pending!=state){
  return std::unexpected(unavailable("Private tag source belongs to a different target reservation"));
 }
 {
  auto gate=impl_->lease->sourceAccess();if(!gate){return std::unexpected(gate.error());}
  if(!gate->observationsAllowed()){return std::unexpected(unavailable("Private tag source preparation requires an unlocked source turn"));}
 }
 auto synced=impl_->sync(session);if(!synced){return std::unexpected(synced.error());}
 auto ids=impl_->wanted(hash);if(!ids){return std::unexpected(ids.error());}
 if(!impl_->sourceSnapshot.writable()||!state->current()
     ||state->source.lock()!=impl_->sourceIdentity||state->tags!=*ids){
  return std::unexpected(unavailable("Private tag source requires its current complete pending target"));
 }
 const auto key=std::pair{*owner,hash.hex()};const auto at=impl_->bindings.find(key);
 if(at==impl_->bindings.end()||at->second.pending!=state){return std::unexpected(unavailable("Private tag source target reservation changed"));}
 for(auto& [id,member]:impl_->owners){(void)id;auto ready=impl_->ensure(*ids,member);
  if(!ready){impl_->dirty=true;impl_->hold();return std::unexpected(ready.error());}
 }
 try{auto data=std::make_shared<PrivateTagRateSource::Data>(hash);
  data->lease=impl_->lease;data->snapshot=impl_->sourceSnapshot;data->expected=state->expected;
  data->session=session.native_handle();data->source=impl_->sourceIdentity;data->target=state;
  data->readTarget=[](const void* raw)noexcept{
   try{const auto& target=*static_cast<const ProfilePreparedTagAttachment::Impl*>(raw);
    if(!target.current()){return false;}std::lock_guard held(target.mutex);
    if(target.phase==ProfilePreparedTagAttachment::Impl::Phase::Fresh||target.phase==ProfilePreparedTagAttachment::Impl::Phase::Installed){return true;}
    return target.phase==ProfilePreparedTagAttachment::Impl::Phase::Bound&&target.handle.is_valid()
        &&std::ranges::all_of(target.tokens,[&](const auto& token){return token->boundTo(target.handle);});
   }catch(...){return false;}
  };
  for(auto uid:*ids){const auto limits=impl_->limits.at(uid);PrivateTagRateSource::Data::Entry entry{uid,limits,{}};
   for(const auto& [id,member]:impl_->owners){(void)id;auto witness=member.groups.at(uid)->readiness();
    if(!witness.currentFor(uid,limits)){impl_->hold();return std::unexpected(unavailable("Private tag source SDK cohort is not ready"));}
    entry.cohort.push_back(std::move(witness));
   }data->entries.push_back(std::move(entry));
  }
  if(!data->current()){return std::unexpected(unavailable("Private tag source lost current target readiness"));}
  return PrivateTagRateSource(std::move(data));
 }catch(...){impl_->dirty=true;impl_->hold();return std::unexpected(unavailable("Cannot capture private tag source readiness"));}
}
