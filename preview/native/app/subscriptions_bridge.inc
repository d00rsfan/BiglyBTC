namespace subscription_bridge_detail {
ui::SubscriptionKey key(const search::SavedSubscriptionSelector& source){
  ui::SubscriptionKey result;result.sourceIndex=source.sourceIndex;result.sid=QByteArray::fromStdString(source.shortId);result.publicKey=QByteArray::fromStdString(source.publicKey);result.singleton=source.singletonKey.has_value();if(source.singletonKey){result.singletonKey=QByteArray::fromStdString(*source.singletonKey);}result.version=source.version;return result;
}
search::SavedSubscriptionSelector selector(const ui::SubscriptionKey& source){
  return {static_cast<std::size_t>(source.sourceIndex),source.sid.toStdString(),source.publicKey.toStdString(),source.singleton?std::optional{source.singletonKey.toStdString()}:std::nullopt,source.version};
}
bool valid(const ui::SubscriptionKey& source){return source.sourceIndex<4096&&source.sid.size()==10&&source.publicKey.size()>0&&source.publicKey.size()<=256&&source.version>0&&(!source.singleton||(!source.singletonKey.isEmpty()&&source.singletonKey.size()<=4096));}
QString failure(search::SubscriptionServiceError value){
  using E=search::SubscriptionServiceError;switch(value){
  case E::PendingWrite:return SessionBridge::tr("A saved subscription write needs recovery. Close and reopen this profile before retrying.");
  case E::Unsupported:return SessionBridge::tr("This subscription needs an unsupported filter, route, notification or acquisition effect. Its history is unchanged.");
  case E::Busy:return SessionBridge::tr("A subscription check or host rate limit is still active.");
  case E::Limit:return SessionBridge::tr("The bounded subscription limit was reached.");
  case E::Cancelled:return SessionBridge::tr("Cancelled");
  default:return SessionBridge::tr("The profile or subscription changed. Refresh Subscriptions before retrying.");}
}
void rows(ui::SubscriptionPacket& packet,const search::SubscriptionResults& results){
  for(const auto& value:results.rows){if(value.deleted){continue;}if(packet.rows.size()==ui::MaximumSubscriptionRows){packet.truncated=true;break;}
    const auto object=QJsonDocument::fromJson(QByteArray::fromStdString(value.json)).object();const auto field=[&](const char* name){return object.value(QLatin1String(name)).toString().left(2048);};
    auto link=field("dl");if(link.isEmpty()){link=field("pl");}if(link.isEmpty()){link=field("cdp");}
    packet.rows.push_back({QByteArray::fromStdString(value.key),value.read,field("n"),field("lb"),field("s"),field("p"),field("d"),std::move(link)});
  }
}
}
search::SavedSubscriptionService& SessionBridge::ensureSubscriptions(){
  if(!host_){throw Failure{{storage::ErrorCode::Conflict,"Subscription profile is closed"}};}
  const bool normal=host_->rssFetchAllowed();
  if(host_->subscriptions&&host_->subscriptionsNormal!=normal){closeWorkerSubscriptions();}
  if(!host_->subscriptions){
    std::error_code ec;const auto profile=std::filesystem::canonical(host_->profile,ec);if(ec){throw Failure{{storage::ErrorCode::UnsafePath,"Subscription profile changed"}};}
    search::SubscriptionFetchFactory factory;
    if(subscriptionFetchFactory_){factory=[chosen=subscriptionFetchFactory_](const storage::ProfileLease& lease,const std::filesystem::path& path,std::uint64_t session,const search::SavedSubscriptionSelector& selected,std::stop_token stop){return chosen(lease,path,session,subscription_bridge_detail::key(selected),stop);};}
    auto opened=search::SavedSubscriptionService::open(host_->lease,profile,host_->columnLayoutSession,normal?search::SubscriptionServiceMode::Normal:search::SubscriptionServiceMode::ReadOnlyPreview,std::move(factory));
    if(!opened){throw Failure{{storage::ErrorCode::Conflict,"Subscription service cannot open safely"}};}host_->subscriptions=std::move(*opened);host_->subscriptionsNormal=normal;
  }return *host_->subscriptions;
}
void SessionBridge::deliverSubscriptionCatalog(ui::SubscriptionCatalogPresentation packet){
  if(!packet||!guiSubscriptionCatalog_||packet->key!=*guiSubscriptionCatalog_||profileClosed_||packet->key.session!=publishedColumnLayoutSession_){return;}
  guiSubscriptionCatalog_.reset();emit subscriptionCatalogReady(std::move(packet));
}
void SessionBridge::deliverSubscription(ui::SubscriptionPresentation packet){
  if(!packet||!guiSubscription_||packet->key!=guiSubscription_->key||profileClosed_||packet->key.session!=publishedColumnLayoutSession_){return;}
  const bool stopped=guiSubscription_->receiver&&guiSubscription_->receiver->stop_requested();
  if(packet->state==ui::SubscriptionState::Started){if(!stopped){emit subscriptionReady(std::move(packet));}return;}
  if(stopped&&!packet->committed){auto cancelled=std::make_shared<ui::SubscriptionPacket>();cancelled->key=packet->key;cancelled->state=ui::SubscriptionState::Cancelled;cancelled->message=tr("Cancelled");packet=std::move(cancelled);}
  guiSubscription_.reset();subscriptionCommitted_.reset();emit subscriptionReady(std::move(packet));
}
void SessionBridge::cancelSubscription(ui::SubscriptionCommandKey key){if(guiSubscription_&&guiSubscription_->key==key&&guiSubscription_->receiver){guiSubscription_->receiver->request_stop();}}
void SessionBridge::stopSubscriptionReceiver()noexcept{if(guiSubscription_&&guiSubscription_->receiver){guiSubscription_->receiver->request_stop();}}
void SessionBridge::postSubscription(ui::SubscriptionPresentation packet){QMetaObject::invokeMethod(this,[this,packet=std::move(packet)]{deliverSubscription(packet);},Qt::QueuedConnection);}
void SessionBridge::finishSubscriptionDispatch(){startingSubscription_.reset();QPointer<SessionBridge> alive(this);completionMonitor_->stop();busy_=false;emit busyChanged(false);if(!alive){return;}}
void SessionBridge::subscriptionsDispatchInterrupted(){
  QPointer<SessionBridge> alive(this);
  if(startingSubscription_&&guiSubscription_&&*startingSubscription_==guiSubscription_->key){auto packet=std::make_shared<ui::SubscriptionPacket>();packet->key=*startingSubscription_;packet->committed=subscriptionCommitted_&&subscriptionCommitted_->load();packet->state=packet->committed?ui::SubscriptionState::Ready:ui::SubscriptionState::Cancelled;packet->message=packet->committed?tr("Changes were saved. Refresh Subscriptions to read the current history."):tr("Cancelled");stopSubscriptionReceiver();startingSubscription_.reset();deliverSubscription(packet);if(!alive){return;}}
  if(guiSubscriptionCatalog_){auto packet=std::make_shared<ui::SubscriptionCatalogPacket>();packet->key=*guiSubscriptionCatalog_;packet->failure=tr("Subscription read was interrupted; refresh after the current operation.");deliverSubscriptionCatalog(packet);}
}
void SessionBridge::closeWorkerSubscriptions(){
  if(!host_){return;}closeWorkerSubscriptionAcquisition();if(host_->subscriptionCommand&&host_->subscriptionCommand->receiver){host_->subscriptionCommand->receiver->request_stop();}
  if(host_->subscriptions){auto closed=host_->subscriptions->close();if(!closed){throw Failure{{storage::ErrorCode::Conflict,"Subscription write recovery is pending"}};}host_->subscriptions.reset();}
  if(host_->subscriptionCommand){auto packet=std::make_shared<ui::SubscriptionPacket>();packet->key=host_->subscriptionCommand->key;packet->committed=host_->subscriptionCommitted&&host_->subscriptionCommitted->load();packet->state=packet->committed?ui::SubscriptionState::Ready:ui::SubscriptionState::Cancelled;packet->message=packet->committed?tr("Changes were saved. Refresh Subscriptions."):tr("Cancelled");host_->subscriptionCommand.reset();host_->subscriptionCommitted.reset();postSubscription(std::move(packet));}
  host_->subscriptionsNormal=false;
}
bool SessionBridge::subscriptionCatalog(ui::SubscriptionCatalogKey request){
  if(profileClosed_||closingSession_||busy_||!request.session||!request.page||!request.request||request.session!=publishedColumnLayoutSession_||guiSubscriptionCatalog_||guiSubscription_){return false;}
  guiSubscriptionCatalog_=request;const QPointer<SessionBridge> alive(this);
  const bool accepted=dispatch([this,request](std::stop_token stop){
    auto packet=std::make_shared<ui::SubscriptionCatalogPacket>();packet->key=request;
    try{checkpoint(stop);if(!host_||host_->columnLayoutSession!=request.session||request.request<=host_->subscriptionLastRequest){throw Failure{{storage::ErrorCode::Conflict,"Subscription request is stale"}};}
      host_->subscriptionLastRequest=request.request;host_->subscriptionCatalogGeneration=request.request;packet->generation=request.request;
      auto& service=ensureSubscriptions();auto catalog=service.catalog(host_->lease,request.session);packet->normalNetwork=host_->rssFetchAllowed();
      if(catalog){for(const auto& row:*catalog){if(packet->sources.size()==ui::MaximumSubscriptionRows){packet->truncated=true;break;}packet->sources.push_back({subscription_bridge_detail::key(row.selected),row.name.left(2048),row.issue?tr("Unsupported saved policy; history is read only"):tr("%1 read, %2 unread").arg(static_cast<qlonglong>(row.readCount)).arg(static_cast<qlonglong>(row.unreadCount)),!row.issue});}}
      else{packet->failure=subscription_bridge_detail::failure(catalog.error());}
    }catch(...){packet->failure=tr("Saved subscriptions could not be read safely.");}
    QMetaObject::invokeMethod(this,[this,packet=std::move(packet)]{QPointer<SessionBridge> guard(this);finishSubscriptionDispatch();if(guard){deliverSubscriptionCatalog(packet);}},Qt::QueuedConnection);
  },false);if(alive&&!accepted){guiSubscriptionCatalog_.reset();}return accepted;
}
bool SessionBridge::subscriptionCommand(ui::SubscriptionCommand command){
  const auto& key=command.key;
  switch(key.action){case ui::SubscriptionAction::ReadResults:case ui::SubscriptionAction::Check:case ui::SubscriptionAction::MarkRead:case ui::SubscriptionAction::MarkUnread:case ui::SubscriptionAction::Delete:case ui::SubscriptionAction::AcquireStopped:break;default:return false;}
  if(profileClosed_||closingSession_||busy_||guiSubscription_||!command.receiver||command.receiver->stop_requested()||!key.session||!key.page||!key.request||!key.generation||key.session!=publishedColumnLayoutSession_||!subscription_bridge_detail::valid(key.target)||command.resultKeys.size()>ui::MaximumSubscriptionRows){return false;}
  if(key.action==ui::SubscriptionAction::AcquireStopped&&(command.resultKeys.size()!=1||command.destination.isEmpty()
      ||command.destination.size()>4096||command.destination.contains(QChar::Null))){return false;}
  for(const auto& value:command.resultKeys){if(value.size()!=10){return false;}}
  guiSubscription_=command;startingSubscription_=key;subscriptionCommitted_=std::make_shared<std::atomic_bool>(false);const auto receipt=subscriptionCommitted_;const QPointer<SessionBridge> alive(this);
  const bool accepted=dispatch([this,command=std::move(command),receipt](std::stop_token stop){
    auto packet=std::make_shared<ui::SubscriptionPacket>();packet->key=command.key;
    try{checkpoint(stop);if(!host_||host_->columnLayoutSession!=command.key.session||command.key.request<=host_->subscriptionLastRequest||command.key.generation!=host_->subscriptionCatalogGeneration){throw Failure{{storage::ErrorCode::Conflict,"Subscription target is stale"}};}
      host_->subscriptionLastRequest=command.key.request;auto& service=ensureSubscriptions();const auto selected=subscription_bridge_detail::selector(command.key.target);
      if(command.receiver->stop_requested()){packet->state=ui::SubscriptionState::Cancelled;packet->message=tr("Cancelled");}
      else if(command.key.action!=ui::SubscriptionAction::ReadResults&&!host_->rssFetchAllowed()){packet->message=tr("Preview reads saved history only.");}
      else if(command.key.action!=ui::SubscriptionAction::ReadResults&&!storage::requireNoPendingSubscriptionAcquisition(host_->lease)){packet->message=tr("A subscription addition needs review before another change.");}
      else if(command.key.action==ui::SubscriptionAction::AcquireStopped){
        if(host_->i2p||host_->mixed){throw Failure{{storage::ErrorCode::Unsupported,"Adding a subscription torrent is unavailable while anonymous-network services are active"}};}
        if(host_->subscriptionAcquisition){throw Failure{{storage::ErrorCode::Busy,"A subscription addition is already closing"}};}
        std::error_code ec;const auto profile=std::filesystem::canonical(host_->profile,ec);if(ec){throw Failure{{storage::ErrorCode::UnsafePath,"Subscription profile changed"}};}
        SavedSubscriptionAcquisition::Factory factory;
        if(subscriptionAcquisitionFactory_){factory=[chosen=subscriptionAcquisitionFactory_](const storage::ProfileLease& lease,const std::filesystem::path& path,std::uint64_t session,const search::SavedSubscriptionResultSelector& target,std::stop_token token){
          return chosen(lease,path,session,subscription_bridge_detail::key(target.subscription),QByteArray::fromStdString(target.resultKey),token);};}
        auto opened=SavedSubscriptionAcquisition::begin(host_->lease,profile,command.key.session,
            {selected,command.resultKeys.front().toStdString()},std::filesystem::path(command.destination.toStdString()),command.receiver->get_token(),std::move(factory),
            [owner=host_.get()](const core::TorrentHash& hash){return owner->subscriptionTargetAvailable(hash);});
        if(!opened){packet->message=text(opened.error().message);}else{
          host_->subscriptionAcquisition=std::move(*opened);host_->acquisitionCommand=command;host_->acquisitionCommitted=receipt;
          packet->state=ui::SubscriptionState::Started;packet->message=tr("Fetching torrent metadata…");
        }
      }
      else if(command.key.action==ui::SubscriptionAction::Check){
        auto started=service.check(host_->lease,command.key.session,selected,command.receiver->get_token());if(!started){packet->message=subscription_bridge_detail::failure(started.error());}
        else{host_->subscriptionCommand=command;host_->subscriptionCommitted=receipt;packet->state=ui::SubscriptionState::Started;packet->message=tr("Checking saved subscription…");}
      }else{
        bool canRead=true;if(command.key.action!=ui::SubscriptionAction::ReadResults){std::vector<std::string> keys;for(const auto& value:command.resultKeys){keys.push_back(value.toStdString());}const auto action=command.key.action==ui::SubscriptionAction::MarkRead?search::SubscriptionResultAction::MarkRead:command.key.action==ui::SubscriptionAction::MarkUnread?search::SubscriptionResultAction::MarkUnread:search::SubscriptionResultAction::Delete;auto applied=service.changeResultState(host_->lease,command.key.session,selected,keys,action);if(!applied){packet->message=subscription_bridge_detail::failure(applied.error());canRead=false;}else{receipt->store(true);packet->committed=true;}}
        if(canRead){auto values=service.results(host_->lease,command.key.session,selected);if(values){subscription_bridge_detail::rows(*packet,*values);packet->state=ui::SubscriptionState::Ready;packet->message=tr("Saved results. Links are text only.");}else{packet->state=packet->committed?ui::SubscriptionState::Ready:ui::SubscriptionState::Rejected;packet->message=packet->committed?tr("Changes were saved; refresh to read the current history."):subscription_bridge_detail::failure(values.error());}}
      }
    }catch(...){packet->committed=receipt->load();packet->state=packet->committed?ui::SubscriptionState::Ready:ui::SubscriptionState::Rejected;packet->message=packet->committed?tr("Changes were saved. Refresh Subscriptions."):tr("The subscription operation could not complete safely.");}
    QMetaObject::invokeMethod(this,[this,packet=std::move(packet)]{QPointer<SessionBridge> guard(this);finishSubscriptionDispatch();if(guard){deliverSubscription(packet);}},Qt::QueuedConnection);
  },false);if(alive&&!accepted){guiSubscription_.reset();startingSubscription_.reset();subscriptionCommitted_.reset();}return accepted;
}
void SessionBridge::pollSubscriptions(){
  if(!host_){return;}
  // Preview may read history on request, but never creates a due-check provider.
  if(!host_->subscriptions&&!host_->rssFetchAllowed()){return;}
  try{
    auto& service=ensureSubscriptions();auto result=service.poll(host_->lease,host_->columnLayoutSession);
    if(!result){
      service.cancel();const auto failure=result.error();const auto retired=service.close();if(retired){host_->subscriptions.reset();}
      if(host_->subscriptionCommand){auto packet=std::make_shared<ui::SubscriptionPacket>();packet->key=host_->subscriptionCommand->key;packet->message=subscription_bridge_detail::failure(failure);host_->subscriptionCommand.reset();host_->subscriptionCommitted.reset();postSubscription(std::move(packet));}return;
    }if(!*result){return;}const auto& outcome=**result;
    if(outcome.stored&&host_->subscriptionCommitted){host_->subscriptionCommitted->store(true);}
    if(host_->subscriptionCommand){
      auto packet=std::make_shared<ui::SubscriptionPacket>();packet->key=host_->subscriptionCommand->key;packet->committed=outcome.stored;
      if(outcome.selected!=subscription_bridge_detail::selector(packet->key.target)){return;}
      if(outcome.stored){if(host_->subscriptionCommitted){host_->subscriptionCommitted->store(true);}packet->state=ui::SubscriptionState::Ready;auto values=service.results(host_->lease,host_->columnLayoutSession,outcome.selected);if(values){subscription_bridge_detail::rows(*packet,*values);}packet->message=values?tr("Subscription results were saved. Links are text only."):tr("Results were saved; refresh to read the current history.");}
      else if(outcome.cancelled){packet->state=ui::SubscriptionState::Cancelled;packet->message=tr("Cancelled");}
      else{packet->message=subscription_bridge_detail::failure(outcome.failure.value_or(search::SubscriptionServiceError::Source));}
      host_->subscriptionCommand.reset();host_->subscriptionCommitted.reset();postSubscription(std::move(packet));
    }else if(outcome.stored){const auto session=host_->columnLayoutSession;QMetaObject::invokeMethod(this,[this,session]{if(!profileClosed_&&session==publishedColumnLayoutSession_){emit subscriptionHistoryChanged(session);}},Qt::QueuedConnection);}
  }catch(...){if(host_->subscriptions){host_->subscriptions->cancel();const auto retired=host_->subscriptions->close();if(retired){host_->subscriptions.reset();}}if(host_->subscriptionCommand){auto packet=std::make_shared<ui::SubscriptionPacket>();packet->key=host_->subscriptionCommand->key;packet->committed=host_->subscriptionCommitted&&host_->subscriptionCommitted->load();packet->state=packet->committed?ui::SubscriptionState::Ready:ui::SubscriptionState::Rejected;packet->message=tr("Refresh Subscriptions to review the current saved history.");host_->subscriptionCommand.reset();host_->subscriptionCommitted.reset();postSubscription(std::move(packet));}}
}

