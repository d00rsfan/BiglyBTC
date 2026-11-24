// Source-bound named metrics for ordinary current membership only.
bool ConstraintNamedMetricCensus::current(const ConstraintFactStamp& stamp,
    const storage::ManualTagStore& store,const storage::ProfileLease& lease,
    const storage::ManualTagSnapshot& source)const{
 return data_&&data_->session==stamp.binding.session&&data_->sequence==stamp.sequence
   &&data_->libraryRevision==stamp.binding.target.generation&&data_->sourceRevision==lease.sourceRevision()
   &&store.matchesSnapshotLineage(data_->source,lease)&&store.matchesSnapshotLineage(source,lease)
   &&data_->source.generation()==source.generation()&&data_->source.sameSource(source);
}
bool ConstraintNamedMetricCensus::containsPrimaryV1(const core::TorrentHash& hash)const{
 return data_&&hash.bytes().size()==20&&std::binary_search(data_->primaryV1.begin(),data_->primaryV1.end(),hash.hex());
}
bool ConstraintNamedMetricCensus::hasCompletePrimaryCohort(std::size_t size)const{
 return data_&&data_->loadedDownloads&&data_->primaryV1.size()==size;
}
std::optional<std::int32_t> ConstraintNamedMetricCensus::queuePosition(const core::TorrentHash& hash)const{
 if(!data_){return {};}
 const auto key=hash.hex();const auto found=std::lower_bound(data_->positions.begin(),data_->positions.end(),key,[](const auto& value,const auto& wanted){return value.first<wanted;});
 return found!=data_->positions.end()&&found->first==key?std::optional{found->second}:std::nullopt;
}
std::optional<std::int32_t> ConstraintNamedMetricCensus::categoryCount(std::int32_t id) const {
 if (!data_) { return {}; }
 if (id == 1) { return data_->loadedDownloads; }
 if (!data_->categories) { return {}; }
 return static_cast<std::int32_t>(std::ranges::count(*data_->categories, id,
     &std::pair<std::string,std::int32_t>::second));
}

std::optional<std::int32_t> ConstraintNamedMetricCensus::categoryRank(std::int32_t id, const core::TorrentHash& hash) const {
 const auto position = queuePosition(hash);
 if (!position || !data_->loadedDownloads) { return {}; }
 std::int32_t before = 0;
 if (id == 1) {
  if (data_->positions.size() != static_cast<std::size_t>(*data_->loadedDownloads)) { return {}; }
  return static_cast<std::int32_t>(std::ranges::count_if(data_->positions,
      [&](const auto& row) { return row.second < *position; }));
 }
 if (!data_->categories) { return {}; }
 const auto target = hash.hex();
 const auto subject = std::ranges::lower_bound(*data_->categories, target, {}, &std::pair<std::string,std::int32_t>::first);
 if (subject == data_->categories->end() || subject->first != target || subject->second != id) { return {}; }
 auto queued = data_->positions.begin();
 for (const auto& [key, category] : *data_->categories) {
  if (category != id) { continue; }
  queued = std::lower_bound(queued, data_->positions.end(), key,
      [](const auto& row, const auto& value) { return row.first < value; });
  if (queued == data_->positions.end() || queued->first != key) { return {}; }
  if (queued->second < *position) { ++before; }
 }
 return before;
}
std::optional<std::int32_t> ConstraintNamedMetricCensus::stateCount(std::int32_t id) const {
 if (!data_ || !data_->states || id < 0 || id >= 15) { return {}; }
 using Membership = core::tags::runtime::Membership;
 std::int32_t count = 0;
 for (const auto& row : *data_->states) {
  const auto membership = row.second[static_cast<std::size_t>(id)];
  if (membership == Membership::Unknown) { return {}; }
  if (membership == Membership::Present) { ++count; }
 }
 return count;
}

std::optional<std::int32_t> ConstraintNamedMetricCensus::peerSetCount(std::int32_t id) const {
 if(!data_){return {};}
 const auto found=std::ranges::find(data_->peerSetCounts,id,&std::pair<std::int32_t,std::int32_t>::first);
 return found==data_->peerSetCounts.end()?std::nullopt:std::optional{found->second};
}

std::optional<std::int32_t> ConstraintNamedMetricCensus::stateRank(
    std::int32_t id, const core::TorrentHash& hash) const {
 const auto position = queuePosition(hash);
 if (!position || !stateCount(id)) { return {}; }
 using Membership = core::tags::runtime::Membership;
 const auto target = hash.hex();
 std::int32_t before = 0;
 bool found = false;
 auto queued = data_->positions.begin();
 for (const auto& [key, states] : *data_->states) {
  if (states[static_cast<std::size_t>(id)] != Membership::Present) { continue; }
  queued = std::lower_bound(queued, data_->positions.end(), key,
      [](const auto& row, const auto& value) { return row.first < value; });
  if (queued == data_->positions.end() || queued->first != key) { return {}; }
  if (queued->second < *position) { ++before; }
  if (key == target) { found = true; }
 }
 return found ? std::optional{before} : std::nullopt;
}

