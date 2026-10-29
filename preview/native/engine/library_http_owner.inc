// Included before the UDP owner: both transports share the same retained
// library/URL response and never attach a torrent or open a payload file.
detail::LibraryHttpTracker libraryHttp;
// Independent same-OFD ownership outlives an optional Mainline service close.
// It is released after all HTTP work and the torrent session have joined.
std::shared_ptr<const storage::ProfileLease> regularTrackerLease;
void releaseRegularTrackerLease()noexcept {
  if(mainlineLease==regularTrackerLease.get()){mainlineLease=nullptr;}
  regularTrackerLease.reset();
}
std::map<std::string,std::int64_t> libraryHttpObservedWall,libraryHttpObservedNanos;
void observeLibraryHttpTime(const std::string& key,const core::QueueTrackerTerminal& terminal,std::int64_t stamp){
  const auto old=libraryHttpObservedNanos.find(key);
  if(old==libraryHttpObservedNanos.end()||stamp>old->second){
    libraryHttpObservedNanos.insert_or_assign(key,stamp);libraryHttpObservedWall.insert_or_assign(key,terminal.observedWallMillis);}
}
std::vector<LibraryTrackerResponse> regularTrackerViews;
Clock::time_point regularTrackerRefresh{};
bool libraryHttpRegularMode()const noexcept {
  return network&&network->mode==NetworkMode::Normal&&network->httpTrackers
      &&(network->proxy.kind==NetworkProxyKind::None||(network->proxy.kind==NetworkProxyKind::Socks5&&network->proxy.trackers));
}
// Current normal admissions are borrowed for metadata authentication only.
// Their actual ring is the sole LT terminal producer; no status sampling or
// hidden handle is introduced by this regular HTTP consumer.
std::expected<std::vector<SavedMainlineScrapeTarget>,storage::Error> regularHttpTargets(std::vector<std::string>* inventory=nullptr){
  std::vector<ManagedMainlineScrapeObservation> active;
  for(const auto& [key,entry]:managed){(void)key;const auto& value=*entry;
    if(!sameMainlineProfileLease()||!value.token||!value.status.admission||value.token->number!=value.status.admission
        ||value.storage.identity().originalKey!=value.status.hash||!value.status.retained||!value.status.checked||value.status.failure
        ||value.stop||value.removing||value.manualPaused||value.policyHeld||value.status.verificationHeld||value.seedIntent||value.allocationIntent
        ||!value.handle.is_valid()||!value.trackerResponse||value.trackerResponse->admission!=value.status.admission
        ||!value.trackerResponse->installed.load(std::memory_order_acquire)||value.trackerResponse->lost.load(std::memory_order_acquire)
        ||(value.status.phase!=ManagedPhase::Downloading&&value.status.phase!=ManagedPhase::Seeding)){continue;}
    const auto handle=managedByHandle.find(value.handle);if(handle==managedByHandle.end()||handle->second!=&value){continue;}
    active.push_back({&value.storage,value.status.admission,value.status.complete});
  }
  auto targets=readCurrentMainlineScrapeTargets(*mainlineLease,active,true,inventory,SavedTrackerPurpose::RegularHttp);
  if(!targets){return targets;}
  // The live Mainline path keeps its existing scope/producer. Only genuine
  // permission-excluded rows, or a policy with no Mainline service, enter this
  // independent regular path. An error cannot be interpreted as no owner.
  if(mainline&&network&&network->dht&&network->proxy.kind==NetworkProxyKind::None){
    auto owned=readCurrentMainlineScrapeTargets(*mainlineLease,active,true);if(!owned){return std::unexpected(owned.error());}
    std::erase_if(*targets,[&](const auto& target){return std::ranges::any_of(*owned,[&](const auto& item){return item.hash==target.hash&&item.admission==target.admission;});});
  }
  // This transport owns HTTP(S), never silently tries a mixed UDP tier.
  std::erase_if(*targets,[](const auto& target){const auto http=[](const auto& url){return url.starts_with("http://")||url.starts_with("https://");};
    return !http(target.configuredTracker)||std::ranges::any_of(target.configuredTrackers,[&](const auto& item){return !http(item.url);});});
  return targets;
}
bool libraryHttpAdmissionCurrent(const SavedMainlineScrapeTarget& target)const noexcept{
  if(!target.admission){return libraryUdpUnadmitted(target.hash);}
  const auto found=managed.find(target.hash.hex());if(found==managed.end()){return false;}const auto& value=*found->second;
  const auto response=trackerResponses.find(target.trackerResponseKey);
  return value.token&&value.token->number==target.admission&&value.status.admission==target.admission&&value.status.retained&&value.status.checked
      &&!value.status.failure&&!value.stop&&!value.removing&&!value.manualPaused&&!value.policyHeld&&!value.status.verificationHeld
      &&!value.seedIntent&&!value.allocationIntent&&value.handle.is_valid()&&value.trackerResponse
      &&value.trackerResponse->admission==target.admission&&value.trackerResponse->installed.load(std::memory_order_acquire)
      &&!value.trackerResponse->lost.load(std::memory_order_acquire)&&response!=trackerResponses.end()&&value.trackerResponse->response==response->second
      &&(value.status.phase==ManagedPhase::Downloading||value.status.phase==ManagedPhase::Seeding);
}
std::uint64_t libraryHttpNonce=0;
void cancelLibraryHttp(const core::TorrentHash* hash=nullptr) noexcept {
  if(const auto* request=libraryHttp.active();request&&(!hash||request->hash==*hash)){
    const auto row=trackerResponses.find(request->responseKey);if(row!=trackerResponses.end()){row->second->cancelRequest();}libraryHttp.cancel();
  }
}
bool libraryHttpAuthorise(const detail::LibraryHttpTracker::Request& request) noexcept {
  try{
    if(!mainlineLease||!networkBinding||!network||network->mode!=NetworkMode::Normal||!network->httpTrackers
        ||(request.regularAuthority?!libraryHttpRegularMode():(!mainline||!network->dht||network->proxy.kind!=NetworkProxyKind::None))||!networkBindingCurrent(*networkBinding)
        ||mainlineScrapes.revoked(request.hash)){return false;}
    auto fresh=request.regularAuthority?regularHttpTargets():readCurrentMainlineScrapeTargets(*mainlineLease,{},true);if(!fresh){return false;}
    const auto row=std::ranges::find(*fresh,request.hash,&SavedMainlineScrapeTarget::hash);
    const auto target=row==fresh->end()?std::nullopt:selectConfiguredMainlineTracker(*row,request.responseKey);
    if(!target||!libraryHttpAdmissionCurrent(*target)||target->scope!=request.scope){return false;}
    const auto route=detail::libraryHttpRoute(networkBinding->effective,target->configuredTracker);
    const auto config=detail::libraryHttpConfiguration(*mainlineLease,route&&route->scrape.scheme()=="https",nullptr,&networkBinding->effective);
    return route&&config&&detail::libraryHttpRouteScope(*route,config->scope)==request.configuration;
  }catch(...){return false;}
}
std::expected<std::optional<QByteArray>,storage::Error> libraryHttpAuthenticate(
    const detail::LibraryHttpTracker::Request& request,const detail::LibraryHttpRoute& challenged,const QByteArray& realm){
  const auto changed=[](){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Saved tracker authentication source changed"});};
  try{
    if(!libraryHttpAuthorise(request)){return changed();}
    auto targets=request.regularAuthority?regularHttpTargets():readCurrentMainlineScrapeTargets(*mainlineLease,{},true);if(!targets){return changed();}
    const auto row=std::ranges::find(*targets,request.hash,&SavedMainlineScrapeTarget::hash);
    const auto target=row==targets->end()?std::nullopt:selectConfiguredMainlineTracker(*row,request.responseKey);
    if(!target||!libraryHttpAdmissionCurrent(*target)||target->scope!=request.scope){return changed();}
    const auto base=detail::libraryHttpRoute(networkBinding->effective,target->configuredTracker);if(!base){return changed();}
    std::optional<storage::ConfigFile> snapshot;
    auto config=detail::libraryHttpConfiguration(*mainlineLease,base->scrape.scheme()=="https",&snapshot,&networkBinding->effective);
    if(!config||detail::libraryHttpRouteScope(*base,config->scope)!=request.configuration){return changed();}
    auto credential=detail::libraryHttpSavedBasic(snapshot?&snapshot->document():nullptr,challenged,realm);
    if(!libraryHttpAuthorise(request)){return changed();}return credential;
  }catch(...){return std::unexpected(storage::Error{storage::ErrorCode::Io,"Saved tracker authentication is unavailable"});}
}
void advanceLibraryHttp(Clock::time_point now){
  if(!libraryHttp.active()){return;}const auto responseKey=libraryHttp.active()->responseKey;
  const auto millis=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  const auto wall=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  auto result=libraryHttp.poll(millis,wall,[&](const auto& request){return libraryHttpAuthorise(request);},
      [&](const auto& request,const auto& route,const auto& realm){return libraryHttpAuthenticate(request,route,realm);});
  const auto response=trackerResponses.find(responseKey);
  if(!result){if(response!=trackerResponses.end()){response->second->cancelRequest();}libraryHttp.cancel();mainlineScrapeFailure=result.error();return;}
  if(*result){if(response!=trackerResponses.end()){
    const bool accepted=response->second->observeChronological((**result).terminal,(**result).observedNanos);
    response->second->cancelRequest(); // This exact Qt operation settled even if a newer producer already won.
    if(accepted&&((**result).terminal.comparable||(**result).terminal.definitiveFailure)){
      observeLibraryHttpTime(responseKey,(**result).terminal,(**result).observedNanos);}}}
  else if(!libraryHttp.active()&&response!=trackerResponses.end()){response->second->cancelRequest();}
}
bool prepareLibraryHttp(SavedMainlineScrapeTarget& target,std::int64_t now,bool& pendingEligibility,bool regularPoll=false,bool dispatch=true,bool regularAuthority=false){
  pendingEligibility=false;
  if(!mainlineLease||!networkBinding||!network||!network->httpTrackers||!networkBindingCurrent(*networkBinding)||!libraryHttpAdmissionCurrent(target)){return false;}
  const auto route=detail::libraryHttpRoute(networkBinding->effective,target.configuredTracker);if(!route){return false;}
  const auto config=detail::libraryHttpConfiguration(*mainlineLease,route&&route->scrape.scheme()=="https",nullptr,&networkBinding->effective);if(!config){return false;}
  const auto configuration=detail::libraryHttpRouteScope(*route,config->scope);
  auto found=trackerResponses.find(target.trackerResponseKey);
  if(found==trackerResponses.end()){
    if(trackerResponses.size()>=256){return false;}
    found=trackerResponses.emplace(target.trackerResponseKey,std::make_shared<core::QueueOriginalTrackerResponse>()).first;
  }
  if(dispatch&&(regularPoll?found->second->regularPollDue(now):found->second->regularDue(now))&&!libraryHttp.active()&&!libraryUdp.active()){
    if(libraryHttpNonce==UINT64_MAX){return false;}
    detail::LibraryHttpTracker::Request request{target.hash,target.trackerResponseKey,target.scope,configuration,++libraryHttpNonce,regularAuthority};
    const auto wall=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto started=libraryHttp.start(std::move(request),*route,now,wall,config->singleOnly,config->trustAnchors);
    if(!started){mainlineScrapeFailure=started.error();return false;}found->second->requested();
  }
  if(const auto* request=libraryHttp.active();request&&request->hash==target.hash&&request->responseKey==target.trackerResponseKey&&request->scope==target.scope&&request->configuration==configuration){pendingEligibility=found->second->eligibleDuringOwnedRequest();}
  const auto scope=detail::originalTrackerUrlDigest(target.scope+configuration);if(!scope){return false;}
  constexpr char hex[]="0123456789abcdef";target.scope.clear();target.scope.reserve(64);for(const auto c:*scope){target.scope+=hex[c>>4];target.scope+=hex[c&15];}return true;
}
