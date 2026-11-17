// SPDX-License-Identifier: GPL-2.0-or-later
// Current-source owner, inside the existing serialized tag worker. No listener
// accumulation, inferred absence, public census flag or second writer.
  using UntaggedFlag=std::pair<std::optional<bool>,bool>;
  std::map<std::int32_t,UntaggedFlag> untaggedFlags;
  std::set<std::int32_t> untaggedEnabled,untaggedDisabled;
  std::set<QString> untaggedPending,untaggedUncertain;
  std::shared_ptr<const core::Document> untaggedSource;
  unsigned nextTagAutomation=0;
  static std::optional<bool> untaggedRole(const core::Node* record){
    if(!record||record->type!=core::Node::Type::Dictionary){return {};}
    const auto* config=record->find("c");if(!config){return false;}if(config->type!=core::Node::Type::Dictionary){return {};}
    const auto* flag=config->find("pp.untagged");if(!flag){return false;}if(flag->type!=core::Node::Type::Integer){return {};}return flag->integer==1;
  }
  static bool untaggedMembersKnown(const core::SavedDownloadTag& row,const core::Node* record){
    if(!record||row.membersMalformed||!row.rawName||QString::fromUtf8(row.rawName->data(),qsizetype(row.rawName->size())).toUtf8().toStdString()!=*row.rawName){return false;}
    for(const auto& member:row.members){if(!member.hash||member.duplicate){return false;}}
    const auto* properties=record->find("p");
    if(properties&&(properties->type!=core::Node::Type::List||properties->children.size()!=row.members.size())){return false;}
    return true;
  }
  std::expected<void,storage::Error> queueUntaggedRows(const std::set<QString>& affected,const ui::LibraryPresentation& library){
    if(untaggedEnabled.empty()&&untaggedDisabled.empty()){return {};}
    std::set<QString> allowed;for(const auto& row:library.items){allowed.insert(row.hash);}
    for(const auto& key:affected){if(allowed.contains(key)){untaggedPending.insert(key);}}
    if(untaggedPending.size()>ui::LibraryModel::MaximumRows){return std::unexpected(error(storage::ErrorCode::Unsupported,"Untagged target work bound reached"));}return {};
  }
  std::expected<void,storage::Error> refreshUntagged(const core::SavedDownloadTags& tags,const ui::LibraryPresentation& library){
    std::set<QString> live;for(const auto& row:library.items){live.insert(row.hash);}
    std::erase_if(untaggedPending,[&](const auto& key){return !live.contains(key);});
    std::erase_if(untaggedUncertain,[&](const auto& key){return !live.contains(key);});
    if(untaggedSource==tags.source){return {};}
    std::map<std::int32_t,UntaggedFlag> next;std::set<std::int32_t> enabled;
    const auto* type=tags.source->find("3");
    for(const auto& row:tags.tags){
      if(!row.id||row.duplicateId||*row.id<0||row.idKey!=std::to_string(*row.id)){continue;}
      const auto* record=type?type->find(row.idKey):nullptr;const auto role=untaggedRole(record);
      const bool supported=record&&core::rateReadyLifecycleSavedTagMembershipEffects(tags,*record)&&untaggedMembersKnown(row,record);
      next[*row.id]={role,supported};if(role==true&&supported){enabled.insert(*row.id);}
      const auto old=untaggedFlags.find(*row.id);
      if(old!=untaggedFlags.end()&&old->second.first==true&&role==false&&supported){untaggedDisabled.insert(*row.id);}
    }
    std::erase_if(untaggedDisabled,[&](int id){const auto found=next.find(id);return found==next.end()||found->second.first!=false||!found->second.second;});
    const bool changed=!untaggedSource||next!=untaggedFlags;
    untaggedFlags=std::move(next);untaggedEnabled=std::move(enabled);untaggedSource=tags.source;
    if(changed&&(!untaggedEnabled.empty()||!untaggedDisabled.empty())){untaggedPending.insert(live.begin(),live.end());}
    if(untaggedEnabled.empty()&&untaggedDisabled.empty()){untaggedPending.clear();}
    return {};
  }
  bool untaggedOwned(int id,const QString& hash)const{
    return untaggedEnabled.contains(id)&&!untaggedPending.contains(hash)&&!untaggedUncertain.contains(hash);
  }
  std::expected<ConstraintWorkerUpdate,storage::Error> untaggedStep(storage::ManualTagStore& store,
      const storage::ManualTagSnapshot& snapshot,const ConstraintWorkerFrame& frame,std::stop_token stop){
    namespace u=core::tags::untagged;namespace r=core::tags::runtime;ConstraintWorkerUpdate result;result.snapshot=snapshot;
    if(untaggedPending.empty()){untaggedDisabled.clear();return result;}
    auto gate=lease->sourceAccess();if(!gate){return std::unexpected(gate.error());}
    if(gate->revision()!=frame.sourceRevision){return std::unexpected(error(storage::ErrorCode::Conflict,"Untagged source turn changed"));}
    auto inventory=storage::ConfigFile::load(*lease,"downloads.config");if(!inventory){return std::unexpected(inventory.error());}
    auto library=core::readLibrary(inventory->document());if(inventory->recoverySource()!=storage::RecoverySource::Primary||!library||!library->issues.empty()){
      return std::unexpected(error(storage::ErrorCode::Conflict,"Untagged inventory requires settled primary data"));}
    std::set<std::string> canonical;for(const auto& row:library->downloads){if(row.persistent&&row.hash&&!canonical.insert(row.hash->hex()).second){return std::unexpected(error(storage::ErrorCode::Conflict,"Untagged saved targets are ambiguous"));}}
    struct View {const core::SavedDownloadTag* row;std::optional<bool> role;bool known=false,foreign=false,executable=false;};
    std::vector<View> views;bool complete=snapshot.tags()->issues.empty()&&snapshot.tags()->manualTypePresent;
    const auto* type=snapshot.tags()->source->find("3");
    for(const auto& row:snapshot.tags()->tags){
      if(!row.id||row.duplicateId||*row.id<0||row.idKey!=std::to_string(*row.id)){complete=false;views.push_back({&row,{}});continue;}
      const auto* record=type?type->find(row.idKey):nullptr;
      const bool executable=record&&core::rateReadyLifecycleSavedTagMembershipEffects(*snapshot.tags(),*record);
      const bool known=executable&&untaggedMembersKnown(row,record);
      bool foreign=false;for(const auto& hash:row.uniqueHashes){if(!canonical.contains(hash.hex())){foreign=true;break;}}
      views.push_back({&row,untaggedRole(record),known,foreign,executable});
    }
    auto states=storage::TorrentStateStore::open(*lease);if(!states){return std::unexpected(states.error());}
    const auto retire=[&](const QString& key){untaggedPending.erase(key);if(untaggedPending.empty()){untaggedDisabled.clear();}};
    struct Preimage {QString key;core::TorrentHash hash;QByteArray digest;};
    const auto digest=[](const core::Document& doc){const auto& bytes=doc.originalBytes();return QCryptographicHash::hash(QByteArrayView(bytes.data(),qsizetype(bytes.size())),QCryptographicHash::Sha256);};
    std::vector<QString> candidates,completed;std::vector<Preimage> preimages;
    std::vector<storage::ManualTagMembershipEdit> batch;std::set<QString> wake;
    candidates.reserve(32);completed.reserve(32);preimages.reserve(32);batch.reserve(32);
    for(const auto& key:untaggedPending){if(candidates.size()==32){break;}candidates.push_back(key);}
    const bool actionMode=std::ranges::any_of(views,[&](const auto& view){return view.row->id&&needsAssignment(snapshot,*view.row->id)&&(untaggedEnabled.contains(*view.row->id)||untaggedDisabled.contains(*view.row->id));});
    const storage::TagAssignmentObservation* assignment=nullptr;
    for(const auto& key:candidates){
      if(stop.stop_requested()){return std::unexpected(error(storage::ErrorCode::Cancelled,"Untagged turn cancelled"));}
      ++result.evaluated;auto hash=constraintFactHash(key);
      if(!hash){untaggedUncertain.insert(key);retire(key);result.kept=hash.error();continue;}
      if(std::count_if(library->downloads.begin(),library->downloads.end(),[&](const auto& row){return row.hash&&*row.hash==*hash&&row.persistent;})!=1){retire(key);continue;}
      if(std::count_if(frame.library->items.begin(),frame.library->items.end(),[&](const auto& row){return row.hash==key;})!=1||trackerBirths.contains(*hash)){
        result.kept=unavailable("Untagged target awaits its current owned library/birth source");continue;}
      auto original=states->load(*lease,*hash);if(!original||original->recoveredBackup()){
        untaggedUncertain.insert(key);retire(key);result.kept=unavailable("Untagged target requires primary original metadata");continue;}
      std::vector<u::Entry> census;census.reserve(views.size());
      for(const auto& view:views){const bool present=std::ranges::find(view.row->uniqueHashes,*hash)!=view.row->uniqueHashes.end();
        auto membership=r::Membership::Unknown;
        const bool trackerPending=view.role==false&&view.row->id&&trackerProperties.contains(*view.row->id)&&trackerProperties.at(*view.row->id)&&!trackerProperties.at(*view.row->id)->empty()&&!trackerOwned(*view.row->id);
        if(view.known&&!trackerPending&&(present||!view.foreign)){membership=present?r::Membership::Present:r::Membership::Absent;}
        census.push_back({view.role,membership});
      }
      auto other=u::otherManualTags(census,complete);if(!other){untaggedUncertain.insert(key);retire(key);result.kept=unavailable("Untagged current census exceeds its bound");continue;}
      std::vector<storage::ManualTagMembershipEdit> changes;bool unknown=*other==r::Truth::Unknown;
      const auto seconds=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
      for(const auto& view:views){if(!view.row->id){continue;}const auto id=*view.row->id;
        const bool enabled=untaggedEnabled.contains(id),disabled=untaggedDisabled.contains(id);if(!enabled&&!disabled){continue;}
        if(!view.executable||!view.known||view.foreign){unknown=true;continue;}
        const bool member=std::ranges::find(view.row->uniqueHashes,*hash)!=view.row->uniqueHashes.end();
        const auto decision=disabled?(member?u::Membership::Remove:u::Membership::Keep):u::project(*other,true,member);
        if(decision==u::Membership::Add){changes.emplace_back(core::SavedTagAddMember{id,*hash,seconds});}
        else if(decision==u::Membership::Remove){changes.emplace_back(core::SavedTagRemoveMember{id,*hash});}
      }
      if(actionMode&&!changes.empty()){
        const auto anchor=std::ranges::find_if(changes,[&](const auto& change){return needsAssignment(snapshot,std::visit([](const auto& value){return value.id;},change));});
        if(anchor!=changes.end()){
          std::vector<std::int32_t> cohort;cohort.reserve(changes.size());
          for(const auto& change:changes){cohort.push_back(std::visit([](const auto& value){return value.id;},change));}
          assignment=assignmentFor(result,frame,snapshot,*hash,std::visit([](const auto& value){return value.id;},*anchor),std::holds_alternative<core::SavedTagAddMember>(*anchor),ConstraintAssignmentKind::Untagged,cohort);
          if(!assignment){if(result.assignmentRequest){return result;}untaggedUncertain.insert(key);retire(key);continue;}
        }
      }
      if(changes.size()>32){untaggedUncertain.insert(key);retire(key);result.kept=unavailable("Untagged batch effect bound reached; membership retained");continue;}
      // Never split one subject's effects across saved batches. Defer the
      // complete subject when earlier subjects already filled this turn.
      if(batch.size()+changes.size()>32){break;}
      if(unknown){untaggedUncertain.insert(key);result.kept=unavailable("Untagged current manual census is incomplete");}else{untaggedUncertain.erase(key);}
      wake.insert(key);
      if(changes.empty()){completed.push_back(key);continue;}
      preimages.push_back({key,*hash,digest(original->document())});
      batch.insert(batch.end(),std::make_move_iterator(changes.begin()),std::make_move_iterator(changes.end()));
      if(actionMode){break;}
    }
    // Reserve all dependent wakeups before saving; a successful CAS wins.
    auto queued=queueChangedRows(wake,*frame.library);if(!queued){return std::unexpected(queued.error());}
    for(const auto& key:completed){retire(key);}
    if(batch.empty()){return result;}
    auto finalInventory=storage::ConfigFile::load(*lease,"downloads.config");
    if(!finalInventory||finalInventory->recoverySource()!=storage::RecoverySource::Primary||finalInventory->document().originalBytes()!=inventory->document().originalBytes()
        ||gate->revision()!=frame.sourceRevision||storage::hasPendingCategoryDeletion(*lease)||storage::hasPendingFileOperation(*lease)){
      return std::unexpected(error(storage::ErrorCode::Conflict,"Untagged inventory preimage changed before publication"));}
    for(const auto& before:preimages){auto finalState=states->load(*lease,before.hash);
      if(!finalState||finalState->recoveredBackup()||digest(finalState->document())!=before.digest){return std::unexpected(error(storage::ErrorCode::Conflict,"Untagged original-state preimage changed before publication"));}}
    if(stop.stop_requested()){return std::unexpected(error(storage::ErrorCode::Cancelled,"Untagged cancelled before publication"));}
    auto committed=[&]()->std::expected<storage::ManualTagCommit,storage::Error>{
      if(assignment){auto changed=frame.assignmentOwner->changeBatch(store,snapshot,batch,*assignment,stop,std::nullopt,assignmentRates(frame));
        if(!changed){return std::unexpected(changed.error());}result.assignmentReceipt=std::move(changed->receipt);return std::move(changed->commit);}
      return store.applyMembershipBatch(snapshot,batch,stop);
    }();if(!committed){
      if(assignment&&(committed.error().code==storage::ErrorCode::Unsupported||committed.error().code==storage::ErrorCode::Busy)){
        for(const auto& before:preimages){untaggedUncertain.insert(before.key);retire(before.key);}
        result.kept=unavailable("Untagged assignment source is unavailable");return result;
      }return std::unexpected(committed.error());
    }
    result.changed=committed->changed;result.snapshot=std::move(committed->snapshot);
    // Every own delta already reserved its dependent wakeup above. Do not
    // enqueue the same completed subjects again as external membership changes.
    membershipSource=result.snapshot.tags()->source;
    if(preimages.size()==1){result.hash=preimages.front().hash;}
    for(const auto& before:preimages){retire(before.key);}return result;
  }
