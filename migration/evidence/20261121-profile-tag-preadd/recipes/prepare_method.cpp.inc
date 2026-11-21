std::expected<std::unique_ptr<ProfilePreparedTagAttachment>,storage::Error> ProfileTagRates::prepareAttachment(
    const core::TorrentHash& hash,const libtorrent::info_hash_t& expected,libtorrent::session_handle session){
 auto live=impl_->current();if(!live){return std::unexpected(live.error());}
 {
  auto gate=impl_->lease->sourceAccess();if(!gate){return std::unexpected(gate.error());}
  if(!gate->observationsAllowed()){return std::unexpected(unavailable("Pending tag preparation requires an unlocked source turn"));}
 }
 const auto bytes=hash.bytes();
 const bool same=bytes.size()==20?expected.has_v1()&&std::memcmp(bytes.data(),expected.v1.data(),20)==0:
     bytes.size()==32&&expected.has_v2()&&std::memcmp(bytes.data(),expected.v2.data(),32)==0;
 if(!same){return std::unexpected(unavailable("Pending tag target canonical and full SDK identity differ"));}
 auto synced=impl_->sync(session);if(!synced){return std::unexpected(synced.error());}
 auto ids=impl_->wanted(hash);if(!ids){return std::unexpected(ids.error());}
 const auto owner=*impl_->ownerId(session);const auto key=std::pair{owner,hash.hex()};
 if(impl_->bindings.contains(key)){return std::unexpected(unavailable("Duplicate pending saved tag target"));}
 auto ready=impl_->ensure(*ids,impl_->owners.at(owner));if(!ready){impl_->dirty=true;impl_->hold();return std::unexpected(ready.error());}
 try{auto state=std::make_shared<ProfilePreparedTagAttachment::Impl>();state->source=impl_->sourceIdentity;
  state->session=session.native_handle();state->expected=expected;state->tags=*ids;
  for(auto id:*ids){auto token=impl_->owners.at(owner).groups.at(id)->prepareAttachment(expected);
   if(!token){impl_->dirty=true;impl_->hold();return std::unexpected(unavailable("Cannot prepare the actual pending tag class"));}
   state->tokens.push_back(std::move(*token));
  }
  if(!state->current()){return std::unexpected(unavailable("Pending tag target lost current SDK/source"));}
  auto result=std::unique_ptr<ProfilePreparedTagAttachment>(new ProfilePreparedTagAttachment(state));
  impl_->bindings.emplace(key,Impl::Binding{hash,{},*ids,state});return result;
 }catch(...){impl_->dirty=true;impl_->hold();return std::unexpected(unavailable("Cannot retain the pending saved tag target"));}
}
