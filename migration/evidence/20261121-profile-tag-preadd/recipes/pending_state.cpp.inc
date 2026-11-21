struct ProfilePreparedTagAttachment::Impl {
 enum class Phase{Fresh,Installed,Bound,Cancelled};
 std::weak_ptr<const std::byte> source;
 std::weak_ptr<libtorrent::aux::session_impl> session;
 libtorrent::info_hash_t expected;
 std::vector<std::uint64_t> tags;
 std::vector<std::unique_ptr<SdkPreparedTagAttachment>> tokens;
 mutable std::mutex mutex;Phase phase=Phase::Fresh;std::atomic_bool cancelled=false;
 libtorrent::torrent_handle handle;
 bool current()const noexcept{
  if(cancelled.load()||source.expired()||session.expired()){return false;}
  return std::ranges::all_of(tokens,[](const auto& token){return token->current();});
 }
 bool owns(libtorrent::torrent_handle target)const{
  const auto sdk=session.lock();const auto actual=target.native_handle();
  return sdk&&actual&& &actual->session()==static_cast<libtorrent::aux::session_interface*>(sdk.get());
 }
 void cancel(){cancelled.store(true);phase=Phase::Cancelled;}
};
ProfilePreparedTagAttachment::ProfilePreparedTagAttachment(std::shared_ptr<Impl> value):impl_(std::move(value)){}
ProfilePreparedTagAttachment::~ProfilePreparedTagAttachment(){
 if(!impl_){return;}
 try{std::lock_guard held(impl_->mutex);
  // An abandoned preparation must release its reservation on the next sync.
  // A completed constructor still belongs to the controller's binding census.
  if(impl_->phase!=Impl::Phase::Bound){impl_->cancel();}
 }catch(...){impl_->cancelled.store(true);}
}
bool ProfilePreparedTagAttachment::current()const noexcept{return impl_&&impl_->current();}
bool ProfilePreparedTagAttachment::boundTo(libtorrent::torrent_handle target)const noexcept{
 try{if(!current()){return false;}std::lock_guard held(impl_->mutex);
  return impl_->phase==Impl::Phase::Bound&&impl_->handle==target&&target.is_valid()
      &&std::ranges::all_of(impl_->tokens,[&](const auto& token){return token->boundTo(target);});
 }catch(...){return false;}
}
std::expected<void,storage::Error> ProfilePreparedTagAttachment::install(libtorrent::add_torrent_params& params){
 if(!current()){return std::unexpected(unavailable("Pending tag target source/cohort is no longer current"));}
 std::lock_guard held(impl_->mutex);
 if(impl_->phase!=Impl::Phase::Fresh||!(params.flags&libtorrent::torrent_flags::paused)
     ||(params.flags&libtorrent::torrent_flags::auto_managed)
     ||(params.ti?params.ti->info_hashes():params.info_hashes)!=impl_->expected){
  return std::unexpected(unavailable("Pending tag target requires its exact paused add"));
 }
 try{auto staged=params;
  for(auto& token:impl_->tokens){if(!token->install(staged)){impl_->cancel();return std::unexpected(unavailable("Cannot install the complete pending tag target"));}}
  staged.extensions.emplace_back([state=impl_](libtorrent::torrent_handle const& target,libtorrent::client_data_t)->std::shared_ptr<libtorrent::torrent_plugin>{
   std::lock_guard held(state->mutex);
   if(state->phase!=Impl::Phase::Installed||!state->current()||!state->owns(target)
       ||target.info_hashes()!=state->expected
       ||!std::ranges::all_of(state->tokens,[&](const auto& token){return token->boundTo(target);})){state->cancel();return {};}
   state->handle=target;state->phase=Impl::Phase::Bound;return {};
  });
  staged.flags|=libtorrent::torrent_flags::duplicate_is_error;
  params=std::move(staged);impl_->phase=Impl::Phase::Installed;return {};
 }catch(...){impl_->cancel();return std::unexpected(unavailable("Cannot retain pending tag target parameters"));}
}
