// SPDX-License-Identifier: GPL-2.0-or-later
// Actual private durable-intake consumer, inside the existing worker Impl.
  detail::TrackerTagBirths trackerBirths;
  std::set<QString> trackerBirthUncertain;
  bool nextBirthTurn=true;
  std::expected<detail::TrackerTagBirths::Cohort,storage::Error> trackerBirthRules(const core::SavedDownloadTags& tags){
    detail::TrackerTagBirths::Rules rules;
    for(const auto& row:tags.tags){auto names=trackerNames(tags,row);if(row.id&&names&&!names->empty()){rules.push_back({*row.id,std::move(*names)});}}
    auto shared=trackerBirths.cohort(std::move(rules));
    if(!shared){return std::unexpected(error(storage::ErrorCode::Unsupported,"Tracker birth source cohort is unavailable or exceeds its bound"));}return *shared;
  }
  std::expected<ConstraintWorkerUpdate,storage::Error> trackerBirthStep(storage::ManualTagStore& store,
      const storage::ManualTagSnapshot& snapshot,const ConstraintWorkerFrame& frame,std::stop_token stop){
    namespace t=core::tags::tracker;ConstraintWorkerUpdate result;result.snapshot=snapshot;
    if(!trackerBirths.size()){return result;}
    auto gate=lease->sourceAccess();if(!gate){return std::unexpected(gate.error());}
    if(gate->revision()!=frame.sourceRevision){return std::unexpected(error(storage::ErrorCode::Conflict,"Tracker birth source turn changed"));}
    auto inventory=storage::ConfigFile::load(*lease,"downloads.config");if(!inventory){return std::unexpected(inventory.error());}
    auto library=core::readLibrary(inventory->document());
    if(inventory->recoverySource()!=storage::RecoverySource::Primary||!library||!library->issues.empty()){
      return std::unexpected(error(storage::ErrorCode::Conflict,"Tracker birth inventory requires settled primary data"));}
    auto stateStore=storage::TorrentStateStore::open(*lease);if(!stateStore){return std::unexpected(stateStore.error());}
    const auto finish=[&](const detail::TrackerTagBirths::Request& request,bool withdraw)->std::expected<void,storage::Error>{
      auto done=withdraw?trackerBirths.withdraw(request):trackerBirths.accept(request);
      if(!done){return std::unexpected(error(storage::ErrorCode::Conflict,"Tracker birth request is no longer current"));}
      if(!trackerBirths.contains(request.hash)){return queueChangedRows({QString::fromStdString(request.hash.hex())},*frame.library);}return {};
    };
    while(trackerBirths.size()&&result.evaluated<32){
      if(stop.stop_requested()){return std::unexpected(error(storage::ErrorCode::Cancelled,"Tracker birth turn cancelled"));}
      auto pending=trackerBirths.next();if(!pending){return std::unexpected(error(storage::ErrorCode::Unsupported,"Tracker birth request bound reached"));}if(!*pending){break;}
      const auto request=**pending;const auto& rule=(*request.rules)[request.index];++result.evaluated;result.tag=rule.id;result.hash=request.hash;
      const auto key=QString::fromStdString(request.hash.hex());
      const auto durable=std::count_if(library->downloads.begin(),library->downloads.end(),[&](const auto& row){return row.hash&&*row.hash==request.hash&&row.persistent;});
      if(durable!=1){auto done=finish(request,true);if(!done){return std::unexpected(done.error());}continue;}
      const auto published=std::count_if(frame.library->items.begin(),frame.library->items.end(),[&](const auto& row){return row.hash==key;});
      if(published>1){return std::unexpected(error(storage::ErrorCode::Conflict,"Tracker birth published target is ambiguous"));}
      if(!published){if(!trackerBirths.defer(request)){return std::unexpected(error(storage::ErrorCode::Io,"Tracker birth deferral failed"));}result.kept=unavailable("Durable tracker birth awaits its current published row");return result;}
      const auto* definition=tag(*snapshot.tags(),rule.id);auto names=definition?trackerNames(*snapshot.tags(),*definition):std::nullopt;
      if(!names||*names!=rule.names){auto done=finish(request,false);if(!done){return std::unexpected(done.error());}continue;}
      auto state=stateStore->load(*lease,request.hash);
      if(!state||state->recoveredBackup()||addedTime(state->document())!=request.addedTime){
        trackerBirthUncertain.insert(key);auto done=finish(request,true);if(!done){return std::unexpected(done.error());}result.kept=unavailable("Tracker birth durable original state changed or requires recovery");continue;}
      // Creation only adds. Existing membership is not reinterpreted as a new
      // removal, and a real callback has no NewOnly five-second expiry here.
      if(std::ranges::find(definition->uniqueHashes,request.hash)!=definition->uniqueHashes.end()){
        auto done=finish(request,false);if(!done){return std::unexpected(done.error());}continue;}
      const auto& document=state->document();const auto* announce=document.find("announce");
      std::vector<t::Endpoint> endpoints;bool known=announce&&announce->type==core::Node::Type::Bytes;
      const auto append=[&](const core::Node& node){if(node.type!=core::Node::Type::Bytes||endpoints.size()==64){known=false;return;}auto parsed=t::endpoint(node.bytes);if(!parsed){known=false;return;}endpoints.push_back(std::move(*parsed));};
      if(known){append(*announce);}
      if(const auto* tiers=document.find("announce-list")){
        if(tiers->type!=core::Node::Type::List||tiers->children.size()>16){known=false;}
        else{for(const auto& tier:tiers->children){if(tier.type!=core::Node::Type::List){known=false;break;}for(const auto& value:tier.children){append(value);}}}
      }
      auto hit=known?t::matches(endpoints,*names):t::Result<bool>{std::unexpected(t::Error::Unsupported)};
      if(!hit){trackerBirthUncertain.insert(key);auto done=finish(request,false);if(!done){return std::unexpected(done.error());}result.kept=unavailable("Tracker birth URL source is outside the qualified subset");continue;}
      if(!*hit){auto done=finish(request,false);if(!done){return std::unexpected(done.error());}continue;}
      const storage::TagAssignmentObservation* assignment=nullptr;
      if(needsAssignment(snapshot,rule.id)){
        assignment=assignmentFor(result,frame,snapshot,request.hash,rule.id,true,ConstraintAssignmentKind::TrackerBirth);
        if(!assignment){
          if(result.assignmentRequest){return result;} // Keep the exact outstanding birth while the Host observes it.
          trackerBirthUncertain.insert(key);auto done=finish(request,false);if(!done){return std::unexpected(done.error());}continue;
        }
      }
      auto finalInventory=storage::ConfigFile::load(*lease,"downloads.config");auto finalState=stateStore->load(*lease,request.hash);
      if(!finalInventory||finalInventory->recoverySource()!=storage::RecoverySource::Primary
          ||finalInventory->document().originalBytes()!=inventory->document().originalBytes()
          ||!finalState||finalState->recoveredBackup()||finalState->document().originalBytes()!=document.originalBytes()
          ||gate->revision()!=frame.sourceRevision||storage::hasPendingCategoryDeletion(*lease)||storage::hasPendingFileOperation(*lease)){
        return std::unexpected(error(storage::ErrorCode::Conflict,"Tracker birth inventory/state preimage changed before publication"));}
      if(stop.stop_requested()){return std::unexpected(error(storage::ErrorCode::Cancelled,"Tracker birth cancelled before publication"));}
      // Allocate the eventual row wakeup before publication. Queue ack itself
      // is noexcept; no cancellation checkpoint may lose the actual save.
      const std::set<QString> affected{key};auto queued=queueChangedRows(affected,*frame.library);if(!queued){return std::unexpected(queued.error());}
      const auto seconds=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
      auto saved=[&]()->std::expected<storage::ManualTagCommit,storage::Error>{
        if(assignment){auto changed=frame.assignmentOwner->change(store,snapshot,rule.id,*assignment,true,seconds,stop,assignmentRates(frame));
          if(!changed){return std::unexpected(changed.error());}result.assignmentReceipt=std::move(changed->receipt);return std::move(changed->commit);}
        return store.apply(snapshot,core::SavedTagAddMember{rule.id,request.hash,seconds},stop);
      }();if(!saved){
        if(assignment&&(saved.error().code==storage::ErrorCode::Unsupported||saved.error().code==storage::ErrorCode::Busy)){
          trackerBirthUncertain.insert(key);auto done=finish(request,false);if(!done){return std::unexpected(done.error());}
          result.kept=unavailable("Tracker birth assignment source is unavailable");return result;
        }return std::unexpected(saved.error());
      }
      result.changed=saved->changed;result.snapshot=std::move(saved->snapshot);
      const auto acknowledged=trackerBirths.accept(request);if(!acknowledged){trackerBirths.clear();}
      return result;
    }return result;
  }
