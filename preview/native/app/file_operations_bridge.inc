// Included after SessionBridge::Host, within biglybtc::app.
namespace {
storage::FileOperationRequest storageRequest(const ui::FileOperationRequest& request,bool deleteArchive=false,bool recycle=false) {
  storage::FileOperationKind kind;
  switch (request.operation) {
    case ui::FileOperation::RemoveKeepData: kind=storage::FileOperationKind::RemoveKeepData;break;
    case ui::FileOperation::RemoveDeleteData: kind=recycle?storage::FileOperationKind::RemoveRecycleData:storage::FileOperationKind::RemoveDeleteData;break;
    case ui::FileOperation::Relocate: kind=storage::FileOperationKind::Relocate;break;
    case ui::FileOperation::Relink: kind=storage::FileOperationKind::Relink;break;
    case ui::FileOperation::MoveFile: kind=storage::FileOperationKind::MoveFile;break;
    case ui::FileOperation::RetargetFile: kind=storage::FileOperationKind::RetargetFile;break;
    case ui::FileOperation::ReplaceFile: kind=storage::FileOperationKind::ReplaceFile;break;
    case ui::FileOperation::RevertFile: kind=storage::FileOperationKind::RevertFile;break;
    default: throw Failure{{storage::ErrorCode::Unsupported,"Unknown file operation"}};
  }
  storage::FileOperationRequest value{identity(request.target),kind,{}};
  const bool file=request.operation>=ui::FileOperation::MoveFile&&request.operation<=ui::FileOperation::ReplaceFile;
  if(file){if(request.originalIndex<0){throw Failure{{storage::ErrorCode::InvalidFormat,"Missing original file index"}};}value.fileIndex=static_cast<std::size_t>(request.originalIndex);
    if(request.operation!=ui::FileOperation::RevertFile){value.fileDestination=std::filesystem::path(request.destinationParent.toUtf8().toStdString());}}
  else{if(request.originalIndex!=-1){throw Failure{{storage::ErrorCode::InvalidFormat,"Unexpected original file index"}};}if(!request.destinationParent.isEmpty()){value.destinationParent=std::filesystem::path(request.destinationParent.toUtf8().toStdString());}}
  if(recycle){value.trashRoot=take(storage::linuxHomeTrashRoot());}
  value.deleteTorrentFile=deleteArchive;return value;
}
bool sameFilePlan(const ui::FileOperationPlanPacket& left,const ui::FileOperationPlanPacket& right) {
  return left.request==right.request&&left.approvalScopeDigest==right.approvalScopeDigest
      &&left.sourceRoot==right.sourceRoot&&left.targetRoot==right.targetRoot
      &&left.presentFiles==right.presentFiles&&left.missingFiles==right.missingFiles
      &&left.totalBytes==right.totalBytes&&left.failure==right.failure
      &&left.retainedPreimage==right.retainedPreimage&&left.priorDestinationBytes==right.priorDestinationBytes&&left.priorDestinationSha256==right.priorDestinationSha256
      &&left.deleteTorrentFile==right.deleteTorrentFile&&left.recyclePayload==right.recyclePayload&&left.archivePath==right.archivePath
      &&left.archiveBytes==right.archiveBytes&&left.archivePresent==right.archivePresent&&left.archiveInternalState==right.archiveInternalState
      &&left.archiveBackupPath==right.archiveBackupPath&&left.archiveBackupBytes==right.archiveBackupBytes&&left.archiveBackupPresent==right.archiveBackupPresent
      &&std::ranges::equal(left.paths,right.paths,[](const auto& a,const auto& b){
        return a.source==b.source&&a.destination==b.destination&&a.bytes==b.bytes&&a.present==b.present;
      });
}
ui::FileOperationPlanPresentation filePlanPacket(const ui::FileOperationRequest& request,const storage::FileOperationPreview& p) {
  auto packet=std::make_shared<ui::FileOperationPlanPacket>();packet->request=request;
  packet->approvalScopeDigest=text(p.approvalScopeDigest);packet->sourceRoot=QString::fromStdString(p.sourceRoot.string());packet->targetRoot=QString::fromStdString(p.targetRoot.string());
  if(p.replacement){packet->retainedPreimage=QString::fromStdString(p.replacement->path.string());packet->priorDestinationBytes=p.replacement->bytes;packet->priorDestinationSha256=text(p.replacement->sha256);}
  packet->recyclePayload=p.kind==storage::FileOperationKind::RemoveRecycleData;
  packet->presentFiles=p.presentFiles;packet->missingFiles=p.missingFiles;packet->totalBytes=p.presentBytes;
  for(const auto& file:p.files){packet->paths.push_back({QString::fromStdString(file.source.string()),QString::fromStdString(file.destination.string()),static_cast<quint64>(std::max<std::int64_t>(0,file.actualBytes)),file.present});}
  if(p.torrentArchive){const auto& archive=*p.torrentArchive;packet->deleteTorrentFile=true;packet->archivePath=QString::fromStdString(archive.path.string());packet->archiveBytes=archive.bytes;packet->archivePresent=archive.present;packet->archiveInternalState=archive.internalState;}
  if(p.torrentArchiveBackup){const auto& backup=*p.torrentArchiveBackup;packet->archiveBackupPath=QString::fromStdString(backup.path.string());packet->archiveBackupBytes=backup.bytes;packet->archiveBackupPresent=backup.present;}
  return packet;
}
}
bool SessionBridge::prepareFileOperation(ui::FileOperationRequest request) {
  if(profileClosed_||!request.session||!request.request||!request.revision||request.session!=publishedColumnLayoutSession_
      ||request.target.generation!=request.revision||request.target.hash.isEmpty()){return false;}
  const bool accepted=dispatch([this,request](std::stop_token stop){
    auto failure=std::make_shared<ui::FileOperationPlanPacket>();failure->request=request;ui::FileOperationPlanPresentation result=failure;
    try{
      if(!host_||host_->columnLayoutSession!=request.session){throw Failure{{storage::ErrorCode::Conflict,"This file-operation session is closed"}};}
      host_->filePlan.reset();host_->filePlanPacket.reset();
      if(host_->fileRecoveryRequired){throw Failure{{storage::ErrorCode::Conflict,"Restart to recover the interrupted file operation before changing the profile"}};}
      if(take(storage::recoverTorrentIntake(host_->lease))){throw Failure{{storage::ErrorCode::Conflict,"A torrent import was recovered; refresh the library"}};}
      host_->accepts(request.target);
      if(host_->pendingTargets.contains(request.target.hash.toStdString())){throw Failure{{storage::ErrorCode::Unsupported,"Metadata must be resolved before operating on torrent files"}};}
      const bool removal=request.operation==ui::FileOperation::RemoveKeepData||request.operation==ui::FileOperation::RemoveDeleteData;
      if(request.operation>=ui::FileOperation::MoveFile){const auto held=take(storage::readVerifiedSeedIntent(host_->lease,identity(request.target)));if(held){throw Failure{{storage::ErrorCode::Unsupported,"Per-file edits of verified-existing seeds need a new approved read-only scope; no data was changed"}};}}
      const bool deleteArchive=removal?(request.deleteTorrentFile?*request.deleteTorrentFile:take(storage::torrentArchiveDeletionPreference(host_->lease,stop))):false;
      if(!removal&&request.deleteTorrentFile.value_or(false)){throw Failure{{storage::ErrorCode::Unsupported,"Archive deletion requires a removal request"}};}
      const bool recycle=request.operation==ui::FileOperation::RemoveDeleteData?(request.recyclePayload?*request.recyclePayload:take(storage::payloadRecyclePreference(host_->lease,stop))):false;
      if(request.operation!=ui::FileOperation::RemoveDeleteData&&request.recyclePayload.value_or(false)){throw Failure{{storage::ErrorCode::InvalidFormat,"Recycling belongs only to data removal"}};}
      auto prepared=take(storage::prepareFileOperation(host_->lease,storageRequest(request,deleteArchive,recycle),stop));
      result=filePlanPacket(request,prepared.preview());host_->filePlan.emplace(std::move(prepared));host_->filePlanPacket=result;
    }catch(const Failure& e){failure->failure=text(e.error.message);}
    catch(const std::exception& e){failure->failure=QString::fromUtf8(e.what());}
    QMetaObject::invokeMethod(this,[this,result]{
      QPointer<SessionBridge> guard(this);if(result->request.session==publishedColumnLayoutSession_){emit fileOperationPrepared(result);}
      if(!guard){return;}guiFileOperation_.reset();completionMonitor_->stop();busy_=false;emit busyChanged(false);
    },Qt::QueuedConnection);
  },false);
  if(accepted){guiFileOperation_=request;}return accepted;
}
bool SessionBridge::executeFileOperation(ui::FileOperationSubmission value) {
  if(profileClosed_||!value.plan||value.plan->request.session!=publishedColumnLayoutSession_||!value.plan->failure.isEmpty()){return false;}
  const auto request=value.plan->request;
  const bool accepted=dispatch([this,value=std::move(value)](std::stop_token stop){
    const auto request=value.plan->request;auto result=std::make_shared<ui::FileOperationResultPacket>();result->request=request;
    try{
      if(!host_||host_->columnLayoutSession!=request.session||!host_->filePlan||!host_->filePlanPacket||!sameFilePlan(*host_->filePlanPacket,*value.plan)){throw Failure{{storage::ErrorCode::Conflict,"This file-operation plan was replaced; prepare it again"}};}
      if(host_->fileRecoveryRequired){throw Failure{{storage::ErrorCode::Conflict,"Restart to recover the interrupted file operation first"}};}
      host_->accepts(request.target);
      const auto stored=storageRequest(request,value.plan->deleteTorrentFile,value.plan->recyclePayload);const auto approved=value.plan->approvalScopeDigest.toStdString();
      take(storage::confirmFileOperation(*host_->filePlan,approved,value.deleteConfirmed,value.archiveConfirmed));
      // Revalidate user-visible scope before stopping anything. Ordinary rates,
      // counts and active-time updates do not change this approval digest.
      {
        auto before=take(storage::prepareFileOperation(host_->lease,stored,stop));
        take(storage::confirmFileOperation(before,approved,value.deleteConfirmed,value.archiveConfirmed));
      }
      host_->filePlan.reset();
      bool readmit=false,paused=false,force=false,queued=false;std::uint64_t oldAdmission=0;
      if(host_->engine){
        const auto rows=host_->engine->managedDownloads();
        const auto found=std::ranges::find(rows,stored.hash,&engine::ManagedDownloadStatus::hash);
        if(found!=rows.end()){
          if(found->failure){throw Failure{*found->failure};}
          readmit=found->retained;paused=found->manuallyPaused;force=found->forceStart;oldAdmission=found->admission;
        }
      }
      const auto library=take(storage::readLibraryView(host_->lease));
      for(const auto& row:library.items){if(row.saved.hash&&*row.saved.hash==stored.hash){force=row.saved.forceStart;queued=row.saved.startup==core::StartupState::Queued;}}
      for(const auto& pause:library.pauses){if(pause.hash==stored.hash){paused=true;}}
      const bool removal=request.operation==ui::FileOperation::RemoveKeepData||request.operation==ui::FileOperation::RemoveDeleteData;
      if(removal){
        auto& revision=host_->manualRevisions[stored.hash.hex()];if(revision==std::numeric_limits<std::uint64_t>::max()){throw Failure{{storage::ErrorCode::Conflict,"Manual action identity exhausted"}};}++revision;
        host_->schedulerPaused.erase(stored.hash.hex());if(host_->scheduler){take(host_->scheduler->revokeForManual(host_->lease,stored.hash));}
      }
      if(host_->engine){
        take(host_->engine->stopManaged(stored.hash));const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{6};
        for(;;){
          checkpoint(stop);const auto rows=host_->engine->managedDownloads();const auto found=std::ranges::find(rows,stored.hash,&engine::ManagedDownloadStatus::hash);
          if(found==rows.end()){break;}if(found->failure){throw Failure{*found->failure};}
          if(!found->retained){take(host_->engine->forgetManaged(stored.hash,removal));break;}
          if(std::chrono::steady_clock::now()>=deadline){throw Failure{{storage::ErrorCode::Busy,"Torrent files are still retained; no file operation was performed"}};}
          const auto poll=host_->engine->poll();if(!poll){throw Failure{{storage::ErrorCode::Io,"Cannot certify torrent retirement"}};}
          std::this_thread::sleep_for(std::chrono::milliseconds{20});
        }
      }
      // Retirement checkpoints legitimately update saved counters/time. Bind a
      // fresh execution CAS only if the exact approved path/inode scope survives.
      auto current=take(storage::prepareFileOperation(host_->lease,stored,stop));auto confirmed=take(storage::confirmFileOperation(current,approved,value.deleteConfirmed,value.archiveConfirmed));
      auto applied=storage::applyFileOperation(host_->lease,std::move(current),confirmed,nullptr,stop);
      if(!applied){host_->fileRecoveryRequired=storage::hasPendingFileOperation(host_->lease);throw Failure{applied.error()};}
      host_->filePlanPacket.reset();host_->known.erase(stored.hash.hex());host_->admissionFailures.erase(stored.hash.hex());host_->lastManagedPhases.erase(stored.hash.hex());host_->schedulerMemberships.erase(stored.hash.hex());
      if(readmit&&!removal&&host_->engine){
        auto restarted=host_->engine->startManaged(host_->lease,{stored.hash,host_->peerPort,force,queued,paused});
        if(!restarted){host_->admissionFailures.insert_or_assign(stored.hash.hex(),restarted.error());result->failure=tr("Files updated; torrent could not restart: %1").arg(text(restarted.error().message));}
        else if(paused&&oldAdmission&&host_->scheduler){
          const auto rows=host_->engine->managedDownloads();const auto found=std::ranges::find(rows,stored.hash,&engine::ManagedDownloadStatus::hash);
          if(found==rows.end()||!found->retained||!found->manuallyPaused){result->failure=tr("Files updated; scheduler pause could not be rebound to the new torrent admission");}
          else{
            auto rebound=host_->scheduler->rebindOwnedPause(host_->lease,stored.hash,oldAdmission,found->admission,host_->manualRevisions[stored.hash.hex()]);
            if(!rebound){result->failure=tr("Files updated; torrent remains paused: %1").arg(text(rebound.error().message));}
          }
        }
      }
      if(applied->retainedPreimage){result->retainedPreimage=QString::fromStdString(applied->retainedPreimage->string());}
      result->completed=true;
    }catch(const Failure& e){result->failure=text(e.error.message);result->cancelled=e.error.code==storage::ErrorCode::Cancelled;}
    catch(const std::exception& e){result->failure=QString::fromUtf8(e.what());}
    QMetaObject::invokeMethod(this,[this,result]{
      if(result->request.session!=publishedColumnLayoutSession_){return;}guiFileOperation_.reset();emit fileOperationFinished(result);
    },Qt::QueuedConnection);
    ready(host_?host_->query({}):ui::Presentation{},result->failure.isEmpty()?(result->completed?(result->retainedPreimage.isEmpty()?tr("File operation completed"):tr("File replaced; prior destination retained at %1").arg(result->retainedPreimage)):tr("File operation cancelled")):result->failure);
  });
  if(accepted){guiFileOperation_=request;}return accepted;
}
void SessionBridge::cancelFileOperation(ui::FileOperationRequest request) {
  if(guiFileOperation_&&*guiFileOperation_==request&&busy_){cancel();}
}
bool SessionBridge::createTorrent(ui::TorrentCreationRequest request) {
  if(profileClosed_||!request.session||!request.request||!request.revision||request.session!=publishedColumnLayoutSession_){return false;}
  const bool accepted=dispatch([this,request](std::stop_token stop){
    auto result=std::make_shared<ui::TorrentCreationResultPacket>();result->session=request.session;result->request=request.request;result->revision=request.revision;
    try{
      if(!host_||host_->columnLayoutSession!=request.session){throw Failure{{storage::ErrorCode::Conflict,"This creation session is closed"}};}
      if(host_->fileRecoveryRequired&&request.options.importAfterCreation){throw Failure{{storage::ErrorCode::Conflict,"Recover the interrupted file operation before importing"}};}
      engine::CreateOptions options;options.source=request.options.source.toUtf8().toStdString();options.output=request.options.output.toUtf8().toStdString();
      std::error_code sourceError;const auto sourceStatus=std::filesystem::symlink_status(options.source,sourceError);
      options.layoutDescriptor=request.options.sourceKind==ui::TorrentCreationSource::LayoutDescriptor;
      if(options.layoutDescriptor){options.filenameConversions=take(storage::readOriginalFilenameConversions(host_->lease));}
      const bool sourceKindMatches=(request.options.sourceKind==ui::TorrentCreationSource::File||options.layoutDescriptor)?std::filesystem::is_regular_file(sourceStatus):request.options.sourceKind==ui::TorrentCreationSource::Directory&&std::filesystem::is_directory(sourceStatus);
      if(sourceError||!sourceKindMatches){throw Failure{{storage::ErrorCode::InvalidFormat,"Selected source type does not match the file or directory"}};}
      if(request.options.pieceSize>std::numeric_limits<std::uint32_t>::max()){throw Failure{{storage::ErrorCode::InvalidFormat,"Invalid piece size"}};}
      switch(request.options.format){
        case ui::TorrentCreationFormat::V1: options.format=engine::CreateFormat::V1;break;
        case ui::TorrentCreationFormat::V2: options.format=engine::CreateFormat::V2;break;
        case ui::TorrentCreationFormat::Hybrid: options.format=engine::CreateFormat::Hybrid;break;
        default: throw Failure{{storage::ErrorCode::InvalidFormat,"Unknown creation format"}};
      }
      options.pieceSize=static_cast<std::uint32_t>(request.options.pieceSize);options.privateTorrent=request.options.privateTorrent;options.comment=request.options.comment.toUtf8().toStdString();
      for(const auto& tier:request.options.trackerTiers){std::vector<std::string> urls;for(const auto& url:tier){urls.push_back(url.toUtf8().toStdString());}options.trackerTiers.push_back(std::move(urls));}
      options.sourceTag=request.options.sourceTag.toUtf8().toStdString();
      for(const auto& url:request.options.urlSeeds) options.urlSeeds.push_back(url.toUtf8().toStdString());
      for(const auto& url:request.options.httpSeeds) options.httpSeeds.push_back(url.toUtf8().toStdString());
      options.dhtBackup=request.options.dhtBackup;options.extraHashes=request.options.extraHashes;
      std::uint64_t sequence=0;std::optional<std::pair<std::uint64_t,std::uint64_t>> last;
      auto created=engine::createTorrent(options,[this,&request,&sequence,&last](const engine::CreateProgress& p){
        const auto progress=std::pair{p.completedBytes,p.totalBytes};if(last&&*last==progress){return;}last=progress;
        auto packet=std::make_shared<const ui::TorrentCreationProgressPacket>(ui::TorrentCreationProgressPacket{request.session,request.request,request.revision,++sequence,p.completedBytes,p.totalBytes});
        creationProgress(std::move(packet));
      },stop);
      if(!created){result->cancelled=created.error().code==engine::CreateErrorCode::Cancelled;result->failure=text(created.error().message);}
      else{
        result->outputPublished=true;result->output=QString::fromStdString(created->output.string());result->hash=text(created->selectedHash.hex());
        if(created->publication!=engine::CreatePublication::Durable){result->failure=tr("Torrent file was published, but directory synchronization failed; verify the saved file before continuing");}
        else{
          result->completed=true;
          if(request.options.importAfterCreation){
            static_cast<void>(take(storage::recoverTorrentIntake(host_->lease)));
            const auto imported=storage::importTorrent(host_->lease,created->output,options.source.parent_path(),true);
            if(!imported){result->failure=tr("Torrent created; adding it to the library failed: %1").arg(text(imported.error().message));}
          }
        }
      }
    }catch(const Failure& e){result->failure=text(e.error.message);result->cancelled=e.error.code==storage::ErrorCode::Cancelled;}
    catch(const std::exception& e){result->failure=QString::fromUtf8(e.what());}
    QMetaObject::invokeMethod(this,[this,result]{if(result->session!=publishedColumnLayoutSession_){return;}guiCreation_.reset();emit torrentCreated(result);},Qt::QueuedConnection);
    ready(host_?host_->query({}):ui::Presentation{},result->failure.isEmpty()?(result->completed?tr("Torrent created"):tr("Torrent creation cancelled")):result->failure);
  });
  if(accepted){guiCreation_=request;}return accepted;
}
void SessionBridge::cancelTorrentCreation(ui::TorrentCreationRequest request) {
  if(guiCreation_&&guiCreation_->session==request.session&&guiCreation_->request==request.request&&guiCreation_->revision==request.revision&&busy_){cancel();}
}

void SessionBridge::creationProgress(ui::TorrentCreationProgressPresentation packet) {
  {
    const std::lock_guard lock(progressMutex_);latestCreationProgress_=std::move(packet);
    if(creationProgressQueued_){return;}creationProgressQueued_=true;
  }
  QMetaObject::invokeMethod(this,[this]{
    ui::TorrentCreationProgressPresentation packet;
    {const std::lock_guard lock(progressMutex_);packet.swap(latestCreationProgress_);creationProgressQueued_=false;}
    if(packet&&guiCreation_&&guiCreation_->session==packet->session&&guiCreation_->request==packet->request&&guiCreation_->revision==packet->revision){emit torrentCreationProgress(packet);}
  },Qt::QueuedConnection);
}
