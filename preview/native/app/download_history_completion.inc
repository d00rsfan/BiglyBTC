// Included inside existing external-worker-owned SessionBridge::Host.
// A real owner event is consumed once AFTER its durable retired checkpoint.
// No startup scan, public hash issuer, timer, second writer or SDK/Qt call.
  QString consumeHistoryCompletions(std::stop_token stop,bool closing=false)noexcept{
    if(!engine){return {};}
    // Preview/review/NoCP never publish optional history. Abandon its actual
    // checkpoint metadata so the real retired owner can still be forgotten.
    if(!normalNetwork||replacementReviewOnly||recoveryDrainStarted){engine->discardHistoryCompletions();return {};}
    if(downloadHistoryPaused){engine->discardHistoryCompletions();return {};}
    if(fileRecoveryRequired||storage::hasPendingCategoryDeletion(lease)||storage::hasPendingFileOperation(lease)){return {};}
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    try{
      // Normal turns publish at most one optional record. Normal close may
      // drain more before lease release or owner replacement in that SAME bound.
      for(std::size_t n=0;n<(closing?ui::LibraryModel::MaximumRows:1);++n){
        if(stop.stop_requested()||std::chrono::steady_clock::now()>=end){
          throw Failure{{storage::ErrorCode::Cancelled,"History completion effort ended after the checkpoint"}};
        }
        auto pending=take(engine->takeHistoryCompletion());if(!pending){return {};}
        if(pending->warning){throw Failure{*pending->warning};}
        if(!pending->receipt){continue;}
        if(!downloadHistoryStore){downloadHistoryStore=take(storage::DownloadHistoryStore::open(lease,stop,end));}
        if(downloadHistoryStore->recoveryRequired()){throw Failure{{storage::ErrorCode::Conflict,"History owner needs explicit review"}};}
        const auto snapshot=take(downloadHistoryStore->reload(stop,end));
        (void)take(downloadHistoryStore->complete(snapshot,std::move(*pending->receipt),stop,end));
      }
      if(closing){throw Failure{{storage::ErrorCode::Unsupported,"History completion work exceeds the saved library bound"}};}
      return {};
    }catch(const Failure& failure){
      downloadHistoryPaused=true;engine->discardHistoryCompletions();
      return SessionBridge::tr("Transfer checkpoint saved; download history needs review (storage %1). The transfer checkpoint remains saved.").arg(static_cast<int>(failure.error.code));
    }catch(...){
      downloadHistoryPaused=true;engine->discardHistoryCompletions();
      return SessionBridge::tr("Transfer checkpoint saved; download history completion could not be recorded. Inspect history before retrying.");
    }
  }
