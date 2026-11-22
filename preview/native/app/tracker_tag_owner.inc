// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside the actual existing serialized tag worker Impl. No caller
// event setter, provider call, second writer, timer or automatic startup sweep.
  using TrackerNames=std::vector<std::string>;
  struct TrackerWork {std::int32_t id=0;TrackerNames names;std::shared_ptr<const std::vector<QString>> targets;std::size_t cursor=0;};
  std::map<std::int32_t,std::optional<TrackerNames>> trackerProperties;
  std::deque<TrackerWork> trackerWork;
  std::shared_ptr<const core::Document> trackerSource;
  bool trackersStarted=false,nextTrackerTurn=true;
  std::set<std::int32_t> trackerUncertain;
  std::optional<TrackerNames> trackerNames(const core::SavedDownloadTags& tags,const core::SavedDownloadTag& row)const{
    if(!row.id||row.duplicateId||row.membersMalformed||row.idKey!=std::to_string(*row.id)){return {};}
    const auto* type=tags.source->find("3");const auto* record=type?type->find(row.idKey):nullptr;
    if(!record||!core::rateReadyLifecycleSavedTagMembershipEffects(tags,*record)){return {};}
    const auto* config=record->find("c");const auto* property=config?config->find("pp.trackers"):nullptr;
    if(!property){return TrackerNames{};}if(!core::supportedSavedTrackerTagNames(*property)){return {};}
    const auto* constraint=config->find("pp.constraint");
    if(constraint&&(constraint->type!=core::Node::Type::List||!constraint->children.empty())){return {};}
    TrackerNames names;for(const auto& item:property->children){names.emplace_back(item.bytes);}return names;
  }
  bool trackerOwned(std::int32_t id)const{
    const auto found=trackerProperties.find(id);return trackersStarted&&found!=trackerProperties.end()&&found->second&&!trackerUncertain.contains(id)
        &&!found->second->empty()&&std::none_of(trackerWork.begin(),trackerWork.end(),[&](const auto& work){return work.id==id;});
  }
  std::expected<void,storage::Error> refreshTrackers(const core::SavedDownloadTags& tags,const ui::LibraryPresentation& library){
    // Completion changes which automatic membership witnesses are known.
    // Wake the real ordinary row queue; it may already have consumed the
    // earlier membership callback while this property's work was pending.
    while(!trackerWork.empty()&&trackerWork.front().cursor>=trackerWork.front().targets->size()*2){
      std::set<QString> affected(trackerWork.front().targets->begin(),trackerWork.front().targets->end());
      auto queued=queueChangedRows(affected,library);if(!queued){return queued;}trackerWork.pop_front();
    }
    if(trackerSource==tags.source){return {};}
    std::map<std::int32_t,std::optional<TrackerNames>> next;
    for(const auto& row:tags.tags){if(row.id&&!row.duplicateId&&row.idKey==std::to_string(*row.id)){next[*row.id]=trackerNames(tags,row);}}
    auto pending=trackerWork;std::shared_ptr<const std::vector<QString>> targets;
    for(const auto& [id,names]:next){const auto old=trackerProperties.find(id);
      if(old==trackerProperties.end()||old->second==names){continue;}
      std::erase_if(pending,[&](const auto& work){return work.id==id;});
      // Unknown prior source is not an actual property-change witness.
      if(!trackersStarted||!old->second||!names){trackerUncertain.insert(id);continue;}
      if(pending.size()==64){return std::unexpected(error(storage::ErrorCode::Unsupported,"Tracker tag property work bound reached"));}
      if(!targets){auto values=std::make_shared<std::vector<QString>>();std::set<QString> unique;
        for(const auto& row:library.items){if(!unique.insert(row.hash).second){return std::unexpected(error(storage::ErrorCode::Conflict,"Tracker tag library identities are ambiguous"));}values->push_back(row.hash);}targets=std::move(values);}
      pending.push_back({id,*names,targets,0});trackerUncertain.erase(id);
    }
    std::erase_if(pending,[&](const auto& work){const auto found=next.find(work.id);return found==next.end()||!found->second||*found->second!=work.names;});
    std::erase_if(trackerUncertain,[&](int id){return !next.contains(id);});
    trackerProperties=std::move(next);trackerWork=std::move(pending);trackerSource=tags.source;trackersStarted=true;return {};
  }
  std::expected<ConstraintWorkerUpdate,storage::Error> trackerStep(storage::ManualTagStore& store,
      const storage::ManualTagSnapshot& snapshot,const ConstraintWorkerFrame& frame,std::stop_token stop){
    namespace t=core::tags::tracker;ConstraintWorkerUpdate result;result.snapshot=snapshot;
    auto refreshed=refreshTrackers(*snapshot.tags(),*frame.library);if(!refreshed){return std::unexpected(refreshed.error());}
    if(trackerWork.empty()){return result;}
    auto gate=lease->sourceAccess();if(!gate){return std::unexpected(gate.error());}
    if(gate->revision()!=frame.sourceRevision){return std::unexpected(error(storage::ErrorCode::Conflict,"Tracker tag source turn changed"));}
    auto inventory=storage::ConfigFile::load(*lease,"downloads.config");if(!inventory){return std::unexpected(inventory.error());}
    auto library=core::readLibrary(inventory->document());
    if(inventory->recoverySource()!=storage::RecoverySource::Primary||!library||!library->issues.empty()){
      return std::unexpected(error(storage::ErrorCode::Conflict,"Tracker tag inventory requires settled primary data"));}
    auto stateStore=storage::TorrentStateStore::open(*lease);if(!stateStore){return std::unexpected(stateStore.error());}
    while(!trackerWork.empty()&&result.evaluated<32){
      if(stop.stop_requested()){return std::unexpected(error(storage::ErrorCode::Cancelled,"Tracker tag property turn cancelled"));}
      auto& work=trackerWork.front();const auto size=work.targets->size();
      // Preserve original remove-existing pass before the add-matching pass.
      if(!size||work.cursor>=size*2){std::set<QString> affected(work.targets->begin(),work.targets->end());
        auto queued=queueChangedRows(affected,*frame.library);if(!queued){return std::unexpected(queued.error());}trackerWork.pop_front();continue;}
      const bool removing=work.cursor<size;const auto key=(*work.targets)[work.cursor%size];++work.cursor;++result.evaluated;result.tag=work.id;
      const auto* definition=tag(*snapshot.tags(),work.id);if(!definition){continue;}
      auto names=trackerNames(*snapshot.tags(),*definition);if(!names||*names!=work.names){result.kept=unavailable("Tracker tag property source is no longer current");continue;}
      auto hash=constraintFactHash(key);if(!hash){trackerUncertain.insert(work.id);result.kept=hash.error();continue;}
      if(std::count_if(frame.library->items.begin(),frame.library->items.end(),[&](const auto& row){return row.hash==key;})!=1
          ||std::count_if(library->downloads.begin(),library->downloads.end(),[&](const auto& row){return row.hash&&*row.hash==*hash&&row.persistent;})!=1){continue;}
      const bool member=std::ranges::find(definition->uniqueHashes,*hash)!=definition->uniqueHashes.end();
      if(member!=removing){continue;}
      auto state=stateStore->load(*lease,*hash);
      if(!state||state->recoveredBackup()){trackerUncertain.insert(work.id);result.kept=unavailable("Tracker tag state requires a settled original source");continue;}
      const auto& document=state->document();const auto* announce=document.find("announce");
      std::vector<t::Endpoint> endpoints;bool known=announce&&announce->type==core::Node::Type::Bytes;
      const auto append=[&](const core::Node& node){if(node.type!=core::Node::Type::Bytes||endpoints.size()==64){known=false;return;}auto parsed=t::endpoint(node.bytes);if(!parsed){known=false;return;}endpoints.push_back(std::move(*parsed));};
      if(known){append(*announce);}
      if(const auto* tiers=document.find("announce-list")){
        if(tiers->type!=core::Node::Type::List||tiers->children.size()>16){known=false;}
        else{for(const auto& tier:tiers->children){if(tier.type!=core::Node::Type::List){known=false;break;}for(const auto& value:tier.children){append(value);}}}
      }
      if(!known){trackerUncertain.insert(work.id);result.kept=unavailable("Tracker tag URL source is outside the qualified subset");continue;}
      auto hit=t::matches(endpoints,*names);if(!hit){trackerUncertain.insert(work.id);result.kept=unavailable("Tracker tag match is unavailable");continue;}
      const auto decision=t::project(t::Event::PropertyChanged,!names->empty(),member,*hit);
      if(decision==t::Membership::Keep){continue;}
      const storage::TagAssignmentObservation* assignment=nullptr;
      if(needsAssignment(snapshot,work.id)&&!detail::privateRateAllowed(frame,snapshot,*hash,work.id)){
        assignment=assignmentFor(result,frame,snapshot,*hash,work.id,decision==t::Membership::Add,ConstraintAssignmentKind::Tracker);
        if(!assignment){if(result.assignmentRequest){--work.cursor;return result;}trackerUncertain.insert(work.id);continue;}
      }
      auto finalInventory=storage::ConfigFile::load(*lease,"downloads.config");auto finalState=stateStore->load(*lease,*hash);
      if(!finalInventory||finalInventory->recoverySource()!=storage::RecoverySource::Primary
          ||finalInventory->document().originalBytes()!=inventory->document().originalBytes()
          ||!finalState||finalState->recoveredBackup()||finalState->document().originalBytes()!=document.originalBytes()
          ||gate->revision()!=frame.sourceRevision||storage::hasPendingCategoryDeletion(*lease)||storage::hasPendingFileOperation(*lease)){return std::unexpected(error(storage::ErrorCode::Conflict,"Tracker tag inventory/state preimage changed before publication"));}
      if(stop.stop_requested()){return std::unexpected(error(storage::ErrorCode::Cancelled,"Tracker tag property turn cancelled before publication"));}
      const core::SavedTagEdit edit=decision==t::Membership::Add?core::SavedTagEdit{core::SavedTagAddMember{work.id,*hash,std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()}}:core::SavedTagEdit{core::SavedTagRemoveMember{work.id,*hash}};
      auto applied=detail::publishCurrentTagCommit(frame,*lease,gate,
      [&](const storage::ProfileSourceAccess& current)->std::expected<void,storage::Error>{
        auto finalInventory=storage::ConfigFile::load(*lease,"downloads.config");auto finalState=stateStore->load(*lease,*hash);
      if(!finalInventory||finalInventory->recoverySource()!=storage::RecoverySource::Primary
          ||finalInventory->document().originalBytes()!=inventory->document().originalBytes()
          ||!finalState||finalState->recoveredBackup()||finalState->document().originalBytes()!=document.originalBytes()
          ||current.revision()!=frame.sourceRevision||storage::hasPendingCategoryDeletion(*lease)||storage::hasPendingFileOperation(*lease)){return std::unexpected(error(storage::ErrorCode::Conflict,"Tracker tag inventory/state preimage changed before publication"));}

        if(stop.stop_requested()){return std::unexpected(error(storage::ErrorCode::Cancelled,"Tag publication cancelled after private reader join"));}
        return {};
      },[&]()->std::expected<storage::ManualTagCommit,storage::Error>{
        if(assignment){auto changed=frame.assignmentOwner->change(store,snapshot,work.id,*assignment,decision==t::Membership::Add,std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(),stop,assignmentRates(frame));
          if(!changed){return std::unexpected(changed.error());}result.assignmentReceipt=std::move(changed->receipt);return std::move(changed->commit);}
        return store.apply(snapshot,edit,stop);
      },detail::privateRateRequest(frame,snapshot,std::array<storage::ManualTagMembershipEdit,1>{
          decision==t::Membership::Add?storage::ManualTagMembershipEdit{std::get<core::SavedTagAddMember>(edit)}:storage::ManualTagMembershipEdit{std::get<core::SavedTagRemoveMember>(edit)}}));if(!applied){
        if((assignment||detail::privateRateSubject(frame,*hash))&&(applied.error().code==storage::ErrorCode::Unsupported||applied.error().code==storage::ErrorCode::Busy)){
          trackerUncertain.insert(work.id);result.kept=unavailable("Tracker assignment source is unavailable");return result;
        }return std::unexpected(applied.error());
      }
      result.changed=applied->changed;result.snapshot=std::move(applied->snapshot);result.hash=*hash;return result;
    }return result;
  }
