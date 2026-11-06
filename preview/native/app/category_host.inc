// Included inside worker-affine SessionBridge::Host. No GUI/provider callback.
  std::unique_ptr<storage::CategoryStore> categoryStore;
  storage::CategorySnapshot categorySnapshot;
  storage::CategoryTargetSnapshot categoryAssignment;
  ui::Target categorySelection;
  std::shared_ptr<const core::SavedCategories> categorySource;
  quint64 categoryRevision=0,lastCategoryRequest=0;
  void invalidateCategoryRead(){
    if(categoryRevision==std::numeric_limits<quint64>::max()){
      throw Failure{{storage::ErrorCode::Unsupported,"Category presentation revisions are exhausted"}};
    }
    ++categoryRevision;categorySnapshot={};categorySource.reset();categoryAssignment={};categorySelection={};
  }
  void acceptCategories(storage::CategorySnapshot value,
      const std::shared_ptr<storage::CategorySnapshot>& holder){
    *holder=std::move(value);categorySnapshot=*holder;
    categorySource=std::shared_ptr<const core::SavedCategories>(holder,holder->categories());
  }
  void reloadCategories(std::stop_token stop){
    auto holder=std::make_shared<storage::CategorySnapshot>();invalidateCategoryRead();
    // Explicit read only: preserve recovery journals/candidates, never replay.
    if(categoryStore&&categoryStore->recoveryRequired()){take(categoryStore->close());categoryStore.reset();}
    if(!categoryStore){categoryStore=take(storage::CategoryStore::open(lease,stop));acceptCategories(take(categoryStore->snapshot()),holder);}
    else{acceptCategories(take(categoryStore->reload(stop)),holder);}
  }
  void categoryState(ui::CategoryCommandPacket& packet)const{
    packet.generation=categoryRevision;packet.libraryRevision=revision;
    packet.categories=categorySource;packet.writable=categorySnapshot.writable();
    packet.readState=!categorySnapshot.valid()?ui::CategoryReadState::Failed:
        categorySnapshot.missing()?ui::CategoryReadState::Missing:ui::CategoryReadState::Ready;
    packet.deletionSupported=categoryStore&&categoryDeletionSupported(*categoryStore);
    packet.recoveryRequired=categoryStore&&categoryStore->recoveryRequired();
  }
  void categoryTarget(ui::CategoryCommandPacket& packet,const ui::Target& target,std::stop_token stop){
    accepts(target);const auto hash=identity(target);
    if(pendingTargets.contains(hash.hex())){throw Failure{{storage::ErrorCode::Unsupported,"Resolve magnet metadata before assigning a category"}};}
    const auto source=take(categoryStore->readTarget(categorySnapshot,hash,stop));
    const auto* assigned=source.assignment();
    if(!assigned){throw Failure{{storage::ErrorCode::Conflict,"Category target is unavailable"}};}
    categoryAssignment=source;categorySelection=target;
    packet.target=target;packet.assignmentLoaded=true;packet.assignmentResolved=assigned->resolved;
    if(assigned->declaredName){packet.assignedName=text(*assigned->declaredName);}
  }
  void invalidateCategoryCaches(){
    schedulerMemberships.clear();schedulerTagsLoaded=false;schedulerTagsAvailable=false;schedulerLastTick={};
    manualResolvedLibrary=0;manualMembership.reset();
  }
  std::expected<void,storage::Error> rebaseCategoryOwners(const core::TorrentHash& hash){
    // Both APIs are FS/pure only, selecting their real retained admission.
    // In particular the I2P facade must not dispatch Qt inside publication.
    if(engine){auto rebased=engine->rebaseCategory(lease,hash);if(!rebased){return std::unexpected(rebased.error());}}
    if(i2p){auto rebased=i2p->rebaseCategory(lease,hash);if(!rebased){return std::unexpected(rebased.error());}}
    return {};
  }
  ui::Details categoryDetails(const ui::Target& target){
    const auto value = take(engine::queryTorrent(lease, identity(target)));
    auto name = text(value.displayName && !value.displayName->empty() ? *value.displayName : value.name);
    if (const auto known = this->known.find(target.hash.toStdString()); known != this->known.end() && !known->second.name.isEmpty()) {
      name = known->second.name; // The original cache can own a newer display name.
    }
    known.insert_or_assign(target.hash.toStdString(), Known{name, value.bytes});
    auto files = std::make_shared<ui::FileDetails>();
    files->target = target;
    files->canEdit = engine && engine->managedAvailable() && !fileRecoveryRequired && !storage::hasPendingOriginalCompaction(lease);
    if (engine) {
      for (const auto& active : engine->managedDownloads()) {
        if (active.hash == identity(target) && (active.failure || active.phase==engine::ManagedPhase::Stopping)) { files->canEdit = false; }
      }
    }
    files->canCompact = compactionReady();
    std::vector<bool> compactable(value.entries.size(), false);
    if (files->canCompact) {
      // Read-only source/default resolution; the mutating primitive repeats all
      // geometry/control/path/alias checks under this same worker-held lease.
      auto store = take(storage::TorrentStateStore::open(lease));
      const auto state = take(store.load(lease, identity(target)));
      const auto layout = storage::readOriginalLayout(lease,state.document());
      const auto* attributes = state.document().find("attributes");
      const auto empty = core::Document::parse("de");
      if (layout && empty) {
        const auto kinds = storage::resolveOriginalStorageTypes(lease, identity(target), *layout,
            attributes ? *attributes : empty->root());
        if (kinds && !kinds->unsupportedConversion && kinds->actual.size() == compactable.size()) {
          for (std::size_t index = 0; index < compactable.size(); ++index) {
            compactable[index] = kinds->actual[index] == storage::OriginalStorageKind::Linear
                && kinds->requested[index] == storage::OriginalStorageKind::Linear;
          }
        }
      }
    }
    files->items.reserve(value.entries.size());
    for (const auto& file : value.entries) {
      files->items.push_back({file.index, text(file.path), file.bytes, file.priority, file.skipped, file.padding,
          !file.padding && compactable[static_cast<std::size_t>(file.index)]});
    }
    return {target,name,value.bytes,std::move(files)};
  }
