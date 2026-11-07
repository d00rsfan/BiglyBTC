// Real saved-source search. No query URL, parser recipe or result authority is
// accepted from a widget. GUI cancellation reaches the actual shared token.
namespace {
ui::MetasearchEngineKey metasearchKey(const search::SavedMetasearchSelector& value){return {static_cast<quint64>(value.sourceIndex),value.id,QByteArray::fromStdString(value.uid)};}
search::SavedMetasearchSelector metasearchSelector(const ui::MetasearchEngineKey& value){return {static_cast<std::size_t>(value.sourceIndex),value.id,value.uid.toStdString()};}
QString metasearchFailure(search::MetasearchServiceError error){
  using E=search::MetasearchServiceError;switch(error){
    case E::Cancelled:return SessionBridge::tr("Cancelled");
    case E::Source:case E::WrongWorker:return SessionBridge::tr("The saved engine or profile changed. Reload engines and search again.");
    case E::Unsupported:return SessionBridge::tr("This saved engine uses unsupported search options; no search was sent.");
    case E::Busy:return SessionBridge::tr("A saved-source operation is already active.");
    case E::Limit:return SessionBridge::tr("The bounded engine, query or result limit was exceeded.");
    case E::Parse:return SessionBridge::tr("The engine response could not be parsed using its saved definition.");
    case E::Fetch:return SessionBridge::tr("The engine request failed. Other engine results remain available.");
    default:return SessionBridge::tr("Search could not finish. Reload engines before retrying.");
  }
}
}
search::SavedMetasearchService& SessionBridge::ensureMetasearch(){
  if(!host_){throw Failure{{storage::ErrorCode::Conflict,"No current search profile"}};}
  if(!host_->metasearch){
    search::MetasearchFetchFactory factory;
    if(metasearchFetchFactory_){const auto actual=metasearchFetchFactory_;factory=[actual](const storage::ProfileLease& lease,const std::filesystem::path& path,std::uint64_t session,const search::SavedMetasearchSelector& selected,const std::string& query,std::stop_token receiver){return actual(lease,path,session,metasearchKey(selected),text(query),receiver);};}
    auto value=search::SavedMetasearchService::open(host_->lease,host_->profile,host_->columnLayoutSession,
        host_->normalNetwork?search::MetasearchServiceMode::Normal:search::MetasearchServiceMode::ReadOnlyPreview,std::move(factory));
    if(!value){throw Failure{{storage::ErrorCode::Conflict,"Saved search service could not open"}};}host_->metasearch=std::move(*value);
  }
  return *host_->metasearch;
}
void SessionBridge::stopMetasearchReceiver()noexcept{
  const auto download=guiMetasearchDownload_?guiMetasearchDownload_->receiver:std::shared_ptr<std::stop_source>{};
  const auto query=guiMetasearch_?guiMetasearch_->receiver:std::shared_ptr<std::stop_source>{};
  if(download){download->request_stop();}if(query){query->request_stop();}
}
void SessionBridge::cancelMetasearch(ui::MetasearchQueryKey key){
  if(guiMetasearch_&&guiMetasearch_->key==key){const auto download=guiMetasearchDownload_?guiMetasearchDownload_->receiver:std::shared_ptr<std::stop_source>{};const auto query=guiMetasearch_->receiver;if(download){download->request_stop();}if(query){query->request_stop();}}
}
void SessionBridge::cancelMetasearchDownload(ui::MetasearchDownloadKey key){if(guiMetasearchDownload_&&guiMetasearchDownload_->key==key&&guiMetasearchDownload_->receiver){guiMetasearchDownload_->receiver->request_stop();}}
void SessionBridge::deliverMetasearchCatalog(ui::MetasearchCatalogPresentation packet){if(!packet||!guiMetasearchCatalog_||packet->key!=*guiMetasearchCatalog_||profileClosed_||packet->key.session!=publishedColumnLayoutSession_){return;}guiMetasearchCatalog_.reset();emit metasearchCatalogReady(std::move(packet));}
void SessionBridge::deliverMetasearch(ui::MetasearchPresentation packet){
  if(!packet||!guiMetasearch_||packet->key!=guiMetasearch_->key||profileClosed_||packet->key.session!=publishedColumnLayoutSession_){return;}
  if(guiMetasearch_->receiver->stop_requested()&&packet->state!=ui::MetasearchState::Cancelled){return;}
  emit metasearchReady(std::move(packet)); // Retain actual query token after completion for Download.
}
void SessionBridge::deliverMetasearchDownload(ui::MetasearchDownloadPresentation packet){
  if(!packet||profileClosed_||packet->key.query.session!=publishedColumnLayoutSession_){return;}
  if(!guiMetasearchDownload_||packet->key!=guiMetasearchDownload_->key){
    // A confirmed durable intent can outlive the query/page. Preserve its
    // session-scoped truth without reopening a stale result/review widget.
    if(packet->magnetCommitted||packet->magnetUncertain){emit messageReady(packet->message);}return;
  }
  if(guiMetasearchDownload_->receiver->stop_requested()&&packet->state!=ui::MetasearchDownloadState::Cancelled&&packet->state!=ui::MetasearchDownloadState::Finished&&packet->state!=ui::MetasearchDownloadState::NeedsReview&&packet->state!=ui::MetasearchDownloadState::Rejected){return;}
  if(packet->state!=ui::MetasearchDownloadState::Started&&packet->state!=ui::MetasearchDownloadState::ReviewReady&&packet->state!=ui::MetasearchDownloadState::MagnetReviewReady){guiMetasearchDownload_.reset();}
  emit metasearchDownloadReady(std::move(packet));
}
void SessionBridge::postMetasearch(ui::MetasearchPresentation packet){QMetaObject::invokeMethod(this,[this,packet=std::move(packet)]{deliverMetasearch(packet);},Qt::QueuedConnection);}
void SessionBridge::postMetasearchDownload(ui::MetasearchDownloadPresentation packet){QMetaObject::invokeMethod(this,[this,packet=std::move(packet)]{deliverMetasearchDownload(packet);},Qt::QueuedConnection);}
void SessionBridge::finishMetasearchDispatch(){startingMetasearch_.reset();startingMetasearchDownload_.reset();completionMonitor_->stop();busy_=false;emit busyChanged(false);}
void SessionBridge::metasearchDispatchInterrupted(){
  QPointer<SessionBridge> alive(this);
  if(startingMetasearch_&&guiMetasearch_&&*startingMetasearch_==guiMetasearch_->key){auto p=std::make_shared<ui::MetasearchPacket>();p->key=*startingMetasearch_;p->sequence=1;p->state=ui::MetasearchState::Cancelled;p->message=tr("Search interrupted. Reload engines before retrying.");startingMetasearch_.reset();const auto receiver=guiMetasearch_->receiver;if(receiver){receiver->request_stop();}if(!alive){return;}deliverMetasearch(p);if(!alive){return;}}
  if(startingMetasearchDownload_&&guiMetasearchDownload_&&*startingMetasearchDownload_==guiMetasearchDownload_->key){auto p=std::make_shared<ui::MetasearchDownloadPacket>();p->key=*startingMetasearchDownload_;p->state=ui::MetasearchDownloadState::Cancelled;p->message=tr("Download cancelled");startingMetasearchDownload_.reset();const auto receiver=guiMetasearchDownload_->receiver;if(receiver){receiver->request_stop();}if(!alive){return;}deliverMetasearchDownload(p);if(!alive){return;}}
  if(guiMetasearchCatalog_){auto p=std::make_shared<ui::MetasearchCatalogPacket>();p->key=*guiMetasearchCatalog_;p->failure=tr("The saved engines could not be read. Reload after the current operation.");deliverMetasearchCatalog(p);}
}
void SessionBridge::closeWorkerMetasearch(){
  if(!host_){return;}
  if(host_->metasearchDownloadCommand){auto p=std::make_shared<ui::MetasearchDownloadPacket>();p->key=host_->metasearchDownloadCommand->key;p->state=ui::MetasearchDownloadState::Cancelled;p->message=tr("Download cancelled");postMetasearchDownload(p);}
  if(host_->metasearchCommand){auto p=std::make_shared<ui::MetasearchPacket>(host_->metasearchPacket);p->state=ui::MetasearchState::Cancelled;p->message=tr("Search cancelled");p->sequence=++host_->metasearchSequence;postMetasearch(p);}
  host_->closeMetasearch();
}
bool SessionBridge::metasearchCatalog(ui::MetasearchCatalogKey key){
  if(profileClosed_||closingSession_||busy_||guiMetasearchCatalog_||!key.session||!key.page||!key.request||key.session!=publishedColumnLayoutSession_){return false;}
  QPointer<SessionBridge> alive(this);stopMetasearchReceiver();if(!alive){return false;}guiMetasearchCatalog_=key;
  const bool accepted=dispatch([this,key](std::stop_token stop){
    auto p=std::make_shared<ui::MetasearchCatalogPacket>();p->key=key;
    try{checkpoint(stop);if(!host_||host_->columnLayoutSession!=key.session||key.request<=host_->metasearchLastRequest){throw Failure{{storage::ErrorCode::Conflict,"Stale search engine read"}};}
      closeWorkerMetasearch();host_->metasearchLastRequest=key.request;host_->metasearchCatalogGeneration=key.request;p->generation=key.request;
      auto raw=rss_detail::readCatalog(host_->lease);auto rows=ensureMetasearch().catalog(host_->lease,key.session);
      if(!rows){p->failure=metasearchFailure(rows.error());}
      else{if(raw){host_->metasearchCatalogSource.emplace(std::move(*raw));}host_->metasearchCatalogRows=std::move(*rows);
        p->normalNetwork=host_->rssFetchAllowed()&&host_->metasearchCatalogCurrent();
        for(const auto& row:host_->metasearchCatalogRows){if(p->engines.size()==ui::MaximumMetasearchEngines){p->truncated=true;break;}ui::MetasearchEngineChoice c;c.key=metasearchKey(row.selected);c.name=text(row.name).left(2048);c.enabled=row.selectionState&&(*row.selectionState==1||*row.selectionState==2);c.queryable=!row.issue&&row.recipe.has_value();c.reason=text(row.diagnostic).left(2048);if(c.reason.isEmpty()&&row.issue){c.reason=tr("This saved definition needs review before it can be queried.");}p->engines.push_back(std::move(c));}
      }
    }catch(...){p->failure=tr("Saved search engines could not be read.");}
    QMetaObject::invokeMethod(this,[this,p]{QPointer<SessionBridge> guard(this);finishMetasearchDispatch();if(guard){deliverMetasearchCatalog(p);}},Qt::QueuedConnection);
  },false);if(alive&&!accepted){guiMetasearchCatalog_.reset();}return accepted;
}
bool SessionBridge::metasearchQuery(ui::MetasearchQueryCommand command){
  const auto& key=command.key;
  if(profileClosed_||closingSession_||busy_||!command.receiver||command.receiver->stop_requested()||!key.session||!key.page||!key.request||!key.catalogGeneration||key.session!=publishedColumnLayoutSession_||command.engines.empty()||command.engines.size()>ui::MaximumMetasearchQueryEngines){return false;}
  QPointer<SessionBridge> alive(this);stopMetasearchReceiver();if(!alive){return false;}guiMetasearchDownload_.reset();guiMetasearch_=command;startingMetasearch_=key;
  const bool accepted=dispatch([this,command](std::stop_token stop){
    auto p=std::make_shared<ui::MetasearchPacket>();p->key=command.key;p->sequence=1;
    try{checkpoint(stop);if(!host_||host_->columnLayoutSession!=command.key.session||command.key.request<=host_->metasearchLastRequest){throw Failure{{storage::ErrorCode::Conflict,"Stale search request"}};}
      host_->metasearchLastRequest=command.key.request;host_->closeMetasearchDownload();
      if(!host_->rssFetchAllowed()||host_->privateOnly()||host_->coexistence()||command.key.catalogGeneration!=host_->metasearchCatalogGeneration||!host_->metasearchCatalogCurrent()){throw Failure{{storage::ErrorCode::Conflict,"Saved search engine catalog changed"}};}
      if(host_->rssFetch||host_->subscriptionAcquisition||(host_->subscriptions&&host_->subscriptions->active())){throw Failure{{storage::ErrorCode::Busy,"Another saved-source operation is active"}};}
      const auto query=command.query.toUtf8();if(query.isEmpty()||query.size()>static_cast<qsizetype>(ui::MaximumMetasearchQueryBytes)||query.contains('\0')||QString::fromUtf8(query)!=command.query){throw Failure{{storage::ErrorCode::InvalidFormat,"Search must be bounded UTF8 text"}};}
      std::vector<search::SavedMetasearchSelector> selected;std::set<std::size_t> indices;
      for(const auto& row:command.engines){if(!host_->metasearchSelected(row)||!indices.insert(static_cast<std::size_t>(row.sourceIndex)).second){throw Failure{{storage::ErrorCode::Conflict,"Selected search engine changed or is duplicated"}};}selected.push_back(metasearchSelector(row));p->engines.push_back({row,host_->metasearchName(row),{},ui::MetasearchEngineState::Queued,0});}
      auto started=ensureMetasearch().beginSearch(host_->lease,command.key.session,std::move(selected),query.toStdString(),command.receiver->get_token());
      if(!started){p->message=metasearchFailure(started.error());}
      else if(!host_->metasearchCatalogCurrent()||command.receiver->stop_requested()){host_->metasearch->cancel();p->state=ui::MetasearchState::Cancelled;p->message=tr("Saved source changed or search cancelled before any query was approved.");}
      else{p->queryNonce=*started;if(!p->engines.empty()){p->engines.front().state=ui::MetasearchEngineState::Searching;}p->state=ui::MetasearchState::Started;p->message=tr("Searching selected saved engines…");host_->metasearchCommand=command;host_->metasearchSequence=1;host_->metasearchPacket=*p;}
    }catch(const Failure& e){p->message=text(e.error.message);}catch(...){p->message=tr("Search could not start. Reload engines and try again.");}
    QMetaObject::invokeMethod(this,[this,p]{QPointer<SessionBridge> guard(this);finishMetasearchDispatch();if(guard){deliverMetasearch(p);}},Qt::QueuedConnection);
  },false);if(alive&&!accepted){guiMetasearch_.reset();startingMetasearch_.reset();}return accepted;
}
void SessionBridge::pollMetasearch(){
  if(!host_||!host_->metasearch||!host_->metasearchCommand){return;}
  const auto receiver=host_->metasearchCommand->receiver;
  if((receiver&&receiver->stop_requested())||!host_->rssFetchAllowed()){
    host_->metasearch->cancel();host_->closeMetasearchDownload();auto p=std::make_shared<ui::MetasearchPacket>(host_->metasearchPacket);p->sequence=++host_->metasearchSequence;p->state=ui::MetasearchState::Cancelled;for(auto& engine:p->engines){if(engine.state==ui::MetasearchEngineState::Queued||engine.state==ui::MetasearchEngineState::Searching){engine.state=ui::MetasearchEngineState::Cancelled;}}p->message=tr("Search cancelled. Earlier rows remain visible.");host_->metasearchCommand.reset();postMetasearch(p);return;
  }
  if(!host_->metasearch->active()){return;}
  auto update=host_->metasearch->poll(host_->lease,host_->columnLayoutSession);if(update&&!*update){return;}
  auto& p=host_->metasearchPacket;p.sequence=++host_->metasearchSequence;
  if(!update){p.state=ui::MetasearchState::Rejected;p.message=metasearchFailure(update.error());host_->metasearch->cancel();}
  else{const auto& u=**update;if(u.queryNonce!=p.queryNonce){host_->metasearch->cancel();p.state=ui::MetasearchState::Rejected;p.message=tr("Search result identity changed.");}
    else{const auto key=metasearchKey(u.engine.engine);for(auto& e:p.engines){if(e.key==key){e.state=u.cancelled?ui::MetasearchEngineState::Cancelled:u.engine.failure?ui::MetasearchEngineState::Failed:ui::MetasearchEngineState::Ready;e.results=u.engine.rows.size();e.message=text(u.engine.diagnostic).left(2048);if(e.message.isEmpty()&&u.engine.failure){e.message=metasearchFailure(*u.engine.failure);}}}
      for(const auto& value:u.engine.rows){if(p.rows.size()==ui::MaximumMetasearchRows){p.truncated=true;break;}const auto& v=value.values;ui::MetasearchResultRow row;row.key={value.key.queryNonce,value.key.rowId};row.engine=metasearchKey(value.engine);row.source=host_->metasearchName(row.engine);const auto str=[](const auto& s){return s?text(*s).left(2048):QString{};};row.title=str(v.title);row.publication=str(v.publicationText);row.link=(v.torrentLink?v.torrentLink:v.downloadButtonLink)?text(*(v.torrentLink?v.torrentLink:v.downloadButtonLink)).left(4096):QString{};row.details=str(v.detailsLink);if(v.size>=0){row.bytes=v.size;}if(v.seeds>=0){row.seeds=v.seeds;}if(v.peers>=0){row.peers=v.peers;}
        const auto url=QUrl::fromEncoded(row.link.toUtf8(),QUrl::StrictMode);const bool actualRawLink=v.rawTorrentLink&&!v.rawTorrentLink->empty();row.downloadable=actualRawLink&&!v.incomplete&&url.isValid()&&(((url.scheme()==QStringLiteral("http")||url.scheme()==QStringLiteral("https"))&&!url.host().isEmpty())||url.scheme()==QStringLiteral("magnet"));
        row.downloadReason=url.scheme()==QStringLiteral("magnet")?tr("Review this magnet identity, destination and Public network before saving or resolving metadata."):!actualRawLink?tr("This result has no actual mapped download link; the reconstructed page URL cannot authorize intake."):v.incomplete?tr("This engine result has an uncertain projection and cannot authorize a download."):row.downloadable?QString{}:tr("This result has no supported direct HTTP(S) torrent link. Copy Link remains available.");p.rows.push_back(std::move(row));}
      if(!u.done&&!u.cancelled){for(auto& engine:p.engines){if(engine.state==ui::MetasearchEngineState::Queued){engine.state=ui::MetasearchEngineState::Searching;break;}}}
      p.state=u.cancelled?ui::MetasearchState::Cancelled:u.done?ui::MetasearchState::Ready:ui::MetasearchState::Results;p.message=u.cancelled?tr("Search cancelled."):u.done?(u.hadFailures?tr("Search finished with engine failures. Successful source rows are shown above."):tr("Search complete. Select a direct result to review its torrent files.")):tr("%1 of %2 engines finished.").arg(static_cast<qulonglong>(u.completed)).arg(static_cast<qulonglong>(u.total));
    }
  }
  postMetasearch(std::make_shared<const ui::MetasearchPacket>(p));
}
bool SessionBridge::metasearchDownload(ui::MetasearchDownloadCommand command){
  if(profileClosed_||closingSession_||busy_||guiMetasearchDownload_||!guiMetasearch_||command.key.query!=guiMetasearch_->key||!command.receiver||command.receiver->stop_requested()||guiMetasearch_->receiver->stop_requested()||!command.key.request||!command.key.result.queryNonce||!command.key.result.rowId){return false;}
  guiMetasearchDownload_=command;startingMetasearchDownload_=command.key;QPointer<SessionBridge> alive(this);
  const bool accepted=dispatch([this,command](std::stop_token stop){
    auto p=std::make_shared<ui::MetasearchDownloadPacket>();p->key=command.key;
    try{checkpoint(stop);if(!host_||host_->columnLayoutSession!=command.key.query.session||command.key.request<=host_->metasearchLastRequest||!host_->metasearchCommand||host_->metasearchCommand->key!=command.key.query||host_->metasearchPacket.state!=ui::MetasearchState::Ready||!host_->rssFetchAllowed()||host_->privateOnly()||host_->coexistence()){throw Failure{{storage::ErrorCode::Conflict,"This search result is no longer available for review"}};}
      host_->metasearchLastRequest=command.key.request;if(host_->metasearchProviderActive()||host_->rssFetch||host_->subscriptionAcquisition||(host_->subscriptions&&host_->subscriptions->active())){throw Failure{{storage::ErrorCode::Busy,"Another saved-source operation is active"}};}
      const auto row=std::ranges::find(host_->metasearchPacket.rows,command.key.result,&ui::MetasearchResultRow::key);
      if(row==host_->metasearchPacket.rows.end()){throw Failure{{storage::ErrorCode::Conflict,"This result is no longer in the actual completed search"}};}
      if(QUrl::fromEncoded(row->link.toUtf8(),QUrl::StrictMode).scheme()==QStringLiteral("magnet")){
        if(!host_->engine||!host_->engine->managedAvailable()||storage::hasPendingOriginalCompaction(host_->lease)||storage::hasPendingTorrentIntake(host_->lease)){throw Failure{{storage::ErrorCode::Unsupported,"Public magnet metadata admission is unavailable until profile recovery and engine startup finish"}};}
        auto handle=ensureMetasearch().retainMagnetResult(host_->lease,host_->columnLayoutSession,{command.key.result.queryNonce,command.key.result.rowId},command.receiver->get_token());
        if(!handle){throw Failure{{storage::ErrorCode::Unsupported,"This actual saved result cannot authorize a Public magnet review; its hash, route or source is unsupported or changed"}};}
        auto prepared=search::MetasearchMagnetAdmission::prepare(host_->lease,std::filesystem::canonical(host_->profile),host_->columnLayoutSession,*handle,host_->networkPolicy,command.receiver->get_token());
        if(!prepared){throw Failure{prepared.error()};}
        take(host_->subscriptionTargetAvailable((*prepared)->hash()));
        const auto library=take(storage::readLibraryView(host_->lease));for(const auto& existing:library.items){if(existing.saved.hash&&*existing.saved.hash==(*prepared)->hash()){throw Failure{{storage::ErrorCode::Conflict,"This magnet is already in the library"}};}}
        if(host_->metasearchMagnetRevision==std::numeric_limits<quint64>::max()){throw Failure{{storage::ErrorCode::Conflict,"Magnet review identities are exhausted"}};}
        const auto& review=(*prepared)->description();p->magnetReview=ui::MetasearchMagnetReview{++host_->metasearchMagnetRevision,text(review.identity),text(review.name),host_->metasearchName(metasearchKey(review.engine)),text(review.query)};
        host_->metasearchMagnetHandle.emplace(std::move(*handle));host_->metasearchMagnetAdmission=std::move(*prepared);host_->metasearchDownloadCommand=command;
        p->state=ui::MetasearchDownloadState::MagnetReviewReady;p->message=tr("Review this Public magnet before any intent is saved or metadata request starts.");
      }else{
      auto handle=ensureMetasearch().retainResult(host_->lease,host_->columnLayoutSession,{command.key.result.queryNonce,command.key.result.rowId});if(!handle){throw Failure{{storage::ErrorCode::Conflict,"The selected result or saved source changed; search again"}};}
      const auto profile=std::filesystem::canonical(host_->profile);std::unique_ptr<search::MetasearchMetainfoFetch> fetch;
      if(metasearchMetainfoFactory_){fetch=metasearchMetainfoFactory_(host_->lease,profile,host_->columnLayoutSession,*handle,command.receiver->get_token());}
      else{auto opened=search::MetasearchMetainfoFetch::openOneShot(host_->lease,profile,host_->columnLayoutSession,*handle,command.receiver->get_token());if(opened){fetch=std::move(*opened);}}
      const auto view=fetch?fetch->observation():search::RssFetchObservation{};
      if(!fetch||!fetch->matchesHandle(host_->lease,profile,host_->columnLayoutSession,*handle,command.receiver->get_token())||view.phase!=search::RssFetchPhase::BeforeDNS||view.gets||view.resolverSubmissions||view.failure||command.receiver->stop_requested()||!handle->current(host_->lease,host_->columnLayoutSession)){
        if(fetch){fetch->requestCancel();fetch->close();}throw Failure{{storage::ErrorCode::Conflict,"The result owner does not match this query, source, profile and cancellation token"}};
      }
      host_->metasearchResultHandle.emplace(std::move(*handle));host_->metasearchMetadataFetch=std::move(fetch);host_->metasearchDownloadCommand=command;p->state=ui::MetasearchDownloadState::Started;p->message=tr("Fetching selected torrent metadata for a stopped review…");
      }
    }catch(const Failure& e){p->message=text(e.error.message);}catch(...){p->message=tr("The selected torrent metadata could not be fetched.");}
    QMetaObject::invokeMethod(this,[this,p]{QPointer<SessionBridge> guard(this);finishMetasearchDispatch();if(guard){deliverMetasearchDownload(p);}},Qt::QueuedConnection);
  },false);if(alive&&!accepted){guiMetasearchDownload_.reset();startingMetasearchDownload_.reset();}return accepted;
}
void SessionBridge::pollMetasearchDownload(){
  if(!host_||!host_->metasearchDownloadCommand){return;}auto p=std::make_shared<ui::MetasearchDownloadPacket>();p->key=host_->metasearchDownloadCommand->key;
  try{
    const bool stopped=host_->metasearchDownloadCommand->receiver->stop_requested();
    const bool sourceCurrent=host_->metasearchMagnetAdmission
        ?host_->metasearchMagnetHandle&&host_->metasearchMagnetHandle->current(host_->lease,host_->columnLayoutSession).has_value()
        :host_->metasearchResultHandle&&host_->metasearchResultHandle->current(host_->lease,host_->columnLayoutSession).has_value();
    if(stopped||!host_->rssFetchAllowed()||!sourceCurrent){p->state=stopped?ui::MetasearchDownloadState::Cancelled:ui::MetasearchDownloadState::Rejected;p->message=stopped?tr("Download cancelled"):tr("The saved engine or query changed; the stopped review was withdrawn.");host_->closeMetasearchDownload(false);postMetasearchDownload(p);return;}
    if(!host_->metasearchMetadataFetch){return;}auto fetched=host_->metasearchMetadataFetch->poll(host_->lease,host_->columnLayoutSession);if(fetched&&!*fetched){return;}
    if(!fetched){p->state=fetched.error()==search::RssFetchError::Cancelled?ui::MetasearchDownloadState::Cancelled:ui::MetasearchDownloadState::Rejected;p->message=rssFetchFailure(fetched.error());host_->closeMetasearchDownload(false);postMetasearchDownload(p);return;}
    const auto& value=**fetched;const auto observation=host_->metasearchMetadataFetch->observation();
    if(value.session!=host_->columnLayoutSession||value.nonce!=observation.nonce||observation.failure||!observation.joined||value.bytes.isEmpty()||value.bytes.size()>8*1024*1024){throw Failure{{storage::ErrorCode::Conflict,"Torrent metadata completion did not retain its actual joined owner"}};}
    host_->metasearchMetadataFetch->close();host_->metasearchMetadataFetch.reset();
    host_->metasearchTemporary=std::make_unique<QTemporaryDir>(QDir::tempPath()+QStringLiteral("/BiglyBTC-search-XXXXXX"));if(!host_->metasearchTemporary->isValid()){throw Failure{{storage::ErrorCode::Io,"Owned metadata directory could not be created"}};}
    host_->metasearchTemporaryFile=std::make_unique<QTemporaryFile>(host_->metasearchTemporary->filePath(QStringLiteral("result-XXXXXX.torrent")));
    auto& file=*host_->metasearchTemporaryFile;if(!file.open()||!file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner)||file.write(value.bytes)!=value.bytes.size()||!file.flush()){throw Failure{{storage::ErrorCode::Io,"Owned torrent metadata could not be written"}};}file.close();
    const auto filename=take(storage::readOriginalFilenameContext(host_->lease));
    auto metadata=take(storage::inspectTorrentIntake(std::filesystem::path(file.fileName().toUtf8().toStdString()),host_->metasearchDownloadCommand->receiver->get_token(),filename.conversions,filename.decoderShowAll));
    if(take(storage::readOriginalFilenameContext(host_->lease))!=filename||!host_->metasearchResultHandle->current(host_->lease,host_->columnLayoutSession)||host_->metasearchDownloadCommand->receiver->stop_requested()){throw Failure{{storage::ErrorCode::Conflict,"The search source or filename context changed during metadata inspection"}};}
    const auto& expected=host_->metasearchResultHandle->description().expectedV1Hash;
    if(expected&&!std::ranges::equal(metadata.hash().bytes(),*expected)){throw Failure{{storage::ErrorCode::Conflict,"Fetched torrent identity differs from the actual search result"}};}
    if(metadata.layout().files.size()>ui::MaximumIntakePreviewFiles||host_->intakePreviewRevision==std::numeric_limits<quint64>::max()){throw Failure{{storage::ErrorCode::Unsupported,"The metadata exceeds the bounded file-choice review"}};}
    auto review=std::make_shared<ui::TorrentIntakePacket>();review->source={host_->columnLayoutSession,p->key.request,file.fileName()};review->revision=++host_->intakePreviewRevision;review->hash=text(metadata.hash().hex());review->name=text(metadata.layout().name);review->trackers=metadata.trackers();review->initialDestinations=metadata.initialDestinations();review->multifile=metadata.layout().multifile;review->decoderShowAll=metadata.decoderShowAll();
    for(std::size_t n=0;n<metadata.layout().files.size();++n){const auto& f=metadata.layout().files[n];review->files.push_back({static_cast<int>(n),text(f.path),f.bytes,f.padding});}
    if(!ui::validTorrentIntake(review)){throw Failure{{storage::ErrorCode::Unsupported,"The torrent cannot be represented in the file-choice editor"}};}
    host_->intakeRequest=review->source;host_->intakeMetadata.emplace(std::move(metadata));host_->metasearchReviewRequest=review->source.request;p->state=ui::MetasearchDownloadState::ReviewReady;p->message=tr("Review the destination and files. This action adds the torrent stopped.");p->review=review;postMetasearchDownload(p);
  }catch(const Failure& e){p->message=text(e.error.message);host_->closeMetasearchDownload(false);postMetasearchDownload(p);}catch(...){p->message=tr("The torrent metadata could not be prepared for review.");host_->closeMetasearchDownload(false);postMetasearchDownload(p);}
}