std::expected<bool,storage::Error> SessionBridge::recoverSubscriptionAcquisitionBeforeHost(storage::ProfileLease& lease,const std::filesystem::path& path){
  std::error_code ec;const auto canonical=std::filesystem::canonical(path,ec);
  if(ec){return std::unexpected(storage::Error{storage::ErrorCode::UnsafePath,"Subscription profile directory changed"});}
  // These two startup paths have not constructed a Host or a torrent engine.
  auto recovered=SavedSubscriptionAcquisition::recoverBeforeHost(lease,canonical);
  if(!recovered){return std::unexpected(recovered.error());}
  return recovered->has_value()&&(**recovered).needsReview;
}
std::expected<bool,storage::Error> SessionBridge::recoverSubscriptionAcquisition(storage::ProfileLease& lease,const std::filesystem::path& path,SubscriptionTargetCheck check){
  std::error_code ec;const auto canonical=std::filesystem::canonical(path,ec);
  if(ec){return std::unexpected(storage::Error{storage::ErrorCode::UnsafePath,"Subscription profile directory changed"});}
  auto recovered=SavedSubscriptionAcquisition::recover(lease,canonical,std::move(check));
  if(!recovered){return std::unexpected(recovered.error());}
  return recovered->has_value()&&(**recovered).needsReview;
}
std::expected<bool,storage::Error> SessionBridge::recoverTorrentIntakeBeforeHost(storage::ProfileLease& lease,const std::filesystem::path& path){
  const auto recovered=recoverSubscriptionAcquisitionBeforeHost(lease,path);
  if(!recovered){return std::unexpected(recovered.error());}
  if(*recovered){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Subscription addition needs review before inventory recovery"});}
  const auto intake=storage::recoverTorrentIntake(lease);
  if(!intake){return std::unexpected(intake.error());}
  return intake->has_value();
}
std::expected<bool,storage::Error> SessionBridge::recoverTorrentIntakeAfterSubscription(storage::ProfileLease& lease,const std::filesystem::path& path,SubscriptionTargetCheck check){
  const auto recovered=recoverSubscriptionAcquisition(lease,path,std::move(check));
  if(!recovered){return std::unexpected(recovered.error());}
  if(*recovered){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Subscription addition needs review before inventory recovery"});}
  const auto intake=storage::recoverTorrentIntake(lease);
  if(!intake){return std::unexpected(intake.error());}
  return intake->has_value();
}
void SessionBridge::finishWorkerSubscriptionAcquisition(QString failure){
  if(!host_||!host_->subscriptionAcquisition){return;}
  auto outcome=host_->subscriptionAcquisition->outcome();const auto closed=host_->subscriptionAcquisition->close();
  outcome=host_->subscriptionAcquisition->outcome();if(!closed&&failure.isEmpty()){failure=text(closed.error().message);}
  const auto command=host_->acquisitionCommand;
  if((outcome.imported||outcome.alreadyPresent||outcome.needsReview)&&host_->acquisitionCommitted){host_->acquisitionCommitted->store(true);}
  host_->subscriptionAcquisition.reset();host_->acquisitionCommand.reset();host_->acquisitionCommitted.reset();
  if(!command){return;}
  auto packet=std::make_shared<ui::SubscriptionPacket>();packet->key=command->key;packet->committed=outcome.imported||outcome.alreadyPresent||outcome.needsReview;
  const bool stopped=command->receiver&&command->receiver->stop_requested();
  if(outcome.imported||outcome.alreadyPresent){
    packet->state=ui::SubscriptionState::Rejected; // Keep old rows unless a fresh read succeeds.
    packet->message=outcome.alreadyPresent?tr("Already present; linked and marked read.")
        :outcome.associated&&outcome.read?tr("Torrent added stopped; the subscription result was marked read.")
        :tr("Torrent added stopped. Review the subscription update before retrying.");
    if(host_->subscriptions&&storage::requireNoPendingSubscriptionAcquisition(host_->lease)){
      auto values=host_->subscriptions->results(host_->lease,host_->columnLayoutSession,subscription_bridge_detail::selector(command->key.target));
      if(values){subscription_bridge_detail::rows(*packet,*values);packet->state=ui::SubscriptionState::Ready;}
    }
    try{ready(host_->query({}));}catch(...){if(failure.isEmpty()){failure=tr("Refresh My Torrents to review the saved stopped torrent.");}}
  }else if(outcome.needsReview){
    packet->state=ui::SubscriptionState::Rejected;packet->message=tr("The addition needs review before retrying; saved files were retained.");
  }else{
    packet->state=stopped||outcome.cancelled?ui::SubscriptionState::Cancelled:ui::SubscriptionState::Rejected;
    packet->message=packet->state==ui::SubscriptionState::Cancelled?tr("Cancelled; no torrent was added."):tr("The subscription torrent could not be added.");
  }
  if(!outcome.warning.isEmpty()){packet->message+=' '+outcome.warning;}
  if(!failure.isEmpty()){packet->message+=' '+failure;}
  postSubscription(std::move(packet));
}
void SessionBridge::closeWorkerSubscriptionAcquisition(){
  if(!host_||!host_->subscriptionAcquisition){return;}
  if(host_->acquisitionCommand&&host_->acquisitionCommand->receiver){host_->acquisitionCommand->receiver->request_stop();}
  host_->subscriptionAcquisition->cancel();finishWorkerSubscriptionAcquisition();
}
void SessionBridge::pollSubscriptionAcquisition(){
  if(!host_||!host_->subscriptionAcquisition){return;}
  try{
    if(!host_->rssFetchAllowed()&&host_->acquisitionCommand&&host_->acquisitionCommand->receiver){host_->acquisitionCommand->receiver->request_stop();}
    if(host_->acquisitionCommand&&host_->acquisitionCommand->receiver&&host_->acquisitionCommand->receiver->stop_requested()){host_->subscriptionAcquisition->cancel();}
    auto polled=host_->subscriptionAcquisition->poll(host_->lease,host_->columnLayoutSession);
    if(!polled){finishWorkerSubscriptionAcquisition(text(polled.error().message));return;}
    if(host_->subscriptionAcquisition->readyForIntake()){
      const auto hash=host_->subscriptionAcquisition->intakeHash();storage::IntakeHistoryOutcome history;
      const auto imported=host_->publishTarget(hash,[&]{auto result=host_->subscriptionAcquisition->saveIntake(host_->lease,host_->columnLayoutSession,&history);
        if((host_->subscriptionAcquisition->outcome().imported||host_->subscriptionAcquisition->outcome().needsReview)&&host_->acquisitionCommitted){host_->acquisitionCommitted->store(true);}return result;});
      QString warning;if(history.receipt||history.warning){warning=host_->consumeHistoryIntake(std::move(history),{});}
      if(!imported){if(!warning.isEmpty()){warning+=' ';}warning+=text(imported.error().message);finishWorkerSubscriptionAcquisition(std::move(warning));return;}
      auto finished=host_->subscriptionAcquisition->finishSubscription(host_->lease,host_->columnLayoutSession);
      if(!finished){if(!warning.isEmpty()){warning+=' ';}warning+=text(finished.error().message);}
      finishWorkerSubscriptionAcquisition(std::move(warning));return;
    }
    if(polled->has_value()){finishWorkerSubscriptionAcquisition();}
  }catch(const Failure& failure){finishWorkerSubscriptionAcquisition(text(failure.error.message));}
  catch(...){finishWorkerSubscriptionAcquisition(tr("Review the subscription addition before retrying."));}
}
