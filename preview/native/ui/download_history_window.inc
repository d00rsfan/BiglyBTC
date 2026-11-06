DownloadHistoryPage* LibraryWindow::downloadHistoryPage()const{return downloadHistoryPage_.data();}
bool LibraryWindow::onDownloadHistoryPage()const{return downloadHistoryPage_&&mainTabs_->currentWidget()==downloadHistoryPage_.data();}
void LibraryWindow::showDownloadHistory(){
  if(closing_||busy_||!columnSession_){return;}
  const QPointer<LibraryWindow> guard(this);bool fresh=false;
  if(!downloadHistoryPage_){
    fresh=true;downloadHistoryPage_=new DownloadHistoryPage(mainTabs_);
    const QPointer<DownloadHistoryPage> page=downloadHistoryPage_;
    connect(page,&DownloadHistoryPage::commandRequested,this,[this,page](DownloadHistoryCommand request){
      if(!page||page!=downloadHistoryPage_||request.session!=columnSession_||closing_||busy_||pendingDownloadHistoryCommand_){return;}
      if(downloadHistoryRequest_==std::numeric_limits<quint64>::max()){setMessage(tr("History request counter exhausted"));return;}
      request.request=++downloadHistoryRequest_;pendingDownloadHistoryCommand_=request;
      const QPointer<LibraryWindow> alive(this);page->setPending(request);if(!alive||!page){return;}
      emit downloadHistoryCommandRequested(request);
    });
    mainTabs_->addTab(page,tr("Download History"));if(!guard){return;}
  }
  const QPointer<DownloadHistoryPage> page=downloadHistoryPage_;
  if(page->session()!=columnSession_){page->beginSession(columnSession_);fresh=true;}
  if(!guard||!page||page!=downloadHistoryPage_){return;}
  page->setAvailable(!busy_&&!closing_);mainTabs_->setCurrentWidget(page);if(!guard||!page){return;}
  if(fresh){page->reload();}
}
void LibraryWindow::setDownloadHistoryCommandFinished(DownloadHistoryCommandPresentation packet){
  if(!packet||!pendingDownloadHistoryCommand_||closing_||packet->session!=columnSession_
      ||packet->request!=pendingDownloadHistoryCommand_->request||packet->kind!=pendingDownloadHistoryCommand_->kind){return;}
  pendingDownloadHistoryCommand_.reset();const QPointer<DownloadHistoryPage> page=downloadHistoryPage_;
  if(page){page->setResult(std::move(packet));}
}
void LibraryWindow::rejectDownloadHistoryCommand(const DownloadHistoryCommand& request){
  if(!pendingDownloadHistoryCommand_||request.session!=pendingDownloadHistoryCommand_->session
      ||request.request!=pendingDownloadHistoryCommand_->request){return;}
  pendingDownloadHistoryCommand_.reset();if(downloadHistoryPage_){downloadHistoryPage_->refuse(request);}
}
