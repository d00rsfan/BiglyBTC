// Included in biglybtc::app after SessionBridge::Host; worker owns all IO.
void SessionBridge::deliverCategoryCommand(ui::CategoryCommandPresentation packet){
  if(!packet||!guiCategoryCommand_||packet->session!=publishedColumnLayoutSession_
      ||packet->session!=guiCategoryCommand_->session||packet->request!=guiCategoryCommand_->request
      ||packet->kind!=guiCategoryCommand_->kind||packet->requestedTarget!=guiCategoryCommand_->target){return;}
  guiCategoryCommand_.reset();emit categoryCommandFinished(std::move(packet)); // Receiver may delete Bridge.
}
bool SessionBridge::categoryCommand(ui::CategoryCommand request){
  if(profileClosed_||!request.session||!request.request||request.session!=publishedColumnLayoutSession_
      ||busy_||closingSession_||guiCategoryCommand_||request.target.hash.size()>64
      ||(request.name&&request.name->size()>4096)){return false;}
  switch(request.kind){case ui::CategoryCommandKind::Reload:case ui::CategoryCommandKind::Create:
    case ui::CategoryCommandKind::Assign:case ui::CategoryCommandKind::Remove:case ui::CategoryCommandKind::ResumeRemove:break;default:return false;}
  guiCategoryCommand_=request;const QPointer<SessionBridge> alive(this);
  const bool accepted=dispatch([this,request=std::move(request)](std::stop_token stop){
    auto packet=std::make_shared<ui::CategoryCommandPacket>();
    packet->session=request.session;packet->request=request.request;packet->kind=request.kind;packet->requestedTarget=request.target;
    ui::Presentation refreshed;std::optional<ui::Details> details;
    bool attempted=false,saved=false;
    try{
      if(!host_||host_->columnLayoutSession!=request.session){throw Failure{{storage::ErrorCode::Conflict,"This category profile is no longer active"}};}
      auto& host=*host_;
      if(request.request<=host.lastCategoryRequest){throw Failure{{storage::ErrorCode::Conflict,"This category request was already consumed"}};}
      host.lastCategoryRequest=request.request;checkpoint(stop);
      if(request.kind==ui::CategoryCommandKind::Reload){host.reloadCategories(stop);packet->status=ui::CategoryCommandStatus::Loaded;}
      else{
        if(!request.generation||request.generation!=host.categoryRevision||!host.categorySnapshot.valid()||!host.categoryStore){
          throw Failure{{storage::ErrorCode::Conflict,"This category presentation is stale; reload categories"}};
        }
        if(host.categoryRevision==std::numeric_limits<quint64>::max()){
          throw Failure{{storage::ErrorCode::Unsupported,"Category presentation revisions are exhausted"}};
        }
        std::optional<std::string> name;
        if(request.name){const auto bytes=request.name->toUtf8();
          if(bytes.size()>4096||QString::fromUtf8(bytes)!=*request.name){throw Failure{{storage::ErrorCode::InvalidFormat,"Category name must be bounded valid UTF-8"}};}
          name=bytes.toStdString();
        }
        auto holder=std::make_shared<storage::CategorySnapshot>(); // Before any commit.
        const auto committedStatus=[&](bool changed){
          packet->changed=changed;saved=changed;
          if(changed){++host.categoryRevision;host.invalidateCategoryCaches();}
          packet->status=changed?ui::CategoryCommandStatus::Saved:ui::CategoryCommandStatus::Unchanged;
        };
        if(request.kind==ui::CategoryCommandKind::Assign){
          host.accepts(request.target);const auto hash=identity(request.target);
          if(host.pendingTargets.contains(hash.hex())){throw Failure{{storage::ErrorCode::Unsupported,"Resolve magnet metadata before assigning a category"}};}
          if(!host.categoryAssignment.valid()||host.categorySelection!=request.target||!host.categoryAssignment.hash()
              ||*host.categoryAssignment.hash()!=hash){throw Failure{{storage::ErrorCode::Conflict,"Reload categories for this current torrent selection"}};}
          // Retain the actual opaque displayed target preimage. A fresh read
          // here would bless an external edit made after the GUI presentation.
          // Only trusted synchronous filesystem work goes under source anchors.
          // No Qt delivery, SDK wait, library refresh or provider call in F.
          take(host.publishTarget(hash,[&]()->std::expected<void,storage::Error>{
            attempted=true;auto changed=host.categoryStore->assign(host.categorySnapshot,host.categoryAssignment,name,stop);
            if(!changed){return std::unexpected(changed.error());}committedStatus(changed->changed);
            if(changed->changed){return host.rebaseCategoryOwners(hash);}return {};
          }));
        }else{
          if(!name){throw Failure{{storage::ErrorCode::InvalidFormat,"Category name is missing"}};}
          // Capture actual owner identities before any multi-file mutation;
          // no provider/status call is made while the store holds its FS gate.
          std::vector<core::TorrentHash> retained;
          if((request.kind==ui::CategoryCommandKind::Remove||request.kind==ui::CategoryCommandKind::ResumeRemove)&&host.engine){
            for(const auto& row:host.engine->managedDownloads()){retained.push_back(row.hash);}
          }
          std::expected<storage::CategoryCommit,storage::Error> result=std::unexpected(storage::Error{storage::ErrorCode::Unsupported,"Category command is unavailable"});
          if(request.kind==ui::CategoryCommandKind::Create){attempted=true;result=host.categoryStore->create(host.categorySnapshot,*name,stop);}
          else{
            if(!categoryDeletionSupported(*host.categoryStore)){throw Failure{{storage::ErrorCode::Unsupported,"Category deletion requires the journal-enabled backend"}};}
            // Multi-file deletion is not a single-target source publication.
            // Keep active private/mixed authorities intact by refusing first.
            // Never close/checkpoint a private owner on top of a pending
            // journal. The old idle close is safe only BEFORE a new deletion.
            if(storage::hasPendingCategoryDeletion(host.lease)&&host.categoryRetainedOwners()){
              throw Failure{{storage::ErrorCode::Busy,"Pending category recovery conflicts with retained owners; preserve files for review"}};
            }
            if(storage::hasPendingFileOperation(host.lease)){throw Failure{{storage::ErrorCode::Conflict,"File-operation recovery must be reviewed before category removal or resume"}};}
            host.requireI2pIdle(true);
            if(host.categoryRetainedOwners()){
              throw Failure{{storage::ErrorCode::Busy,"Stop and retire Public torrents and metadata handles before deleting or resuming a category"}};
            }
            if(request.kind==ui::CategoryCommandKind::ResumeRemove){
              if(host.categoryRecovery.state!=storage::CategoryDeletionRecovery::State::Resumable
                  ||host.categoryRecovery.name!=*name){
                throw Failure{{storage::ErrorCode::Conflict,"Reload and select the exact acknowledged deletion before resuming"}};
              }
              attempted=true;result=host.categoryStore->resumeRemove(host.categorySnapshot,*name,stop);
            }else{attempted=true;result=categoryRemove(*host.categoryStore,host.categorySnapshot,*name,stop);}
          }
          auto changed=take(std::move(result));host.acceptCategories(std::move(changed.snapshot),holder);committedStatus(changed.changed);
          if(changed.changed){
            std::optional<storage::Error> firstFailure;
            for(const auto& hash:retained){auto rebased=host.rebaseCategoryOwners(hash);
              if(!rebased&&!firstFailure){firstFailure.emplace(std::move(rebased.error()));}}
            if(firstFailure){throw Failure{std::move(*firstFailure)};}
          }
        }
        host.categoryRecovery=take(host.categoryStore->inspectRemove(host.categorySnapshot,{}));
        if(packet->changed){
          // Save already happened: no post-commit cancellation checkpoint.
          // Refresh binds the same full hash to a NEW revision; never recheck
          // the caller's obsolete Target after our own library revision update.
          refreshed=host.query({});
        }
      }
      if(!request.target.hash.isEmpty()){
        const ui::Target fresh{request.target.hash,host.revision};host.categoryTarget(*packet,fresh,{});
        if(refreshed){details=host.categoryDetails(fresh);}
      }
    }catch(const Failure& failure){
      if(saved){packet->refreshFailure=text(failure.error.message).left(8192);}
      else{packet->status=failure.error.code==storage::ErrorCode::Cancelled?ui::CategoryCommandStatus::Cancelled:ui::CategoryCommandStatus::Rejected;
        packet->failure=text(failure.error.message).left(8192);}
      if(host_&&host_->columnLayoutSession==request.session&&(attempted||request.kind==ui::CategoryCommandKind::Reload)){
        host_->invalidateCategoryRead();
      }
    }catch(const std::bad_alloc&){
      if(saved){packet->refreshFailure=tr("Categories were saved; refresh failed. Reload before another edit.");}
      else{packet->status=ui::CategoryCommandStatus::Rejected;packet->failure=tr("Insufficient memory for category command; reload before retry");}
      if(host_&&host_->columnLayoutSession==request.session){host_->invalidateCategoryRead();}
    }
    if(host_&&host_->columnLayoutSession==request.session){host_->categoryState(*packet);}
    ui::ManualTagStatisticsPresentation statistics;try{statistics=manualTagPacket();}catch(...){}
    QMetaObject::invokeMethod(this,[this,packet=std::move(packet),refreshed=std::move(refreshed),details=std::move(details),statistics=std::move(statistics)]{
      QPointer<SessionBridge> guard(this);
      if(packet->session==publishedColumnLayoutSession_&&!profileClosed_){
        // All refreshed views precede Saved while busy still fences reentry.
        if(refreshed){emit snapshotReady(refreshed);if(!guard){return;}}
        if(details){emit detailsReady(*details);if(!guard){return;}}
        if(statistics){deliverManualTags(statistics);if(!guard){return;}}
        deliverCategoryCommand(packet);if(!guard){return;}
      }
      completionMonitor_->stop();busy_=false;emit busyChanged(false);
    },Qt::QueuedConnection);
  },false);
  if(alive&&!accepted){guiCategoryCommand_.reset();}return accepted;
}