c::Result<ConstraintFactSnapshot> captureConstraintNamedTagMetrics(ConstraintFactSnapshot result,
    const ConstraintFactStamp& current,const storage::ManualTagStore& store,const storage::ProfileLease& lease,
    const storage::ManualTagSnapshot& tags,std::span<const c::NamedTagRequest> requests,std::int64_t wall,
    const ConstraintNamedMetricCensus* census,std::span<const ConstraintNamedCategoryBinding> categoryBindings){
 try{
  if(!valid(current)||current!=result.stamp()||!result.hasTagScope()||result.tagPlan_.valid()
      ||!tags.valid()||!tags.writable()||!tags.tags()||!tags.tags()->source
      ||!store.matchesSnapshotLineage(tags,lease)||!store.matchesSnapshotLineage(result.tagSnapshot_,lease)
      ||tags.generation()!=result.tagSnapshot_.generation()||!tags.sameSource(result.tagSnapshot_)
      ||(result.internalTagScope_&&!result.internalTagScope_->current(lease,current.binding.session))){
   return std::unexpected(unavailable("Named metrics require current ordinary source lineage"));
  }
  if(requests.size()>4096||categoryBindings.size()>4096){
   return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Named metric request bound"});
  }
  if(!std::ranges::is_sorted(categoryBindings,{},&ConstraintNamedCategoryBinding::name)){
   return std::unexpected(unavailable("Unordered compiled category bindings"));
  }
  result.facts_.namedTagMetrics.reset();result.facts_.wallMilliseconds=wall;
  if(requests.empty()){return result;}
  const auto target=constraintFactHash(current.binding.target.hash);
  const auto* manual=tags.tags()->source->find("3");
  if(!target||!manual||!result.facts_.tagRegistry){return std::unexpected(unavailable("Named metric metadata is unavailable"));}
  std::vector<c::NamedTagMetric> metrics;std::size_t visits=0;
  const auto charge=[&](std::size_t count){if(count>32768-visits){return false;}visits+=count;return true;};
  const bool countCurrent=census&&census->current(current,store,lease,tags);
  for(const auto& request:requests){
   const auto bound = std::ranges::lower_bound(categoryBindings, request.name, {}, &ConstraintNamedCategoryBinding::name);
   if (bound != categoryBindings.end() && bound->name == request.name) {
    c::NamedTagMetric metric;
    metric.uid = (std::int64_t{1} << 32) | bound->id;
    auto membership = core::tags::runtime::Membership::Unknown;
    if (const auto& type = result.categoryTagType_) {
     const auto projected = std::ranges::find(type->tags, bound->id, &core::tags::runtime::Tag::id);
     if (projected != type->tags.end()) {
      membership = projected->membership;
      if (request.count && countCurrent) { metric.count = census->categoryCount(bound->id); }
      if (request.position && countCurrent) { metric.queuePosition = census->categoryRank(bound->id, *target); }
     } else if (bound->retired) {
      // The same normal category removal cleared every assigned member before
      // deleting the definition. This ID can never be rebound in this session.
      membership = core::tags::runtime::Membership::Absent;
      if (request.count) { metric.count = 0; }
     }
    }
    metric.category = c::NamedTagMetric::CategoryBinding{request.name, membership};
    if (request.age) { metric.addedMilliseconds = -1; }
    if (request.position) { metric.addedPosition = -1; }
    metrics.push_back(std::move(metric));
    continue;
   }
   auto selection=result.facts_.tagRegistry->lookupConstraintName(request.name);
   if(!selection){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Named metric name source bound"});}
   if(!selection->complete||selection->tags.size()!=1){continue;}
   const auto reference=selection->tags.front();const auto* projected=result.facts_.tagRegistry->at(reference);
   if(!projected||projected->membership==core::tags::runtime::Membership::Unknown){continue;}
   if(reference.type==1){
    c::NamedTagMetric metric;metric.uid=reference.uid();
    // CategoryImpl inherits the original no-added-time value from TagBase.
    if(request.age){metric.addedMilliseconds=-1;}
    if(request.count&&countCurrent){metric.count=census->categoryCount(reference.id);}
    if(request.position){
     metric.addedPosition=-1;
     if(countCurrent){metric.queuePosition=census->categoryRank(reference.id,*target);}
    }
    metrics.push_back(std::move(metric));
    continue;
   }
   if (reference.type == 2) {
    // Original state tags are nonpersistent. TagWithState supplies the
    // sentinel for both age and ranks sorted by added time.
    c::NamedTagMetric metric;
    metric.uid = reference.uid();
    if (request.age) { metric.addedMilliseconds = -1; }
    if (request.count && countCurrent) { metric.count = census->stateCount(reference.id); }
    if (request.position) {
     metric.addedPosition = -1;
     if (countCurrent) { metric.queuePosition = census->stateRank(reference.id,*target); }
    }
    metrics.push_back(std::move(metric));
    continue;
   }
   if(reference.type==4){
    if(request.count&&countCurrent){
     c::NamedTagMetric metric;
     metric.uid=reference.uid();
     metric.count=census->peerSetCount(reference.id);
     metrics.push_back(std::move(metric));
    }
    continue;
   }
   if(reference.type==5&&result.internalTagScope_){
    const auto members=result.internalTagScope_->trackerMembers(lease,current.binding.session,reference.id);
    if(!members){continue;}
    c::NamedTagMetric metric;metric.uid=reference.uid();
    if(request.age){metric.addedMilliseconds=-1;}
    if(request.position){metric.addedPosition=-1;}
    if(countCurrent&&census->hasCompletePrimaryCohort(members->size())&&(request.count||request.position)){
     if(!charge(members->size())){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Named metric member work bound"});}
     const auto targetPosition=request.position?census->queuePosition(*target):std::nullopt;
     bool complete=true;
     bool positions=targetPosition.has_value();
     std::int32_t count=0,before=0;
     for(const auto& [key,member]:*members){
      const auto hash=constraintFactHash(QString::fromStdString(key));
      if(!hash||!census->containsPrimaryV1(*hash)){complete=false;break;}
      if(!member){continue;}
      ++count;
      if(positions){
       const auto position=census->queuePosition(*hash);
       if(!position){positions=false;}
       else if(*position<*targetPosition){++before;}
      }
     }
     if(complete&&request.count){metric.count=count;}
     if(complete&&positions&&projected->membership==core::tags::runtime::Membership::Present){
      metric.queuePosition=before;
     }
    }
    metrics.push_back(std::move(metric));
    continue;
   }
   if(reference.type!=3){continue;}
   const core::SavedDownloadTag* row=nullptr;
   for(const auto& candidate:tags.tags()->tags){if(candidate.id==reference.id&&!candidate.duplicateId){if(row){row=nullptr;break;}row=&candidate;}}
   const auto* record=row?manual->find(row->idKey):nullptr;
   if(!row||!record||!core::lifecycleSavedTagMembershipEffects(*tags.tags(),*record)||!tagFactMembers(*row,*record)){continue;}
   c::NamedTagMetric metric;metric.uid=reference.uid();
   if(request.count&&countCurrent){
    if(!charge(row->uniqueHashes.size())){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Named metric member work bound"});}
    bool complete=row->uniqueHashes.size()<=4096;
    for(const auto& member:row->uniqueHashes){if(!census->containsPrimaryV1(member)){complete=false;break;}}
    if(complete){metric.count=static_cast<std::int32_t>(row->uniqueHashes.size());}
   }
   if(request.age&&projected->membership==core::tags::runtime::Membership::Present){
    if(!charge(row->members.size())){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Named metric time work bound"});}
    const auto* properties=record->find("p");
    for(const auto& member:row->members){if(member.hash&&*member.hash==*target&&properties){
     const auto* time=properties->children[member.sourceIndex].find("a");
     if(time&&time->type==core::Node::Type::Integer){metric.addedMilliseconds=std::bit_cast<std::int64_t>(std::uint64_t(time->integer)*1000U);}
    }}
   }
   if(request.position&&countCurrent&&projected->membership==core::tags::runtime::Membership::Present){
    if(!charge(row->members.size())){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Named rank member work bound"});}
    bool complete=row->members.size()<=4096;std::vector<c::NamedTagMember> members;members.reserve(std::min<std::size_t>(row->members.size(),4096));std::optional<std::uint64_t> subject;
    const auto* properties=record->find("p");
    for(const auto& member:row->members){
     if(!member.hash||!census->containsPrimaryV1(*member.hash)||members.size()==4096){complete=false;break;}
     c::NamedTagMember entry;entry.identity=member.sourceIndex+1;entry.taggableType=2;entry.position=census->queuePosition(*member.hash);
     if(properties){const auto* time=properties->children[member.sourceIndex].find("a");if(time&&time->type==core::Node::Type::Integer){entry.addedMilliseconds=std::bit_cast<std::int64_t>(std::uint64_t(time->integer)*1000U);}}
     if(*member.hash==*target){subject=entry.identity;}members.push_back(std::move(entry));
    }
    if(complete&&subject){auto ranked=c::namedMetricFromMembers(reference.uid(),members,subject,true);
     if(!ranked){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Named rank result bound"});}
     metric.queuePosition=ranked->queuePosition;metric.addedPosition=ranked->addedPosition;
    }
   }
   metrics.push_back(std::move(metric));
  }
  auto owned=c::NamedTagMetrics::create(std::move(metrics));
  if(!owned){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Named metric result bounds"});}
  result.facts_.namedTagMetrics=std::make_shared<const c::NamedTagMetrics>(std::move(*owned));return result;
 }catch(const std::bad_alloc&){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Named metric source resources unavailable"});}
}
