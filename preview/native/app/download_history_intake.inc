// Included only inside existing external-worker-owned SessionBridge::Host.
// No startup scan, timer, row callback, new pool or payload/SDK action.
  std::unique_ptr<storage::DownloadHistoryStore> downloadHistoryStore;
  bool downloadHistoryPaused=false;
  std::optional<bool> historyManualTagsAtOpen;
  std::optional<storage::Error> historyManualTagModeFailure;
  void initializeHistoryRemovalTags(std::stop_token stop)noexcept{
    if(historyManualTagsAtOpen||historyManualTagModeFailure){return;}
    try{auto value=storage::ManualTagStore::historyRemovalModeAtOpen(lease,stop);if(value){historyManualTagsAtOpen=*value;}else{historyManualTagModeFailure=std::move(value.error());}}
    catch(...){try{historyManualTagModeFailure=storage::Error{storage::ErrorCode::Io,"Manual-tag startup mode unavailable"};}catch(...){}}
  }
  std::expected<storage::CapturedRemovalTags,storage::Error> captureHistoryRemovalTags(const storage::PreparedFileOperation& prepared,std::stop_token stop){
    if(!normalNetwork||replacementReviewOnly||recoveryDrainStarted||!historyManualTagsAtOpen){return std::unexpected(historyManualTagModeFailure.value_or(storage::Error{storage::ErrorCode::Unsupported,"Normal startup manual-tag mode unavailable"}));}
    // Reuse the actual single worker Store. An uncertain owner is not reopened
    // by a removal; its ordinary explicit Reload remains the only reopen path.
    if(!manualTagStore){auto opened=storage::ManualTagStore::open(lease,stop);if(!opened){return std::unexpected(opened.error());}manualTagStore=std::move(*opened);}
    return manualTagStore->captureHistoryRemovalTags(lease,prepared,*historyManualTagsAtOpen,stop);
  }

  QString downloadHistoryIntakeNotice; // One bounded owning auxiliary warning, no event/retry queue.
  std::optional<storage::Error> consumeMagnetHistoryIntake(storage::IntakeHistoryOutcome outcome)noexcept{
    const auto warning=consumeHistoryIntake(std::move(outcome),{});
    if(warning.isEmpty()){return {};}
    downloadHistoryIntakeNotice=warning.left(512);
    return storage::Error{storage::ErrorCode::Conflict,"Torrent added; download history needs review. The import remains saved."};
  }
  QString consumeHistoryIntake(storage::IntakeHistoryOutcome outcome,std::stop_token stop)noexcept{
    if(!normalNetwork||replacementReviewOnly||recoveryDrainStarted){return {};}
    try{
      if(outcome.warning){throw Failure{*outcome.warning};}
      if(!outcome.receipt){return {};}
      if(downloadHistoryPaused||fileRecoveryRequired||(storage::hasPendingCategoryDeletion(lease)||storage::hasPendingFileOperation(lease))){
        throw Failure{{storage::ErrorCode::Conflict,"History intake requires explicit profile review"}};
      }
      const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
      if(!downloadHistoryStore){downloadHistoryStore=take(storage::DownloadHistoryStore::open(lease,stop,end));}
      if(downloadHistoryStore->recoveryRequired()){throw Failure{{storage::ErrorCode::Conflict,"History requires review; never replay intake"}};}
      const auto snapshot=take(downloadHistoryStore->reload(stop,end));
      (void)take(downloadHistoryStore->appendIntake(snapshot,std::move(*outcome.receipt),stop,end));
      return {};
    }catch(const Failure& error){
      downloadHistoryPaused=true;
      return SessionBridge::tr("Torrent added; download history needs review (storage %1). The import remains saved.").arg(static_cast<int>(error.error.code));
    }catch(...){
      downloadHistoryPaused=true;
      return SessionBridge::tr("Torrent added; download history could not be recorded. The import remains saved; inspect history before retrying.");
    }
  }

  QString consumeHistoryRemoval(storage::RemovalHistoryOutcome outcome,std::stop_token stop)noexcept{
    if(!normalNetwork||replacementReviewOnly||recoveryDrainStarted){return {};}
    try{
      if(outcome.warning){throw Failure{*outcome.warning};}if(!outcome.receipt){return {};}
      if(downloadHistoryPaused||fileRecoveryRequired||storage::hasPendingCategoryDeletion(lease)||storage::hasPendingFileOperation(lease)){
        throw Failure{{storage::ErrorCode::Conflict,"History removal requires explicit profile review"}};
      }
      const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
      if(!downloadHistoryStore){downloadHistoryStore=take(storage::DownloadHistoryStore::open(lease,stop,end));}
      if(downloadHistoryStore->recoveryRequired()){throw Failure{{storage::ErrorCode::Conflict,"History owner requires explicit review"}};}
      const auto snapshot=take(downloadHistoryStore->reload(stop,end));
      const auto saved=take(downloadHistoryStore->remove(snapshot,std::move(*outcome.receipt),stop,end));
      if(saved.changed&&outcome.tagWarning){return SessionBridge::tr("Torrent removed and history saved with its previous tags. Current manual tags could not be captured.");}
      return {};
    }catch(const Failure& failure){downloadHistoryPaused=true;return SessionBridge::tr("Torrent removed; download history needs review (storage %1). The removal remains saved.").arg(static_cast<int>(failure.error.code));}
    catch(...){downloadHistoryPaused=true;return SessionBridge::tr("Torrent removed; download history could not be updated. The removal remains saved; inspect history before retrying.");}
  }
