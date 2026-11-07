namespace {
QString rssFetchFailure(search::RssFetchError error){
  using E=search::RssFetchError;
  switch(error){
    case E::Tls:return SessionBridge::tr("HTTPS verification failed. Review the saved source and trust settings.");
    case E::Dns:return SessionBridge::tr("The feed address could not be resolved.");
    case E::Deadline:return SessionBridge::tr("Fetch timed out. Previous results are unchanged.");
    case E::Encoding:case E::Parse:return SessionBridge::tr("The feed format is unsupported or malformed.");
    case E::Limit:return SessionBridge::tr("The feed or redirect limit was exceeded.");
    case E::Source:case E::WrongWorker:return SessionBridge::tr("The profile or saved source changed; refresh Sources.");
    case E::UnsupportedRoute:return SessionBridge::tr("The saved feed route is unsupported.");
    default:return SessionBridge::tr("Fetch failed. Previous results are unchanged.");
  }
}
}
void SessionBridge::deliverRssCatalog(ui::RssCatalogPresentation packet){
  if(!packet||!guiRssCatalog_||packet->key!=*guiRssCatalog_||profileClosed_||packet->key.session!=publishedColumnLayoutSession_){return;}
  guiRssCatalog_.reset();emit rssCatalogReady(std::move(packet));
}
void SessionBridge::deliverRssFetch(ui::RssFetchPresentation packet){
  if(!packet||!guiRssFetch_||packet->key!=guiRssFetch_->key||profileClosed_||packet->key.session!=publishedColumnLayoutSession_){return;}
  const bool stopped=guiRssFetch_->receiver&&guiRssFetch_->receiver->stop_requested();
  if(packet->state==ui::RssFetchState::Started){if(!stopped){emit rssFetchReady(std::move(packet));}return;}
  if(stopped&&packet->state==ui::RssFetchState::Ready){auto cancelled=std::make_shared<ui::RssFetchPacket>();cancelled->key=packet->key;cancelled->state=ui::RssFetchState::Cancelled;cancelled->message=tr("Cancelled");packet=std::move(cancelled);}
  guiRssFetch_.reset();emit rssFetchReady(std::move(packet));
}
void SessionBridge::cancelRssFetch(ui::RssFetchKey key){
  if(guiRssFetch_&&guiRssFetch_->key==key&&guiRssFetch_->receiver){guiRssFetch_->receiver->request_stop();}
}
void SessionBridge::stopRssReceiver()noexcept{if(guiRssFetch_&&guiRssFetch_->receiver){guiRssFetch_->receiver->request_stop();}}
void SessionBridge::postRssFetch(ui::RssFetchPresentation packet){
  QMetaObject::invokeMethod(this,[this,packet=std::move(packet)]{deliverRssFetch(packet);},Qt::QueuedConnection);
}
void SessionBridge::finishRssDispatch(){
  rssStarting_.reset();QPointer<SessionBridge> alive(this);completionMonitor_->stop();busy_=false;emit busyChanged(false);if(!alive){return;}
}
void SessionBridge::rssDispatchInterrupted(){
  QPointer<SessionBridge> alive(this);
  if(rssStarting_&&guiRssFetch_&&*rssStarting_==guiRssFetch_->key){auto packet=std::make_shared<ui::RssFetchPacket>();packet->key=*rssStarting_;packet->state=ui::RssFetchState::Cancelled;packet->message=tr("Cancelled");stopRssReceiver();rssStarting_.reset();deliverRssFetch(packet);if(!alive){return;}}
  if(guiRssCatalog_){auto packet=std::make_shared<ui::RssCatalogPacket>();packet->key=*guiRssCatalog_;packet->failure=tr("Source read was interrupted; refresh Sources.");deliverRssCatalog(packet);}
}
void SessionBridge::closeWorkerRss(){
  if(!host_||!host_->rssCommand){return;}const auto key=host_->rssCommand->key;host_->closeRssFetch();
  auto packet=std::make_shared<ui::RssFetchPacket>();packet->key=key;packet->state=ui::RssFetchState::Cancelled;packet->message=tr("Cancelled");postRssFetch(std::move(packet));
}
bool SessionBridge::rssCatalog(ui::RssCatalogRequest request){
  if(profileClosed_||closingSession_||busy_||!request.session||!request.page||!request.request||request.session!=publishedColumnLayoutSession_||guiRssCatalog_
      ||(guiRssFetch_&&guiRssFetch_->receiver&&!guiRssFetch_->receiver->stop_requested())){return false;}
  guiRssCatalog_=request;const QPointer<SessionBridge> alive(this);
  const bool accepted=dispatch([this,request](std::stop_token stop){
    auto packet=std::make_shared<ui::RssCatalogPacket>();packet->key=request;
    try{
      checkpoint(stop);if(!host_||host_->columnLayoutSession!=request.session||request.request<=host_->rssLastRequest){throw Failure{{storage::ErrorCode::Conflict,"RSS source request is stale"}};}
      host_->rssLastRequest=request.request;if(host_->rssCommand){closeWorkerRss();}
      host_->rssCatalog.reset();host_->rssCatalogGeneration=request.request;
      auto catalog=rss_detail::readCatalog(host_->lease);
      if(catalog){host_->rssCatalog.emplace(std::move(*catalog));host_->fillRssCatalog(*packet);}
      else if(catalog.error().code!=storage::ErrorCode::Missing){packet->failure=tr("Saved RSS sources need review before fetching.");}
    }catch(...){packet->failure=tr("Sources could not be read; refresh after the current operation.");}
    QMetaObject::invokeMethod(this,[this,packet=std::move(packet)]{QPointer<SessionBridge> guard(this);finishRssDispatch();if(guard){deliverRssCatalog(packet);}},Qt::QueuedConnection);
  },false);
  if(alive&&!accepted){guiRssCatalog_.reset();}return accepted;
}
bool SessionBridge::rssFetch(ui::RssFetchCommand command){
  const auto& key=command.key;
  if(profileClosed_||closingSession_||busy_||guiRssFetch_||!command.receiver||command.receiver->stop_requested()||!key.session||!key.page||!key.request||!key.catalogGeneration||key.session!=publishedColumnLayoutSession_){return false;}
  guiRssFetch_=command;rssStarting_=key;const QPointer<SessionBridge> alive(this);
  const bool accepted=dispatch([this,command=std::move(command)](std::stop_token stop){
    auto packet=std::make_shared<ui::RssFetchPacket>();packet->key=command.key;
    try{
      checkpoint(stop);if(!host_||host_->columnLayoutSession!=command.key.session||command.key.request<=host_->rssLastRequest){throw Failure{{storage::ErrorCode::Conflict,"RSS fetch request is stale"}};}
      host_->rssLastRequest=command.key.request;
      if(command.receiver->stop_requested()){packet->state=ui::RssFetchState::Cancelled;packet->message=tr("Cancelled");}
      else if(!host_->rssFetchAllowed()||!host_->rssTargetCurrent(command.key)){packet->message=tr("This saved source is unavailable or changed; refresh Sources.");}
      else{
        if(host_->rssFetch){throw Failure{{storage::ErrorCode::Busy,"RSS fetch is still closing"}};}
        std::error_code pathError;const auto profileDirectory=std::filesystem::canonical(host_->profile,pathError);
        if(pathError){throw Failure{{storage::ErrorCode::UnsafePath,"RSS profile directory changed"}};}
        if(host_->subscriptionAcquisition||(host_->subscriptions&&host_->subscriptions->active())){throw Failure{{storage::ErrorCode::Busy,"A saved-source fetch is already active"}};}
        std::unique_ptr<search::RssHttpFetch> provider;
        if(rssFetchFactory_){provider=rssFetchFactory_(host_->lease,profileDirectory,host_->columnLayoutSession,command.key.target,command.receiver->get_token());}
        else{auto opened=search::RssHttpFetch::openOneShot(host_->lease,profileDirectory,host_->columnLayoutSession,
            {static_cast<std::size_t>(command.key.target.sourceIndex),command.key.target.id,command.key.target.uid.toStdString()},command.receiver->get_token());if(opened){provider=std::move(*opened);}}
        // Fresh factory returns an unapproved concrete owner. Recheck catalog
        // after construction, before any poll can approve its first DNS phase.
        // An injected concrete owner must also retain this exact source/token.
        const auto view=provider?provider->observation():search::RssFetchObservation{};
        if(!provider||!provider->matchesSavedRssSource(host_->lease,profileDirectory,host_->columnLayoutSession,
              {static_cast<std::size_t>(command.key.target.sourceIndex),command.key.target.id,command.key.target.uid.toStdString()},command.receiver->get_token())
            ||view.phase!=search::RssFetchPhase::BeforeDNS||view.gets||view.resolverSubmissions||view.failure
            ||command.receiver->stop_requested()||!host_->rssTargetCurrent(command.key)){
          if(provider){provider->requestCancel();provider->close();}packet->state=command.receiver->stop_requested()?ui::RssFetchState::Cancelled:ui::RssFetchState::Rejected;packet->message=tr("Source validation failed; refresh Sources before retrying.");
        }else{host_->rssFetch=std::move(provider);host_->rssCommand=command;packet->state=ui::RssFetchState::Started;packet->message=tr("Fetching selected source…");}
      }
    }catch(...){packet->state=command.receiver->stop_requested()?ui::RssFetchState::Cancelled:ui::RssFetchState::Rejected;packet->message=tr("Fetch could not start; refresh Sources before retrying.");}
    QMetaObject::invokeMethod(this,[this,packet=std::move(packet)]{QPointer<SessionBridge> guard(this);finishRssDispatch();if(guard){deliverRssFetch(packet);}},Qt::QueuedConnection);
  },false);
  if(alive&&!accepted){guiRssFetch_.reset();rssStarting_.reset();}return accepted;
}
void SessionBridge::pollRssFetch(){
  if(!host_||!host_->rssFetch||!host_->rssCommand){return;}
  auto packet=std::make_shared<ui::RssFetchPacket>();packet->key=host_->rssCommand->key;
  try{
    if(!host_->rssFetchAllowed()&&host_->rssCommand->receiver){host_->rssCommand->receiver->request_stop();}
    auto result=host_->rssFetch->poll(host_->lease,host_->columnLayoutSession);
    if(result&&!*result){return;}
    const bool stopped=host_->rssCommand->receiver&&host_->rssCommand->receiver->stop_requested();
    if(!result){packet->state=stopped||result.error()==search::RssFetchError::Cancelled?ui::RssFetchState::Cancelled:ui::RssFetchState::Rejected;packet->message=packet->state==ui::RssFetchState::Cancelled?tr("Cancelled"):rssFetchFailure(result.error());}
    else if(stopped){packet->state=ui::RssFetchState::Cancelled;packet->message=tr("Cancelled");}
    else{
      packet->state=ui::RssFetchState::Ready;packet->truncated=(**result).document.results.size()>ui::MaximumRssResultRows;
      for(const auto& row:(**result).document.results){if(packet->rows.size()==ui::MaximumRssResultRows){break;}
        const auto value=[](const auto& v){return v?v->left(2048):QString{};};const auto number=[](const auto& v){return v?QString::number(static_cast<qlonglong>(*v)):QStringLiteral("—");};
        packet->rows.push_back({value(row.title),number(row.size),number(row.seeds),number(row.peers),value(row.publicationText),value(row.torrentLink?row.torrentLink:row.detailsLink)});
      }
      packet->message=packet->truncated?tr("Showing the first 256 results. Links are text only."):tr("Fetch complete. Links are text only.");
    }
  }catch(...){packet->message=tr("Fetch failed; previous results are unchanged.");}
  // Joining a terminal provider is not a new GUI receiver cancellation.
  host_->closeRssFetch(false);postRssFetch(std::move(packet));
}
