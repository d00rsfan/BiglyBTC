// Actual retained-result admission only. Review inputs never contain a URI,
// hash or replacement source; the private plan owns those from the real query.
bool SessionBridge::confirmMetasearchMagnet(ui::MetasearchMagnetSubmission command){
 if(profileClosed_||closingSession_||busy_||!guiMetasearchDownload_||guiMetasearchDownload_->key!=command.key
     ||!guiMetasearch_||guiMetasearch_->key!=command.key.query||!command.revision
     ||!guiMetasearchDownload_->receiver||guiMetasearchDownload_->receiver->stop_requested()
     ||!guiMetasearch_->receiver||guiMetasearch_->receiver->stop_requested()
     ||command.key.query.session!=publishedColumnLayoutSession_||command.destination.isEmpty()
     ||command.network!=ui::TorrentIntakeNetwork::Public){return false;}
 startingMetasearchDownload_=command.key;QPointer<SessionBridge> alive(this);
 const bool accepted=dispatch([this,command](std::stop_token stop){
  auto p=std::make_shared<ui::MetasearchDownloadPacket>();p->key=command.key;ui::Presentation library;
  try{
   checkpoint(stop);
   if(!host_||host_->columnLayoutSession!=command.key.query.session||!host_->metasearchDownloadCommand
       ||host_->metasearchDownloadCommand->key!=command.key||host_->metasearchMagnetRevision!=command.revision
       ||!host_->metasearchMagnetAdmission||!host_->metasearchMagnetHandle
       ||!host_->metasearchCommand||host_->metasearchCommand->key!=command.key.query
       ||host_->metasearchPacket.state!=ui::MetasearchState::Ready||!host_->rssFetchAllowed()
       ||host_->privateOnly()||host_->coexistence()||!host_->engine||!host_->engine->managedAvailable()
       ||storage::hasPendingOriginalCompaction(host_->lease)||storage::hasPendingTorrentIntake(host_->lease)){
    throw Failure{{storage::ErrorCode::Conflict,"This magnet review is stale or Public metadata admission is unavailable"}};
   }
   if(command.network!=ui::TorrentIntakeNetwork::Public){throw Failure{{storage::ErrorCode::Unsupported,"This review supports the Public network only"}};}
   if(!host_->metasearchMagnetHandle->current(host_->lease,host_->columnLayoutSession)
       ||host_->metasearchDownloadCommand->receiver->stop_requested()){
    throw Failure{{storage::ErrorCode::Cancelled,"The actual search source or receiver changed before confirmation"}};
   }
   const auto destination=command.destination.toUtf8();
   if(destination.isEmpty()||destination.size()>4096||destination.contains('\0')||QString::fromUtf8(destination)!=command.destination){throw Failure{{storage::ErrorCode::UnsafePath,"Choose one valid download folder"}};}
   const auto& hash=host_->metasearchMagnetAdmission->hash();
   // Hash comparisons only refuse duplicate live/library ownership; they never
   // mint admission authority or replace the actual private reviewed handle.
   take(host_->subscriptionTargetAvailable(hash));
   const auto existing=take(storage::readLibraryView(host_->lease));for(const auto& row:existing.items){if(row.saved.hash&&*row.saved.hash==hash){throw Failure{{storage::ErrorCode::Conflict,"This magnet is already in the library"}};}}
   engine::PendingMagnetCommitResult outcome;
   const auto published=host_->publishPending(hash,[&]()->std::expected<void,storage::Error>{
    outcome=host_->metasearchMagnetAdmission->confirm(host_->lease,host_->columnLayoutSession,
        std::filesystem::path(destination.toStdString()),host_->networkPolicy,stop);
    if(outcome.state!=engine::PendingMagnetCommitState::Committed){return std::unexpected(outcome.failure.value_or(storage::Error{storage::ErrorCode::Conflict,"The magnet confirmation did not commit"}));}
    return {};
   });
   if(outcome.state==engine::PendingMagnetCommitState::Committed){
    // Set committed truth before any optional admission, refresh or GUI work.
    p->magnetCommitted=true;p->state=ui::MetasearchDownloadState::Finished;
    if(!outcome.pending){throw std::runtime_error("Committed magnet has no retained pending result");}p->pendingHash=text(outcome.pending->hash.hex());
    const auto admitted=host_->startPending(*outcome.pending);p->magnetAdmitted=admitted.has_value();
    if(admitted){host_->admissionFailures.erase(outcome.pending->hash.hex());p->message=tr("Magnet saved; resolving metadata. The torrent will be added stopped.");}
    else{host_->admissionFailures.insert_or_assign(outcome.pending->hash.hex(),admitted.error());p->message=tr("Magnet saved. Its metadata request could not start: %1").arg(text(admitted.error().message));}
   }else if(outcome.state==engine::PendingMagnetCommitState::Uncertain){
    p->magnetUncertain=true;p->state=ui::MetasearchDownloadState::NeedsReview;
    p->message=tr("The magnet save needs profile recovery or review before retrying. No metadata request was started.");
    if(outcome.failure){p->message+=QStringLiteral("\n")+text(outcome.failure->message).left(1024);}
    host_->fileRecoveryRequired=true;
   }else{
    const auto error=outcome.failure.value_or(published?storage::Error{storage::ErrorCode::Conflict,"The magnet was not saved"}:published.error());
    throw Failure{error};
   }
   // Saving changes the exact watched configuration. Retire old actionable
   // rows after both committed and uncertain outcomes; never rebase an old
   // handle or call current() after save to relabel a durable result.
   if(host_->metasearch){host_->metasearch->cancel();}
   if(host_->metasearchCommand&&host_->metasearchCommand->receiver){host_->metasearchCommand->receiver->request_stop();}
   auto search=std::make_shared<ui::MetasearchPacket>(host_->metasearchPacket);search->sequence=++host_->metasearchSequence;search->state=ui::MetasearchState::Cancelled;
   search->message=p->magnetCommitted?tr("Magnet saved. Search again before selecting another result."):tr("The magnet save needs review. Earlier result actions were withdrawn.");
   for(auto& row:search->rows){row.downloadable=false;row.downloadReason=tr("Search again for a fresh source-bound result.");}
   host_->metasearchPacket=*search;host_->metasearchCommand.reset();postMetasearch(search);
  }catch(const Failure& error){
   if(!p->magnetCommitted&&!p->magnetUncertain){p->state=error.error.code==storage::ErrorCode::Cancelled?ui::MetasearchDownloadState::Cancelled:ui::MetasearchDownloadState::Rejected;p->message=text(error.error.message);}
  }catch(...){
   if(p->magnetCommitted){p->state=ui::MetasearchDownloadState::Finished;p->message=tr("Magnet saved. Inspect its metadata status in the library.");}
   else if(p->magnetUncertain){p->state=ui::MetasearchDownloadState::NeedsReview;p->message=tr("The magnet save needs profile recovery or review. No automatic retry was made.");}
   else{p->message=tr("The magnet confirmation failed before a committed result was returned; inspect profile status before retrying.");}
  }
  if(host_){host_->closeMetasearchDownload(false);if(p->magnetCommitted){try{library=host_->query({});}catch(...){}}}
  QMetaObject::invokeMethod(this,[this,p,library]{
   QPointer<SessionBridge> guard(this);finishMetasearchDispatch();if(!guard){return;}deliverMetasearchDownload(p);if(!guard){return;}
   if(library&&!profileClosed_&&!closingSession_&&p->key.query.session==publishedColumnLayoutSession_){emit snapshotReady(library);}
  },Qt::QueuedConnection);
 },false);
 if(alive&&!accepted){startingMetasearchDownload_.reset();}return accepted;
}
