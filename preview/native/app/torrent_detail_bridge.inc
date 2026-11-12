// SPDX-License-Identifier: GPL-2.0-or-later
void SessionBridge::setTorrentDetailInterest(ui::TorrentDetailInterest interest){
 const std::lock_guard guard(progressMutex_);detailInterest_=std::move(interest);
}
void SessionBridge::publishTorrentDetail(){
 if(!host_){return;}ui::TorrentDetailInterest interest;{const std::lock_guard guard(progressMutex_);interest=detailInterest_;}
 if(!interest.session||interest.session!=host_->columnLayoutSession||interest.hash.isEmpty()||host_->detailSequence==UINT64_MAX){if(host_->engine){host_->engine->clearTorrentInspection();}return;}
 const auto now=std::chrono::steady_clock::now();if(interest.request==host_->detailLastRequest&&now<host_->nextDetailSnapshot){return;}
 host_->detailLastRequest=interest.request;host_->nextDetailSnapshot=now+std::chrono::seconds(2);
 auto packet=std::make_shared<ui::TorrentDetailPacket>();packet->interest=interest;packet->sequence=++host_->detailSequence;packet->observedMillis=QDateTime::currentMSecsSinceEpoch();
 if(!host_->detailLayoutsAttempted){host_->detailLayoutsAttempted=true;auto config=storage::ConfigFile::load(host_->lease,"tables.config");if(config){const std::array names{"Files","Trackers","Peers","Pieces"};for(std::size_t n=0;n<names.size();++n){const auto* table=config->document().find(std::string("Table.")+names[n]);if(!table){table=config->document().find(std::string("TableDefault.")+names[n]);}if(!table){continue;}auto bytes=core::encodeValue(*table,storage::compatibleConfigLimits());if(!bytes){continue;}auto wrapped=core::Document::parse("d16:Table.MyTorrents"+*bytes+"e");if(!wrapped){continue;}auto layout=core::readColumnLayouts(*wrapped);if(layout){host_->detailLayouts[n]=std::make_shared<const core::SavedTableLayout>(std::move(layout->incomplete));}}}}
 switch(interest.section){case engine::InspectionSection::Files:packet->layout=host_->detailLayouts[0];break;case engine::InspectionSection::Trackers:packet->layout=host_->detailLayouts[1];break;case engine::InspectionSection::Peers:packet->layout=host_->detailLayouts[2];break;case engine::InspectionSection::Pieces:packet->layout=host_->detailLayouts[3];break;default:break;}
 const auto hash=core::TorrentHash::parse(QByteArray::fromHex(interest.hash.toLatin1()).toStdString());
 if(!hash||!host_->currentHashes.contains(interest.hash.toStdString())){return;}
 if(host_->detailMetadataHash!=interest.hash){host_->detailMetadataHash=interest.hash;host_->detailMetadata.reset();host_->detailSavedOptions.reset();const auto options=storage::readOriginalTorrentOptions(host_->lease,*hash);if(options){host_->detailSavedOptions=*options;}const auto metadata=engine::queryTorrent(host_->lease,*hash);if(metadata){host_->detailMetadata=*metadata;}else{packet->failure=text(metadata.error().message);}}
 packet->actual.hash=interest.hash.toStdString();
 if(host_->engine){if(const auto observation=host_->engine->latest()){packet->epoch=observation->identity.engineEpoch;}auto actual=host_->engine->inspectTorrent(*hash,interest.section,interest.firstPiece);if(actual){packet->actual=std::move(*actual);}else{packet->failure=text(actual.error().message);}}
 if(interest.section==engine::InspectionSection::Options&&host_->detailOptionsRevision!=host_->revision){host_->detailOptionsRevision=host_->revision;const auto options=storage::readOriginalTorrentOptions(host_->lease,*hash);host_->detailSavedOptions=options?std::optional{*options}:std::nullopt;}
 auto& actual=packet->actual;
 if(host_->detailSavedOptions){packet->savedOptions=host_->detailSavedOptions->values;packet->optionsKnown=true;packet->optionsWritable=host_->detailSavedOptions->stopped&&!actual.retained&&!host_->recheckTargets.contains(interest.hash.toStdString())&&!host_->pendingTargets.contains(interest.hash.toStdString());}

 if(host_->detailMetadata){const auto& meta=*host_->detailMetadata;
  if(actual.name.empty()){actual.name=meta.displayName.value_or(meta.name);actual.comment=meta.comment;actual.creator=meta.creator;actual.created=meta.created;actual.pieceLength=meta.pieceLength;actual.totalPieces=meta.pieces;actual.bytes=meta.bytes;actual.privateTorrent=meta.privateTorrent;}
  if(interest.section==engine::InspectionSection::Trackers&&actual.trackers.empty()){for(const auto& tracker:meta.trackers){engine::TrackerInspection row;row.url=tracker.first;row.tier=tracker.second;actual.trackers.push_back(std::move(row));}}
 }
 if(host_->engine&&(interest.section==engine::InspectionSection::Peers||interest.section==engine::InspectionSection::Swarm)){
  auto peers=host_->engine->inspectPeers(20000,interest.hash.toStdString());if(peers){auto rows=std::make_shared<ui::PeerPacket>();rows->session=interest.session;rows->layout=packet->layout;rows->sequence=packet->sequence;rows->available=true;rows->truncated=peers->truncated;rows->admitted=peers->admitted;
   for(const auto& peer:peers->peers){ui::PeerRow row;row.hash=interest.hash;row.name=text(actual.name);row.admission=peer.admission;row.address=text(peer.address);row.port=peer.port;row.localAddress=text(peer.localAddress);row.localPort=peer.localPort;row.client=text(peer.client);row.peerId=QString::fromLatin1(QByteArray(peer.peerId.data(),qsizetype(peer.peerId.size())).toHex());row.protocol=peer.webSeed?"HTTP":peer.utp?"uTP":"TCP";row.encryption=peer.encrypted?"Encrypted":"Plain";row.key=row.hash+'|'+QString::number(row.admission)+'|'+row.address+':'+QString::number(row.port)+'|'+row.localAddress+':'+QString::number(row.localPort)+'|'+row.protocol;
    row.received=peer.received;row.sent=peer.sent;row.downRate=peer.downRate;row.upRate=peer.upRate;row.protocolDownRate=peer.protocolDownRate;row.protocolUpRate=peer.protocolUpRate;row.state=peer.state;row.progressPpm=peer.progressPpm;row.pieces=peer.pieces;row.totalPieces=peer.totalPieces;row.rtt=peer.rtt;row.downloadRequests=peer.downloadRequests;row.uploadRequests=peer.uploadRequests;row.queuedBytes=peer.queuedBytes;row.incoming=peer.incoming;row.seed=peer.seed;row.interesting=peer.interesting;row.choked=peer.choked;row.remoteInterested=peer.remoteInterested;row.remoteChoked=peer.remoteChoked;row.snubbed=peer.snubbed;row.optimistic=peer.optimistic;
    if(peer.progressPpm>=0&&peer.totalPieces>0){row.availability=QByteArray(reinterpret_cast<const char*>(peer.availability.data()),qsizetype(peer.availability.size()));}rows->rows.push_back(std::move(row));
   }packet->peers=std::move(rows);
  }
 }
 {const std::lock_guard guard(progressMutex_);latestTorrentDetail_=std::move(packet);if(detailDeliveryQueued_){return;}detailDeliveryQueued_=true;}
 QMetaObject::invokeMethod(this,[this]{std::optional<ui::TorrentDetailPresentation> packet;ui::TorrentDetailInterest current;{const std::lock_guard guard(progressMutex_);packet.swap(latestTorrentDetail_);detailDeliveryQueued_=false;current=detailInterest_;}
  if(packet&&(*packet)->interest==current&&current.session==publishedColumnLayoutSession_&&!profileClosed_){emit torrentDetailReady(std::move(*packet));}
 },Qt::QueuedConnection);
}

