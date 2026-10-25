// Included inside biglybtc::ui. The bulk file-operation coordinator is unchanged.
void LibraryWindow::openLocalRating() {
  if(busy_||closing_||offlinePreview_||!columnSession_){return;}
  const auto targets=selectedDownloadTargets();
  if(!targets||targets->size()!=1){setMessage(tr("Select one torrent to edit your rating"));return;}
  QPointer<LibraryWindow> guard(this);
  if(localRatingDialog_&&localRatingIdentity_&&localRatingIdentity_->target==targets->front()){
    const auto dialog=localRatingDialog_;dialog->show();if(!guard||!dialog){return;}
    dialog->raise();if(!guard||!dialog){return;}dialog->activateWindow();return;
  }
  if(localRatingDialog_){localRatingDialog_->reject();if(!guard){return;}}
  if(localRatingRequest_==std::numeric_limits<quint64>::max()){setMessage(tr("Reopen the profile before editing more ratings"));return;}
  const LocalRatingRequest request{columnSession_,++localRatingRequest_,targets->front().generation,targets->front()};
  auto* dialog=new LocalRatingDialog(request,this);
  const QPointer<LocalRatingDialog> expected(dialog);localRatingDialog_=dialog;localRatingIdentity_=request;
  localRatingLoading_=false;localRatingSaving_=false;
  dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowModality(Qt::WindowModal);
  connect(dialog,&LocalRatingDialog::readRequested,this,[this,expected](LocalRatingRequest value){
    if(!expected||localRatingDialog_!=expected||!localRatingIdentity_||*localRatingIdentity_!=value){return;}
    localRatingLoading_=true;
    if(busy_||closing_||value.session!=columnSession_||!model_->accepts(value.target)){rejectLocalRatingRequest(value);return;}
    emit localRatingRequested(std::move(value));
  });
  connect(dialog,&LocalRatingDialog::saveRequested,this,[this,expected](LocalRatingSubmission value){
    if(!expected||localRatingDialog_!=expected||!localRatingIdentity_||*localRatingIdentity_!=value.identity){return;}
    localRatingSaving_=true;
    if(busy_||closing_||value.identity.session!=columnSession_||!model_->accepts(value.identity.target)){rejectLocalRatingSave(value);return;}
    emit localRatingSaveRequested(std::move(value));
  });
  auto clear=[this,request]{
    if(localRatingIdentity_&&*localRatingIdentity_==request){localRatingIdentity_.reset();localRatingLoading_=false;localRatingSaving_=false;}
  };
  connect(dialog,&QDialog::finished,this,clear);connect(dialog,&QObject::destroyed,this,clear);
  dialog->show();if(!guard||!expected){return;}dialog->raise();if(!guard||!expected){return;}dialog->activateWindow();
}
void LibraryWindow::setLocalRating(LocalRatingPresentation packet) {
  if(!packet||closing_||!localRatingLoading_||!localRatingIdentity_||packet->identity!=*localRatingIdentity_
      ||packet->identity.session!=columnSession_||!localRatingDialog_){return;}
  if(packet->snapshot){
    if(!packet->failure.isEmpty()||packet->snapshot->attributesRevision.size()!=32
        ||packet->snapshot->score>5||packet->snapshot->comment.size()>1024*1024){return;}
  }else if(packet->failure.isEmpty()){return;}
  localRatingLoading_=false;
  const auto dialog=localRatingDialog_;
  if(!model_->accepts(packet->identity.target)){
    auto failed=std::make_shared<LocalRatingPacket>();failed->identity=packet->identity;
    failed->failure=tr("The torrent selection changed; reopen My rating");packet=std::move(failed);
  }
  dialog->setPresentation(std::move(packet)); // Final access; direct callbacks may delete Window.
}
void LibraryWindow::setLocalRatingSaved(LocalRatingSavePresentation packet) {
  if(!packet||closing_||!localRatingSaving_||!localRatingIdentity_||packet->identity!=*localRatingIdentity_
      ||packet->identity.session!=columnSession_||!localRatingDialog_){return;}
  if((packet->saved&&!packet->failure.isEmpty())||(!packet->saved&&packet->failure.isEmpty())){return;}
  localRatingSaving_=false;
  const auto dialog=localRatingDialog_;dialog->setResult(std::move(packet));
}
void LibraryWindow::rejectLocalRatingRequest(const LocalRatingRequest& request) {
  auto packet=std::make_shared<LocalRatingPacket>();packet->identity=request;
  packet->failure=tr("The rating could not be loaded; reopen it after the current operation finishes");setLocalRating(std::move(packet));
}
void LibraryWindow::rejectLocalRatingSave(const LocalRatingSubmission& value) {
  auto packet=std::make_shared<LocalRatingSavePacket>();packet->identity=value.identity;
  packet->failure=tr("No local rating save result was received; reopen before retrying");setLocalRatingSaved(std::move(packet));
}
