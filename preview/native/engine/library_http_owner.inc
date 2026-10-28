// Included before the UDP owner: both transports share the same retained
// library/URL response and never attach a torrent or open a payload file.
detail::LibraryHttpTracker libraryHttp;
std::uint64_t libraryHttpNonce=0;
void cancelLibraryHttp(const core::TorrentHash* hash=nullptr) noexcept {
  if(const auto* request=libraryHttp.active();request&&(!hash||request->hash==*hash)){
    const auto row=trackerResponses.find(request->responseKey);if(row!=trackerResponses.end()){row->second->cancelRequest();}libraryHttp.cancel();
  }
}
bool libraryHttpAuthorise(const detail::LibraryHttpTracker::Request& request) noexcept {
  try{
    if(!mainlineLease||!networkBinding||!network||network->mode!=NetworkMode::Normal||!network->dht||!network->httpTrackers
        ||network->proxy.kind!=NetworkProxyKind::None||!networkBindingCurrent(*networkBinding)
        ||mainlineScrapes.revoked(request.hash)||!libraryUdpUnadmitted(request.hash)){return false;}
    auto fresh=readCurrentMainlineScrapeTargets(*mainlineLease,{},true);if(!fresh){return false;}
    const auto target=std::ranges::find(*fresh,request.hash,&SavedMainlineScrapeTarget::hash);
    if(target==fresh->end()||target->admission||target->trackerResponseKey!=request.responseKey||target->scope!=request.scope){return false;}
    const auto route=detail::libraryHttpRoute(networkBinding->effective,target->configuredTracker);
    const auto config=detail::libraryHttpConfiguration(*mainlineLease);
    return route&&config&&detail::libraryHttpRouteScope(*route,config->scope)==request.configuration;
  }catch(...){return false;}
}
void advanceLibraryHttp(Clock::time_point now){
  if(!libraryHttp.active()){return;}const auto responseKey=libraryHttp.active()->responseKey;
  const auto millis=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  const auto wall=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  auto result=libraryHttp.poll(millis,wall,[&](const auto& request){return libraryHttpAuthorise(request);});
  const auto response=trackerResponses.find(responseKey);
  if(!result){if(response!=trackerResponses.end()){response->second->cancelRequest();}libraryHttp.cancel();mainlineScrapeFailure=result.error();return;}
  if(*result){if(response!=trackerResponses.end()){response->second->observe((**result).terminal);}}
  else if(!libraryHttp.active()&&response!=trackerResponses.end()){response->second->cancelRequest();}
}
bool prepareLibraryHttp(SavedMainlineScrapeTarget& target,std::int64_t now,bool& pendingEligibility){
  pendingEligibility=false;
  if(!mainlineLease||!networkBinding||!network||!network->httpTrackers||!networkBindingCurrent(*networkBinding)||!libraryUdpUnadmitted(target.hash)){return false;}
  const auto route=detail::libraryHttpRoute(networkBinding->effective,target.configuredTracker);if(!route){return false;}
  const auto config=detail::libraryHttpConfiguration(*mainlineLease);if(!config){return false;}
  const auto configuration=detail::libraryHttpRouteScope(*route,config->scope);
  auto found=trackerResponses.find(target.trackerResponseKey);
  if(found==trackerResponses.end()){
    if(trackerResponses.size()>=256){return false;}
    found=trackerResponses.emplace(target.trackerResponseKey,std::make_shared<core::QueueOriginalTrackerResponse>()).first;
  }
  if(found->second->regularDue(now)&&!libraryHttp.active()&&!libraryUdp.active()){
    if(libraryHttpNonce==UINT64_MAX){return false;}
    detail::LibraryHttpTracker::Request request{target.hash,target.trackerResponseKey,target.scope,configuration,++libraryHttpNonce};
    const auto wall=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto started=libraryHttp.start(std::move(request),*route,now,wall,config->singleOnly);
    if(!started){mainlineScrapeFailure=started.error();return false;}found->second->requested();
  }
  if(const auto* request=libraryHttp.active();request&&request->hash==target.hash&&request->responseKey==target.trackerResponseKey&&request->scope==target.scope&&request->configuration==configuration){pendingEligibility=found->second->eligibleDuringOwnedRequest();}
  const auto scope=detail::originalTrackerUrlDigest(target.scope+configuration);if(!scope){return false;}
  constexpr char hex[]="0123456789abcdef";target.scope.clear();target.scope.reserve(64);for(const auto c:*scope){target.scope+=hex[c>>4];target.scope+=hex[c&15];}return true;
}
