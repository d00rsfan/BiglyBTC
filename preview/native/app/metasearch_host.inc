// Inside the existing serialized Host. Idle polls own all service/provider IO.
  std::unique_ptr<search::SavedMetasearchService> metasearch;
  std::optional<rss_detail::Catalog> metasearchCatalogSource;
  std::vector<search::MetasearchCatalogRow> metasearchCatalogRows;
  quint64 metasearchCatalogGeneration=0,metasearchLastRequest=0,metasearchSequence=0;
  std::optional<ui::MetasearchQueryCommand> metasearchCommand;
  ui::MetasearchPacket metasearchPacket;
  std::unique_ptr<search::MetasearchMetainfoFetch> metasearchMetadataFetch;
  std::optional<search::SavedMetasearchResultHandle> metasearchResultHandle;
  std::optional<search::SavedMetasearchMagnetHandle> metasearchMagnetHandle;
  std::unique_ptr<search::MetasearchMagnetAdmission> metasearchMagnetAdmission;
  quint64 metasearchMagnetRevision=0;
  std::optional<ui::MetasearchDownloadCommand> metasearchDownloadCommand;
  std::unique_ptr<QTemporaryDir> metasearchTemporary;
  std::unique_ptr<QTemporaryFile> metasearchTemporaryFile;
  std::optional<quint64> metasearchReviewRequest;
  bool metasearchProviderActive()const noexcept{return (metasearch&&metasearch->active())||bool(metasearchMetadataFetch);}
  bool metasearchCatalogCurrent()const {
    if(!metasearchCatalogSource||!metasearchCatalogSource->primary||!metasearchCatalogSource->value.source){return false;}
    const auto now=rss_detail::readCatalog(lease);
    return now&&now->primary&&now->value.source
        &&now->value.source->originalBytes()==metasearchCatalogSource->value.source->originalBytes();
  }
  bool metasearchSelected(const ui::MetasearchEngineKey& key)const {
    return std::ranges::any_of(metasearchCatalogRows,[&](const auto& row){return row.selected.sourceIndex==key.sourceIndex
        &&row.selected.id==key.id&&QByteArray::fromStdString(row.selected.uid)==key.uid&&!row.issue&&row.recipe.has_value();});
  }
  QString metasearchName(const ui::MetasearchEngineKey& key)const {
    for(const auto& row:metasearchCatalogRows){if(row.selected.sourceIndex==key.sourceIndex&&row.selected.id==key.id&&QByteArray::fromStdString(row.selected.uid)==key.uid){return text(row.name).left(2048);}}
    return SessionBridge::tr("Saved engine");
  }
  void closeMetasearchDownload(bool revoke=true)noexcept {
    if(revoke&&metasearchDownloadCommand&&metasearchDownloadCommand->receiver){metasearchDownloadCommand->receiver->request_stop();}
    if(metasearchMetadataFetch){metasearchMetadataFetch->requestCancel();metasearchMetadataFetch->close();metasearchMetadataFetch.reset();}
    if(metasearchReviewRequest&&intakeRequest&&intakeRequest->request==*metasearchReviewRequest){intakeMetadata.reset();intakeRequest.reset();}
    metasearchReviewRequest.reset();metasearchMagnetAdmission.reset();metasearchMagnetHandle.reset();metasearchResultHandle.reset();metasearchDownloadCommand.reset();
    // Owned file first, then its private directory; no persistent intake effects.
    metasearchTemporaryFile.reset();metasearchTemporary.reset();
  }
  void closeMetasearch()noexcept {
    closeMetasearchDownload();
    if(metasearchCommand&&metasearchCommand->receiver){metasearchCommand->receiver->request_stop();}
    if(metasearch){metasearch->cancel();metasearch.reset();}
    metasearchCommand.reset();metasearchCatalogSource.reset();metasearchCatalogRows.clear();
  }
  bool metasearchReviewCurrent(quint64 request)const {
    return metasearchDownloadCommand&&metasearchReviewRequest&&request==*metasearchReviewRequest
        &&metasearchResultHandle&&metasearchDownloadCommand->receiver&&!metasearchDownloadCommand->receiver->stop_requested()
        &&rssFetchAllowed()&&!privateOnly()&&!coexistence()&&metasearchResultHandle->current(lease,columnLayoutSession).has_value();
  }
