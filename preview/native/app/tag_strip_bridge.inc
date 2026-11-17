bool SessionBridge::saveTagStripSettings(ui::TagStripSettingsCommand command){
 if(!command.session||!command.request||command.session!=publishedColumnLayoutSession_||busy_||closingSession_){return false;}
 return dispatch([this,command](std::stop_token stop){ui::TagStripSettingsResult result{command.session,command.request,command.expected,{}};checkpoint(stop);if(!host_||host_->columnLayoutSession!=command.session){result.failure=tr("This library profile is no longer active");}else{auto saved=app::saveTagStripSettings(host_->lease,command.expected,command.value);if(!saved){result.failure=text(saved.error().message);}else{result.value=*saved;if(host_->lastSnapshot){auto snapshot=std::make_shared<ui::LibraryPresentation>(*host_->lastSnapshot);snapshot->tagStripSettings=*saved;host_->lastSnapshot=std::move(snapshot);}}}
  QMetaObject::invokeMethod(this,[this,result=std::move(result)]{const QPointer<SessionBridge> guard(this);if(result.session==publishedColumnLayoutSession_){emit tagStripSettingsSaved(result);}if(!guard){return;}completionMonitor_->stop();busy_=false;emit busyChanged(false);},Qt::QueuedConnection);
 },false);
}

bool SessionBridge::assignTagChip(ui::TagChipAssignmentCommand command){
 if(profileClosed_||!command.session||!command.request||command.session!=publishedColumnLayoutSession_||busy_||closingSession_||command.targets.empty()||command.targets.size()>ui::LibraryModel::MaximumRows||(command.category&&command.category->size()>4096)){return false;}
 for(const auto& target:command.targets){if((target.hash.size()!=40&&target.hash.size()!=64)||!target.generation){return false;}}
 return dispatch([this,command=std::move(command)](std::stop_token stop){
  ui::TagChipAssignmentResult result{command.session,command.request,command.targets.size(),0,0,false,{}};ui::Presentation refreshed;
  try{
   if(!host_||host_->columnLayoutSession!=command.session||command.request<=host_->lastTagChipAssignment){throw Failure{{storage::ErrorCode::Conflict,"Library assignment source changed"}};}auto& host=*host_;host.lastTagChipAssignment=command.request;checkpoint(stop);for(const auto& target:command.targets){host.accepts(target);if(host.pendingTargets.contains(identity(target).hex())){throw Failure{{storage::ErrorCode::Unsupported,"Resolve magnet metadata before assigning categories or tags"}};}}
   if(!host.assignLifecycleTagChip(command,result,stop)){
     result=app::assignTagChip(host.lease,command,stop,[&host](const auto& hash){return host.rebaseCategoryOwners(hash);});
   }
   if(result.changed||result.uncertain){if(command.kind==ui::TagChipAssignmentKind::Manual){host.reloadManualTags({});}host.invalidateCategoryCaches();refreshed=host.query({});}
  }catch(const Failure& failure){result.failure=text(failure.error.message).left(8192);
    if(host_&&host_->manualTagStore&&host_->manualTagStore->recoveryRequired()){result.uncertain=true;host_->beginManualTagRead();}
  }catch(const std::bad_alloc&){result.failure=tr("Insufficient memory; review the library assignment before retrying");result.uncertain=true;}
  // Publication follows filesystem access/rebase, never inside a source permit.
  publishManualTags();QMetaObject::invokeMethod(this,[this,result=std::move(result),refreshed=std::move(refreshed)]{const QPointer<SessionBridge> guard(this);if(result.session==publishedColumnLayoutSession_){if(refreshed){emit snapshotReady(refreshed);if(!guard){return;}}emit tagChipAssignmentFinished(result);if(!guard){return;}}completionMonitor_->stop();busy_=false;emit busyChanged(false);},Qt::QueuedConnection);
 },false);
}
