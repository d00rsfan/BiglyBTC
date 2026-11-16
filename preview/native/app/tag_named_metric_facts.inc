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
c::Result<ConstraintFactSnapshot> captureConstraintNamedTagMetrics(ConstraintFactSnapshot result,
    const ConstraintFactStamp& current,const storage::ManualTagStore& store,const storage::ProfileLease& lease,
    const storage::ManualTagSnapshot& tags,std::span<const c::NamedTagRequest> requests,std::int64_t wall,
    const ConstraintNamedMetricCensus* census){
 try{
  if(!valid(current)||current!=result.stamp()||!result.hasTagScope()||result.tagPlan_.valid()
      ||!tags.valid()||!tags.writable()||!tags.tags()||!tags.tags()->source
      ||!store.matchesSnapshotLineage(tags,lease)||!store.matchesSnapshotLineage(result.tagSnapshot_,lease)
      ||tags.generation()!=result.tagSnapshot_.generation()||!tags.sameSource(result.tagSnapshot_)
      ||(result.internalTagScope_&&!result.internalTagScope_->current(lease,current.binding.session))){
   return std::unexpected(unavailable("Named metrics require current ordinary source lineage"));
  }
  if(requests.size()>4096){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Named metric request bound"});}
  result.facts_.namedTagMetrics.reset();result.facts_.wallMilliseconds=wall;
  if(requests.empty()){return result;}
  const auto target=constraintFactHash(current.binding.target.hash);
  const auto* manual=tags.tags()->source->find("3");
  if(!target||!manual||!result.facts_.tagRegistry){return std::unexpected(unavailable("Named metric metadata is unavailable"));}
  std::vector<c::NamedTagMetric> metrics;std::size_t visits=0;
  const bool countCurrent=census&&census->current(current,store,lease,tags);
  for(const auto& request:requests){
   auto selection=result.facts_.tagRegistry->lookupConstraintName(request.name);
   if(!selection){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Named metric name source bound"});}
   if(!selection->complete||selection->tags.size()!=1||selection->tags.front().type!=3){continue;}
   const auto reference=selection->tags.front();const auto* projected=result.facts_.tagRegistry->at(reference);
   if(!projected||projected->membership==core::tags::runtime::Membership::Unknown){continue;}
   const core::SavedDownloadTag* row=nullptr;
   for(const auto& candidate:tags.tags()->tags){if(candidate.id==reference.id&&!candidate.duplicateId){if(row){row=nullptr;break;}row=&candidate;}}
   const auto* record=row?manual->find(row->idKey):nullptr;
   if(!row||!record||!core::ordinarySavedTagMembershipEffects(*tags.tags(),*record)||!tagFactMembers(*row,*record)){continue;}
   c::NamedTagMetric metric;metric.uid=reference.uid();
   const auto charge=[&](std::size_t count){if(count>32768-visits){return false;}visits+=count;return true;};
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
   // No actual original DM.getPosition/all-member added-time source here.
   // Queue/rank values remain absent; absent subject is separately known.
   metrics.push_back(std::move(metric));
  }
  auto owned=c::NamedTagMetrics::create(std::move(metrics));
  if(!owned){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Named metric result bounds"});}
  result.facts_.namedTagMetrics=std::make_shared<const c::NamedTagMetrics>(std::move(*owned));return result;
 }catch(const std::bad_alloc&){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"Named metric source resources unavailable"});}
}
