// SPDX-License-Identifier: GPL-2.0-or-later
// Included in biglybtc::app after the ordinary owning fact capture. No IO,
// callback, registry setter, live DownloadManager or publication authority.
namespace {
std::optional<c::Text> tagFactText(const core::Node* node){
  if(!node||node->type!=core::Node::Type::Bytes||node->bytes.size()>4096){return {};}
  const auto value=QString::fromUtf8(node->bytes.data(),static_cast<qsizetype>(node->bytes.size()));
  if(value.toUtf8().toStdString()!=node->bytes){return {};}return value.toStdU16String();
}
std::optional<c::Text> tagFactName(const core::Node* node,const std::optional<std::string>& locale){
  auto name=tagFactText(node);if(!name||!name->starts_with(u"tag.")){return name;}
  if(!locale){return {};}auto stock=core::tags::stockTagName(*locale,*name);
  return stock?std::optional<c::Text>{*stock}:std::nullopt;
}
c::Result<core::tags::runtime::Type> savedSwarmTagType(const core::Document& source,const std::optional<std::string>& locale){
  namespace runtime=core::tags::runtime;runtime::Type type;type.id=7;
  const auto* saved=source.find("7");if(!saved){return type;}
  if(saved->type!=core::Node::Type::Dictionary){return std::unexpected(unavailable("Saved Swarm Tag type is unreadable"));}
  std::set<std::int32_t> identities;
  for(std::size_t at=0;at<saved->children.size();at+=2){
    const auto key=saved->children[at].bytes;
    // Original loadTags ignores type-level c and other non-digit attributes.
    if(key.empty()||key.front()<'0'||key.front()>'9'){continue;}
    if(type.tags.size()==4096){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Swarm Tag metadata source bound"});}
    const auto& record=saved->children[at+1];std::int32_t id=-1;
    const auto parsed=std::from_chars(key.data(),key.data()+key.size(),id);
    if(parsed.ec!=std::errc{}||parsed.ptr!=key.data()+key.size()||id<0||key!=std::to_string(id)
        ||!identities.insert(id).second||record.type!=core::Node::Type::Dictionary){
      return std::unexpected(unavailable("Saved Swarm Tag identity is ambiguous"));
    }
    runtime::Tag value;value.id=id;value.taggableTypes=0;value.name=tagFactName(record.find("n"),locale);
    // Actual TagSwarmTagImpl is TT_NONE and cannot add/remove a taggable.
    // Its saved-map constructor has no resolver, so saved o/p never becomes
    // actual download/peer membership. Saved metadata is not a source grant.
    value.membership=runtime::Membership::Absent;
    const auto* config=record.find("c");
    if(config&&config->type!=core::Node::Type::Dictionary){value.groupKnown=false;}
    else if(const auto* group=config?config->find("gr"):nullptr){value.group=tagFactText(group);value.groupKnown=bool(value.group);}
    type.tags.push_back(std::move(value));
  }
  // Persisted declarations do not certify the full live type registration
  // order or later runtime creation/last-seen events. Keep the census partial.
  return type;
}
bool knownDownloadTagDomain(const core::Document& source){
 // Unknown stored families or possible legacy runtime-name overrides do not
 // certify a closed current name domain. Preserve them without inferring No.
 std::size_t work=0;const auto& root=source.root();if(root.type!=core::Node::Type::Dictionary){return false;}
 for(std::size_t at=0;at<root.children.size();at+=2){if(++work>8192){return false;}const auto key=root.children[at].bytes;
  if(key.empty()||key.front()<'0'||key.front()>'9'){continue;}
  int type=0;const auto parsed=std::from_chars(key.data(),key.data()+key.size(),type);
  if(parsed.ec!=std::errc{}||parsed.ptr!=key.data()+key.size()||type<1||type>7||key!=std::to_string(type)){return false;}
  if(type==3||type==7||type==6){continue;}
  const auto& saved=root.children[at+1];if(saved.type!=core::Node::Type::Dictionary){return false;}
  for(std::size_t i=0;i<saved.children.size();i+=2){if(++work>8192){return false;}const auto& record=saved.children[i+1];
   if(saved.children[i].bytes=="c"){continue;}
   if(record.type!=core::Node::Type::Dictionary||record.find("n")){return false;}
   const auto* config=record.find("c");if(config&&(config->type!=core::Node::Type::Dictionary||config->find("oname")||config->find("n"))){return false;}
  }
 }
 return true;
}
bool tagFactStable(const core::Node& record){
  const auto policy=classifyManualTagMembership(record);
  if(policy.malformed||policy.autoAdd||policy.autoRemove||policy.other){return false;}
  const auto* config=record.find("c");if(!config){return true;}
  if(config->type!=core::Node::Type::Dictionary){return false;}
  for(std::size_t i=0;i<config->children.size();i+=2){const auto& key=config->children[i].bytes;
    if(!core::savedTagLifecycleProperty(key,config->children[i+1])&&!core::savedTagDisplayMetadata(key,config->children[i+1])&&key!="col.rgb"&&key!="gr"&&key!="vis"&&key!="oname"&&key!="pp.constraint"&&key!="pp.enabled.constraint"&&key!="pp.trackers"&&key!="pp.untagged"){return false;}
  }
  return true;
}
bool tagFactMembers(const core::SavedDownloadTag& row,const core::Node& record){
  if(row.membersMalformed){return false;}const auto* properties=record.find("p");
  if(!row.membersPresent){return true;}
  if(properties&&(properties->type!=core::Node::Type::List||properties->children.size()!=row.members.size())){return false;}
  for(const auto& member:row.members){
    if(!member.hash||member.duplicate){return false;}
    if(properties){const auto& prop=properties->children[member.sourceIndex];const auto* time=prop.find("a");
      if(prop.type!=core::Node::Type::Dictionary||(time&&time->type!=core::Node::Type::Integer)){return false;}}
  }
  return true;
}
std::int64_t tagFactAge(std::int64_t seconds,std::int64_t wall){
  // Original TagWithState returns seconds*1000 with Java long wrap; handler
  // then performs wrapped subtraction, truncating signed division and clamp.
  const auto added=std::bit_cast<std::int64_t>(static_cast<std::uint64_t>(seconds)*1000U);
  if(added<=0){return 0;}
  const auto difference=std::bit_cast<std::int64_t>(static_cast<std::uint64_t>(wall)-static_cast<std::uint64_t>(added));
  return std::max<std::int64_t>(0,difference/1000);
}
}
c::Result<ConstraintFactSnapshot> captureConstraintTagMembership(ConstraintFactSnapshot result,
    const ConstraintFactStamp& current,const storage::ManualTagStore& store,
    const storage::ProfileLease& lease,const storage::ManualTagSnapshot& snapshot,
    std::int32_t selectedTag,ui::Presentation library,std::int64_t wall,const storage::ManualTagMembershipPlan* plan,const ConstraintMembershipCohort* cohort){
  try{
    if(!valid(current)||current!=result.stamp()||selectedTag<0||!store.matchesSnapshotLineage(snapshot,lease)||!snapshot.valid()||!snapshot.generation()
        ||!snapshot.writable()||snapshot.missing()||!snapshot.tags()||!snapshot.tags()->source||!library
        ||library->revision!=current.binding.target.generation||library->items.size()>ui::LibraryModel::MaximumRows){
      return std::unexpected(unavailable("Tag facts require an actual current owning source/target/turn"));
    }
    if(cohort&&(!cohort->valid()||cohort->stamp()!=current||!cohort->ordinaryRoot(selectedTag)
        ||!store.matchesSnapshotLineage(cohort->source(),lease)||cohort->source().generation()!=snapshot.generation()
        ||!cohort->source().sameSource(snapshot))){
      return std::unexpected(unavailable("Ordinary constraint cohort has no current actor/source/root scope"));
    }
    const auto target=constraintFactHash(current.binding.target.hash);if(!target){return std::unexpected(target.error());}
    const auto matches=std::count_if(library->items.begin(),library->items.end(),[&](const auto& row){return row.hash==current.binding.target.hash;});
    if(matches!=1){return std::unexpected(unavailable("Tag fact library target is missing or ambiguous"));}
    if(plan&&(!plan->valid()||!store.matchesMembershipPlan(*plan,lease,snapshot)||!plan->tags()||!plan->rawBytes())){
      return std::unexpected(unavailable("Membership plan has no current actual source lineage"));
    }
    const auto& tags=plan?*plan->tags():*snapshot.tags();const auto* manual=tags.source->find("3");
    const auto raw=plan?plan->rawBytes():snapshot.rawBytes();
    if(!manual||manual->type!=core::Node::Type::Dictionary||tags.source->root().changed
        ||!raw||*raw!=tags.source->originalBytes()){
      return std::unexpected(unavailable("Tag facts require exact pristine saved manual source"));
    }
    if(result.internalTagScope_&&!result.internalTagScope_->current(lease,current.binding.session)){return std::unexpected(unavailable("Internal tag namespace belongs to a stale/foreign profile"));}
    const bool reused=result.hasTagScope();
    if(reused&&(result.tagCohort_.valid()!=bool(cohort)||(cohort&&!result.tagCohort_.sameScope(*cohort)))){
      return std::unexpected(unavailable("Ordinary constraint cohort changed before metadata reuse"));
    }
    if(reused&&(result.tagPlan_.valid()!=bool(plan)||(plan&&!result.tagPlan_.sameView(*plan)))){
      return std::unexpected(unavailable("Membership plan view changed before registry reuse"));
    }
    if(reused&&(!result.facts_.tagRegistry||!store.matchesSnapshotLineage(result.tagSnapshot_,lease)
        ||result.tagSnapshot_.generation()!=snapshot.generation()||!result.tagSnapshot_.sameSource(snapshot))){
      return std::unexpected(unavailable("Runtime tag registry source/lineage changed before reuse"));
    }
    const core::SavedDownloadTag* own=nullptr;
    for(const auto& row:tags.tags){if(row.id&&*row.id==selectedTag&&!row.duplicateId&&row.idKey==std::to_string(*row.id)){
      if(own){return std::unexpected(unavailable("Selected tag identity is ambiguous"));}own=&row;}}
    if(!own){return std::unexpected(unavailable("Selected actual manual tag is missing"));}
    if(!reused){
      for(const auto* key:{"registered_tags","present_manual_tag_names","present_manual_tag_groups","tagage"}){
        if(std::any_of(result.facts_.values.begin(),result.facts_.values.end(),[&](const auto& fact){return fact.key==key;})){
          return std::unexpected(unavailable("Tag facts cannot replace another provider"));}}
      if(result.facts_.tagRegistry){return std::unexpected(unavailable("Tag facts cannot replace another registry provider"));}
      namespace runtime=core::tags::runtime;
      bool identities=true;runtime::Type type;type.id=3;
      for(const auto& row:tags.tags){
        if(!row.id||row.duplicateId||row.idKey!=std::to_string(*row.id)){identities=false;continue;}
        const auto* record=manual->find(row.idKey);if(!record||record->type!=core::Node::Type::Dictionary){identities=false;continue;}
        runtime::Tag item;item.id=*row.id;item.taggableTypes=2;item.name=tagFactName(record->find("n"),result.stockTagLocale_);
        const bool stable=core::lifecycleSavedTagMembershipEffects(tags,*record)&&(tagFactStable(*record)||(cohort&&cohort->knows(*row.id)))&&tagFactMembers(row,*record);
        const bool member=std::find(row.uniqueHashes.begin(),row.uniqueHashes.end(),*target)!=row.uniqueHashes.end();
        // A full32 runtime/retained member may need the original source-bound
        // identity selector. A missing raw-byte equality is not proven absence.
        const bool absentKnown=target->bytes().size()==20&&std::all_of(row.uniqueHashes.begin(),row.uniqueHashes.end(),[](const auto& value){return value.bytes().size()==20;});
        item.membership=stable&&(member||absentKnown)?(member?runtime::Membership::Present:runtime::Membership::Absent):runtime::Membership::Unknown;
        const auto* config=record->find("c");if(const auto* group=config?config->find("gr"):nullptr){item.group=tagFactText(group);item.groupKnown=bool(item.group);}
        if(type.tags.size()==4096){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Runtime tag source count bound"});}
        type.tags.push_back(std::move(item));
      }
      // A complete observed name roster does not certify source registration
      // order or complete per-subject membership. General missing families and
      // constrained membership must not become absent/false.
      type.complete=identities;
      if(!identities){for(auto& item:type.tags){item.membership=runtime::Membership::Unknown;}}
      std::vector<runtime::Type> types;
      if(result.categoryTagType_){types.push_back(*result.categoryTagType_);}
      if(result.downloadTagNames_){
        runtime::Type state;state.id=2;state.complete=true; // Fixed15 names; membership stays separately observed.
        const auto group=result.stockTagLocale_?core::tags::stockTagName(*result.stockTagLocale_,u"tag.type.ds"):std::nullopt;
        for(unsigned id=0;id<15;++id){runtime::Tag item;item.id=static_cast<std::int32_t>(id);item.taggableTypes=2;
          item.name=runtime::Text((*result.downloadTagNames_)[id]);item.groupKnown=bool(group);if(group){item.group=runtime::Text(*group);}
          item.membership=result.downloadTagMembers_?(*result.downloadTagMembers_)[id]:runtime::Membership::Unknown;state.tags.push_back(std::move(item));}
        types.push_back(std::move(state));
      }
      if(result.peerSetTagType_){types.push_back(*result.peerSetTagType_);}
      types.push_back(std::move(type));
      if(result.internalTagType_){types.push_back(*result.internalTagType_);}
      auto swarm=savedSwarmTagType(*tags.source,result.stockTagLocale_);if(!swarm){return std::unexpected(swarm.error());}
      types.push_back(std::move(*swarm));
      auto registry=runtime::Registry::create(std::move(types),false);
      if(!registry){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Runtime tag registry source bounds"});}
      result.facts_.tagRegistry=std::move(*registry);
    }
    // The shared registry belongs to the target/turn/preimage. Own age belongs
    // to THIS selected rule; never inherit another rule's saved timestamp.
    std::erase_if(result.facts_.values,[](const auto& fact){return fact.key=="tagage";});
    const auto* record=manual->find(own->idKey);
    if(core::lifecycleSavedTagMembershipEffects(tags,*record)&&tagFactMembers(*own,*record)){
      // Original transient ta:UID survives removal, but saved o/p no longer
      // contains that timestamp. Do not invent0 for an absent/unknown member.
      std::optional<std::int64_t> age;const auto* properties=record->find("p");
      for(const auto& member:own->members){if(member.hash&&*member.hash==*target&&properties){
        if(const auto* added=properties->children[member.sourceIndex].find("a")){age=tagFactAge(added->integer,wall);}}}
      if(age){result.facts_.values.push_back({"tagage",*age});}
    }
    // Existing core quotas remain authoritative; do not truncate a list into a
    // false negative or grant global-registry completeness from this snapshot.
    std::size_t units=result.facts_.tagRegistry?result.facts_.tagRegistry->textUnits():0;
    for(const auto& fact:result.facts_.values){
      const auto count=[&](const c::Text& value){if(value.size()>65536||value.size()>262144-units){return false;}units+=value.size();return true;};
      if(const auto* value=std::get_if<c::Text>(&fact.value);value&&!count(*value)){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Tag fact text bound"});}
      if(const auto* values=std::get_if<c::Strings>(&fact.value)){if(values->size()>4096){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Tag fact list bound"});}
        for(const auto& value:*values){if(!count(value)){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Tag fact text bound"});}}}
    }
    if(result.facts_.values.size()>128){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Tag fact count bound"});}
    result.facts_.downloadTagCensus=!plan&&target->bytes().size()==20&&result.internalTagScope_.has_value()&&knownDownloadTagDomain(*tags.source);
    result.tagSnapshot_=snapshot;if(plan){result.tagPlan_=*plan;}if(cohort){result.tagCohort_=*cohort;}return result;
  }catch(const std::bad_alloc&){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Tag fact resources unavailable"});}
}
c::Result<c::Value> evaluateCurrentConstraint(const c::Expression& expression,const ConstraintFactStamp& current,
    const ConstraintFactSnapshot& snapshot,const storage::ManualTagStore& store,
    const storage::ProfileLease& lease,const storage::ManualTagSnapshot& tags){
  if(!valid(current)||current!=snapshot.stamp()){return std::unexpected(unavailable("Tag fact target/session/turn is stale"));}
  if(snapshot.internalTagScope_&&!snapshot.internalTagScope_->current(lease,current.binding.session)){return std::unexpected(unavailable("Internal tag namespace source generation is stale"));}
  if(snapshot.hasTagScope()&&(!store.matchesSnapshotLineage(snapshot.tagSnapshot_,lease)||!store.matchesSnapshotLineage(tags,lease)||!tags.valid()||!tags.writable()||snapshot.tagSnapshot_.generation()!=tags.generation()
      ||!snapshot.tagSnapshot_.sameSource(tags))){return std::unexpected(unavailable("Tag fact source generation/preimage is stale"));}
  if(snapshot.tagCohort_.valid()&&(snapshot.tagCohort_.stamp()!=current
      ||!store.matchesSnapshotLineage(snapshot.tagCohort_.source(),lease)
      ||snapshot.tagCohort_.source().generation()!=tags.generation()||!snapshot.tagCohort_.source().sameSource(tags))){
    return std::unexpected(unavailable("Ordinary constraint cohort lost its actual source"));
  }
  if(snapshot.tagPlan_.valid()&&!store.matchesMembershipPlan(snapshot.tagPlan_,lease,tags)){
    return std::unexpected(unavailable("Membership plan lost its actual physical base"));
  }
  return c::evaluate(expression,snapshot.facts());
}
