// A real source-owned magnet review shares the existing Search Download key.
void LibraryWindow::invalidateMetasearchMagnetReview(){
 metasearchMagnetReview_.reset();if(metasearchMagnetDialog_){metasearchMagnetDialog_->invalidate();}
}
void LibraryWindow::showMetasearchMagnetReview(MetasearchDownloadPresentation packet){
 if(!packet||!packet->magnetReview||!metasearchPage_||!metasearchPage_->ownsDownload(packet->key,true)||closing_||busy_||offlinePreview_||packet->key.query.session!=columnSession_){return;}
 if((intakeDialog_&&intakeDialog_->isVisible())||(metasearchMagnetDialog_&&metasearchMagnetDialog_->isVisible())){emit metasearchDownloadCancelRequested(packet->key);return;}
 QPointer<LibraryWindow> alive(this);QPointer<MetasearchPage> page=metasearchPage_;
 if(!metasearchMagnetDialog_){
  metasearchMagnetDialog_=new MetasearchMagnetDialog(this);
  connect(metasearchMagnetDialog_,&MetasearchMagnetDialog::submitted,this,[this](MetasearchMagnetSubmission command){
   const auto frame=metasearchMagnetDialog_?metasearchMagnetDialog_->review():MetasearchDownloadPresentation{};
   if(closing_||busy_||offlinePreview_||!metasearchPage_||!metasearchPage_->ownsDownload(command.key,true)||!metasearchMagnetReview_||*metasearchMagnetReview_!=command.key||command.key.query.session!=columnSession_||!frame||!frame->magnetReview||frame->key!=command.key||frame->magnetReview->revision!=command.revision){if(metasearchMagnetDialog_){metasearchMagnetDialog_->refuse(command);}return;}
   emit metasearchMagnetConfirmed(std::move(command)); // No raw URI/hash comes from the widget.
  });
  connect(metasearchMagnetDialog_,&MetasearchMagnetDialog::cancelled,this,[this](MetasearchDownloadKey key){if(metasearchMagnetReview_&&*metasearchMagnetReview_==key){metasearchMagnetReview_.reset();emit metasearchDownloadCancelRequested(key);}});
  connect(metasearchMagnetDialog_,&QObject::destroyed,this,[this]{const auto old=metasearchMagnetReview_;metasearchMagnetReview_.reset();if(old){emit metasearchDownloadCancelRequested(*old);}});
 }
 if(!alive||!page||page!=metasearchPage_||!page->ownsDownload(packet->key,true)){return;}
 const auto dialog=metasearchMagnetDialog_;metasearchMagnetReview_=packet->key;
 if(!dialog->beginReview(packet,defaultSaveDirectory_)){if(alive){metasearchMagnetReview_.reset();emit metasearchDownloadCancelRequested(packet->key);}return;}
 if(!alive||!dialog||!page||page!=metasearchPage_||!page->ownsDownload(packet->key,true)||closing_||packet->key.query.session!=columnSession_){return;}dialog->open();
}
void LibraryWindow::rejectMetasearchMagnet(const MetasearchMagnetSubmission& command){if(metasearchMagnetDialog_){metasearchMagnetDialog_->refuse(command);}}
