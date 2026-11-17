// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/tag_assignment_actions.hpp>
namespace biglybtc::core::tags::assignment {
SavedActions readSaved(const Node& record,std::int32_t type)noexcept{
  SavedActions out;
  // The actual manual type supports every enumerated bit2..32768. The state
  // type supports only script and post-magnet; other types support none.
  if(type==3){out.supported=65534;}
  else if(type==2){out.supported=32|512;}
  const auto* config=record.find("c");
  if(!config||config->type!=Node::Type::Dictionary){return out;}
  constexpr std::array<std::pair<std::string_view,std::uint32_t>,17> keys{{
    {"pp.1",1},{"pp.2",2},{"pp.4",4},{"pp.8",8},{"pp.16",16},{"pp.32",32},
    {"pp.64",64},{"pp.128",128},{"pp.256",256},{"pp.512",512},{"pp.1024",1024},
    {"pp.2048",2048},{"pp.4096",4096},{"pp.8192",8192},{"pp.16384",16384},
    {"pp.32768",32768},{"pp.65536",65536}
  }};
  for(const auto& [key,action]:keys){
    if(!(out.supported&action)){continue;}
    const auto* value=config->find(key);
    if(value&&value->type==Node::Type::Integer&&value->integer==1){out.enabled|=action;}
  }
  return out;
}

std::expected<Plan,Error> plan(std::uint32_t enabled,std::int32_t state,bool metadata,bool destroyed)noexcept{
  Plan out;
  if(metadata||destroyed){return out;}
  if(enabled&~LifecycleMask){return std::unexpected(Error::Unsupported);}
  if(enabled&(Start|Resume)){
    if(state==70||state==100){out.values[out.count++]=Task::StartOrResume;}
  }else if(enabled&(Stop|Pause|Queue)){
    if(state!=70&&state!=65&&state!=100){out.values[out.count++]=Task::StopQueueOrPause;}
  }
  if(enabled&Force){out.values[out.count++]=Task::ForceStart;}
  else if(enabled&NotForce){out.values[out.count++]=Task::CancelForceStart;}
  return out;
}
std::expected<Effects,Error> resolve(Task task,std::uint32_t enabled)noexcept{
  if(enabled&~LifecycleMask){return std::unexpected(Error::Unsupported);}
  Effects out;
  switch(task){
  case Task::StartOrResume:out.values[out.count++]=(enabled&Start)?Effect::Queue:Effect::Resume;break;
  case Task::StopQueueOrPause:
    out.values[out.count++]=(enabled&Stop)?Effect::Stop:(enabled&Queue)?Effect::QueueAfterStop:Effect::Pause;
    out.values[out.count++]=Effect::CheckMaximum;break;
  case Task::ForceStart:out.values[out.count++]=Effect::ForceStart;break;
  case Task::CancelForceStart:out.values[out.count++]=Effect::CancelForceStart;break;
  default:return std::unexpected(Error::Invalid);
  }
  return out;
}
} // namespace biglybtc::core::tags::assignment
