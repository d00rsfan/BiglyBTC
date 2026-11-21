from pathlib import Path
import hashlib,json,shutil
D=Path(__file__).resolve().parent;prior=D.parent/'profile-common-tag-owner-20261121';N=D.parents[1].parent/'BiglyBTC/preview/native';O=D/'native-overlay';shutil.copytree(prior/'native-overlay-qualified',O,dirs_exist_ok=True)
BASE=D/'baseline';base={}
for name in ['profile_tag_rates.hpp','profile_tag_rates.cpp']:
 p=BASE/name;base['engine/'+name]=hashlib.sha256(p.read_bytes()).hexdigest()
h=(BASE/'profile_tag_rates.hpp').read_text();needle=' std::expected<void,storage::Error> attach(const core::TorrentHash&,libtorrent::torrent_handle);';assert h.count(needle)==1;h=h.replace(needle,''' // Capture current complete readiness for an already admitted actual handle.
 // No pre-add token,source write,private storage or transport grant follows.
 std::expected<PrivateTagRateSource,storage::Error> refreshPrivateSource(
     const core::TorrentHash&,libtorrent::session_handle,libtorrent::torrent_handle);
'''+needle);(O/'engine/profile_tag_rates.hpp').write_text(h)
s=(BASE/'profile_tag_rates.cpp').read_text();needle=' std::weak_ptr<const void> target;';assert s.count(needle)==1;s=s.replace(needle,needle+'\n std::shared_ptr<const void> liveTarget;')
needle='std::expected<std::unique_ptr<ProfilePreparedTagAttachment>,storage::Error> ProfileTagRates::prepareAttachment('
assert s.count(needle)==1
s=s.replace(needle,'''std::expected<PrivateTagRateSource,storage::Error> ProfileTagRates::refreshPrivateSource(
    const core::TorrentHash& hash,libtorrent::session_handle session,libtorrent::torrent_handle handle){
 auto live=impl_->current();if(!live){return std::unexpected(live.error());}
 if(!impl_->enabled||!SdkSharedQuotaScope::supported()||!impl_->scope){
  return std::unexpected(unavailable("Live private tag sources require the actual common SDK quota runtime"));
 }
 const auto owner=impl_->ownerId(session);const auto sdk=session.native_handle();
 if(!owner||!sdk){return std::unexpected(unavailable("Live private tag source requires an admitted SDK owner"));}
 const auto key=std::pair{*owner,hash.hex()};const auto before=impl_->bindings.find(key);
 if(before==impl_->bindings.end()){return std::unexpected(unavailable("Live private tag source requires its actual binding census"));}
 if(before->second.pending){
  const auto state=before->second.pending;std::lock_guard held(state->mutex);
  if(state->phase!=ProfilePreparedTagAttachment::Impl::Phase::Bound||state->handle!=handle){
   return std::unexpected(unavailable("Unconstructed pending targets cannot refresh private readiness"));
  }
 }else if(before->second.handle!=handle){return std::unexpected(unavailable("Live private tag source belongs to a different handle"));}
 {
  auto gate=impl_->lease->sourceAccess();if(!gate){return std::unexpected(gate.error());}
  if(!gate->observationsAllowed()){return std::unexpected(unavailable("Live private tag source requires an unlocked source turn"));}
 }
 libtorrent::info_hash_t expected;
 try{const auto actual=handle.native_handle();
  if(!actual||&actual->session()!=static_cast<libtorrent::aux::session_interface*>(sdk.get())){
   return std::unexpected(unavailable("Live private tag handle belongs to a different SDK"));
  }
  expected=handle.info_hashes();const auto bytes=hash.bytes();
  if(bytes.size()!=20||!expected.has_v1()||std::memcmp(bytes.data(),expected.v1.data(),20)!=0){
   return std::unexpected(unavailable("Live private tag source requires its original canonical identity"));
  }
 }catch(...){return std::unexpected(unavailable("Live private tag handle has expired"));}
 auto synced=impl_->sync(session);if(!synced){return std::unexpected(synced.error());}
 auto ids=impl_->wanted(hash);if(!ids){return std::unexpected(ids.error());}
 const auto at=impl_->bindings.find(key);
 if(at==impl_->bindings.end()||at->second.pending||at->second.handle!=handle
     ||!handle.is_valid()||at->second.tags!=*ids||!impl_->sourceSnapshot.writable()){
  return std::unexpected(unavailable("Live private tag source requires its current complete binding"));
 }
 for(auto& [id,member]:impl_->owners){(void)id;auto ready=impl_->ensure(*ids,member);
  if(!ready){impl_->dirty=true;impl_->hold();return std::unexpected(ready.error());}
 }
 try{struct Target{libtorrent::torrent_handle handle;std::weak_ptr<libtorrent::aux::session_impl> session;};
  auto target=std::make_shared<const Target>(Target{handle,sdk});
  auto data=std::make_shared<PrivateTagRateSource::Data>(hash);
  data->lease=impl_->lease;data->snapshot=impl_->sourceSnapshot;data->expected=expected;
  data->session=sdk;data->source=impl_->sourceIdentity;data->liveTarget=target;data->target=target;
  data->readTarget=[](const void* raw)noexcept{
   try{const auto& value=*static_cast<const Target*>(raw);const auto sdk=value.session.lock();const auto actual=value.handle.native_handle();
    return sdk&&actual&&value.handle.is_valid()
        && &actual->session()==static_cast<libtorrent::aux::session_interface*>(sdk.get());
   }catch(...){return false;}
  };
  for(auto uid:*ids){const auto limits=impl_->limits.at(uid);PrivateTagRateSource::Data::Entry entry{uid,limits,{}};
   for(const auto& [id,member]:impl_->owners){(void)id;auto witness=member.groups.at(uid)->readiness();
    if(!witness.currentFor(uid,limits)){impl_->hold();return std::unexpected(unavailable("Live private tag SDK cohort is not ready"));}
    entry.cohort.push_back(std::move(witness));
   }data->entries.push_back(std::move(entry));
  }
  if(!data->current()){return std::unexpected(unavailable("Live private tag target lost current readiness"));}
  return PrivateTagRateSource(std::move(data));
 }catch(...){impl_->dirty=true;impl_->hold();return std::unexpected(unavailable("Cannot capture live private tag readiness"));}
}

'''+needle);
h=(O/'engine/profile_tag_rates.hpp').read_text();mark=' std::expected<PrivateTagRateSource,storage::Error> refreshPrivateSource(\n     const core::TorrentHash&,libtorrent::session_handle,libtorrent::torrent_handle);';assert h.count(mark)==1
h=h.replace(mark,mark+'\n // Stable creating-worker ownership,including an expired source epoch.\n bool ownsPrivateSource(const PrivateTagRateSource&)const noexcept;');(O/'engine/profile_tag_rates.hpp').write_text(h)
start=s.index('struct PrivateTagRateSource::Data {');before,body=s[:start],s[start:];mark=' std::weak_ptr<const std::byte> source;';assert body.count(mark)==1;body=body.replace(mark,mark+'\n std::weak_ptr<const std::byte> origin;');s=before+body
mark='  if(lease.expired()||source.expired()||session.expired()';assert s.count(mark)==1;s=s.replace(mark,'  if(lease.expired()||source.expired()||origin.expired()||session.expired()')
mark=' std::shared_ptr<const std::byte> sourceIdentity=std::make_shared<const std::byte>(std::byte{});';assert s.count(mark)==1;s=s.replace(mark,' std::shared_ptr<const std::byte> ownerIdentity=std::make_shared<const std::byte>(std::byte{});\n'+mark)
mark='data->source=impl_->sourceIdentity;';assert s.count(mark)==2;s=s.replace(mark,mark+'data->origin=impl_->ownerIdentity;')
mark='std::expected<PrivateTagRateSource,storage::Error> ProfileTagRates::preparePrivateSource(';assert s.count(mark)==1;s=s.replace(mark,'''bool ProfileTagRates::ownsPrivateSource(const PrivateTagRateSource& source)const noexcept{
 return impl_&&impl_->worker==std::this_thread::get_id()&&impl_->lease&&impl_->ownerIdentity
     &&source.data_&&source.data_->origin.lock()==impl_->ownerIdentity;
}
'''+mark)
mark='impl_->scope.reset();impl_->lease.reset();';assert s.count(mark)==1;s=s.replace(mark,'impl_->scope.reset();impl_->ownerIdentity.reset();impl_->lease.reset();')
(O/'engine/profile_tag_rates.cpp').write_text(s)
record=json.loads((D/'prototype-baseline.json').read_text())if(D/'prototype-baseline.json').exists()else{}
record.update(base_source_sha256=base,previous_verification=str(prior/'verification-inputs.json'));record.setdefault('parent_milestone_pending',True)
(D/'prototype-baseline.json').write_text(json.dumps(record,indent=2)+'\n')
print('Staged current-bound-handle source capture separately;Native sources remain frozen for D10 builds')
