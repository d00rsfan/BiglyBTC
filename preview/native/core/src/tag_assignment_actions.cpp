// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/tag_assignment_actions.hpp>
namespace biglybtc::core::tags::assignment {
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
