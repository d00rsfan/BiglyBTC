// Optional PORT hints use one fixed SPSC buffer per actual admission. No owner,
// lease or mutable LT object is borrowed by the provider callback.
struct MainlinePortQueue {
  struct Hint {
    std::uint64_t sequence=0,admission=0;
    std::array<unsigned char,20> wire_info_hash{};
    std::array<unsigned char,16> address{};
    std::uint16_t peer_port=0,dht_port=0;
    std::uint32_t family=0,protocol=0,kind=0,reserved=0;
  };
  static constexpr std::uint64_t Capacity=64;
  static_assert(std::atomic<std::uint16_t>::is_always_lock_free&&std::atomic<std::uint64_t>::is_always_lock_free
      &&std::atomic_bool::is_always_lock_free,"PORT callbacks require lock-free fixed scalars");
  const std::uint64_t admission;
  const std::uint32_t protocol;
  const lt::sha1_hash hash;
  std::atomic<std::uint16_t> advertised{0};
  std::atomic_bool installed{false},retired{false},lost{false};
  std::array<Hint,Capacity> events{};
  std::atomic<std::uint64_t> written{0},read{0};
  std::uint64_t previous=0; // Worker only.
  MainlinePortQueue(std::uint64_t id,std::uint32_t version,lt::sha1_hash value)
      :admission(id),protocol(version),hash(value){}
  void fail() noexcept {advertised.store(0,std::memory_order_release);lost.store(true,std::memory_order_release);}
  void retire() noexcept {advertised.store(0,std::memory_order_release);retired.store(true,std::memory_order_release);}
  std::uint16_t port() const noexcept {
    if(retired.load(std::memory_order_acquire)||lost.load(std::memory_order_acquire)){return 0;}
    return advertised.load(std::memory_order_acquire);
  }
  void observe(const Hint& hint) noexcept {
    if(retired.load(std::memory_order_acquire)||lost.load(std::memory_order_acquire)){return;}
    const auto end=written.load(std::memory_order_relaxed),begin=read.load(std::memory_order_acquire);
    if(end==UINT64_MAX||end-begin>=Capacity){fail();return;}
    events[end%Capacity]=hint;written.store(end+1,std::memory_order_release);
  }
};
#if BIGLYBTC_MAINLINE_PORT_BRIDGE
struct MainlinePortBinding {
  std::shared_ptr<void> library;
  decltype(&lt::biglybtc_attach_mainline_port_observer_v1) attach=nullptr;
  decltype(&lt::biglybtc_refresh_mainline_port_v1) refresh=nullptr;
};
std::shared_ptr<const MainlinePortBinding> mainlinePortApi;
struct MainlinePortSink final : lt::BiglybtcMainlinePortObserverV1 {
  const std::shared_ptr<MainlinePortQueue> queue;
  const std::shared_ptr<const MainlinePortBinding> binding;
  MainlinePortSink(std::shared_ptr<MainlinePortQueue> q,std::shared_ptr<const MainlinePortBinding> b)
      :queue(std::move(q)),binding(std::move(b)){}
  std::uint16_t advertised_port() const noexcept override {return queue->port();}
  void observe(const lt::BiglybtcMainlinePortEventV1& value) noexcept override {
    queue->observe({value.sequence,value.admission,value.wire_info_hash,value.address,
        value.peer_port,value.dht_port,value.family,value.protocol,value.kind,value.reserved});
  }
};
#endif
std::shared_ptr<MainlinePortQueue> attachMainlinePort(lt::add_torrent_params& parameters,
    std::uint64_t admission,const core::TorrentHash& original,std::uint32_t protocol) noexcept {
#if BIGLYBTC_MAINLINE_PORT_BRIDGE
  if(!mainline||!network||network->mode!=NetworkMode::Normal||!network->dht
      ||network->proxy.kind!=NetworkProxyKind::None){return {};}
  try {
    if(!mainlinePortApi){
      auto library=detail::mainlinePortRuntimePin(); // Complete exact source/artifact/mapping proof.
      if(!library){return {};}
      const auto abi=reinterpret_cast<decltype(&lt::biglybtc_mainline_port_abi_v1)>(::dlsym(library.get(),"biglybtc_mainline_port_abi_v1"));
      const auto attach=reinterpret_cast<decltype(&lt::biglybtc_attach_mainline_port_observer_v1)>(::dlsym(library.get(),"biglybtc_attach_mainline_port_observer_v1"));
      const auto refresh=reinterpret_cast<decltype(&lt::biglybtc_refresh_mainline_port_v1)>(::dlsym(library.get(),"biglybtc_refresh_mainline_port_v1"));
      if(!abi||!attach||!refresh||abi()!=1
          ||reinterpret_cast<void*>(abi)!=::dlsym(RTLD_DEFAULT,"biglybtc_mainline_port_abi_v1")
          ||reinterpret_cast<void*>(attach)!=::dlsym(RTLD_DEFAULT,"biglybtc_attach_mainline_port_observer_v1")
          ||reinterpret_cast<void*>(refresh)!=::dlsym(RTLD_DEFAULT,"biglybtc_refresh_mainline_port_v1")){return {};}
      auto api=std::make_shared<MainlinePortBinding>();api->library=std::move(library);api->attach=attach;api->refresh=refresh;
      mainlinePortApi=std::move(api);
    }
    auto queue=std::make_shared<MainlinePortQueue>(admission,protocol,discoveryHash(original));
    std::shared_ptr<lt::BiglybtcMainlinePortObserverV1> sink=std::make_shared<MainlinePortSink>(queue,mainlinePortApi);
    parameters.extensions.push_back([queue,sink,api=mainlinePortApi](const lt::torrent_handle& handle,lt::client_data_t)
        ->std::shared_ptr<lt::torrent_plugin>{
      if(queue->retired.load(std::memory_order_acquire)){return {};}
      if(api->attach(&handle,&sink,queue->admission)==static_cast<std::uint32_t>(lt::BiglybtcMainlinePortStatusV1::Accepted)){
        queue->installed.store(true,std::memory_order_release);
      }else{queue->fail();}
      return {}; // The provider owns the sink for exactly this actual torrent.
    });
    return queue;
  }catch(...){return {};}
#else
  (void)parameters;(void)admission;(void)original;(void)protocol;
  return {}; // Old SDK/provider never gains PORT authority from a header or marker.
#endif
}
static void retireMainlinePort(const std::shared_ptr<MainlinePortQueue>& queue) noexcept {if(queue){queue->retire();}}
void retireMainlinePorts() noexcept {
  for(auto& [key,value]:managed){(void)key;retireMainlinePort(value->mainlinePort);}
  for(auto& [key,value]:magnets){(void)key;retireMainlinePort(value->mainlinePort);}
}
void silenceMainlinePorts() noexcept {
  for(auto& [key,value]:managed){(void)key;if(value->mainlinePort){value->mainlinePort->advertised.store(0,std::memory_order_release);}}
  for(auto& [key,value]:magnets){(void)key;if(value->mainlinePort){value->mainlinePort->advertised.store(0,std::memory_order_release);}}
}
void refreshMainlinePort(lt::torrent_handle handle,const std::shared_ptr<MainlinePortQueue>& queue,bool eligible) noexcept {
  if(!queue){return;}
  const auto live=mainline?mainline->status():MainlineDhtStatus{};
  const auto port=eligible&&network&&network->mode==NetworkMode::Normal&&network->dht
      &&network->proxy.kind==NetworkProxyKind::None&&live.phase==MainlineDhtPhase::Listening
      &&queue->installed.load(std::memory_order_acquire)&&!queue->retired.load(std::memory_order_acquire)
      &&!queue->lost.load(std::memory_order_acquire)?live.listenPort:std::uint16_t{0};
  if(queue->advertised.exchange(port,std::memory_order_acq_rel)==port){return;}
#if BIGLYBTC_MAINLINE_PORT_BRIDGE
  if(!mainlinePortApi||!handle.is_valid()){queue->fail();return;}
  try{
    handle.add_extension([queue,api=mainlinePortApi](const lt::torrent_handle& current,lt::client_data_t)
        ->std::shared_ptr<lt::torrent_plugin>{
      if(!queue->retired.load(std::memory_order_acquire)
          &&api->refresh(&current,queue->admission)!=static_cast<std::uint32_t>(lt::BiglybtcMainlinePortStatusV1::Accepted)){queue->fail();}
      return {};
    });
  }catch(...){queue->fail();}
#endif
}
std::optional<std::pair<std::string,int>> mainlinePortEndpoint(const MainlinePortQueue::Hint& hint) const {
  if(hint.peer_port==0||hint.dht_port==0||(hint.family!=4&&hint.family!=6)){return {};}
  lt::address address;
  if(hint.family==4){
    lt::address_v4::bytes_type bytes{};std::copy_n(hint.address.begin(),4,bytes.begin());address=lt::address_v4(bytes);
    if(std::ranges::any_of(hint.address.begin()+4,hint.address.end(),[](auto value){return value!=0;})){return {};}
  }else{lt::address_v6::bytes_type bytes{};std::copy(hint.address.begin(),hint.address.end(),bytes.begin());address=lt::address_v6(bytes);}
  if(address.is_unspecified()||address.is_multicast()||!networkBinding){return {};}
  if((address.is_v4()&&!network->mainlineIpv4)||(address.is_v6()&&(!network->ipv6||!network->mainlineIpv6))){return {};}
  bool privateRoute=false;
  if(address.is_v4()){
    const auto value=address.to_v4().to_uint(),first=value>>24;
    if(first==0||first>=224){return {};}
    privateRoute=first==10||(value&0xfff00000u)==0xac100000u||(value&0xffff0000u)==0xc0a80000u
        ||(value&0xffc00000u)==0x64400000u||(value&0xffff0000u)==0xa9fe0000u||address.is_loopback();
  }else{
    const auto value=address.to_v6();if(value.is_v4_mapped()||value.scope_id()!=0){return {};}
    privateRoute=value.is_loopback()||value.is_link_local()||value.is_site_local()||(value.to_bytes()[0]&0xfe)==0xfc;
  }
  if(privateRoute){
    if(!address.is_loopback()||!std::ranges::any_of(networkBinding->effective.bindAddresses,[&](const auto& text){
      lt::error_code ec;const auto bound=lt::make_address(text,ec);return !ec&&bound.is_loopback()&&bound.is_v4()==address.is_v4();
    })){return {};}
  }
  return std::pair{address.to_string(),static_cast<int>(hint.dht_port)};
}
void drainMainlinePort(const std::shared_ptr<MainlinePortQueue>& queue,bool eligible) noexcept {
  if(!queue){return;}
  if(queue->installed.load(std::memory_order_acquire)){mainlineStatus.portBridgeAvailable=true;}
  const auto loss=[&]{queue->fail();mainlineStatus.portHintsLost=true;};
  if(queue->lost.load(std::memory_order_acquire)){loss();return;}
  auto begin=queue->read.load(std::memory_order_relaxed);const auto end=queue->written.load(std::memory_order_acquire);
  try{
    while(begin<end){
      const auto hint=queue->events[begin%MainlinePortQueue::Capacity];++begin;
      if(queue->previous==UINT64_MAX||hint.sequence!=queue->previous+1){loss();break;}
      queue->previous=hint.sequence;
      if(hint.kind==2){loss();break;}
      if(hint.kind!=1||hint.reserved!=0){loss();break;}
      if(mainlineStatus.portHintsObserved==UINT64_MAX){loss();break;}
      ++mainlineStatus.portHintsObserved;
      const bool owned=hint.admission==queue->admission&&hint.protocol==queue->protocol
          &&std::equal(hint.wire_info_hash.begin(),hint.wire_info_hash.end(),queue->hash.begin());
      auto endpoint=owned&&eligible&&queue->port()!=0&&!queue->retired.load(std::memory_order_acquire)
          ?mainlinePortEndpoint(hint):std::nullopt;
      if(endpoint&&mainline&&mainline->addPeerNode(*endpoint)){
        if(mainlineStatus.portHintsAccepted==UINT64_MAX){loss();break;}++mainlineStatus.portHintsAccepted;
      }else{if(mainlineStatus.portHintsRejected==UINT64_MAX){loss();break;}++mainlineStatus.portHintsRejected;}
    }
    queue->read.store(begin,std::memory_order_release);
    if(queue->lost.load(std::memory_order_acquire)){loss();}
  }catch(...){loss();}
}
void advanceMainlinePorts() noexcept {
  const bool live=mainline&&!closingAdmission&&!poisoned&&!closed&&network&&network->mode==NetworkMode::Normal
      &&network->dht&&network->proxy.kind==NetworkProxyKind::None;
  for(auto& [key,value]:managed){
    (void)key;const bool eligible=live&&discoverable(*value);
    refreshMainlinePort(value->handle,value->mainlinePort,eligible);drainMainlinePort(value->mainlinePort,eligible);
  }
  for(auto& [key,value]:magnets){
    (void)key;const bool eligible=live&&discoverable(*value);
    refreshMainlinePort(value->handle,value->mainlinePort,eligible);drainMainlinePort(value->mainlinePort,eligible);
  }
}

