// SPDX-License-Identifier: GPL-2.0-or-later
struct TrackerSelection {
  std::int32_t tagId=0;
  QStringList origins;
  std::vector<std::string> selectors;
  AllTrackerLibrary library;
  std::shared_ptr<const AllTrackerMembership> members;
};
std::optional<TrackerSelection> trackerSelection;
std::chrono::steady_clock::time_point trackerSelectionNext;
std::uint64_t trackerSelectionAttemptedRevision=0;
quint64 allTrackersRequest=0;

bool trackerSelectionAvailable() const{
  return normalNetwork&&internalTagNamespace&&tagManagerAtOpen&&tagManagerAtOpen->enabled()&&
      tagManagerAtOpen->matches(lease)&&!replacementReviewOnly&&!fileRecoveryRequired&&!manualTagEnded;
}
void publishTrackerSelection(AllTrackerLibrary library,
    std::shared_ptr<const AllTrackerMembership> members){
  take(internalTagNamespace->setTrackerSelection(
      trackerSelection->tagId,library.sourceRevision,members));
  trackerSelection->library=std::move(library);
  trackerSelection->members=std::move(members);
}
void refreshTrackerSelection(AllTrackerLibrary library){
  if(!trackerSelection||!trackerSelectionAvailable()){return;}
  auto members=take(reconcileAllTrackerSelection(library,&trackerSelection->library,
      *trackerSelection->members,trackerSelection->selectors,false));
  publishTrackerSelection(std::move(library),
      std::make_shared<const AllTrackerMembership>(std::move(members)));
}
void tickTrackerSelection(std::stop_token stop){
  if(!trackerSelection||!trackerSelectionAvailable()){return;}
  const auto now=std::chrono::steady_clock::now();
  const auto revision=lease.sourceRevision();
  // A guarded write invalidates the last cohort immediately. Recheck each
  // new source revision once so several dependent rules can run in one cycle.
  if(revision==trackerSelectionAttemptedRevision&&now<trackerSelectionNext){return;}
  trackerSelectionAttemptedRevision=revision;
  trackerSelectionNext=now+std::chrono::seconds(5);
  try{refreshTrackerSelection(take(readAllTrackerLibrary(lease,stop)));}
  catch(const Failure&){
    static_cast<void>(internalTagNamespace->setTrackerSelection(trackerSelection->tagId,0,nullptr));
  }catch(const std::bad_alloc&){
    static_cast<void>(internalTagNamespace->setTrackerSelection(trackerSelection->tagId,0,nullptr));
  }
}
void updateTrackerSelection(AllTrackerLibrary library,QStringList names,std::stop_token stop){
  if(!trackerSelectionAvailable()){
    throw Failure{{storage::ErrorCode::Unsupported,"Tracker selection requires the active original tag manager"}};
  }
  if(!manualTagStore||!manualTagSnapshot.valid()){reloadManualTags(stop);}
  if(!manualTagSnapshot.writable()||manualTagRevision==std::numeric_limits<quint64>::max()){
    throw Failure{{storage::ErrorCode::Conflict,"The original tracker tag settings require review"}};
  }
  TrackerSelection next;
  next.tagId=take(internalTagNamespace->trackerSelectionId());
  std::set<QString> selected;
  for(const auto& name:names){
    const auto raw=name.toUtf8().toStdString();
    const auto found=std::ranges::find_if(library.origins,[&](const auto& row){return row.tracker.name==raw;});
    if(found==library.origins.end()||!selected.insert(name).second){
      throw Failure{{storage::ErrorCode::Conflict,"The selected tracker list changed. Refresh the view."}};
    }
    next.selectors.push_back(found->tracker.selector);
  }
  const auto previous=core::readSavedInternalTrackerSelection(*manualTagSnapshot.tags(),next.tagId);
  if(!previous){
    throw Failure{{storage::ErrorCode::InvalidFormat,"The original tracker selection metadata needs review"}};
  }
  const bool propertyChanged=!*previous||**previous!=next.selectors;
  const AllTrackerMembership empty;
  auto members=take(reconcileAllTrackerSelection(library,trackerSelection?&trackerSelection->library:nullptr,
      trackerSelection?*trackerSelection->members:empty,next.selectors,propertyChanged));
  next.origins=std::move(names);
  next.members=std::make_shared<const AllTrackerMembership>(std::move(members));

  const auto originalRevision=library.sourceRevision;
  auto holder=std::make_shared<storage::ManualTagSnapshot>();
  auto authorizedRevision=originalRevision;
  auto committed=take(publishTagChange([&]()->std::expected<storage::ManualTagCommit,storage::Error>{
    auto access=lease.sourceAccess();
    if(!access){return std::unexpected(access.error());}
    if(access->revision()!=originalRevision){
      return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"The original tracker inventory changed"});
    }
    auto saved=manualTagStore->setInternalTrackerSelection(lease,manualTagSnapshot,
        core::SavedTagSetInternalTrackerSelection{next.tagId,next.selectors},stop);
    if(saved){authorizedRevision=lease.sourceRevision();}
    return saved;
  }));
  if(committed.changed){
    ++manualTagRevision;
    acceptManualTagSnapshot(std::move(committed.snapshot),holder);
  }
  library.sourceRevision=authorizedRevision;
  next.library=std::move(library);
  trackerSelection=std::move(next);
  take(internalTagNamespace->setTrackerSelection(
      trackerSelection->tagId,authorizedRevision,trackerSelection->members));
}
void readAllTrackersView(ui::AllTrackersViewResult& output,
    std::optional<QStringList> selection,std::stop_token stop){
  auto library=take(readAllTrackerLibrary(lease,stop));
  for(const auto& current:library.origins){
    output.origins.push_back({text(current.tracker.name),current.torrents});
  }
  output.canSelect=trackerSelectionAvailable();
  if(selection){updateTrackerSelection(std::move(library),std::move(*selection),stop);}
  else if(trackerSelection){refreshTrackerSelection(std::move(library));}
  if(!trackerSelection||!output.canSelect){return;}
  output.selectionActive=true;
  output.selected=trackerSelection->origins;
  for(const auto& subject:trackerSelection->library.subjects){
    const auto found=trackerSelection->members->find(subject.hash.hex());
    if(found!=trackerSelection->members->end()&&found->second){
      output.members.push_back(text(subject.name));
    }
  }
}
