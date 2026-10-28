// Same owner worker/pump as Mainline. One real regular scrape at a time;
// terminal/cancel closes its only socket before any response can be reused.
detail::LibraryUdpTracker libraryUdp;
std::uint64_t libraryUdpNonce=0;
void cancelLibraryUdp(const core::TorrentHash* hash=nullptr) noexcept {
  if(const auto* request=libraryUdp.active();request&&(!hash||request->hash==*hash)){
    const auto row=trackerResponses.find(request->responseKey);if(row!=trackerResponses.end()){row->second->cancelRequest();}libraryUdp.cancel();
  }
}
bool libraryUdpUnadmitted(const core::TorrentHash& hash)const {
  const auto live=managed.find(hash.hex());const auto metadata=magnets.find(hash.hex());
  return (live==managed.end()||(!live->second->status.retained&&!live->second->status.failure))
      &&(metadata==magnets.end()||!metadata->second->status.retained);
}
bool libraryUdpAuthorise(const detail::LibraryUdpTracker::Request& request) noexcept {
  try{
    if(!mainlineLease||!networkBinding||!network||network->mode!=NetworkMode::Normal||!network->dht||!network->udpTrackers||network->proxy.kind!=NetworkProxyKind::None||!networkBindingCurrent(*networkBinding)||mainlineScrapes.revoked(request.hash)||!libraryUdpUnadmitted(request.hash)){return false;}
    auto fresh=readCurrentMainlineScrapeTargets(*mainlineLease,{},true);if(!fresh){return false;}
    const auto target=std::ranges::find(*fresh,request.hash,&SavedMainlineScrapeTarget::hash);
    if(target==fresh->end()||target->admission||target->trackerResponseKey!=request.responseKey||target->scope!=request.scope){return false;}
    auto route=detail::libraryUdpRoute(networkBinding->effective,target->configuredTracker);if(!route){return false;}
    auto config=detail::libraryUdpConfiguration(*mainlineLease);if(!config){return false;}
    return detail::libraryUdpRouteScope(*route,*config)==request.configuration;
  }catch(...){return false;}
}
void advanceLibraryUdp(Clock::time_point now){
  if(!libraryUdp.active()){return;}
  const auto responseKey=libraryUdp.active()->responseKey;
  const auto millis=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  const auto wall=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  auto result=libraryUdp.poll(millis,wall,[&](const auto& request){return libraryUdpAuthorise(request);});
  const auto response=trackerResponses.find(responseKey);
  if(!result){if(response!=trackerResponses.end()){response->second->cancelRequest();}libraryUdp.cancel();mainlineScrapeFailure=result.error();return;}
  if(*result){if(response!=trackerResponses.end()){response->second->observe((**result).terminal);}}
  else if(!libraryUdp.active()&&response!=trackerResponses.end()){response->second->cancelRequest();}
}
bool prepareLibraryUdp(SavedMainlineScrapeTarget& target,std::int64_t now,std::string_view config,bool& pendingEligibility){
  pendingEligibility=false;
  if(!mainlineLease||!networkBinding||!network||!network->udpTrackers||!networkBindingCurrent(*networkBinding)||!libraryUdpUnadmitted(target.hash)){return false;}
  auto route=detail::libraryUdpRoute(networkBinding->effective,target.configuredTracker);if(!route){return false;}
  const auto configuration=detail::libraryUdpRouteScope(*route,config);
  auto found=trackerResponses.find(target.trackerResponseKey);
  if(found==trackerResponses.end()){
    if(trackerResponses.size()>=256){return false;}
    found=trackerResponses.emplace(target.trackerResponseKey,std::make_shared<core::QueueOriginalTrackerResponse>()).first;
  }
  if(found->second->regularDue(now)&&!libraryUdp.active()&&!libraryHttp.active()){
    if(libraryUdpNonce==UINT64_MAX){return false;}
    detail::LibraryUdpTracker::Request request{target.hash,target.trackerResponseKey,target.scope,configuration,++libraryUdpNonce};
    const auto wall=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto sent=libraryUdp.start(std::move(request),*route,now,wall);
    if(!sent){mainlineScrapeFailure=sent.error();return false;}found->second->requested();
  }
  if(const auto* request=libraryUdp.active();request&&request->hash==target.hash&&request->responseKey==target.trackerResponseKey&&request->scope==target.scope&&request->configuration==configuration){
    pendingEligibility=found->second->eligibleDuringOwnedRequest();
  }
  // Configuration/route changes withdraw a previously dispatched DHT result.
  const auto scope=detail::originalTrackerUrlDigest(target.scope+configuration);if(!scope){return false;}
  constexpr char hex[]="0123456789abcdef";target.scope.clear();target.scope.reserve(64);for(const auto c:*scope){target.scope+=hex[c>>4];target.scope+=hex[c&15];}
  return true;
}
