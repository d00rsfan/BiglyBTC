// Exact-source optional original local-ID policy. One actual socket per enabled
// family; original Node/family reservations remain held until the actual join.
class OriginalWireIdentity final {
public:
  using RoutingNodes=std::vector<std::pair<lt::sha1_hash,UdpEndpoint>>;
  struct Routing {lt::sha1_hash local;RoutingNodes nodes;bool matched=false;};
  static std::expected<std::unique_ptr<OriginalWireIdentity>,storage::Error> create(
      const MainlineDhtOptions& options,const NetworkPolicy& policy,
      const std::vector<Address>& bindings,bool ipv4,bool ipv6,unsigned short port) {
    if(!options.originalIdentity&&!options.originalIdentityLease){return std::unique_ptr<OriginalWireIdentity>{};}
#if BIGLYBTC_HAS_ORIGINAL_MAINLINE_IDENTITY
    if(!options.originalIdentity||!options.originalIdentityLease||!options.restoreIdentity
        ||!policy.mainlineRestoreIdentity||(!ipv4&&!ipv6)||bindings.size()!=std::size_t(ipv4)+std::size_t(ipv6)) {
      return error(Code::Unsupported,"Original Mainline identity requires one explicit socket per enabled family");
    }
    unsigned families=0;
    for(const auto& address:bindings){
      const auto bit=address.is_v4()?1U:2U;
      if((families&bit)||address.is_unspecified()||address.is_multicast()
          ||(address.is_v4()&&!ipv4)||(address.is_v6()&&(!ipv6||address.to_v6().is_link_local()||address.to_v6().scope_id()!=0))){
        return error(Code::Unsupported,"Original Mainline identity does not support ambiguous family bindings");
      }
      families|=bit;
    }
    const auto& snapshot=options.originalIdentity->snapshot();
    if(snapshot.state!=storage::OriginalMainlineIdentityState::Imported||!snapshot.root
        ||snapshot.pluginDirectory!=policy.mainlineDirectory){
      return error(Code::Unsupported,"Original Mainline identity source is unavailable or belongs to another directory");
    }
    auto valid=options.originalIdentity->validate(*options.originalIdentityLease);
    if(!valid){return std::unexpected(valid.error());}
#if BIGLYBTC_HAS_ORIGINAL_MAINLINE_ROUTING
    auto pin=detail::mainlineRoutingRuntimePin();
#else
    if(bindings.size()!=1){return error(Code::Unsupported,"Dual original Mainline identities require the exact socket-qualified provider SDK");}
    auto pin=detail::mainlineIdentityRuntimePin(); // Preserve the qualified SDK25 single-family path.
#endif
    if(!pin){return error(Code::Unsupported,"Original Mainline identity requires its exact approved provider");}
    auto configure=reinterpret_cast<decltype(&lt::biglybtc_configure_mainline_identity_v1)>(
        ::dlsym(pin.get(),"biglybtc_configure_mainline_identity_v1"));
    auto read=reinterpret_cast<decltype(&lt::biglybtc_read_mainline_identity_v1)>(
        ::dlsym(pin.get(),"biglybtc_read_mainline_identity_v1"));
#if BIGLYBTC_HAS_ORIGINAL_MAINLINE_ROUTING
    auto query=reinterpret_cast<decltype(&lt::biglybtc_query_mainline_routing_v1)>(
        ::dlsym(pin.get(),"biglybtc_query_mainline_routing_v1"));
    auto marker=reinterpret_cast<decltype(&lt::biglybtc_mainline_routing_abi_v1)>(
        ::dlsym(pin.get(),"biglybtc_mainline_routing_abi_v1"));
    if(!configure||!read||!query||!marker||marker()!=1){return error(Code::Unsupported,"Original Mainline socket routing ABI is unavailable");}
#else
    auto marker=reinterpret_cast<decltype(&lt::biglybtc_mainline_identity_abi_v1)>(::dlsym(pin.get(),"biglybtc_mainline_identity_abi_v1"));
    if(!configure||!read||!marker||marker()!=1){return error(Code::Unsupported,"Original Mainline identity ABI is unavailable");}
#endif
    struct stat profile{};
    if(!options.originalIdentityLease->valid()||::fstat(options.originalIdentityLease->directoryHandle(),&profile)
        ||!S_ISDIR(profile.st_mode)){return error(Code::Conflict,"Original Mainline identity profile lease changed");}
    using Scope=std::tuple<std::uint64_t,std::uint64_t,std::string,unsigned>;
    static std::mutex mutex;static std::map<Scope,std::weak_ptr<int>> reservations;
    std::vector<std::shared_ptr<int>> retained;
    {
      const std::lock_guard lock(mutex);
      std::erase_if(reservations,[](const auto& row){return row.second.expired();});
      std::vector<Scope> requested;
      for(const auto& address:bindings){
        requested.emplace_back(static_cast<std::uint64_t>(profile.st_dev),static_cast<std::uint64_t>(profile.st_ino),
            snapshot.pluginDirectory,address.is_v4()?4U:6U);
        if(reservations.contains(requested.back())){return error(Code::Busy,"Original Mainline family slot is already retained");}
      }
      if(requested.size()>64-reservations.size()){return error(Code::Busy,"Original Mainline family reservation bound");}
      // Check both scopes before reserving either. Failure never leaks a partial
      // reservation; weak entries expire with the local tokens on exception.
      for(auto& scope:requested){auto token=std::make_shared<int>(0);reservations.emplace(std::move(scope),token);retained.push_back(std::move(token));}
    }
    static std::atomic<std::uint64_t> generations{0};auto prior=generations.load(std::memory_order_relaxed);
    do {if(prior==UINT64_MAX){return error(Code::Busy,"Original Mainline identity generation exhausted");}}
    while(!generations.compare_exchange_weak(prior,prior+1,std::memory_order_relaxed));
    auto result=std::unique_ptr<OriginalWireIdentity>(new OriginalWireIdentity);
    result->library_=std::move(pin);result->source_=options.originalIdentity;result->lease_=options.originalIdentityLease;
    result->reservations_=std::move(retained);result->configure_=configure;result->read_=read;
#if BIGLYBTC_HAS_ORIGINAL_MAINLINE_ROUTING
    result->query_=query;
#endif
    result->policy_.version=1;result->policy_.size=sizeof(result->policy_);result->policy_.count=static_cast<unsigned>(bindings.size());
    result->policy_.generation=prior+1;result->expected_=families;
    for(unsigned i=0;i<bindings.size();++i){
      // Each original Node has its OWN first-free set. Slot0 may deliberately
      // have the SAME arbitrary/zero ID in the two different families.
      const auto slot=options.originalIdentity->select({});if(!slot){return std::unexpected(slot.error());}
      if(slot->index!=0){return error(Code::Unsupported,"Original Mainline identity supports only the first free family slot");}
      auto& binding=result->policy_.bindings[i];binding.family=bindings[i].is_v4()?4:6;binding.port=port;binding.slot=0;binding.key=slot->key;
      if(bindings[i].is_v4()){auto bytes=bindings[i].to_v4().to_bytes();std::copy(bytes.begin(),bytes.end(),binding.address.begin());}
      else{binding.address=bindings[i].to_v6().to_bytes();}
    }
    result->observer_=std::make_shared<Observer>(result->library_);return result;
#else
    (void)policy;(void)bindings;(void)ipv4;(void)ipv6;(void)port;
    return error(Code::Unsupported,"Original Mainline identity requires the explicit reader and socket-qualified provider SDK");
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
    for(unsigned i=0;i<policy_.count;++i){const auto& binding=policy_.bindings[i];
      if(port==binding.port&&address==bindingAddress(binding)){listeners_|=binding.family==4?1U:2U;return true;}}
    return false;
#else
    (void)address;(void)port;return false;
#endif
  }
  bool listenerSeen() const noexcept {
#if BIGLYBTC_HAS_ORIGINAL_MAINLINE_IDENTITY
    return listeners_==expected_;
#else
    return false;
#endif
  }
  bool localId(const lt::sha1_hash& id) const noexcept {
#if BIGLYBTC_HAS_ORIGINAL_MAINLINE_IDENTITY
    for(unsigned i=0;i<policy_.count;++i){if((active_&(policy_.bindings[i].family==4?1U:2U))
        &&std::equal(policy_.bindings[i].key.begin(),policy_.bindings[i].key.end(),reinterpret_cast<const unsigned char*>(id.data()))){return true;}}
    return false;
#else
    (void)id;return false;
#endif
  }
  bool socketRouting() const noexcept {return BIGLYBTC_HAS_ORIGINAL_MAINLINE_ROUTING!=0;}
  std::expected<Routing,storage::Error> routing(lt::session& session,bool v4,
      std::optional<std::pair<lt::sha1_hash,UdpEndpoint>> remote={}) {
#if BIGLYBTC_HAS_ORIGINAL_MAINLINE_IDENTITY && BIGLYBTC_HAS_ORIGINAL_MAINLINE_ROUTING
    auto fresh=validate();if(!fresh){return std::unexpected(fresh.error());}
    if(active_!=expected_||queryCookie_==UINT64_MAX){return error(Code::Busy,"Original Mainline socket query is unavailable");}
    const auto found=std::ranges::find_if(policy_.bindings.begin(),policy_.bindings.begin()+policy_.count,
        [&](const auto& binding){return (binding.family==4)==v4;});
    if(found==policy_.bindings.begin()+policy_.count){return error(Code::Unsupported,"Original Mainline socket family is absent");}
    lt::BiglybtcMainlineRoutingQueryV1 request{};request.version=1;request.size=sizeof(request);request.mode=remote?2:1;
    request.generation=policy_.generation;request.cookie=++queryCookie_;request.local=*found;
    if(remote){
      request.remote.id=bytes(remote->first);request.remote.port=remote->second.port();request.remote.family=remote->second.address().is_v4()?4:6;
      if(remote->second.address().is_v4()){auto a=remote->second.address().to_v4().to_bytes();std::copy(a.begin(),a.end(),request.remote.address.begin());}
      else{request.remote.address=remote->second.address().to_v6().to_bytes();}
    }
    lt::BiglybtcMainlineRoutingAckV1 ack{};
    // Synchronous LT-executor submission is AFTER the direct response alert;
    // rpc_manager's subsequent node_seen() therefore precedes this live read.
    if(query_(&session,&request,&ack)!=0||ack.result||ack.generation!=request.generation||ack.cookie!=request.cookie
        ||!sameBinding(ack.local,*found)||ack.overflow||ack.count>ack.nodes.size()||ack.matched>1
        ||(remote&&ack.count)||(!remote&&ack.matched)){
      return error(Code::Io,"Original Mainline actual socket routing proof failed");
    }
    Routing result;result.local=lt::sha1_hash(reinterpret_cast<const char*>(ack.local.key.data()));result.matched=ack.matched==1;
    for(unsigned i=0;i<ack.count;++i){const auto& node=ack.nodes[i];
      if(node.family!=ack.local.family||!node.port){return error(Code::Io,"Original Mainline live routing family changed");}
      Address address;
      if(node.family==4){if(std::any_of(node.address.begin()+4,node.address.end(),[](auto b){return b!=0;})){return error(Code::Io,"Original Mainline live IPv4 encoding changed");}
        address=lt::address_v4({node.address[0],node.address[1],node.address[2],node.address[3]});}
      else{address=lt::address_v6(node.address);}
      result.nodes.emplace_back(lt::sha1_hash(reinterpret_cast<const char*>(node.id.data())),UdpEndpoint(address,node.port));
    }
    return result;
#else
    (void)session;(void)v4;(void)remote;return error(Code::Unsupported,"Original Mainline socket routing proof is unavailable");
#endif
  }
  std::expected<void,storage::Error> observe(lt::session* session,MainlineDhtStatus& status,bool joined=false) {
#if BIGLYBTC_HAS_ORIGINAL_MAINLINE_IDENTITY
    if(observer_->lost.load(std::memory_order_acquire)){return error(Code::Io,"Original Mainline identity acknowledgements were lost");}
    auto position=observer_->read.load(std::memory_order_relaxed);const auto end=observer_->write.load(std::memory_order_acquire);
    while(position!=end){const auto event=observer_->events[position%observer_->events.size()];
      const auto binding=std::find_if(policy_.bindings.begin(),policy_.bindings.begin()+policy_.count,[&](const auto& b){return sameBinding(event.binding,b);});
      if(sequence_==UINT64_MAX||event.generation!=policy_.generation||event.sequence!=sequence_+1||event.reserved
          ||(event.kind!=1&&event.kind!=2)||binding==policy_.bindings.begin()+policy_.count){return error(Code::Io,"Original Mainline identity lifecycle correlation failed");}
      sequence_=event.sequence;const auto bit=binding->family==4?1U:2U;
      if(event.kind==1){
        if(adopted_&bit){return error(Code::Io,"Original Mainline identity was rebound");}
        adopted_|=bit;active_|=bit;++status.originalIdentityAdoptions;
      }else{
        if(!(active_&bit)){return error(Code::Io,"Original Mainline identity retirement lacks adoption");}
        active_&=~bit;++status.originalIdentityRetirements;
        if(!joined){return error(Code::Io,"Original Mainline identity retired before service join");}
      }
      ++position;observer_->read.store(position,std::memory_order_release);
    }
    if(active_==expected_&&!joined){
      lt::BiglybtcMainlineIdentityStatusV1 actual{};
      if(!session||read_(session,&actual)!=0||actual.generation!=policy_.generation||actual.phase!=2
          ||actual.expected_mask!=expected_||actual.active_mask!=expected_
          ||actual.adopted!=status.originalIdentityAdoptions||actual.retired!=status.originalIdentityRetirements){
        return error(Code::Io,"Original Mainline identity actual adoption snapshot changed");}
      for(unsigned i=0;i<policy_.count;++i){if(!sameBinding(actual.bindings[i],policy_.bindings[i])){return error(Code::Io,"Original Mainline identity actual socket changed");}}
    }
    status.originalIdentityActive=active_==expected_;
    if(joined){
      status.originalIdentityJoined=true;
      if(active_||status.originalIdentityAdoptions!=status.originalIdentityRetirements){return error(Code::Io,"Original Mainline identity retirement was not acknowledged");}
      reservations_.clear();source_.reset();lease_.reset();
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
    std::shared_ptr<void> library;
    std::array<lt::BiglybtcMainlineIdentityEventV1,8> events{};
    std::atomic<std::uint64_t> read{0},write{0};std::atomic<bool> lost{false};
    explicit Observer(std::shared_ptr<void> pin):library(std::move(pin)){}
    bool observe(const lt::BiglybtcMainlineIdentityEventV1& event) noexcept override {
      const auto at=write.load(std::memory_order_relaxed);
      if(at==UINT64_MAX||at-read.load(std::memory_order_acquire)>=events.size()){lost.store(true,std::memory_order_release);return false;}
      events[at%events.size()]=event;write.store(at+1,std::memory_order_release);return true;
    }
  };
  static bool sameBinding(const lt::BiglybtcMainlineIdentityBindingV1& a,const lt::BiglybtcMainlineIdentityBindingV1& b) {
    return a.family==b.family&&a.slot==b.slot&&a.port==b.port&&a.reserved==b.reserved&&a.address==b.address&&a.key==b.key;
  }
  static Address bindingAddress(const lt::BiglybtcMainlineIdentityBindingV1& binding){
    if(binding.family==4){return lt::address_v4({binding.address[0],binding.address[1],binding.address[2],binding.address[3]});}
    return lt::address_v6(binding.address);
  }
  static std::array<unsigned char,20> bytes(const lt::sha1_hash& id){
    std::array<unsigned char,20> result{};std::copy_n(reinterpret_cast<const unsigned char*>(id.data()),20,result.begin());return result;
  }
  std::shared_ptr<void> library_;
  std::shared_ptr<const storage::OriginalMainlineIdentity> source_;
  std::shared_ptr<const storage::ProfileLease> lease_;
  std::vector<std::shared_ptr<int>> reservations_;
  decltype(&lt::biglybtc_configure_mainline_identity_v1) configure_=nullptr;
  decltype(&lt::biglybtc_read_mainline_identity_v1) read_=nullptr;
#if BIGLYBTC_HAS_ORIGINAL_MAINLINE_ROUTING
  decltype(&lt::biglybtc_query_mainline_routing_v1) query_=nullptr;
#endif
  lt::BiglybtcMainlineIdentityPolicyV1 policy_{};
  std::shared_ptr<Observer> observer_;
  std::uint64_t sequence_=0,queryCookie_=0;unsigned listeners_=0,active_=0,adopted_=0,expected_=0;
#endif
};
