// SPDX-License-Identifier: GPL-2.0-or-later
// Private genuine-new-only graph preparation inside the existing worker. All
// effects stay virtual until the normal inventory/state/event fences and CAS.
  struct PreparedNewOnly {
    storage::ManualTagMembershipPlan plan;
    std::vector<std::int32_t> additions;
  };
  c::Result<PreparedNewOnly> prepareNewOnlyDependencies(storage::ManualTagStore& store,
      const storage::ManualTagSnapshot& snapshot,const ConstraintWorkerFrame& frame,
      const ConstraintFactSnapshot& facts,const ConstraintMembershipCohort& base,
      std::int32_t rootId,const core::TorrentHash& hash,std::chrono::steady_clock::time_point end,
      unsigned& used,std::stop_token stop){
    namespace d=core::tags::dependency;
    if(used>=32||!base.valid()||!snapshot.tags()->issues.empty()){
      return std::unexpected(unavailable("New-only dependency source/budget is incomplete"));}
    // Only manual properties participate in this handler's constrained-tag map.
    // Their complete name census is required; an unresolved name may supersede
    // a later apparent first match and cannot be silently skipped.
    for(const auto& row:snapshot.tags()->tags){
      if(!row.id||row.duplicateId||row.idKey!=std::to_string(*row.id)||!row.rawName
          ||QString::fromUtf8(row.rawName->data(),qsizetype(row.rawName->size())).toUtf8().toStdString()!=*row.rawName
          ||(row.nameMayRequireLocalization&&(!stockTagLocale||!core::tags::stockTagName(*stockTagLocale,QString::fromUtf8(row.rawName->data(),qsizetype(row.rawName->size())).toStdU16String())))){
        return std::unexpected(unavailable("New-only dependency manual-name census is unresolved"));}
    }
    auto prepared=store.planMembership(snapshot);if(!prepared){return std::unexpected(unavailable("New-only membership base is unavailable"));}
    PreparedNewOnly result;result.plan=std::move(*prepared);result.additions.reserve(32);
    // This selector is created only for the actual unexpired durable-new event.
    // It changes metadata visibility, never Store/effect authority, and is never
    // installed on the ordinary current-membership path.
    auto scope=std::make_shared<ConstraintMembershipCohort::Data>(*base.data_);scope->roots=scope->known;
    ConstraintMembershipCohort cohort;cohort.data_=std::move(scope);
    const auto wall=[](){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();};
    auto initial=captureConstraintTagMembership(facts,facts.stamp(),store,*lease,snapshot,rootId,frame.library,wall(),&result.plan,&cohort);
    if(!initial||!initial->facts().tagRegistry){return std::unexpected(initial?unavailable("New-only registry is unavailable"):initial.error());}
    const auto& registry=*initial->facts().tagRegistry;
    const auto monotone=std::chrono::steady_clock::now();std::erase_if(recentAdds,[&](const auto& value){return monotone-value.at>=std::chrono::seconds(1);});
    struct Selected {Rule* rule;std::optional<c::Expression> expression;};
    std::vector<Selected> selected;std::vector<d::Node> nodes;std::map<std::int32_t,std::size_t> indices;
    const auto insert=[&](std::int32_t id)->c::Result<std::size_t>{
      if(const auto found=indices.find(id);found!=indices.end()){return found->second;}
      if(nodes.size()==128){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"New-only dependency node bound reached"});}
      auto found=std::find_if(rules.begin(),rules.end(),[&](const auto& rule){return rule.id==id;});
      const auto* definition=tag(*snapshot.tags(),id);const auto* type=snapshot.tags()->source->find("3");const auto* record=definition&&type?type->find(definition->idKey):nullptr;
      if(found==rules.end()||!found->property.configured||found->problem||!record
          ||!compatibleConfiguration(*record,core::ordinarySavedTagGroup(*snapshot.tags(),*record))
          ||!core::lifecycleSavedTagMembershipEffects(*snapshot.tags(),*record)||!cohort.knows(id)){
        return std::unexpected(unavailable("New-only dependency requires a supported current constrained source"));}
      std::optional<c::Expression> expression;
      if(found->property.enabled){if(found->expression){expression=*found->expression;}else{auto value=c::compile(found->property.expression);if(!value){return std::unexpected(value.error());}expression=std::move(*value);}}
      const auto index=nodes.size();const bool member=std::ranges::find(definition->uniqueHashes,hash)!=definition->uniqueHashes.end();
      const bool recent=std::ranges::any_of(recentAdds,[&](const auto& value){return value.id==id&&value.hash==hash;});
      indices.emplace(id,index);selected.push_back({&*found,std::move(expression)});nodes.push_back({found->property.mode,found->property.enabled,member,!recent,{}});return index;
    };
    auto root=insert(rootId);if(!root){return std::unexpected(root.error());}
    std::size_t edges=0;
    for(std::size_t at=0;at<selected.size();++at){
      if(stop.stop_requested()||std::chrono::steady_clock::now()>=end){return std::unexpected(unavailable("New-only dependency preparation was cancelled or expired"));}
      // Copy the immutable handle: inserting another node may grow selected.
      const auto expression=selected[at].expression;if(!expression){continue;}
      for(const auto& name:expression->requiredTagNames()){auto resolved=registry.lookupName(name);if(!resolved||resolved->tags.empty()){return std::unexpected(unavailable("New-only eager tag reference is unresolved"));}}
      for(const auto& name:expression->tagDependencies()){
        auto resolved=registry.lookupName(name);if(!resolved){return std::unexpected(unavailable("New-only dependency name lookup unavailable"));}
        for(const auto& ref:resolved->tags){if(ref.type!=3){continue;}
          const auto rule=std::find_if(rules.begin(),rules.end(),[&](const auto& value){return value.id==ref.id&&value.property.configured;});if(rule==rules.end()){continue;}
          auto dependency=insert(ref.id);if(!dependency){return std::unexpected(dependency.error());}
          auto& dependencies=nodes[at].dependencies;if(std::ranges::find(dependencies,*dependency)!=dependencies.end()){continue;}
          if(edges==256){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"New-only dependency edge bound reached"});}dependencies.push_back(*dependency);++edges;
        }
      }
    }
    d::Limits limits;limits.nodes=128;limits.edges=256;limits.depth=128;limits.steps=4096;
    auto protocol=d::Apply::create(std::move(nodes),*root,true,true,{},limits);if(!protocol){return std::unexpected(unavailable("New-only dependency control model refused its graph"));}
    for(unsigned step=0;step<4096;++step){
      if(stop.stop_requested()||std::chrono::steady_clock::now()>=end){return std::unexpected(unavailable("New-only dependency turn was cancelled or expired"));}
      auto request=protocol->next();if(!request){return std::unexpected(unavailable("New-only dependency protocol exceeded its bound"));}
      if(request->action==d::Action::Finished){return result;}
      const auto& current=selected[request->node];bool accepted=false;
      if(request->action==d::Action::Evaluate){
        if(used==32||!current.expression){return std::unexpected(unavailable("New-only dependency evaluation budget is exhausted"));}++used;
        auto captured=captureConstraintTagMembership(facts,facts.stamp(),store,*lease,snapshot,current.rule->id,frame.library,wall(),&result.plan,&cohort);
        if(!captured){return std::unexpected(captured.error());}
        auto evaluated=evaluateCurrentConstraint(*current.expression,facts.stamp(),*captured,store,*lease,snapshot);if(!evaluated){return std::unexpected(evaluated.error());}
        auto truth=c::truth(*evaluated);if(!truth){return std::unexpected(truth.error());}accepted=*truth;
      }else if(request->action==d::Action::Add){
        // A genuine new-only root shares its original (absent) root snapshot.
        // Its dependency walk can only add, never remove existing membership.
        if(recentAdds.size()+result.additions.size()>=32){return std::unexpected(c::Problem{c::ProblemKind::Bounds,"New-only addition batch/history bound reached; complete plan retained"});}
        {
          auto next=store.extendMembershipPlan(result.plan,core::SavedTagAddMember{current.rule->id,hash,wall()/1000},stop);
          if(!next){return std::unexpected(unavailable("New-only staged membership/effects are unavailable"));}
          if(!next->sameView(result.plan)){result.additions.push_back(current.rule->id);}result.plan=std::move(*next);accepted=true;
        }
      }else{return std::unexpected(unavailable("New-only dependency proposed an unexpected removal"));}
      if(!protocol->accept(*request,accepted)){return std::unexpected(unavailable("New-only dependency response is stale"));}
    }
    return std::unexpected(c::Problem{c::ProblemKind::Bounds,"New-only dependency step bound reached"});
  }
