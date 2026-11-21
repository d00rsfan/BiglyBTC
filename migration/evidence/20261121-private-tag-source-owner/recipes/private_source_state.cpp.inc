struct PrivateTagRateSource::Data {
 struct Entry {std::uint64_t uid;core::SavedTagRateLimits limits;std::vector<core::TagRateReadiness> cohort;};
 std::weak_ptr<const storage::ProfileLease> lease;
 storage::ManualTagSnapshot snapshot;core::TorrentHash hash;
 libtorrent::info_hash_t expected;
 std::weak_ptr<libtorrent::aux::session_impl> session;
 std::weak_ptr<const std::byte> source;
 std::weak_ptr<const void> target;
 bool (*readTarget)(const void*)noexcept=nullptr;
 std::vector<Entry> entries;
 explicit Data(core::TorrentHash value):hash(std::move(value)){}
 bool current()const noexcept{
  const auto held=target.lock();
  if(lease.expired()||source.expired()||session.expired()||!held||!readTarget||!readTarget(held.get())){return false;}
  for(const auto& entry:entries){if(entry.cohort.empty()){return false;}
   for(const auto& witness:entry.cohort){if(!witness.currentFor(entry.uid,entry.limits)){return false;}}
  }return true;
 }
};
PrivateTagRateSource::PrivateTagRateSource(std::shared_ptr<const Data> value):data_(std::move(value)){}
bool PrivateTagRateSource::current()const noexcept{return data_&&data_->current();}
bool PrivateTagRateSource::matches(const storage::ProfileLease& lease,const core::TorrentHash& hash,
    const storage::ManualTagSnapshot& snapshot,const libtorrent::info_hash_t& expected,
    const libtorrent::session_handle& session)const noexcept{
 try{if(!current()||hash!=data_->hash||expected!=data_->expected||!snapshot.writable()
      ||!data_->snapshot.sameSource(snapshot)){return false;}
  const auto owner=data_->lease.lock();const auto actual=data_->session.lock();
  return owner&&owner->sharesOwnership(lease)&&actual&&actual==session.native_handle();
 }catch(...){return false;}
}
