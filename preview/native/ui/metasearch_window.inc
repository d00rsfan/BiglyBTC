// Included after the existing RSS identity allocator. One actual classic tab.
MetasearchPage* LibraryWindow::metasearchPage()const{return metasearchPage_.data();}
bool LibraryWindow::onMetasearchPage()const{return metasearchPage_&&mainTabs_->currentWidget()==metasearchPage_.data();}
void LibraryWindow::showMetasearch(){
 if(closing_||busy_||!columnSession_){return;}QPointer<LibraryWindow> alive(this);bool fresh=false;
 if(!metasearchPage_){const auto id=reserveRssUiIdentity();if(!id){setMessage(tr("Search view identities are exhausted"));return;}fresh=true;metasearchPage_=new MetasearchPage(mainTabs_);QPointer<MetasearchPage> page=metasearchPage_;page->beginSession(columnSession_,id);if(!alive||!page){return;}
  connect(page,&MetasearchPage::catalogRequested,this,[this,page](MetasearchCatalogKey key){if(!page||page!=metasearchPage_||closing_||busy_||key.session!=columnSession_){if(page){page->refuseCatalog(key);}return;}key.request=reserveRssUiIdentity();if(!key.request){page->refuseCatalog(key);return;}QPointer<LibraryWindow> guard(this);page->setCatalogPending(key);if(guard&&page&&page==metasearchPage_&&!closing_&&key.session==columnSession_){emit metasearchCatalogRequested(key);}});
  connect(page,&MetasearchPage::searchRequested,this,[this,page](MetasearchQueryCommand command){if(!page||page!=metasearchPage_||closing_||busy_||offlinePreview_||command.key.session!=columnSession_){if(command.receiver){command.receiver->request_stop();}if(page){page->refuseSearch(command);}return;}command.key.request=reserveRssUiIdentity();if(!command.key.request){if(command.receiver){command.receiver->request_stop();}if(page){page->refuseSearch(command);}return;}QPointer<LibraryWindow> guard(this);page->setSearchPending(command);if(guard&&page&&page==metasearchPage_&&!closing_&&command.key.session==columnSession_&&!command.receiver->stop_requested()){emit metasearchRequested(std::move(command));}});
  connect(page,&MetasearchPage::cancelRequested,this,[this](MetasearchQueryKey key){QPointer<LibraryWindow> guard(this);if(metasearchMagnetReview_&&metasearchMagnetReview_->query==key){invalidateMetasearchMagnetReview();if(!guard){return;}}if(metasearchReview_&&metasearchReview_->query==key){metasearchReview_.reset();if(intakeDialog_){intakeDialog_->invalidateSession();if(!guard){return;}}}emit metasearchCancelRequested(key);});
  connect(page,&MetasearchPage::downloadRequested,this,[this,page](MetasearchDownloadCommand command){if(!page||page!=metasearchPage_||closing_||busy_||offlinePreview_||command.key.query.session!=columnSession_){if(command.receiver){command.receiver->request_stop();}if(page){page->refuseDownload(command);}return;}command.key.request=reserveRssUiIdentity();if(!command.key.request){if(command.receiver){command.receiver->request_stop();}if(page){page->refuseDownload(command);}return;}QPointer<LibraryWindow> guard(this);page->setDownloadPending(command);if(guard&&page&&page==metasearchPage_&&!closing_&&command.key.query.session==columnSession_&&!command.receiver->stop_requested()){emit metasearchDownloadRequested(std::move(command));}});
  connect(page,&MetasearchPage::downloadCancelRequested,this,[this](MetasearchDownloadKey key){QPointer<LibraryWindow> guard(this);if(metasearchMagnetReview_&&*metasearchMagnetReview_==key){invalidateMetasearchMagnetReview();if(!guard){return;}}if(metasearchReview_&&*metasearchReview_==key){metasearchReview_.reset();if(intakeDialog_){intakeDialog_->invalidateSession();if(!guard){return;}}}emit metasearchDownloadCancelRequested(key);});mainTabs_->addTab(page,tr("Search"));if(!alive||!page||page!=metasearchPage_){return;}
 }
 QPointer<MetasearchPage> page=metasearchPage_;if(page->session()!=columnSession_||!page->page()){const auto id=reserveRssUiIdentity();if(!id){setMessage(tr("Search view identities are exhausted"));return;}page->beginSession(columnSession_,id);fresh=true;if(!alive||!page||page!=metasearchPage_){return;}}
 page->setAvailable(!busy_&&!closing_,offlinePreview_);if(!alive||!page){return;}mainTabs_->setCurrentWidget(page);if(alive&&page&&page==metasearchPage_&&fresh){page->refresh();}
}
void LibraryWindow::setMetasearchCatalog(MetasearchCatalogPresentation packet){if(packet&&metasearchPage_&&!closing_&&packet->key.session==columnSession_){metasearchPage_->setCatalog(std::move(packet));}}
void LibraryWindow::setMetasearchResult(MetasearchPresentation packet){if(packet&&metasearchPage_&&!closing_&&packet->key.session==columnSession_){metasearchPage_->setSearchResult(std::move(packet));}}
void LibraryWindow::setMetasearchDownloadResult(MetasearchDownloadPresentation packet){
 if(!packet||!metasearchPage_||closing_||packet->key.query.session!=columnSession_||packet->key.query.page!=metasearchPage_->page()||!metasearchPage_->ownsDownload(packet->key)){return;}
 QPointer<LibraryWindow> alive(this);QPointer<MetasearchPage> page=metasearchPage_;page->setDownloadResult(packet);
 if(!alive||!page||page!=metasearchPage_||closing_||packet->key.query.session!=columnSession_||((packet->state==MetasearchDownloadState::ReviewReady||packet->state==MetasearchDownloadState::MagnetReviewReady)&&!page->ownsDownload(packet->key,true))){return;}
 if(packet->state==MetasearchDownloadState::MagnetReviewReady&&packet->magnetReview){
   if(busy_){pendingMetasearchReview_=packet;return;}pendingMetasearchReview_.reset();showMetasearchMagnetReview(packet);return;
 }
 if(packet->state==MetasearchDownloadState::ReviewReady&&validTorrentIntake(packet->review)){
   if(busy_){pendingMetasearchReview_=packet;return;}pendingMetasearchReview_.reset();
   if((intakeDialog_&&intakeDialog_->isVisible())||(metasearchMagnetDialog_&&metasearchMagnetDialog_->isVisible())){emit metasearchDownloadCancelRequested(packet->key);return;}
   // The normal dialog is the one file-choice editor. Its private preload is
   // presentation; the worker alone retains result/source and inspected bytes.
   ensureTorrentIntakeDialog();if(!alive||!intakeDialog_||!page||page!=metasearchPage_){return;}
   const auto dialog=intakeDialog_;metasearchReview_=packet->key;
   if(!dialog->beginOwnedReview(packet->review,defaultSaveDirectory_)){if(alive){metasearchReview_.reset();emit metasearchDownloadCancelRequested(packet->key);}return;}
   if(alive&&dialog&&page&&page==metasearchPage_&&page->ownsDownload(packet->key,true)&&packet->key.query.session==columnSession_){dialog->show();}
 }else if(packet->state!=MetasearchDownloadState::Started){
   if(metasearchMagnetReview_&&*metasearchMagnetReview_==packet->key){metasearchMagnetReview_.reset();if(metasearchMagnetDialog_){const auto dialog=metasearchMagnetDialog_;if(packet->state==MetasearchDownloadState::Finished&&packet->magnetCommitted){dialog->complete();}else{dialog->invalidate();}if(!alive){return;}}}
   if(pendingMetasearchReview_&&pendingMetasearchReview_->key==packet->key){pendingMetasearchReview_.reset();}
   if(metasearchReview_&&*metasearchReview_==packet->key){metasearchReview_.reset();if(intakeDialog_&&packet->state!=MetasearchDownloadState::Finished){intakeDialog_->invalidateSession();}}
 }
}
void LibraryWindow::rejectMetasearchCatalog(MetasearchCatalogKey key){if(metasearchPage_){metasearchPage_->refuseCatalog(key);}}
void LibraryWindow::rejectMetasearchSearch(const MetasearchQueryCommand& command){if(metasearchPage_){metasearchPage_->refuseSearch(command);}}
void LibraryWindow::rejectMetasearchDownload(const MetasearchDownloadCommand& command){if(metasearchPage_){metasearchPage_->refuseDownload(command);}}
