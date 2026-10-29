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
    if(!mainlineLease||!networkBinding||!network||network->mode!=NetworkMode::Normal||!network->udpTrackers||network->proxy.kind!=NetworkProxyKind::None
        ||(request.regularAuthority?!libraryUdpRegularMode():(!mainline||!network->dht))||!networkBindingCurrent(*networkBinding)||mainlineScrapes.revoked(request.hash)){return false;}
    auto fresh=request.regularAuthority?regularUdpTargets():readCurrentMainlineScrapeTargets(*mainlineLease,{},true);if(!fresh){return false;}
    const auto row=std::ranges::find(*fresh,request.hash,&SavedMainlineScrapeTarget::hash);
    const auto target=row==fresh->end()?std::nullopt:selectConfiguredMainlineTracker(*row,request.responseKey);
    if(!target||target->scope!=request.scope||(request.regularAuthority?!libraryHttpAdmissionCurrent(*target):(target->admission||!libraryUdpUnadmitted(request.hash)))){return false;}
    auto route=detail::libraryUdpRoute(networkBinding->effective,target->configuredTracker,request.regularAuthority);if(!route){return false;}
    auto config=detail::libraryUdpConfiguration(*mainlineLease,&networkBinding->effective);if(!config){return false;}
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
  if(*result){if(response!=trackerResponses.end()){
    auto terminal=(**result).terminal;
    if(terminal.kind==core::QueueTrackerTerminalKind::Scrape&&terminal.comparable&&terminal.complete>=0&&terminal.incomplete>=0&&terminal.downloaded>=-1){
      const auto delay=detail::libraryTrackerScrapeDelay(static_cast<std::int32_t>(terminal.complete));
      if(!delay){response->second->unavailable();return;}terminal.regularDelayMillis=*delay;
    }
    const bool accepted=response->second->observeChronological(terminal,(**result).observedNanos);response->second->cancelRequest();
    if(accepted&&(terminal.comparable||terminal.definitiveFailure)){
      observeLibraryHttpTime(responseKey,terminal,(**result).observedNanos);}}}
  else if(!libraryUdp.active()&&response!=trackerResponses.end()){response->second->cancelRequest();}
}
bool prepareLibraryUdp(SavedMainlineScrapeTarget& target,std::int64_t now,std::string_view config,bool& pendingEligibility,bool regularPoll=false,bool dispatch=true,bool regularAuthority=false){
  pendingEligibility=false;
  if(!mainlineLease||!networkBinding||!network||!network->udpTrackers||!networkBindingCurrent(*networkBinding)||(regularAuthority?!libraryHttpAdmissionCurrent(target):!libraryUdpUnadmitted(target.hash))){return false;}
  auto route=detail::libraryUdpRoute(networkBinding->effective,target.configuredTracker,regularAuthority);if(!route){return false;}
  const auto configuration=detail::libraryUdpRouteScope(*route,config);
  auto found=trackerResponses.find(target.trackerResponseKey);
  if(found==trackerResponses.end()){
    if(trackerResponses.size()>=256){return false;}
    found=trackerResponses.emplace(target.trackerResponseKey,std::make_shared<core::QueueOriginalTrackerResponse>()).first;
  }
  if(dispatch&&(regularPoll?found->second->regularPollDue(now):found->second->regularDue(now))&&!libraryUdp.active()&&!libraryHttp.active()){
    if(libraryUdpNonce==UINT64_MAX){return false;}
    detail::LibraryUdpTracker::Request request{target.hash,target.trackerResponseKey,target.scope,configuration,++libraryUdpNonce,regularAuthority};
    const auto wall=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if(!libraryUdpAuthorise(request)){return false;} // Fresh authority before socket/bind/first CONNECT.
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
