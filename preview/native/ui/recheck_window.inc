std::optional<DownloadTargets> LibraryWindow::recheckTargets()const{
 if(!columnSession_||busy_||closing_||offlinePreview_||updating_||activeRecheck_||mainTabs_->currentWidget()!=torrentsPage_){return {};}
 auto targets=selectedDownloadTargets();if(!targets){return {};}for(const auto& target:*targets){if(!model_->canRecheck(target)){return {};}}return targets;
}
void LibraryWindow::requestRecheck(){
 if(downloadDispatching_||renderedDownloadRevision_!=downloadActionRevision_){return;}auto targets=recheckTargets();if(!targets){return;}const auto request=reserveRssUiIdentity();if(!request){return;}
 RecheckRequest command{{columnSession_,request,model_->revision()},std::move(*targets),std::make_shared<std::stop_source>()};activeRecheck_=command;recheckSequence_=0;recheckCancelSent_=false;QPointer<LibraryWindow> alive(this);
 if(!recheckDialog_){recheckDialog_=new RecheckDialog(this);connect(recheckDialog_,&RecheckDialog::cancelled,this,[this](RecheckKey key){if(!activeRecheck_||activeRecheck_->key!=key){return;}cancelRecheckView();});}
 const auto dialog=recheckDialog_;if(!dialog->begin(command)||!alive||!dialog||!activeRecheck_||activeRecheck_->key!=command.key||command.receiver->stop_requested()){if(alive){rejectRecheck(command.key);}return;}dialog->open();if(!alive||!activeRecheck_||activeRecheck_->key!=command.key){return;}queueDownloadActions();updateActions();if(alive&&activeRecheck_&&activeRecheck_->key==command.key&&!command.receiver->stop_requested()){emit recheckRequested(std::move(command));}
}
void LibraryWindow::cancelRecheckView(){if(!activeRecheck_||!activeRecheck_->receiver||recheckCancelSent_){return;}recheckCancelSent_=true;const auto key=activeRecheck_->key;const auto receiver=activeRecheck_->receiver;QPointer<LibraryWindow> alive(this);receiver->request_stop();if(alive){emit recheckCancelRequested(key);}}
void LibraryWindow::rejectRecheck(RecheckKey key){if(!activeRecheck_||activeRecheck_->key!=key){return;}const auto receiver=activeRecheck_->receiver;activeRecheck_.reset();QPointer<LibraryWindow> alive(this);receiver->request_stop();if(!alive){return;}if(recheckDialog_){recheckDialog_->refused(key);if(!alive){return;}}queueDownloadActions();updateActions();}
void LibraryWindow::setRecheckProgress(RecheckPresentation packet){
 if(!packet||closing_||packet->key.session!=columnSession_||!activeRecheck_||packet->key!=activeRecheck_->key||packet->sequence<=recheckSequence_){return;}recheckSequence_=packet->sequence;QPointer<LibraryWindow> alive(this);if(recheckDialog_){recheckDialog_->present(packet);if(!alive){return;}}
 if(packet->terminal){activeRecheck_.reset();queueDownloadActions();updateActions();}
}
