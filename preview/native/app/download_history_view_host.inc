// Inside the existing external-worker-owned Host; reuses the SAME intake Store.
  quint64 downloadHistoryViewRevision=0,lastDownloadHistoryRequest=0;
  storage::DownloadHistorySnapshot downloadHistoryViewSnapshot;
  bool historyEditsAllowed()const {
    return normalNetwork&&!replacementReviewOnly&&!fileRecoveryRequired&&!recoveryDrainStarted
        &&!(storage::hasPendingCategoryDeletion(lease)||storage::hasPendingFileOperation(lease));
  }
  bool historyResetAvailable()const{
    // A retained key0/key1 graph has extra unsaved backend state not represented
    // by this bounded Public-library reconstruction. No implicit graph close.
    return historyEditsAllowed()&&engine&&!i2p&&!mixed&&engine->historyReconstructionAvailable();
  }
  std::expected<storage::DownloadHistoryCommit,storage::Error> resetDownloadHistory(std::stop_token stop,std::chrono::steady_clock::time_point end){
    if(!historyResetAvailable()){return std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"History Reset requires a complete stopped normal Public library and no private graph"});}
    auto source=engine->prepareHistoryReconstruction(lease,stop,end);if(!source){return std::unexpected(source.error());}
    return downloadHistoryStore->reset(downloadHistoryViewSnapshot,std::move(*source),stop,end);
  }
  void reloadDownloadHistory(std::stop_token stop){
    if(downloadHistoryViewRevision==std::numeric_limits<quint64>::max()){
      throw Failure{{storage::ErrorCode::Unsupported,"Download-history view revisions exhausted"}};
    }
    ++downloadHistoryViewRevision;downloadHistoryViewSnapshot={};
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    // Only explicit Reload may reopen a poisoned/closed optional owner. No
    // intent replay, primary fallback, reset or fresh-intake backfill occurs.
    if(downloadHistoryStore&&downloadHistoryStore->recoveryRequired()){
      take(downloadHistoryStore->close());downloadHistoryStore.reset();
    }
    if(!downloadHistoryStore){downloadHistoryStore=take(storage::DownloadHistoryStore::open(lease,stop,end));}
    downloadHistoryViewSnapshot=take(downloadHistoryStore->reload(stop,end));
    if(downloadHistoryViewSnapshot.writable()&&historyEditsAllowed()){
      // Explicit healthy inspection permits FUTURE fresh events only. The
      // consumed failed receipt is never retained, replayed or backfilled.
      downloadHistoryPaused=false;downloadHistoryIntakeNotice.clear();
    }
  }
  void fillDownloadHistory(ui::DownloadHistoryCommandPacket& packet,
      const std::shared_ptr<storage::DownloadHistorySnapshot>& holder){
    packet.generation=downloadHistoryViewRevision;
    if(!downloadHistoryViewSnapshot.valid()){return;}
    *holder=downloadHistoryViewSnapshot;
    packet.active={holder,holder->history(storage::DownloadHistorySide::Active)};
    packet.dead={holder,holder->history(storage::DownloadHistorySide::Dead)};
    packet.enabled=holder->enabled();packet.missingActive=holder->missing(storage::DownloadHistorySide::Active);
    packet.missingDead=holder->missing(storage::DownloadHistorySide::Dead);
    packet.readState=packet.missingActive&&packet.missingDead?ui::DownloadHistoryReadState::Missing:ui::DownloadHistoryReadState::Ready;
    switch(holder->review()){
      case storage::DownloadHistoryReadOnly::None:packet.review=ui::DownloadHistoryReview::None;break;
      case storage::DownloadHistoryReadOnly::PendingCandidate:packet.review=ui::DownloadHistoryReview::Candidate;break;
      case storage::DownloadHistoryReadOnly::PendingIntent:packet.review=ui::DownloadHistoryReview::Intent;break;
      case storage::DownloadHistoryReadOnly::DuplicatePairUid:packet.review=ui::DownloadHistoryReview::DuplicateUid;break;
    }
    packet.writable=holder->writable()&&historyEditsAllowed();
    packet.resetAvailable=packet.writable&&packet.enabled&&historyResetAvailable();
  }
