// SPDX-License-Identifier: GPL-2.0-or-later
#include "constraint_state_history.hpp"
#include <bit>

namespace biglybtc::app::detail {
void ConstraintStateHistory::observe(const engine::ManagedDownloadStatus& value,Clock::time_point now){
  namespace ds=core::tags::download_state;
  using P=engine::ManagedPhase;
  if(last_&&now<*last_){*this=ConstraintStateHistory{};}
  last_=now;
  if(!activityAt_){activityAt_=now+std::chrono::seconds(10);}
  std::optional<int> state;
  bool stable=true;
  switch(value.phase){
  case P::Downloading:state=50;break;
  case P::Seeding:state=60;break;
  case P::Stopping:state=65;break;
  case P::Stopped:case P::Paused:state=70;break;
  case P::Failed:state=100;break;
  case P::Queued:if(value.checked){state=75;}else{mainKnown_=false;pausedKnown_=false;activityKnown_=false;}break;
  case P::Checking:state=30;stable=false;break;
  case P::Allocating:state=20;stable=false;break;
  }
  if(state&&(stable||mainKnown_)){
    const bool wasKnown=mainKnown_;
    const auto oldMain=state_.main;
    ds::stateChanged(state_,{*state,value.checked?value.selectedComplete:state_.complete.value_or(false),
        value.manuallyPaused||value.phase==P::Paused,false});
    mainKnown_=true;
    if(stable&&*state!=65){pausedKnown_=true;}
    if(state_.main!=ds::Tag::Downloading&&state_.main!=ds::Tag::Seeding){activityKnown_=true;}
    else if(!wasKnown||!oldMain){activityKnown_=false;}
  }
  completeKnown_=value.checked;
  if(value.checked){
    state_.members[10]=value.selectedComplete;state_.members[11]=!value.selectedComplete;
    state_.complete=value.selectedComplete;
  }
  if(now>=*activityAt_){
    activityAt_=now+std::chrono::seconds(10);
    if(!mainKnown_){state_.activityPrefix.reset();activityKnown_=false;return;}
    if(value.received<0||value.sent<0||value.downloadRate<0||value.uploadRate<0){
      state_.activityPrefix.reset();activityKnown_=false;return;
    }
    const bool hadPrefix=state_.activityPrefix.has_value();
    const bool active=ds::updateActive(state_,{value.received,value.sent,value.downloadRate,value.uploadRate,false});
    // Admission-local payload prefixes differ from the original cumulative
    // prefixes by constants. Equality/delta is invariant within that admission.
    // The first zero-rate sample has no prior original observation: do not
    // infer inactivity until a second complete sample or a non-running state.
    activityKnown_=active||hadPrefix||
        (state_.main!=ds::Tag::Downloading&&state_.main!=ds::Tag::Seeding);
  }
}
ConstraintStateHistory::Members ConstraintStateHistory::members()const noexcept{
  using M=core::tags::runtime::Membership;
  Members out;out.fill(M::Unknown);
  const auto known=[&](unsigned index){out[index]=state_.members[index]?M::Present:M::Absent;};
  if(mainKnown_){for(unsigned id=0;id<7;++id){known(id);}}
  if(pausedKnown_){known(8);}
  if(activityKnown_){known(7);known(9);}
  if(completeKnown_){known(10);known(11);}
  // Move/check/delete operation memberships require actual lifecycle events;
  // an engine phase or a payload hash job is not such an event.
  return out;
}
} // namespace biglybtc::app::detail
