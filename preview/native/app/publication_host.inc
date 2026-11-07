  std::unique_ptr<sharing::SavedSharingService> sharingService;
  std::unique_ptr<tracker::EmbeddedTrackerService> trackerService;
  std::optional<sharing::ShareCatalog> shareCatalog;
  std::optional<tracker::TrackerCatalog> trackerCatalog;
  quint64 shareCatalogUi=0,trackerCatalogUi=0,publicationLastRequest=0,publicationRevision=0;
  std::optional<ui::PublicationCommand> publicationCommand;
  std::optional<std::chrono::steady_clock::time_point> publicationQueueDeadline;
  std::optional<sharing::ShareCreateReview> shareReview;
  bool publicationImported=false,publicationStopping=false,publicationRemove=false,publicationCancelled=false;
  std::optional<sharing::SavedShareSelector> publicationSelected;
  std::uint64_t publicationShareGeneration=0;
  std::unique_ptr<sharing::SavedShareHandle> publishingShare;
  std::map<std::string,std::unique_ptr<sharing::SavedShareHandle>> publishedShares;
  std::set<std::string> retiringShares;
  sharing::SavedSharingService& sharing(){if(!sharingService){sharingService=take(sharing::SavedSharingService::open(lease,std::filesystem::canonical(profile),columnLayoutSession,normalNetwork?sharing::SharingMode::Normal:sharing::SharingMode::ReadOnlyPreview));}return *sharingService;}
  tracker::EmbeddedTrackerService& tracker(){if(!trackerService){trackerService=take(tracker::EmbeddedTrackerService::open(lease,std::filesystem::canonical(profile),columnLayoutSession,normalNetwork?tracker::TrackerServiceMode::Normal:tracker::TrackerServiceMode::Preview));}return *trackerService;}
  void closePublications()noexcept{if(publicationCommand&&publicationCommand->receiver){publicationCommand->receiver->request_stop();}publicationCommand.reset();publicationQueueDeadline.reset();publishingShare.reset();publishedShares.clear();retiringShares.clear();shareReview.reset();if(trackerService){static_cast<void>(trackerService->close());trackerService.reset();}if(sharingService){static_cast<void>(sharingService->close());sharingService.reset();}shareCatalog.reset();trackerCatalog.reset();}
