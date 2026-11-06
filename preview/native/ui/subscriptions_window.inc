SubscriptionsPage* LibraryWindow::subscriptionsPage()const{return subscriptionsPage_.data();}
bool LibraryWindow::onSubscriptionsPage()const{return subscriptionsPage_&&mainTabs_->currentWidget()==subscriptionsPage_.data();}
void LibraryWindow::showSubscriptions(){
  if(closing_||busy_||!columnSession_){return;}QPointer<LibraryWindow> guard(this);
  if(!subscriptionsPage_){const auto id=reserveRssUiIdentity();if(!id){setMessage(tr("View identities are exhausted"));return;}subscriptionsPage_=new SubscriptionsPage(mainTabs_);const QPointer<SubscriptionsPage> page=subscriptionsPage_;page->beginSession(columnSession_,id);
    connect(page,&SubscriptionsPage::catalogRequested,this,[this,page](SubscriptionCatalogKey key){if(!page||page!=subscriptionsPage_||closing_||busy_||key.session!=columnSession_){return;}key.request=reserveRssUiIdentity();if(!key.request){page->refuseCatalog(key);return;}QPointer<LibraryWindow> alive(this);page->setCatalogPending(key);if(alive&&page&&page==subscriptionsPage_){emit subscriptionCatalogRequested(key);}});
    connect(page,&SubscriptionsPage::commandRequested,this,[this,page](SubscriptionCommand command){if(!page||page!=subscriptionsPage_||closing_||busy_||command.key.session!=columnSession_||(offlinePreview_&&command.key.action!=SubscriptionAction::ReadResults)){if(command.receiver){command.receiver->request_stop();}if(page){page->refuseCommand(command);}return;}command.key.request=reserveRssUiIdentity();if(!command.key.request){if(command.receiver){command.receiver->request_stop();}page->refuseCommand(command);return;}QPointer<LibraryWindow> alive(this);page->setPending(command);if(alive&&page&&page==subscriptionsPage_){emit subscriptionCommandRequested(std::move(command));}});
    connect(page,&SubscriptionsPage::cancelRequested,this,[this](SubscriptionCommandKey key){emit subscriptionCancelRequested(std::move(key));});mainTabs_->addTab(page,tr("Subscriptions"));if(!guard){return;}
  }
  const QPointer<SubscriptionsPage> page=subscriptionsPage_;if(page->session()!=columnSession_||!page->page()){const auto id=reserveRssUiIdentity();if(!id){setMessage(tr("View identities are exhausted"));return;}page->beginSession(columnSession_,id);if(!guard||!page||page!=subscriptionsPage_){return;}}
  page->setAvailable(!busy_&&!closing_,offlinePreview_);if(!guard||!page){return;}mainTabs_->setCurrentWidget(page);if(guard&&page&&page==subscriptionsPage_&&page->session()==columnSession_){page->refresh();}
}
void LibraryWindow::setSubscriptionCatalog(SubscriptionCatalogPresentation packet){if(packet&&subscriptionsPage_&&!closing_&&packet->key.session==columnSession_){subscriptionsPage_->setCatalog(std::move(packet));}}
void LibraryWindow::setSubscriptionResult(SubscriptionPresentation packet){if(packet&&subscriptionsPage_&&!closing_&&packet->key.session==columnSession_){subscriptionsPage_->setResult(std::move(packet));}}
void LibraryWindow::rejectSubscriptionCatalog(SubscriptionCatalogKey key){if(subscriptionsPage_){subscriptionsPage_->refuseCatalog(key);}}
void LibraryWindow::rejectSubscriptionCommand(const SubscriptionCommand& command){if(subscriptionsPage_){subscriptionsPage_->refuseCommand(command);}}
void LibraryWindow::subscriptionHistoryChanged(quint64 session){if(!closing_&&!busy_&&session==columnSession_&&subscriptionsPage_){subscriptionsPage_->refresh();}}
