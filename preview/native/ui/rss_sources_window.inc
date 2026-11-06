namespace {
quint64 reserveRssUiIdentity()noexcept{
  static std::atomic<quint64> last{0};auto previous=last.load();
  while(previous!=std::numeric_limits<quint64>::max()){if(last.compare_exchange_weak(previous,previous+1)){return previous+1;}}return 0;
}
}
RssSourcesPage* LibraryWindow::rssSourcesPage()const{return rssPage_.data();}
bool LibraryWindow::onRssSourcesPage()const{return rssPage_&&mainTabs_->currentWidget()==rssPage_.data();}
void LibraryWindow::showRssSources(){
  if(closing_||busy_||!columnSession_){return;}const QPointer<LibraryWindow> guard(this);bool fresh=false;
  if(!rssPage_){
    const auto id=reserveRssUiIdentity();if(!id){setMessage(tr("RSS view identities are exhausted"));return;}
    fresh=true;rssPage_=new RssSourcesPage(mainTabs_);const QPointer<RssSourcesPage> page=rssPage_;page->beginSession(columnSession_,id);
    connect(page,&RssSourcesPage::catalogRequested,this,[this,page](RssCatalogRequest request){
      if(!page||page!=rssPage_||closing_||busy_||request.session!=columnSession_){return;}
      request.request=reserveRssUiIdentity();if(!request.request){setMessage(tr("RSS request identities are exhausted"));return;}
      const QPointer<LibraryWindow> alive(this);page->setCatalogPending(request);if(alive&&page&&page==rssPage_){emit rssCatalogRequested(request);}
    });
    connect(page,&RssSourcesPage::fetchRequested,this,[this,page](RssFetchCommand command){
      if(!page||page!=rssPage_||closing_||busy_||offlinePreview_||command.key.session!=columnSession_){if(command.receiver){command.receiver->request_stop();}if(page){page->refuseFetch(command);}return;}
      command.key.request=reserveRssUiIdentity();if(!command.key.request){if(command.receiver){command.receiver->request_stop();}page->refuseFetch(command);return;}
      const QPointer<LibraryWindow> alive(this);page->setFetchPending(command);if(alive&&page&&page==rssPage_){emit rssFetchRequested(std::move(command));}
    });
    connect(page,&RssSourcesPage::cancelRequested,this,[this](RssFetchKey key){emit rssCancelRequested(std::move(key));});
    mainTabs_->addTab(page,tr("RSS Sources"));if(!guard){return;}
  }
  const QPointer<RssSourcesPage> page=rssPage_;
  if(page->session()!=columnSession_||!page->page()){const auto id=reserveRssUiIdentity();if(!id){setMessage(tr("RSS view identities are exhausted"));return;}page->beginSession(columnSession_,id);fresh=true;if(!guard||!page||page!=rssPage_){return;}}
  page->setAvailable(!busy_&&!closing_,offlinePreview_);if(!guard||!page){return;}
  mainTabs_->setCurrentWidget(page);if(guard&&page&&fresh){page->refreshSources();}
}
void LibraryWindow::setRssCatalog(RssCatalogPresentation packet){if(packet&&rssPage_&&!closing_&&packet->key.session==columnSession_){rssPage_->setCatalog(std::move(packet));}}
void LibraryWindow::setRssFetchResult(RssFetchPresentation packet){if(packet&&rssPage_&&!closing_&&packet->key.session==columnSession_){rssPage_->setFetchResult(std::move(packet));}}
void LibraryWindow::rejectRssCatalog(RssCatalogRequest request){if(rssPage_){rssPage_->refuseCatalog(request);}}
void LibraryWindow::rejectRssFetch(const RssFetchCommand& command){if(rssPage_){rssPage_->refuseFetch(command);}}
