void SessionBridge::deliverDownloadHistoryCommand(ui::DownloadHistoryCommandPresentation packet){
  if(!packet||!guiDownloadHistoryCommand_||packet->session!=publishedColumnLayoutSession_
      ||packet->session!=guiDownloadHistoryCommand_->session||packet->request!=guiDownloadHistoryCommand_->request
      ||packet->kind!=guiDownloadHistoryCommand_->kind){return;}
  guiDownloadHistoryCommand_.reset();emit downloadHistoryCommandFinished(std::move(packet));
}
bool SessionBridge::downloadHistoryCommand(ui::DownloadHistoryCommand request){
  if(!request.session||!request.request||request.session!=publishedColumnLayoutSession_
      ||busy_||closingSession_||guiDownloadHistoryCommand_){return false;}
  switch(request.kind){case ui::DownloadHistoryKind::Reload:case ui::DownloadHistoryKind::Erase:case ui::DownloadHistoryKind::Clear:case ui::DownloadHistoryKind::Reset:break;default:return false;}
  switch(request.side){case ui::DownloadHistorySide::Active:case ui::DownloadHistorySide::Dead:break;default:return false;}
  guiDownloadHistoryCommand_=request;const QPointer<SessionBridge> alive(this);
  const bool accepted=dispatch([this,request](std::stop_token stop){
    auto packet=std::make_shared<ui::DownloadHistoryCommandPacket>();
    packet->session=request.session;packet->request=request.request;packet->kind=request.kind;
    packet->side=request.side;packet->uid=request.uid;
    auto holder=std::make_shared<storage::DownloadHistorySnapshot>(); // Before any durable mutation.
    try{
      if(!host_||host_->columnLayoutSession!=request.session){throw Failure{{storage::ErrorCode::Conflict,"This history profile is no longer active"}};}
      auto& host=*host_;
      if(request.request<=host.lastDownloadHistoryRequest){throw Failure{{storage::ErrorCode::Conflict,"Download-history request already consumed"}};}
      host.lastDownloadHistoryRequest=request.request;checkpoint(stop);
      if(request.kind==ui::DownloadHistoryKind::Reload){host.reloadDownloadHistory(stop);packet->status=ui::DownloadHistoryStatus::Loaded;}
      else{
        if(!request.generation||request.generation!=host.downloadHistoryViewRevision
            ||!host.downloadHistoryViewSnapshot.valid()||!host.downloadHistoryStore){
          throw Failure{{storage::ErrorCode::Conflict,"Download-history presentation is stale; reload"}};
        }
        if(host.downloadHistoryViewRevision==std::numeric_limits<quint64>::max()){
          throw Failure{{storage::ErrorCode::Unsupported,"Download-history revisions exhausted"}};
        }
        auto access=take(host.lease.sourceAccess());
        if(!host.historyEditsAllowed()){throw Failure{{storage::ErrorCode::Unsupported,"History is read only in preview or pending profile review"}};}
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        auto result=[&]() -> storage::DownloadHistoryCommit {
          if(request.kind==ui::DownloadHistoryKind::Reset){return take(host.resetDownloadHistory(stop,end));}
          if(request.kind==ui::DownloadHistoryKind::Clear){return take(host.downloadHistoryStore->clear(host.downloadHistoryViewSnapshot,stop,end));}
          const auto side=request.side==ui::DownloadHistorySide::Active?storage::DownloadHistorySide::Active:storage::DownloadHistorySide::Dead;
          const auto* records=host.downloadHistoryViewSnapshot.history(side);
          if(!records||std::count_if(records->records.begin(),records->records.end(),[&](const auto& row){return row.uid==request.uid;})!=1){
            throw Failure{{storage::ErrorCode::Conflict,"Selected history record changed; reload"}};
          }
          return take(host.downloadHistoryStore->apply(host.downloadHistoryViewSnapshot,side,core::DownloadHistoryErase{request.uid},stop,end));
        }();
        packet->changed=result.changed;
        packet->status=result.changed?ui::DownloadHistoryStatus::Saved:ui::DownloadHistoryStatus::Unchanged;
        host.downloadHistoryViewSnapshot=std::move(result.snapshot);if(result.changed){++host.downloadHistoryViewRevision;}
        // Scope ends before value projection / queued Qt publication; no SDK work.
      }
      host.fillDownloadHistory(*packet,holder);
    }catch(const Failure& failure){
      if(packet->status==ui::DownloadHistoryStatus::Saved||packet->status==ui::DownloadHistoryStatus::Unchanged){packet->refreshFailure=text(failure.error.message).left(512);}
      else{packet->status=failure.error.code==storage::ErrorCode::Cancelled?ui::DownloadHistoryStatus::Cancelled:ui::DownloadHistoryStatus::Rejected;packet->failure=text(failure.error.message).left(512);}
    }catch(...){
      if(packet->status==ui::DownloadHistoryStatus::Saved||packet->status==ui::DownloadHistoryStatus::Unchanged){packet->refreshFailure=tr("Saved; reload history to refresh the view");}
      else{packet->status=ui::DownloadHistoryStatus::Rejected;packet->failure=tr("History is unavailable; reload before editing");}
    }
    QMetaObject::invokeMethod(this,[this,packet=std::move(packet)]{
      QPointer<SessionBridge> guard(this);deliverDownloadHistoryCommand(packet);if(!guard){return;}
      completionMonitor_->stop();busy_=false;emit busyChanged(false);
    },Qt::QueuedConnection);
  },false);
  if(alive&&!accepted){guiDownloadHistoryCommand_.reset();}return accepted;
}