bool SessionBridge::torrentOptions(ui::TorrentOptionsCommand command){
 if(profileClosed_||busy_||closingSession_||!command.request||command.session!=publishedColumnLayoutSession_){return false;}
 return dispatch([this,command](std::stop_token stop){
  QString failure;
  try{
   if(!host_||host_->columnLayoutSession!=command.session||command.request<=host_->lastTorrentOptionsRequest){throw Failure{{storage::ErrorCode::Conflict,"Torrent options command is stale"}};}
   host_->lastTorrentOptionsRequest=command.request;host_->accepts(command.target);checkpoint(stop);const auto hash=identity(command.target);
   if(host_->engine){for(const auto& row:host_->engine->managedDownloads()){if(row.hash==hash){if(row.retained){throw Failure{{storage::ErrorCode::Busy,"Wait for this torrent to stop and retire"}};}take(host_->engine->forgetManaged(hash));break;}}}
   take(host_->subscriptionTargetAvailable(hash));
   take(host_->publishTarget(hash,[&]{return storage::saveOriginalTorrentOptions(host_->lease,hash,command.rates,command.expected,command.desired);}));
   host_->detailMetadataHash.clear();host_->detailLastRequest=0;
  }catch(const Failure& error){failure=text(error.error.message);}
  QMetaObject::invokeMethod(this,[this,command,failure]{completionMonitor_->stop();busy_=false;QPointer<SessionBridge> guard(this);emit busyChanged(false);if(guard&&command.session==publishedColumnLayoutSession_){emit torrentOptionsFinished(command.session,command.request,failure);}},Qt::QueuedConnection);
 });
}

bool SessionBridge::torrentTracker(ui::TorrentTrackerCommand command){
 if(profileClosed_||busy_||closingSession_||!command.request||command.session!=publishedColumnLayoutSession_){return false;}
 return dispatch([this,command](std::stop_token stop){QString failure;
  try{if(!host_||!host_->engine||host_->columnLayoutSession!=command.session||command.request<=host_->lastTorrentTrackerRequest){throw Failure{{storage::ErrorCode::Conflict,"Tracker request is stale"}};}host_->lastTorrentTrackerRequest=command.request;host_->accepts(command.target);checkpoint(stop);take(host_->engine->requestTorrentTracker(identity(command.target),command.admission,command.scrape));}
  catch(const Failure& error){failure=text(error.error.message);}
  QMetaObject::invokeMethod(this,[this,command,failure]{completionMonitor_->stop();busy_=false;QPointer<SessionBridge> guard(this);emit busyChanged(false);if(guard&&command.session==publishedColumnLayoutSession_){emit torrentTrackerFinished(command.session,command.request,failure);}},Qt::QueuedConnection);
 });
}
