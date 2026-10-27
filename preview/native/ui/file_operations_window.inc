// Included within biglybtc::ui after LibraryWindow's other methods.
void LibraryWindow::openFileOperation(FileOperation operation,int originalIndex,QString destination) {
  QPointer<LibraryWindow> guard(this);
  if(busy_||closing_||offlinePreview_||!columnSession_){return;}
  if(fileOperationSequence_&&fileOperationSequence_->active()){
    const QPointer<FileOperationDialog> dialog(fileOperationSequence_->dialog());if(dialog){dialog->show();if(!guard||!dialog){return;}dialog->raise();}return;
  }
  const auto targets=selectedDownloadTargets();if(!targets||targets->empty()||targets->size()>MaximumFileOperationSequence){setMessage(tr("Select between 1 and %1 torrents for this operation").arg(MaximumFileOperationSequence));return;}
  if(fileOperationDialog_){const auto dialog=fileOperationDialog_;dialog->show();if(!guard||!dialog){return;}dialog->raise();if(!guard||!dialog){return;}dialog->activateWindow();return;}
  if((originalIndex>=0||operation==FileOperation::ReviewReplacement)&&targets->size()!=1){return;}
  if(targets->size()>1){
    if(fileOperationSequence_){const auto previous=fileOperationSequence_;previous->invalidateSession();if(!guard){return;}previous->deleteLater();}
    auto* sequence=new FileOperationSequence(this);fileOperationSequence_=sequence;const QPointer<FileOperationSequence> expected(sequence);
    connect(sequence,&FileOperationSequence::planRequested,this,[this,expected](FileOperationRequest request){
      if(!expected||fileOperationSequence_!=expected){return;}if(busy_||closing_||request.session!=columnSession_){rejectFileOperationRequest(request);return;}emit fileOperationPlanRequested(std::move(request));
    });
    connect(sequence,&FileOperationSequence::executeRequested,this,[this,expected](FileOperationSubmission value){
      if(!expected||fileOperationSequence_!=expected||!value.plan){return;}if(busy_||closing_||value.plan->request.session!=columnSession_){rejectFileOperationSubmission(value);return;}emit fileOperationExecuteRequested(std::move(value));
    });
    connect(sequence,&FileOperationSequence::cancelRequested,this,&LibraryWindow::fileOperationCancelRequested);
    connect(sequence,&FileOperationSequence::finished,this,[this,expected](FileOperationSequencePresentation report){
      if(!expected||fileOperationSequence_!=expected||!report||closing_||report->session!=columnSession_){return;}emit fileOperationSequenceFinished(std::move(report));
    });
    QStringList names;for(const auto& target:*targets){const auto row=model_->rowFor(target.hash);names<<model_->data(model_->index(row,0),Qt::DisplayRole).toString();}
    if(!sequence->begin(columnSession_,operation,*targets,std::move(names))){sequence->deleteLater();setMessage(tr("The selected torrent list changed. Select it again."));return;}
    sequence->setBusy(busy_);return;
  }
  auto* dialog=new FileOperationDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);fileOperationDialog_=dialog;
  const QPointer<FileOperationDialog> expected(dialog);
  connect(dialog,&FileOperationDialog::planRequested,this,[this,expected](FileOperationRequest request){
    if(!expected||fileOperationDialog_!=expected){return;}activeFileOperation_=request;fileOperationExecuting_=false;
    if(busy_||closing_||request.session!=columnSession_){rejectFileOperationRequest(request);return;}
    emit fileOperationPlanRequested(std::move(request));
  });
  connect(dialog,&FileOperationDialog::executeRequested,this,[this,expected](FileOperationSubmission value){
    if(!expected||fileOperationDialog_!=expected||!value.plan){return;}activeFileOperation_=value.plan->request;fileOperationExecuting_=true;
    if(busy_||closing_||value.plan->request.session!=columnSession_){rejectFileOperationSubmission(value);return;}
    emit fileOperationExecuteRequested(std::move(value));
  });
  connect(dialog,&FileOperationDialog::cancelRequested,this,&LibraryWindow::fileOperationCancelRequested);
  const bool began=dialog->beginOperation(columnSession_,targets->front().generation,targets->front(),operation,std::move(destination),originalIndex);if(!guard||!expected){return;}
  if(!began){dialog->deleteLater();return;}
  dialog->show();if(!guard||!expected){return;}dialog->raise();if(!guard||!expected){return;}dialog->activateWindow();
}
void LibraryWindow::openFileMapping(FileOperation operation) {
  if(downloadDispatching_||renderedDownloadRevision_!=downloadActionRevision_){return;}
  const auto selection=filePolicySelection();if(!selection||selection->indices.size()!=1){return;}
  // The popup retains this exact immutable file frame and original index. No
  // selection/model row is consulted after a modal picker or worker dispatch.
  openFileOperation(operation,selection->indices.front());
}
void LibraryWindow::openTorrentCreation() {
  QPointer<LibraryWindow> guard(this);
  if(busy_||closing_||offlinePreview_||!columnSession_){return;}
  if(torrentCreationDialog_){const auto dialog=torrentCreationDialog_;dialog->show();if(!guard||!dialog){return;}dialog->raise();if(!guard||!dialog){return;}dialog->activateWindow();return;}
  if(creationDialogRevision_==std::numeric_limits<quint64>::max()){setMessage(tr("Reopen the application before creating another torrent"));return;}
  auto* dialog=new TorrentCreationDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);torrentCreationDialog_=dialog;
  const QPointer<TorrentCreationDialog> expected(dialog);
  connect(dialog,&TorrentCreationDialog::createRequested,this,[this,expected](TorrentCreationRequest request){
    if(!expected||torrentCreationDialog_!=expected){return;}activeTorrentCreation_=request;
    if(busy_||closing_||request.session!=columnSession_){rejectTorrentCreationRequest(request);return;}
    emit torrentCreateRequested(std::move(request));
  });
  connect(dialog,&TorrentCreationDialog::cancelRequested,this,&LibraryWindow::torrentCreateCancelRequested);
  const bool began=dialog->beginSession(columnSession_,++creationDialogRevision_);if(!guard||!expected){return;}
  if(!began){dialog->deleteLater();return;}
  dialog->show();if(!guard||!expected){return;}dialog->raise();if(!guard||!expected){return;}dialog->activateWindow();
}
void LibraryWindow::setFileOperationPlan(FileOperationPlanPresentation packet) {
  if(fileOperationSequence_&&fileOperationSequence_->active()){if(packet&&!closing_&&packet->request.session==columnSession_){fileOperationSequence_->setPlan(packet);}return;}
  if(!packet||closing_||packet->request.session!=columnSession_||!fileOperationDialog_){return;}
  const auto dialog=fileOperationDialog_;QPointer<LibraryWindow> guard(this);const bool accepted=dialog->setPlan(packet);if(!guard){return;}if(accepted&&activeFileOperation_&&*activeFileOperation_==packet->request){activeFileOperation_.reset();}
}
void LibraryWindow::setFileOperationResult(FileOperationResultPresentation packet) {
  if(fileOperationSequence_&&fileOperationSequence_->active()){if(packet&&!closing_&&packet->request.session==columnSession_){fileOperationSequence_->setResult(packet);}return;}
  if(!packet||closing_||packet->request.session!=columnSession_||!fileOperationDialog_){return;}
  const auto dialog=fileOperationDialog_;QPointer<LibraryWindow> guard(this);const bool accepted=dialog->setResult(packet);if(!guard){return;}if(accepted&&activeFileOperation_&&*activeFileOperation_==packet->request){activeFileOperation_.reset();}
}
void LibraryWindow::setTorrentCreationProgress(TorrentCreationProgressPresentation packet) {
  if(!packet||closing_||packet->session!=columnSession_||!torrentCreationDialog_){return;}torrentCreationDialog_->setProgress(std::move(packet));
}
void LibraryWindow::setTorrentCreationResult(TorrentCreationResultPresentation packet) {
  if(!packet||closing_||packet->session!=columnSession_||!torrentCreationDialog_){return;}
  const auto dialog=torrentCreationDialog_;QPointer<LibraryWindow> guard(this);const bool accepted=dialog->setResult(packet);if(!guard){return;}if(accepted&&activeTorrentCreation_&&activeTorrentCreation_->session==packet->session&&activeTorrentCreation_->request==packet->request&&activeTorrentCreation_->revision==packet->revision){activeTorrentCreation_.reset();}
}
void LibraryWindow::rejectFileOperationRequest(FileOperationRequest request) {
  auto failed=std::make_shared<FileOperationPlanPacket>();failed->request=request;failed->failure=tr("Session is busy or changed; prepare the operation again");setFileOperationPlan(std::move(failed));
}
void LibraryWindow::rejectFileOperationSubmission(const FileOperationSubmission& value) {
  if(!value.plan){return;}auto failed=std::make_shared<FileOperationResultPacket>();failed->request=value.plan->request;failed->failure=tr("Session is busy or changed; no file operation was submitted");setFileOperationResult(std::move(failed));
}
void LibraryWindow::rejectTorrentCreationRequest(const TorrentCreationRequest& request) {
  auto failed=std::make_shared<TorrentCreationResultPacket>();failed->session=request.session;failed->request=request.request;failed->revision=request.revision;failed->failure=tr("Session is busy or changed; no torrent creation was submitted");setTorrentCreationResult(std::move(failed));
}
