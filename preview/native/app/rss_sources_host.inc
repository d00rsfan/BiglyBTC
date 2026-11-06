// Inside the existing serialized Host. No new timer/pool or startup read.
  std::optional<rss_detail::Catalog> rssCatalog;
  quint64 rssCatalogGeneration=0,rssLastRequest=0;
  std::unique_ptr<search::RssHttpFetch> rssFetch;
  std::optional<ui::RssFetchCommand> rssCommand;
  void closeRssFetch(bool revokeReceiver=true)noexcept{
    if(revokeReceiver&&rssCommand&&rssCommand->receiver){rssCommand->receiver->request_stop();}
    if(rssFetch){rssFetch->requestCancel();rssFetch->close();rssFetch.reset();}rssCommand.reset();
  }
  bool rssFetchAllowed()const{
    return normalNetwork&&!replacementReviewOnly&&!fileRecoveryRequired&&!recoveryDrainStarted
        &&!(storage::hasPendingCategoryDeletion(lease)||storage::hasPendingFileOperation(lease));
  }
  void fillRssCatalog(ui::RssCatalogPacket& packet){
    packet.normalNetwork=rssFetchAllowed()&&rssCatalog&&rssCatalog->primary;packet.generation=rssCatalogGeneration;
    if(!rssCatalog){return;}
    for(const auto& row:rssCatalog->value.engines){
      if(row.kind!=core::SavedSearchKind::Rss){continue;}
      if(packet.sources.size()==ui::MaximumRssSourceRows){packet.truncated=true;break;}
      ui::RssSourceChoice choice;choice.key.sourceIndex=row.sourceIndex;choice.key.id=row.id.value_or(0);
      if(row.uid&&row.uid->size()<=256){choice.key.uid=QByteArray(row.uid->data(),static_cast<qsizetype>(row.uid->size()));}
      choice.name=row.name?text(*row.name).left(2048):SessionBridge::tr("Unnamed RSS source");
      // Display an origin only; saved URL queries/cookie suffixes remain private.
      if(row.searchUrl){const auto url=QUrl::fromEncoded(QByteArray::fromStdString(*row.searchUrl),QUrl::StrictMode);
        if(url.isValid()&&!url.host().isEmpty()){choice.url=(url.scheme()+QStringLiteral("://")+url.host()+(url.port()==-1?QString{}:QStringLiteral(":")+QString::number(url.port()))).left(2048);}
        else{choice.url=SessionBridge::tr("Source URL needs review");}}
      choice.selectable=row.id&&row.uid&&!row.uid->empty()&&row.uid->size()<=256&&row.searchUrl&&!row.searchUrl->empty()
          &&row.searchUrl->size()<=2048&&!row.duplicateId&&!row.duplicateUid;
      packet.sources.push_back(std::move(choice));
    }
  }
  bool rssCatalogCurrent()const{
    if(!rssCatalog||!rssCatalog->primary||!rssCatalog->value.source){return false;}
    const auto fresh=rss_detail::readCatalog(lease);
    return fresh&&fresh->primary&&fresh->value.source
        &&fresh->value.source->originalBytes()==rssCatalog->value.source->originalBytes();
  }
  bool rssTargetCurrent(const ui::RssFetchKey& key)const{
    if(key.catalogGeneration!=rssCatalogGeneration||!rssCatalogCurrent()){return false;}
    const auto& rows=rssCatalog->value.engines;
    if(key.target.sourceIndex>=rows.size()){return false;}const auto& row=rows[static_cast<std::size_t>(key.target.sourceIndex)];
    return row.sourceIndex==key.target.sourceIndex&&row.kind==core::SavedSearchKind::Rss&&row.id&&*row.id==key.target.id
        &&row.uid&&row.uid->size()<=256&&QByteArray(row.uid->data(),static_cast<qsizetype>(row.uid->size()))==key.target.uid
        &&!row.duplicateId&&!row.duplicateUid;
  }
