// Optional source/artifact-qualified original local-ID policy. Included in the
// service's anonymous namespace; no original cache/root writes or owner rights.
class OriginalWireIdentity final {
public:
  static std::expected<std::unique_ptr<OriginalWireIdentity>,storage::Error> create(
      const MainlineDhtOptions& options,const NetworkPolicy& policy,
      const std::vector<Address>& bindings,bool ipv4,bool ipv6,unsigned short port) {
    if(!options.originalIdentity&&!options.originalIdentityLease){return std::unique_ptr<OriginalWireIdentity>{};}
#if BIGLYBTC_HAS_ORIGINAL_MAINLINE_IDENTITY
    if(!options.originalIdentity||!options.originalIdentityLease||!options.restoreIdentity
        ||!policy.mainlineRestoreIdentity||ipv4==ipv6||bindings.size()!=1
        ||bindings[0].is_unspecified()||bindings[0].is_multicast()
        ||(bindings[0].is_v6()&&(bindings[0].to_v6().is_link_local()||bindings[0].to_v6().scope_id()!=0))) {
      return error(Code::Unsupported,"Original Mainline identity requires one explicit enabled bound socket");
    }
    const auto& snapshot=options.originalIdentity->snapshot();
    if(snapshot.state!=storage::OriginalMainlineIdentityState::Imported||!snapshot.root
        ||snapshot.pluginDirectory!=policy.mainlineDirectory){
      return error(Code::Unsupported,"Original Mainline identity source is unavailable or belongs to another directory");
    }
    auto valid=options.originalIdentity->validate(*options.originalIdentityLease);
    if(!valid){return std::unexpected(valid.error());}
    auto pin=detail::mainlineIdentityRuntimePin();
    if(!pin){return error(Code::Unsupported,"Original Mainline identity requires its exact approved provider");}
    auto configure=reinterpret_cast<decltype(&lt::biglybtc_configure_mainline_identity_v1)>(
        ::dlsym(pin.get(),"biglybtc_configure_mainline_identity_v1"));
    auto read=reinterpret_cast<decltype(&lt::biglybtc_read_mainline_identity_v1)>(
        ::dlsym(pin.get(),"biglybtc_read_mainline_identity_v1"));
    auto marker=reinterpret_cast<decltype(&lt::biglybtc_mainline_identity_abi_v1)>(
        ::dlsym(pin.get(),"biglybtc_mainline_identity_abi_v1"));
    if(!configure||!read||!marker||marker()!=1){return error(Code::Unsupported,"Original Mainline identity ABI is unavailable");}
    struct stat profile{};
    if(!options.originalIdentityLease->valid()||::fstat(options.originalIdentityLease->directoryHandle(),&profile)
        ||!S_ISDIR(profile.st_mode)){return error(Code::Conflict,"Original Mainline identity profile lease changed");}
    // One original Node/family scope, slot0 only. Another active service is
    // refused rather than reusing its ID or pretending to support multi-bind.
    using Scope=std::tuple<std::uint64_t,std::uint64_t,std::string,unsigned>;
    static std::mutex mutex;
    static std::map<Scope,std::weak_ptr<int>> reservations;
    std::shared_ptr<int> reservation;
    {
      const std::lock_guard lock(mutex);
      std::erase_if(reservations,[](const auto& row){return row.second.expired();});
      Scope scope{static_cast<std::uint64_t>(profile.st_dev),static_cast<std::uint64_t>(profile.st_ino),
          snapshot.pluginDirectory,ipv4?4U:6U};
      if(reservations.contains(scope)){return error(Code::Busy,"Original Mainline family slot is already retained");}
      if(reservations.size()>=64){return error(Code::Busy,"Original Mainline family reservation bound");}
      reservation=std::make_shared<int>(0);reservations.emplace(std::move(scope),reservation);
    }
    const auto slot=options.originalIdentity->select({});
    if(!slot){return std::unexpected(slot.error());}
    if(slot->index!=0){return error(Code::Unsupported,"Original Mainline identity supports only the first free slot");}
    static std::atomic<std::uint64_t> generations{0};
    auto prior=generations.load(std::memory_order_relaxed);
    do {if(prior==UINT64_MAX){return error(Code::Busy,"Original Mainline identity generation exhausted");}}
    while(!generations.compare_exchange_weak(prior,prior+1,std::memory_order_relaxed));
    auto result=std::unique_ptr<OriginalWireIdentity>(new OriginalWireIdentity);
    result->library_=std::move(pin);result->source_=options.originalIdentity;result->lease_=options.originalIdentityLease;
    result->reservation_=std::move(reservation);result->configure_=configure;result->read_=read;
    result->policy_.version=1;result->policy_.size=sizeof(result->policy_);result->policy_.count=1;
    result->policy_.generation=prior+1;
    auto& binding=result->policy_.bindings[0];binding.family=ipv4?4:6;binding.port=port;binding.slot=slot->index;binding.key=slot->key;
    if(ipv4){auto bytes=bindings[0].to_v4().to_bytes();std::copy(bytes.begin(),bytes.end(),binding.address.begin());}
    else{binding.address=bindings[0].to_v6().to_bytes();}
    result->observer_=std::make_shared<Observer>(result->library_);
    return result;
#else
    (void)policy;(void)bindings;(void)ipv4;(void)ipv6;(void)port;
    return error(Code::Unsupported,"Original Mainline identity requires the explicit reader and provider SDK");
#endif
  }
  std::expected<void,storage::Error> validate() const {
#if BIGLYBTC_HAS_ORIGINAL_MAINLINE_IDENTITY
    return source_->validate(*lease_);
#else
    return error(Code::Unsupported,"Original Mainline identity is unavailable");
#endif
  }
  std::expected<void,storage::Error> install(lt::session& session) {
#if BIGLYBTC_HAS_ORIGINAL_MAINLINE_IDENTITY
    auto valid=validate();if(!valid){return valid;}
    const std::shared_ptr<lt::BiglybtcMainlineIdentityObserverV1> observer=observer_;
    if(configure_(&session,&policy_,&observer)!=0){return error(Code::Unsupported,"Original Mainline identity installation refused");}
    return {};
#else
    (void)session;return error(Code::Unsupported,"Original Mainline identity is unavailable");
#endif
  }
  bool listener(const Address& address,unsigned short port) {
#if BIGLYBTC_HAS_ORIGINAL_MAINLINE_IDENTITY
    const auto& binding=policy_.bindings[0];
    const auto matches=port==binding.port&&address.is_v4()==(binding.family==4)
        &&address==(binding.family==4?Address(lt::address_v4({binding.address[0],binding.address[1],binding.address[2],binding.address[3]}))
            :Address(lt::address_v6(binding.address)));
    listener_=matches;return matches;
#else
    (void)address;(void)port;return false;
#endif
  }
  bool listenerSeen() const noexcept {
#if BIGLYBTC_HAS_ORIGINAL_MAINLINE_IDENTITY
    return listener_;
#else
    return false;
#endif
  }
  bool localId(const lt::sha1_hash& id) const noexcept {
#if BIGLYBTC_HAS_ORIGINAL_MAINLINE_IDENTITY
    return active_&&std::equal(policy_.bindings[0].key.begin(),policy_.bindings[0].key.end(),
        reinterpret_cast<const unsigned char*>(id.data()));
#else
    (void)id;return false;
#endif
  }
  std::expected<void,storage::Error> observe(lt::session* session,MainlineDhtStatus& status,bool joined=false) {
#if BIGLYBTC_HAS_ORIGINAL_MAINLINE_IDENTITY
    if(observer_->lost.load(std::memory_order_acquire)){return error(Code::Io,"Original Mainline identity acknowledgements were lost");}
    auto position=observer_->read.load(std::memory_order_relaxed);
    const auto end=observer_->write.load(std::memory_order_acquire);
    while(position!=end){
      const auto event=observer_->events[position%observer_->events.size()];
      if(sequence_==UINT64_MAX||event.generation!=policy_.generation||event.sequence!=sequence_+1||event.reserved
          ||(event.kind!=1&&event.kind!=2)||!sameBinding(event.binding,policy_.bindings[0])) {
        return error(Code::Io,"Original Mainline identity lifecycle correlation failed");
      }
      sequence_=event.sequence;
      if(event.kind==1){
        if(active_||status.originalIdentityAdoptions){return error(Code::Io,"Original Mainline identity was rebound");}
        active_=true;++status.originalIdentityAdoptions;
      }else{
        if(!active_){return error(Code::Io,"Original Mainline identity retirement lacks adoption");}
        active_=false;++status.originalIdentityRetirements;
        if(!joined){return error(Code::Io,"Original Mainline identity retired before service join");}
      }
      ++position;observer_->read.store(position,std::memory_order_release);
    }
    if(active_&&!joined){
      lt::BiglybtcMainlineIdentityStatusV1 actual{};
      if(!session||read_(session,&actual)!=0||actual.generation!=policy_.generation||actual.phase!=2
          ||actual.expected_mask!=(policy_.bindings[0].family==4?1U:2U)||actual.active_mask!=actual.expected_mask
          ||actual.adopted!=status.originalIdentityAdoptions||actual.retired!=status.originalIdentityRetirements
          ||!sameBinding(actual.bindings[0],policy_.bindings[0])) {
        return error(Code::Io,"Original Mainline identity actual adoption snapshot changed");
      }
    }
    status.originalIdentityActive=active_;
    if(joined){
      status.originalIdentityJoined=true;
      if(active_||status.originalIdentityAdoptions!=status.originalIdentityRetirements){
        return error(Code::Io,"Original Mainline identity retirement was not acknowledged");
      }
      reservation_.reset();source_.reset();lease_.reset();
    }
    return {};
#else
    (void)session;(void)status;(void)joined;return error(Code::Unsupported,"Original Mainline identity is unavailable");
#endif
  }
private:
#if BIGLYBTC_HAS_ORIGINAL_MAINLINE_IDENTITY
  struct Observer final : lt::BiglybtcMainlineIdentityObserverV1 {
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free&&std::atomic<bool>::is_always_lock_free);
    std::shared_ptr<void> library; // Outlives the actual executor's callback.
    std::array<lt::BiglybtcMainlineIdentityEventV1,8> events{};
    std::atomic<std::uint64_t> read{0},write{0};std::atomic<bool> lost{false};
    explicit Observer(std::shared_ptr<void> pin):library(std::move(pin)){}
    bool observe(const lt::BiglybtcMainlineIdentityEventV1& event) noexcept override {
      const auto at=write.load(std::memory_order_relaxed);
      if(at==UINT64_MAX||at-read.load(std::memory_order_acquire)>=events.size()){
        lost.store(true,std::memory_order_release);return false;
      }
      events[at%events.size()]=event;write.store(at+1,std::memory_order_release);return true;
    }
  };
  static bool sameBinding(const lt::BiglybtcMainlineIdentityBindingV1& a,const lt::BiglybtcMainlineIdentityBindingV1& b) {
    return a.family==b.family&&a.slot==b.slot&&a.port==b.port&&a.reserved==b.reserved&&a.address==b.address&&a.key==b.key;
  }
  std::shared_ptr<void> library_;
  std::shared_ptr<const storage::OriginalMainlineIdentity> source_;
  std::shared_ptr<const storage::ProfileLease> lease_;
  std::shared_ptr<int> reservation_;
  decltype(&lt::biglybtc_configure_mainline_identity_v1) configure_=nullptr;
  decltype(&lt::biglybtc_read_mainline_identity_v1) read_=nullptr;
  lt::BiglybtcMainlineIdentityPolicyV1 policy_{};
  std::shared_ptr<Observer> observer_;
  std::uint64_t sequence_=0;bool listener_=false,active_=false;
#endif
};
