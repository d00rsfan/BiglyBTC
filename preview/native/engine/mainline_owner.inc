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
  cancelLibraryUdp();cancelLibraryHttp();libraryHttp.close();
  for(auto& [key,value]:managed){(void)key;retireMainlinePort(value->mainlinePort);}
  for(auto& [key,value]:magnets){(void)key;retireMainlinePort(value->mainlinePort);}
}
void silenceMainlinePorts() noexcept {
  mainlineStatus.admittedScrapeCandidates.clear();
  cancelLibraryUdp();cancelLibraryHttp();libraryHttp.close();
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
#else
  (void)handle;
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
  const auto& discovery=mainlineResolvedPolicy?*mainlineResolvedPolicy:*network;
  if((address.is_v4()&&!discovery.mainlineIpv4)||(address.is_v6()&&(!discovery.ipv6||!discovery.mainlineIpv6))){return {};}
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
bool mainlineOriginalConfigured=false,mainlineOriginalRejected=false;
std::shared_ptr<const storage::OriginalMainlineIdentity> mainlineOriginalSource;
std::shared_ptr<const storage::ProfileLease> mainlineOriginalLease;
// Only original default/wildcard owner activation uses this retained plan.
// Explicit numeric service/Host callers keep their established route contract.
std::optional<MainlineBindCapture> mainlineBindCapture;
std::optional<NetworkPolicy> mainlineResolvedPolicy;
struct OriginalBindSource {NetworkPolicy policy;MainlineBindOptions options;};
std::expected<OriginalBindSource,storage::Error> originalBindSource() const {
  if(!mainlineOriginalLease||!network){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Original Mainline bind source has no held profile"});}
  auto config=detail::udpTrackerConfig(*mainlineOriginalLease,"biglybt.config");
  if(!config){return std::unexpected(config.error());}
  auto empty=core::Document::parse("de");
  if(!empty){return std::unexpected(storage::Error{storage::ErrorCode::Io,"Cannot read original Mainline bind defaults"});}
  const auto& document=*config?(**config).document():*empty;
  auto policy=readNetworkPolicy(&document,{.mode=NetworkMode::Normal,.socks4Capability=network->socks4Capability,.mldhtDirectory=network->mainlineDirectory});
  if(!policy){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Original Mainline bind source is no longer supported"});}
  auto settings=storage::readOriginalMldhtSettings(document,network->mainlineDirectory);
  if(!settings){return std::unexpected(storage::Error{storage::ErrorCode::InvalidFormat,"Invalid original Mainline plugin binding"});}
  auto options=mainlineBindOptions(*settings,policy->bindAddresses);
  if(!options){return std::unexpected(options.error());}
  return OriginalBindSource{std::move(*policy),std::move(*options)};
}
bool sameOriginalBindPolicy(const NetworkPolicy& fresh) const {
  return network&&fresh.mode==network->mode&&fresh.dht==network->dht
      &&fresh.proxy.kind==NetworkProxyKind::None&&network->proxy.kind==NetworkProxyKind::None
      &&fresh.mainlineOriginalBindPlan&&fresh.bindAddresses==network->bindAddresses
      &&fresh.listenPort==network->listenPort&&fresh.mainlineDirectory==network->mainlineDirectory
      &&fresh.mainlinePort==network->mainlinePort&&fresh.mainlineRestoreIdentity==network->mainlineRestoreIdentity
      &&fresh.mainlineBackupOnly==network->mainlineBackupOnly&&fresh.mainlinePeerBootstrapOnly==network->mainlinePeerBootstrapOnly
      &&fresh.mainlineOriginalAutostart4==network->mainlineOriginalAutostart4
      &&fresh.mainlineOriginalAutostart6==network->mainlineOriginalAutostart6
      &&fresh.mainlineMultihoming4==network->mainlineMultihoming4&&fresh.mainlineMultihoming6==network->mainlineMultihoming6;
}
std::expected<void,storage::Error> currentMainlineBind() const {
  if(!mainlineBindCapture){return {};}
  auto valid=mainlineBindCapture->validate();if(!valid){return valid;}
  auto source=originalBindSource();if(!source){return std::unexpected(source.error());}
  if(!sameOriginalBindPolicy(source->policy)||source->options!=mainlineBindCapture->options()){
    return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Original Mainline bind settings changed"});
  }
#if BIGLYBTC_ORIGINAL_MLDHT_IDENTITY_READER
  if(!mainlineOriginalSource||!mainlineOriginalLease){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Original Mainline bind source was retired"});}
  return mainlineOriginalSource->validate(*mainlineOriginalLease);
#else
  return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Original Mainline identity reader is unavailable"});
#endif
}
bool checkMainlineBind() {
  auto current=currentMainlineBind();if(current){return true;}
  mainlineStatus.failure=current.error();mainlineStatus.state=MainlineDiscoveryStatus::State::Unavailable;
  silenceMainlinePorts();closeMainline();return false;
}

void observeOriginalMainline() noexcept {
  if(!mainline){return;}const auto observed=mainline->status();
  mainlineStatus.originalIdentityRequested|=observed.originalIdentityRequested;
  mainlineStatus.originalIdentityActive=observed.originalIdentityActive;
  mainlineStatus.originalIdentityJoined=observed.originalIdentityJoined;
  mainlineStatus.originalIdentityAdoptions=observed.originalIdentityAdoptions;
  mainlineStatus.originalIdentityRetirements=observed.originalIdentityRetirements;
}
void closeMainline() noexcept {
  mainlineStatus.admittedScrapeCandidates.clear();
  if(mainline){
    try{const auto closed=mainline->close();observeOriginalMainline();
      if(!closed){mainlineStatus.failure=closed.error();}}
    catch(...){try{mainlineStatus.failure=storage::Error{storage::ErrorCode::Io,"Original Mainline service join could not be acknowledged"};}catch(...){}}
    mainline.reset(); // Joins before releasing the source/SAME-OFD token below.
  }
  if(mainlineLease==mainlineOriginalLease.get()){mainlineLease=nullptr;}
  mainlineBindCapture.reset();mainlineResolvedPolicy.reset();
  mainlineOriginalSource.reset();mainlineOriginalLease.reset();
}
std::optional<storage::MainlineCheckpoint> mainlineCheckpoint;
Clock::time_point mainlineCheckpointNext{};
std::optional<std::vector<std::pair<std::string,int>>> mainlineBootstrap;
MainlineDiscoveryStatus mainlineStatus;
std::vector<lt::sha1_hash> mainlineActive;
std::uint64_t mainlinePortProgress=0;
QueuedMainlineScrapes mainlineScrapes{std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count()};
Clock::time_point mainlineScrapeRefresh{};
std::optional<storage::Error> mainlineScrapeFailure;
// The ordinary library owns responses beyond a single transfer/admission.
// No state/cache restore populates this map and no eviction resets validity.
detail::LibraryTrackerResponses trackerResponses;
#include "library_http_owner.inc"
#include "library_udp_owner.inc"
// Original no-announcer selection for ordered, stably shuffled tiers. The same
// library response owns each URL across retries/readmissions. Unknown/local
// unsupported is not an all-trackers-failed observation.
std::map<std::string,std::int64_t> trackerSelectionSeeds;
std::optional<std::int64_t> trackerSelectionSeed(const core::TorrentHash& hash){
  const auto found=trackerSelectionSeeds.find(hash.hex());if(found!=trackerSelectionSeeds.end()){return found->second;}
  if(trackerSelectionSeeds.size()>=256){return {};}
  const auto wall=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  trackerSelectionSeeds.emplace(hash.hex(),wall);return wall;
}
bool prepareLibraryTrackerTiers(SavedMainlineScrapeTarget& target,std::int64_t now,bool& pendingEligibility){
  if(target.admission||target.configuredTrackers.size()<2||target.configuredTrackers.size()>8){return false;}
  const auto seed=trackerSelectionSeed(target.hash);if(!seed){return false;}
  std::vector<unsigned> tiers;for(const auto& url:target.configuredTrackers){tiers.push_back(url.tier);}
  const auto order=core::sourceQueueTrackerOrder(tiers,*seed);if(order.size()!=tiers.size()){return false;}
  std::vector<const core::QueueOriginalTrackerResponse*> responses;
  std::optional<std::expected<std::string,storage::Error>> udpConfig;
  const auto prepare=[&](SavedMainlineScrapeTarget& member,bool& pending,bool dispatch){
    if(member.configuredTracker.starts_with("http://")||member.configuredTracker.starts_with("https://")){
      return prepareLibraryHttp(member,now,pending,true,dispatch);
    }
    if(!udpConfig){udpConfig=detail::libraryUdpConfiguration(*mainlineLease);}
    return *udpConfig&&prepareLibraryUdp(member,now,**udpConfig,pending,true,dispatch);
  };
  // Authenticate every transport/configuration without IO first. A later
  // unsupported route must not be reported as an observed tracker failure.
  for(const auto index:order){
    const auto& url=target.configuredTrackers[index];
    auto member=selectConfiguredMainlineTracker(target,url.responseKey);bool pending=false;
    if(!member||!prepare(*member,pending,false)){return false;}
    const auto response=trackerResponses.find(url.responseKey);if(response==trackerResponses.end()){return false;}
    responses.push_back(response->second.get());
  }
  // Do not start an unvisited later tier after an earlier healthy response,
  // or while the original selector is waiting for an earlier initial scrape.
  auto limit=responses.size();const auto previous=core::sourceQueueTrackerSelection(responses);
  if(previous){
    const auto state=responses[*previous]->selectionStatus();
    if(state==core::QueueTrackerSelectionStatus::Initializing||state==core::QueueTrackerSelectionStatus::Scraping
        ||(state==core::QueueTrackerSelectionStatus::Online&&!responses[*previous]->dhtBackup())){limit=*previous+1;}
  }else{
    for(std::size_t i=0;i<responses.size();++i){if(responses[i]->selectionStatus()==core::QueueTrackerSelectionStatus::Unknown){limit=i+1;break;}}
  }
  // One actual regular scrape at a time. Retry deadlines are per URL; unlike
  // the single-URL DHT gate, multi-tier selection also revisits healthy URLs.
  for(std::size_t i=0;i<limit;++i){
    if(!responses[i]->regularPollDue(now)){continue;}
    auto member=selectConfiguredMainlineTracker(target,target.configuredTrackers[order[i]].responseKey);bool pending=false;
    if(!member||!prepare(*member,pending,true)){return false;}
    break;
  }
  const auto chosen=core::sourceQueueTrackerSelection(responses);if(!chosen){return false;}
  auto member=selectConfiguredMainlineTracker(target,target.configuredTrackers[order[*chosen]].responseKey);
  if(!member||!prepare(*member,pendingEligibility,false)){return false;}
  target=std::move(*member);return true;
}

// Stop/Pause removes the wrapper's TrackedTorrent own-start authority. The
// core BT response itself survives; positions/reconcile only revoke RPCs.
void forgetTrackerDhtAuthority(const core::TorrentHash& hash) noexcept {
  try {const auto prefix=hash.hex()+":";for(auto& [key,response]:trackerResponses){
    if(key.starts_with(prefix)){response->forgetDhtAuthority();}
  }}catch(...){}
}
std::uint32_t scrapeRandom() {
  std::uint32_t value=0;
  for(unsigned attempt=0;attempt<8;++attempt){
    if(RAND_bytes(reinterpret_cast<unsigned char*>(&value),sizeof(value))!=1){break;}
    if(value<UINT32_MAX-UINT32_MAX%600){return value;} // Uniform for60 and600.
  }
  throw detail::Failure{{storage::ErrorCode::Io,"Mainline scrape scheduling entropy failed"}};
}
bool sameMainlineProfileLease() const noexcept {
  if(managedLease==mainlineLease){return true;} // Preserve the ordinary direct-token path.
  if(!managedLease||!mainlineOriginalLease||mainlineLease!=mainlineOriginalLease.get()
      ||!managedLease->sharesOwnership(*mainlineOriginalLease)||!profileBound
      ||!networkBinding||!networkBindingCurrent(*networkBinding)){return false;}
  struct stat managedProfile{},mainlineProfile{};
  return ::fstat(managedLease->directoryHandle(),&managedProfile)==0&&S_ISDIR(managedProfile.st_mode)
      &&::fstat(mainlineOriginalLease->directoryHandle(),&mainlineProfile)==0&&S_ISDIR(mainlineProfile.st_mode)
      &&managedProfile.st_dev==mainlineProfile.st_dev&&managedProfile.st_ino==mainlineProfile.st_ino
      &&mainlineProfile.st_dev==profileDevice&&mainlineProfile.st_ino==profileInode;
}
std::expected<std::vector<SavedMainlineScrapeTarget>,storage::Error> savedScrapeTargets(){
  if(!mainlineLease||!network||!networkBinding||network->mode!=NetworkMode::Normal||!network->dht
      ||network->proxy.kind!=NetworkProxyKind::None){return std::vector<SavedMainlineScrapeTarget>{};}
  std::vector<ManagedMainlineScrapeObservation> admitted;
  for(const auto& [key,entry]:managed){
    (void)key;const auto& value=*entry;
    if(!sameMainlineProfileLease()||!value.token||value.token->number!=value.status.admission
        ||!value.status.admission||value.storage.identity().originalKey!=value.status.hash
        ||!value.status.retained||!value.status.checked||value.status.failure
        ||value.stop||value.removing||value.manualPaused||value.policyHeld||value.status.verificationHeld
        ||value.seedIntent||value.allocationIntent||!value.handle.is_valid()
        ||(value.status.phase!=ManagedPhase::Downloading&&value.status.phase!=ManagedPhase::Seeding)){continue;}
    const auto handle=managedByHandle.find(value.handle);
    if(handle==managedByHandle.end()||handle->second!=&value){continue;}
    admitted.push_back({&value.storage,value.status.admission,value.status.complete});
  }
  std::vector<std::string> inventoryHashes;
  auto targets=readCurrentMainlineScrapeTargets(*mainlineLease,admitted,true,&inventoryHashes);if(!targets){return targets;}
  // A manager seed survives Stop, permission loss, admission and tier edits.
  // Only a successfully authenticated complete inventory can end that row's
  // lifetime. Failed reads and merely ineligible-target absence never prune it.
  std::erase_if(trackerSelectionSeeds,[&](const auto& row){return std::ranges::find(inventoryHashes,row.first)==inventoryHashes.end();});
  const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
  std::optional<std::expected<std::string,storage::Error>> udpConfiguration;
  std::erase_if(*targets,[&](auto& target){
    if(target.trackerResponseKey.empty()){return false;}
    if(mainlineScrapes.revoked(target.hash)){return true;}
    // Never-admitted rows use their own regular direct tracker transport. No hidden LT
    // torrent, payload handle or constructor-null response grants authority.
    bool pendingEligibility=false;
    if(!target.admission){
      // Construct once even for a single URL, retaining the same manager seed
      // if its announce tiers are edited later during this library lifetime.
      if(!trackerSelectionSeed(target.hash)){return true;}
      if(!target.configuredTrackers.empty()){
        if(!prepareLibraryTrackerTiers(target,now,pendingEligibility)){return true;}
      }else if(target.configuredTracker.starts_with("http://")||target.configuredTracker.starts_with("https://")){
        if(!prepareLibraryHttp(target,now,pendingEligibility)){return true;}
      }else{
        if(!udpConfiguration){udpConfiguration=detail::libraryUdpConfiguration(*mainlineLease);}
        if(!*udpConfiguration||!prepareLibraryUdp(target,now,**udpConfiguration,pendingEligibility)){return true;}
      }
    }
    const auto found=trackerResponses.find(target.trackerResponseKey);
    if(found==trackerResponses.end()){return true;}
    const auto entry=managed.find(target.hash.hex());
    if(target.admission){
      if(entry==managed.end()||entry->second->status.admission!=target.admission||!entry->second->trackerResponse
          ||entry->second->trackerResponse->response!=found->second||!entry->second->trackerResponse->drain()){return true;}
      // Exactly one request while outstanding; the existing10s owner cadence
      // rechecks saved permissions before retrying the original600s failure
      // interval. No new timer/sampler, tracker/rank result or peer injection.
      if(found->second->regularDue(now)){
        found->second->requested();entry->second->handle.scrape_tracker();
      }
    }
    // A retired/unadmitted row may reuse its live library response only until
    // a fresh regular scrape is due. No hidden torrent or stale error is made.
    return !found->second->eligible(now)&&!pendingEligibility;
  });
  // Saved-only authority cannot borrow any retained/checking/failed admission.
  // Active targets above have their own immutable accepted admission stamp.
  std::erase_if(*targets,[&](const auto& target){
    const auto live=managed.find(target.hash.hex());const auto metadata=magnets.find(target.hash.hex());
    return (!target.admission&&live!=managed.end()&&(live->second->status.retained||live->second->status.failure))
        ||(metadata!=magnets.end()&&metadata->second->status.retained);
  });return targets;
}
void advanceMainlineScrapes(Clock::time_point now) noexcept {
  try {
  advanceLibraryUdp(now);advanceLibraryHttp(now);
  const auto millis=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  if(now>=mainlineScrapeRefresh){
    mainlineScrapeRefresh=now+10s;
    mainlineStatus.admittedScrapeCandidates.clear();
    auto targets=savedScrapeTargets();
    if(!targets){mainlineScrapeFailure=targets.error();mainlineScrapes.unavailable();}
    else{auto accepted=mainlineScrapes.reconcile(*targets,millis,scrapeRandom());if(!accepted){throw detail::Failure{accepted.error()};}mainlineScrapeFailure.reset();
      // Already authenticated, response-filtered and bounded by reconcile. A
      // passive snapshot cannot feed dispatch, scheduling or saved authority.
      for(const auto& target:*targets){if(target.admission&&!mainlineScrapes.revoked(target.hash)){mainlineStatus.admittedScrapeCandidates.push_back({target.hash,target.admission});}}
    }
  }
  if(mainlineScrapes.active()&&!mainlineScrapes.activeTarget()){
    const auto number=*mainlineScrapes.active();static_cast<void>(mainline->cancelScrape(number));
    mainlineScrapes.completed(number,{},{},millis,scrapeRandom());
  }
  auto completed=mainline->takeScrapes();if(!completed){throw detail::Failure{completed.error()};}
  for(const auto& result:*completed){
    if(!mainlineScrapes.active()||*mainlineScrapes.active()!=result.round){continue;}
    // Delivery never reuses the last periodic refresh as current permission.
    auto fresh=savedScrapeTargets();std::optional<SavedMainlineScrapeTarget> target;
    const auto active=mainlineScrapes.activeTarget();
    if(fresh&&active){const auto row=std::ranges::find(*fresh,active->hash,&SavedMainlineScrapeTarget::hash);if(row!=fresh->end()){target=*row;}}
    mainlineScrapes.completed(result.round,result,target,millis,scrapeRandom());
    if(target&&!target->trackerResponseKey.empty()){
      const auto owner=trackerResponses.find(target->trackerResponseKey);
      const auto estimates=mainlineScrapes.estimates();const auto accepted=std::ranges::find(estimates,result.round,&MainlineScrapeEstimate::round);
      if(owner!=trackerResponses.end()&&accepted!=estimates.end()&&accepted->hash==target->hash&&accepted->scope==target->scope){
        static_cast<void>(owner->second->publishDht(accepted->counts.seeds,accepted->counts.peers,accepted->startedWallMillis));
      }
    }
  }
  if(mainlineScrapes.checkDue(millis)){
    const auto candidate=mainlineScrapes.due(millis);
    if(candidate){
      auto fresh=savedScrapeTargets();
      if(!fresh){mainlineStatus.admittedScrapeCandidates.clear();mainlineScrapeFailure=fresh.error();return;}
      const auto current=std::ranges::find(*fresh,candidate->hash,&SavedMainlineScrapeTarget::hash);
      if(current==fresh->end()||current->scope!=candidate->scope||current->full!=candidate->full||current->admission!=candidate->admission){mainlineScrapes.revoke(candidate->hash);return;}
      auto round=mainline->scrape(lt::sha1_hash(reinterpret_cast<const char*>(candidate->hash.bytes().data())),services::mainline_scrape::Dialect::OriginalBFse,candidate->noSeeds);
      if(!round){mainlineStatus.admittedScrapeCandidates.clear();mainlineScrapeFailure=round.error();return;}
      const auto wall=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
      if(!mainlineScrapes.started(candidate->hash,candidate->scope,*round,wall)){static_cast<void>(mainline->cancelScrape(*round));}
    }
  }
  }catch(...){
    // Optional authority/scheduling failures never tear down peer discovery.
    mainlineStatus.admittedScrapeCandidates.clear();
    try{
      if(mainlineScrapes.active()){static_cast<void>(mainline->cancelScrape(*mainlineScrapes.active()));}
      const auto millis=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
      mainlineScrapes.unavailable();
      if(mainlineScrapes.active()){mainlineScrapes.completed(*mainlineScrapes.active(),{},{},millis,0);}
      mainlineScrapeFailure=storage::Error{storage::ErrorCode::Io,"Mainline scrape observations unavailable"};
    }catch(...){}
  }
}

static lt::sha1_hash discoveryHash(const core::TorrentHash& hash) {
  const auto bytes=hash.bytes();
  detail::require(bytes.size()>=20,"Mainline discovery requires a complete selected identity");
  return lt::sha1_hash(reinterpret_cast<const char*>(bytes.data()));
}
void startMainline() noexcept {
  if (!network || !networkBinding || network->mode!=NetworkMode::Normal || !network->dht) { return; }
  if(mainlineOriginalRejected){return;} // A failed original restore never falls back to stock IDs.
  if(network->mainlineOriginalBindPlan&&(!mainlineOriginalSource||!mainlineBindCapture)){
    mainlineStatus.state=MainlineDiscoveryStatus::State::Unavailable;
    try{mainlineStatus.failure=storage::Error{storage::ErrorCode::Unsupported,"Imported Mainline wildcard policy requires original identity and captured numeric plan"};}catch(...){}
    return;
  }
  try {
    MainlineDhtOptions options;
    if(!checkMainlineBind()){return;}
    const auto& selected=mainlineResolvedPolicy?*mainlineResolvedPolicy:networkBinding->effective;
    options.port=selected.mainlinePort;
    options.ipv4=selected.mainlineIpv4;
    options.ipv6=selected.mainlineIpv6 && selected.ipv6;
    options.onlyPeerBootstrap=network->mainlinePeerBootstrapOnly;
    options.restoreIdentity=network->mainlineRestoreIdentity;
    options.originalIdentity=mainlineOriginalSource;
    options.originalIdentityLease=mainlineOriginalLease;
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
    auto created=MainlineDhtService::create(selected,options);
    if (!created && !options.restoredState.empty() && created.error().code==storage::ErrorCode::InvalidFormat) {
      // Known optional-cache corruption only. Preserve the file and revoke
      // writes; repeat with identical routes and no untrusted saved state.
      mainlineStatus.checkpointFailure=created.error(); mainlineCheckpoint.reset();
      options.restoredState.clear();
      created=MainlineDhtService::create(selected,options);
    }
    if (!created) {
      mainlineStatus.state=MainlineDiscoveryStatus::State::Unavailable;
      mainlineStatus.failure=created.error(); return;
    }
    mainline=std::move(*created);observeOriginalMainline();
    mainlineStatus.checkpointLoaded=!options.restoredState.empty();
    mainlineStatus.legacyHints=options.restoredNodes.size();
    mainlineStatus.state=MainlineDiscoveryStatus::State::Running;
    mainlineCheckpointNext=Clock::now()+5min;
  } catch (...) {
    silenceMainlinePorts();closeMainline(); mainlineStatus.state=MainlineDiscoveryStatus::State::Unavailable;
    try { mainlineStatus.failure=storage::Error{storage::ErrorCode::Io,"Mainline service startup failed"}; } catch (...) {}
  }
}
void persistMainline() noexcept {
  if (!mainline || !mainlineCheckpoint || !mainlineLease || !mainlineLease->valid()
      || mainline->status().phase!=MainlineDhtPhase::Listening) { return; }
  try {
    if(!checkMainlineBind()){return;}
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
    if(!checkMainlineBind()){return;}
    const auto now=Clock::now();
    if (now>=mainlineCheckpointNext) { persistMainline(); mainlineCheckpointNext=now+5min; }
    if(!mainline){return;} // A second checkpoint fence may have withdrawn/joined it.
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
    auto replies=mainline->poll();observeOriginalMainline();
    if (!replies) {
      mainlineStatus.failure=replies.error(); mainlineStatus.state=MainlineDiscoveryStatus::State::Unavailable;
      silenceMainlinePorts();closeMainline(); return;
    }
    advanceMainlineScrapes(now);
    advanceMainlinePorts();
    // Peer hints cannot authorize storage, metadata, network changes or a stale
    // handle. Re-evaluate the current admission at delivery, after stop/retire.
    for (const auto& reply:*replies) {
      for (auto& [key,entry]:managed) {
        static_cast<void>(key); auto& value=*entry;
        if (!discoverable(value) || discoveryHash(value.storage.identity().originalKey)!=reply.hash) { continue; }
        for (const auto& peer:reply.peers) { if(peer.address().is_v6()&&!network->ipv6){continue;} value.handle.connect_peer(peer,lt::peer_info::dht); ++mainlineStatus.deliveredPeers; }
      }
      for (auto& [key,entry]:magnets) {
        static_cast<void>(key); auto& value=*entry;
        if (!discoverable(value) || discoveryHash(value.intent.hash)!=reply.hash) { continue; }
        for (const auto& peer:reply.peers) { if(peer.address().is_v6()&&!network->ipv6){continue;} value.handle.connect_peer(peer,lt::peer_info::dht); ++mainlineStatus.deliveredPeers; }
      }
    }
  } catch (...) {
    silenceMainlinePorts();closeMainline(); mainlineStatus.state=MainlineDiscoveryStatus::State::Unavailable;
    try { mainlineStatus.failure=storage::Error{storage::ErrorCode::Io,"Mainline service failed; torrent owner remains available"}; } catch (...) {}
  }
}