// This separate service owns its own DHT alert queue. The torrent session's
// single pump, payload counters and retirement certificates remain independent.
std::unique_ptr<MainlineDhtService> mainline;
const storage::ProfileLease* mainlineLease=nullptr;
std::optional<storage::MainlineCheckpoint> mainlineCheckpoint;
Clock::time_point mainlineCheckpointNext{};
std::optional<std::vector<std::pair<std::string,int>>> mainlineBootstrap;
MainlineDiscoveryStatus mainlineStatus;
std::vector<lt::sha1_hash> mainlineActive;
std::uint64_t mainlinePortProgress=0;
static lt::sha1_hash discoveryHash(const core::TorrentHash& hash) {
  const auto bytes=hash.bytes();
  detail::require(bytes.size()>=20,"Mainline discovery requires a complete selected identity");
  return lt::sha1_hash(reinterpret_cast<const char*>(bytes.data()));
}
void startMainline() noexcept {
  if (!network || !networkBinding || network->mode!=NetworkMode::Normal || !network->dht) { return; }
  try {
    MainlineDhtOptions options;
    options.port=network->mainlinePort;
    options.ipv4=network->mainlineIpv4;
    options.ipv6=network->mainlineIpv6 && network->ipv6;
    options.onlyPeerBootstrap=network->mainlinePeerBootstrapOnly;
    options.restoreIdentity=network->mainlineRestoreIdentity;
    if (mainlineBootstrap) { options.bootstrapNodes=*mainlineBootstrap; }
    // Optional state never changes route policy. Unsupported proxies do not
    // even open these files; the service itself reports unavailable below.
    if (mainlineLease && network->proxy.kind==NetworkProxyKind::None) {
      auto cached=storage::MainlineCheckpoint::load(*mainlineLease,network->mainlineDirectory);
      if (cached) {
        mainlineCheckpoint.emplace(std::move(*cached));
        if (mainlineCheckpoint->state()) { options.restoredState=*mainlineCheckpoint->state(); }
      } else { mainlineStatus.checkpointFailure=cached.error(); }
      const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
      auto legacy=storage::readLegacyMldhtHints(*mainlineLease,network->mainlineDirectory,now);
      if (legacy) {
        for (const auto& hint:legacy->endpoints) {
          const bool ipv6=hint.address.find(':')!=std::string::npos;
          if ((ipv6 && options.ipv6) || (!ipv6 && options.ipv4)) { options.restoredNodes.emplace_back(hint.address,hint.port); }
        }
      } else { mainlineStatus.checkpointFailure=legacy.error(); }
    }
    auto created=MainlineDhtService::create(networkBinding->effective,options);
    if (!created && !options.restoredState.empty() && created.error().code==storage::ErrorCode::InvalidFormat) {
      // Known optional-cache corruption only. Preserve the file and revoke
      // writes; repeat with identical routes and no untrusted saved state.
      mainlineStatus.checkpointFailure=created.error(); mainlineCheckpoint.reset();
      options.restoredState.clear();
      created=MainlineDhtService::create(networkBinding->effective,options);
    }
    if (!created) {
      mainlineStatus.state=MainlineDiscoveryStatus::State::Unavailable;
      mainlineStatus.failure=created.error(); return;
    }
    mainline=std::move(*created);
    mainlineStatus.checkpointLoaded=!options.restoredState.empty();
    mainlineStatus.legacyHints=options.restoredNodes.size();
    mainlineStatus.state=MainlineDiscoveryStatus::State::Running;
    mainlineCheckpointNext=Clock::now()+5min;
  } catch (...) {
    silenceMainlinePorts();mainline.reset(); mainlineStatus.state=MainlineDiscoveryStatus::State::Unavailable;
    try { mainlineStatus.failure=storage::Error{storage::ErrorCode::Io,"Mainline service startup failed"}; } catch (...) {}
  }
}
void persistMainline() noexcept {
  if (!mainline || !mainlineCheckpoint || !mainlineLease || !mainlineLease->valid()
      || mainline->status().phase!=MainlineDhtPhase::Listening) { return; }
  try {
    auto snapshot=mainline->checkpoint();
    if (!snapshot) { mainlineStatus.checkpointFailure=snapshot.error(); mainlineCheckpoint.reset(); return; }
    auto saved=mainlineCheckpoint->save(*mainlineLease,*snapshot);
    if (!saved) { mainlineStatus.checkpointFailure=saved.error(); mainlineCheckpoint.reset(); return; }
    mainlineStatus.checkpointSaved=true;
  } catch (...) {
    mainlineCheckpoint.reset();
    try { mainlineStatus.checkpointFailure=storage::Error{storage::ErrorCode::Io,"Optional Mainline checkpoint failed; original data preserved"}; } catch (...) {}
  }
}
bool discoverable(const Managed& value) const {
  return value.status.retained && !value.stop && !value.removing && !value.manualPaused
      && !value.policyHeld && !value.status.verificationHeld && value.handle.is_valid()
      && (value.status.phase==ManagedPhase::Downloading || value.status.phase==ManagedPhase::Seeding)
      && value.storage.mainlinePolicy().discover
      && (!network->mainlineBackupOnly || !value.mainlineTrackerHealthy);
}
bool discoverable(const Magnet& value) const {
  return value.status.retained && !value.cancel && !value.shutdown && !value.removing
      && value.status.phase==MagnetPhase::Resolving && value.handle.is_valid();
}
void advanceMainline() noexcept {
  if (!mainline || closingAdmission || poisoned || closed) { return; }
  try {
    const auto now=Clock::now();
    if (now>=mainlineCheckpointNext) { persistMainline(); mainlineCheckpointNext=now+5min; }
    const auto peerProgress=mainline->status().peerBootstrapProgress;
    const bool peerAdvanced=peerProgress>mainlinePortProgress;
    if(peerAdvanced){mainlinePortProgress=peerProgress;}
    std::vector<lt::sha1_hash> current;
    const auto retain=[&](const lt::sha1_hash& hash) {
      if (std::ranges::find(current,hash)==current.end() && current.size()<MainlineDhtService::MaximumActiveHashes) { current.push_back(hash); }
      return std::ranges::find(current,hash)!=current.end();
    };
    // Revoke obsolete queries before spending the bounded active-hash budget.
    for (const auto& hash:mainlineActive) {
      const bool managedCurrent=std::ranges::any_of(managed,[&](const auto& item){return discoverable(*item.second) && discoveryHash(item.second->storage.identity().originalKey)==hash;});
      const bool magnetCurrent=std::ranges::any_of(magnets,[&](const auto& item){return discoverable(*item.second) && discoveryHash(item.second->intent.hash)==hash;});
      if (!managedCurrent && !magnetCurrent) { static_cast<void>(mainline->forget(hash)); }
    }
    for (auto& [key,entry]:managed) {
      static_cast<void>(key); auto& value=*entry;
      if (!discoverable(value)) { continue; }
      const auto hash=discoveryHash(value.storage.identity().originalKey);
      if (!retain(hash)) { continue; }
      if (now>=value.mainlineNext) {
        if (mainline->lookup(hash)) { ++mainlineStatus.lookups; }
        const bool announce=value.storage.mainlinePolicy().announce
            && (!network->mainlineBackupOnly || !value.mainlineTrackerHealthy);
        const auto port=session->listen_port();
        if (announce && network->incomingTcp && port>0 && mainline->announce(hash,port)) { ++mainlineStatus.announces; }
        value.mainlineNext=now+15min;
      }else if(peerAdvanced){
        // Only a real newly confirmed PORT bootstrap re-arms this lookup.
        // It never adds an announce or a nominal/per-poll retry.
        if(mainline->lookup(hash)){++mainlineStatus.lookups;}
      }
    }
    for (auto& [key,entry]:magnets) {
      static_cast<void>(key); auto& value=*entry;
      if (!discoverable(value)) { continue; }
      const auto hash=discoveryHash(value.intent.hash);
      if (!retain(hash)) { continue; }
      if (now>=value.mainlineNext) {
        for (const auto& node:value.intent.dhtNodes) { static_cast<void>(mainline->addNode(node)); }
        if (mainline->lookup(hash)) { ++mainlineStatus.lookups; }
        value.mainlineNext=now+15min; // Metadata discovery NEVER announces.
      }else if(peerAdvanced){
        if(mainline->lookup(hash)){++mainlineStatus.lookups;}
      }
    }
    for (const auto& hash:mainlineActive) {
      if (std::ranges::find(current,hash)==current.end()) { static_cast<void>(mainline->forget(hash)); }
    }
    mainlineActive=std::move(current);
    auto replies=mainline->poll();
    if (!replies) {
      mainlineStatus.failure=replies.error(); mainlineStatus.state=MainlineDiscoveryStatus::State::Unavailable;
      silenceMainlinePorts();mainline.reset(); return;
    }
    advanceMainlinePorts();
    // Peer hints cannot authorize storage, metadata, network changes or a stale
    // handle. Re-evaluate the current admission at delivery, after stop/retire.
    for (const auto& reply:*replies) {
      for (auto& [key,entry]:managed) {
        static_cast<void>(key); auto& value=*entry;
        if (!discoverable(value) || discoveryHash(value.storage.identity().originalKey)!=reply.hash) { continue; }
        for (const auto& peer:reply.peers) { value.handle.connect_peer(peer,lt::peer_info::dht); ++mainlineStatus.deliveredPeers; }
      }
      for (auto& [key,entry]:magnets) {
        static_cast<void>(key); auto& value=*entry;
        if (!discoverable(value) || discoveryHash(value.intent.hash)!=reply.hash) { continue; }
        for (const auto& peer:reply.peers) { value.handle.connect_peer(peer,lt::peer_info::dht); ++mainlineStatus.deliveredPeers; }
      }
    }
  } catch (...) {
    silenceMainlinePorts();mainline.reset(); mainlineStatus.state=MainlineDiscoveryStatus::State::Unavailable;
    try { mainlineStatus.failure=storage::Error{storage::ErrorCode::Io,"Mainline service failed; torrent owner remains available"}; } catch (...) {}
  }
}
