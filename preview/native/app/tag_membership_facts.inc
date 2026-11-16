// SPDX-License-Identifier: GPL-2.0-or-later
// Included in biglybtc::app after the ordinary owning fact capture. No IO,
// callback, registry setter, live DownloadManager or publication authority.
namespace {
std::optional<c::Text> tagFactText(const core::Node* node){
  if(!node||node->type!=core::Node::Type::Bytes||node->bytes.size()>4096){return {};}
  const auto value=QString::fromUtf8(node->bytes.data(),static_cast<qsizetype>(node->bytes.size()));
  if(value.toUtf8().toStdString()!=node->bytes){return {};}return value.toStdU16String();
}
bool tagFactStable(const core::Node& record){
  const auto policy=classifyManualTagMembership(record);
  if(policy.malformed||policy.autoAdd||policy.autoRemove||policy.other){return false;}
  const auto* config=record.find("c");if(!config){return true;}
  if(config->type!=core::Node::Type::Dictionary){return false;}
  for(std::size_t i=0;i<config->children.size();i+=2){const auto& key=config->children[i].bytes;
    if(key!="col.rgb"&&key!="gr"&&key!="vis"&&key!="oname"&&key!="pp.constraint"&&key!="pp.enabled.constraint"&&key!="pp.trackers"&&key!="pp.untagged"){return false;}
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
    std::int32_t selectedTag,ui::Presentation library,std::int64_t wall){
  try{
    if(!valid(current)||current!=result.stamp()||selectedTag<0||!store.matchesSnapshotLineage(snapshot,lease)||!snapshot.valid()||!snapshot.generation()
        ||!snapshot.writable()||snapshot.missing()||!snapshot.tags()||!snapshot.tags()->source||!library
        ||library->revision!=current.binding.target.generation||library->items.size()>ui::LibraryModel::MaximumRows){
      return std::unexpected(unavailable("Tag facts require an actual current owning source/target/turn"));
    }
    const auto target=constraintFactHash(current.binding.target.hash);if(!target){return std::unexpected(target.error());}
    const auto matches=std::count_if(library->items.begin(),library->items.end(),[&](const auto& row){return row.hash==current.binding.target.hash;});
    if(matches!=1){return std::unexpected(unavailable("Tag fact library target is missing or ambiguous"));}
    const auto& tags=*snapshot.tags();const auto* manual=tags.source->find("3");
    if(!manual||manual->type!=core::Node::Type::Dictionary||tags.source->root().changed
        ||!snapshot.rawBytes()||*snapshot.rawBytes()!=tags.source->originalBytes()){
      return std::unexpected(unavailable("Tag facts require exact pristine saved manual source"));
    }
    const bool reused=result.hasTagScope();
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
        runtime::Tag item;item.id=*row.id;item.taggableTypes=2;item.name=tagFactText(record->find("n"));if(row.nameMayRequireLocalization){item.name.reset();}
        const bool stable=core::ordinarySavedTagMembershipEffects(tags,*record)&&tagFactStable(*record)&&tagFactMembers(row,*record);
        const bool member=std::find(row.uniqueHashes.begin(),row.uniqueHashes.end(),*target)!=row.uniqueHashes.end();
        item.membership=stable?(member?runtime::Membership::Present:runtime::Membership::Absent):runtime::Membership::Unknown;
        const auto* config=record->find("c");if(const auto* group=config?config->find("gr"):nullptr){item.group=tagFactText(group);item.groupKnown=bool(item.group);}
        if(type.tags.size()==4096){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Runtime tag source count bound"});}
        type.tags.push_back(std::move(item));
      }
      // Saved byte order is retained as metadata, not advertised as a complete
      // live type roster. Unobserved category/state/peer/internal/swarm sources
      // and constrained membership must not become absent/false.
      if(!identities){for(auto& item:type.tags){item.membership=runtime::Membership::Unknown;}}
      std::vector<runtime::Type> types;
      if(result.categoryTagType_){types.push_back(*result.categoryTagType_);}
      if(result.downloadTagNames_&&result.downloadTagMembers_){
        runtime::Type state;state.id=2; // Incomplete operation/activity census.
        for(unsigned id=0;id<15;++id){runtime::Tag item;item.id=static_cast<std::int32_t>(id);item.taggableTypes=2;
          item.name=runtime::Text((*result.downloadTagNames_)[id]);item.groupKnown=false;
          item.membership=(*result.downloadTagMembers_)[id];state.tags.push_back(std::move(item));}
        types.push_back(std::move(state));
      }
      types.push_back(std::move(type));
      auto registry=runtime::Registry::create(std::move(types),false);
      if(!registry){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Runtime tag registry source bounds"});}
      result.facts_.tagRegistry=std::move(*registry);
    }
    // The shared registry belongs to the target/turn/preimage. Own age belongs
    // to THIS selected rule; never inherit another rule's saved timestamp.
    std::erase_if(result.facts_.values,[](const auto& fact){return fact.key=="tagage";});
    const auto* record=manual->find(own->idKey);
    if(core::ordinarySavedTagMembershipEffects(tags,*record)&&tagFactMembers(*own,*record)){
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
    result.tagSnapshot_=snapshot;return result;
  }catch(const std::bad_alloc&){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Tag fact resources unavailable"});}
}
c::Result<c::Value> evaluateCurrentConstraint(const c::Expression& expression,const ConstraintFactStamp& current,
    const ConstraintFactSnapshot& snapshot,const storage::ManualTagStore& store,
    const storage::ProfileLease& lease,const storage::ManualTagSnapshot& tags){
  if(!valid(current)||current!=snapshot.stamp()){return std::unexpected(unavailable("Tag fact target/session/turn is stale"));}
  if(snapshot.hasTagScope()&&(!store.matchesSnapshotLineage(snapshot.tagSnapshot_,lease)||!store.matchesSnapshotLineage(tags,lease)||!tags.valid()||!tags.writable()||snapshot.tagSnapshot_.generation()!=tags.generation()
      ||!snapshot.tagSnapshot_.sameSource(tags))){return std::unexpected(unavailable("Tag fact source generation/preimage is stale"));}
  return c::evaluate(expression,snapshot.facts());
}
