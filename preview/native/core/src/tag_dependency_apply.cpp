// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/tag_dependency_apply.hpp>
#include <algorithm>
#include <set>
namespace biglybtc::core::tags::dependency {
std::expected<Apply,Error> Apply::create(std::vector<Node> nodes,std::size_t root,
    bool check,bool fresh,std::vector<std::size_t> checked,Limits limits){
 try{
  const Limits maximum;limits.nodes=std::min(limits.nodes,maximum.nodes);limits.edges=std::min(limits.edges,maximum.edges);
  limits.depth=std::min(limits.depth,maximum.depth);limits.steps=std::min(limits.steps,maximum.steps);
  if(!limits.nodes||!limits.edges||!limits.depth||!limits.steps||nodes.size()>limits.nodes){return std::unexpected(Error::Bounds);}
  if(nodes.empty()||root>=nodes.size()){return std::unexpected(Error::Invalid);}
  std::size_t edges=0;
  for(const auto& node:nodes){
   if(node.dependencies.size()>limits.edges-edges){return std::unexpected(Error::Bounds);}edges+=node.dependencies.size();
   std::set<std::size_t> unique;
   for(const auto target:node.dependencies){if(target>=nodes.size()||!unique.insert(target).second){return std::unexpected(Error::Invalid);}}
  }
  Apply out;out.nodes_=std::move(nodes);out.checked_.resize(out.nodes_.size());out.limits_=limits;
  for(const auto node:checked){if(node>=out.nodes_.size()||out.checked_[node]){return std::unexpected(Error::Invalid);}out.checked_[node]=true;}
  out.existing_=out.nodes_[root].member;out.isNew_=fresh;out.stack_.push_back({root,0,check});return out;
 }catch(...){return std::unexpected(Error::Memory);}
}
std::expected<Request,Error> Apply::next(){
 try{
  if(failure_){return std::unexpected(*failure_);}if(pending_){return *pending_;}
  const auto request=[&](Action action,std::size_t node){Request value{action,node,++sequence_};pending_=value;return value;};
  while(!stack_.empty()){
   if(++steps_>limits_.steps){failure_=Error::Bounds;return std::unexpected(*failure_);}
   auto& frame=stack_.back();auto& node=nodes_[frame.node];
   switch(frame.phase){
   case Phase::Enter:
    if((frame.check&&checked_[frame.node])||(node.mode.newOnly&&!isNew_)){stack_.pop_back();break;}
    frame.phase=Phase::Evaluation;break;
   case Phase::Evaluation:
    if(node.enabled){return request(Action::Evaluate,frame.node);}
    frame.truth=false;frame.phase=Phase::Branch;break;
   case Phase::Branch:
    if(frame.truth){
     const bool add=node.mode.newOnly?isNew_:node.mode.add;
     if(add&&!existing_){
      if(frame.check&&!node.dependencies.empty()){frame.phase=Phase::Dependencies;break;}
      if(!stopping_&&!node.member&&node.mayAdd){frame.phase=Phase::Effect;return request(Action::Add,frame.node);}
     }
    }else if(node.mode.remove&&existing_&&!stopping_&&node.member){frame.phase=Phase::Effect;return request(Action::Remove,frame.node);}
    frame.phase=Phase::Done;break;
   case Phase::Dependencies:
    if(frame.cursor){checked_[frame.node]=false;}
    if(frame.cursor<node.dependencies.size()){
     if(stack_.size()==limits_.depth){failure_=Error::Bounds;return std::unexpected(*failure_);}
     const auto dependency=node.dependencies[frame.cursor++];checked_[frame.node]=true;
     stack_.push_back({dependency,0,true});
    }else{frame.phase=Phase::Recheck;}
    break;
   case Phase::Recheck:
    // Tail call applySupport2(...,false,checked,is_new), not a fresh root.
    frame.check=false;frame.cursor=0;frame.phase=Phase::Enter;break;
   case Phase::Effect:return std::unexpected(Error::Invalid); // Needs exact pending reply.
   case Phase::Done:stack_.pop_back();break;
   }
  }
  return Request{};
 }catch(...){failure_=Error::Memory;return std::unexpected(*failure_);}
}
std::expected<void,Error> Apply::accept(const Request& reply,bool result){
 if(failure_){return std::unexpected(*failure_);}
 if(!pending_||*pending_!=reply||stack_.empty()||stack_.back().node!=reply.node){return std::unexpected(Error::Invalid);}
 auto& frame=stack_.back();auto& node=nodes_[reply.node];
 if(reply.action==Action::Evaluate&&frame.phase==Phase::Evaluation){frame.truth=result;frame.phase=Phase::Branch;}
 else if((reply.action==Action::Add||reply.action==Action::Remove)&&frame.phase==Phase::Effect){
  if(result){node.member=reply.action==Action::Add;}frame.phase=Phase::Done;
 }else{return std::unexpected(Error::Invalid);}
 pending_.reset();return {};
}
}
