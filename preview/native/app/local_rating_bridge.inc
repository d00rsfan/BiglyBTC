// Included inside biglybtc::app after SessionBridge::Host.
namespace {
bool ratingIdentity(const ui::LocalRatingRequest& request) {
  return request.session && request.request && request.revision
      && request.target.generation==request.revision && !request.target.hash.isEmpty();
}
ui::LocalRatingPresentation ratingPacket(const ui::LocalRatingRequest& request,
    const storage::LocalRatingSnapshot& source) {
  auto values=std::make_shared<ui::LocalRatingValues>();
  values->score=source.local.value.score;
  values->comment=QString::fromStdU16String(source.local.value.comment.value_or(u""));
  values->attributesRevision=QByteArray(reinterpret_cast<const char*>(source.attributesRevision.data()),32);
  values->invalidScoreText=source.local.invalidScoreText;
  values->recoveredBackup=source.recoveredBackup;
  auto packet=std::make_shared<ui::LocalRatingPacket>();packet->identity=request;packet->snapshot=std::move(values);
  return packet;
}
}
bool SessionBridge::loadLocalRating(ui::LocalRatingRequest request) {
  if(profileClosed_||!ratingIdentity(request)||request.session!=publishedColumnLayoutSession_){return false;}
  return dispatch([this,request](std::stop_token stop){
    auto failure=std::make_shared<ui::LocalRatingPacket>();failure->identity=request;
    ui::LocalRatingPresentation result=failure;
    try{
      if(!host_||host_->columnLayoutSession!=request.session){throw Failure{{storage::ErrorCode::Conflict,"This rating session is no longer active"}};}
      host_->ratingRequest.reset();host_->accepts(request.target);checkpoint(stop);
      if(host_->pendingTargets.contains(request.target.hash.toStdString())){throw Failure{{storage::ErrorCode::Unsupported,"Resolve the torrent metadata before editing its rating"}};}
      const auto value=take(storage::readLocalRating(host_->lease,identity(request.target)));
      checkpoint(stop);result=ratingPacket(request,value);
      host_->ratingRevision=value.attributesRevision;host_->ratingRequest=request;
    }catch(const Failure& e){failure->failure=text(e.error.message);}
    catch(const std::exception& e){failure->failure=QString::fromUtf8(e.what());}
    QMetaObject::invokeMethod(this,[this,result=std::move(result)]{
      QPointer<SessionBridge> guard(this);
      if(result->identity.session==publishedColumnLayoutSession_){emit localRatingReady(result);}
      if(!guard){return;}completionMonitor_->stop();busy_=false;emit busyChanged(false);
    },Qt::QueuedConnection);
  },false);
}
bool SessionBridge::saveLocalRating(ui::LocalRatingSubmission value) {
  if(profileClosed_||!ratingIdentity(value.identity)||value.identity.session!=publishedColumnLayoutSession_
      ||value.attributesRevision.size()!=32||value.score>5||value.comment.size()>1024*1024){return false;}
  return dispatch([this,value=std::move(value)](std::stop_token stop){
    if(host_){host_->requireI2pIdle();}
    auto result=std::make_shared<ui::LocalRatingSavePacket>();result->identity=value.identity;
    try{
      if(!host_||host_->columnLayoutSession!=value.identity.session||!host_->ratingRequest
          ||*host_->ratingRequest!=value.identity){throw Failure{{storage::ErrorCode::Conflict,"Load this torrent's rating before saving"}};}
      std::array<std::uint8_t,32> revision{};
      std::copy_n(reinterpret_cast<const std::uint8_t*>(value.attributesRevision.constData()),32,revision.begin());
      if(revision!=host_->ratingRevision){throw Failure{{storage::ErrorCode::Conflict,"The rating edit no longer matches its original snapshot"}};}
      host_->ratingRequest.reset();host_->accepts(value.identity.target);checkpoint(stop);
      if(host_->fileRecoveryRequired||storage::hasPendingFileOperation(host_->lease)||storage::hasPendingOriginalCompaction(host_->lease)){
        throw Failure{{storage::ErrorCode::Conflict,"Recover the interrupted file operation before saving a rating"}};
      }
      take(storage::saveLocalRating(host_->lease,identity(value.identity.target),revision,value.score,value.comment.toStdU16String()));
      result->saved=true; // Actual local durable write/no-op; no DDB operation.
    }catch(const Failure& e){result->failure=text(e.error.message);}
    catch(const std::exception& e){result->failure=QString::fromUtf8(e.what());}
    QMetaObject::invokeMethod(this,[this,result=std::move(result)]{
      QPointer<SessionBridge> guard(this);
      if(result->identity.session==publishedColumnLayoutSession_){emit localRatingSaved(result);}
      if(!guard){return;}completionMonitor_->stop();busy_=false;emit busyChanged(false);
    },Qt::QueuedConnection);
  },false);
}
