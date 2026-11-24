// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/download_state_tags.hpp>
#include <biglybtc/bencode.hpp>
#include <bit>
#include <limits>

namespace biglybtc::core::tags::download_state {
namespace {
void set(State& value,Tag tag,bool member=true)noexcept{
  value.members[static_cast<unsigned>(tag)]=member;
}
std::int64_t add(std::int64_t a,std::int64_t b)noexcept{
  return std::bit_cast<std::int64_t>(static_cast<std::uint64_t>(a)+static_cast<std::uint64_t>(b));
}
}
std::optional<std::int64_t> lastActiveStamp(const Document& document) noexcept {
  if (document.root().type != Node::Type::Dictionary) { return {}; }
  const auto* attrs = document.find("attributes");
  if (!attrs) { return 0; }
  if (attrs->type != Node::Type::Dictionary) { return {}; }
  const auto* stamp = attrs->find(LastActiveAttribute);
  if (!stamp) { return 0; }
  if (stamp->type != Node::Type::Integer) { return {}; }
  return stamp->integer;
}
std::int64_t lastActiveAge(std::int64_t wallMilliseconds, std::int64_t stamp) noexcept {
  if (stamp <= 0) { return std::numeric_limits<std::int64_t>::max(); }
  const auto difference = static_cast<std::uint64_t>(wallMilliseconds)
      - static_cast<std::uint64_t>(stamp);
  return std::bit_cast<std::int64_t>(difference) / 1000;
}
void remove(State& value)noexcept{
  if(value.main){set(value,*value.main,false);}
  for(unsigned id=7;id<15;++id){value.members[id]=false;}
  // Original user-data keys are not cleared by removal.
}
void stateChanged(State& value,Change change)noexcept{
  if(change.destroyed){remove(value);return;}
  const auto old=value.main;
  auto next=old;
  switch(change.state){
  case 0:case 5:case 10:case 20:case 30:case 40:
    if(!next){next=Tag::Initializing;}break;
  case 55:break;
  case 50:next=Tag::Downloading;break;
  case 60:next=Tag::Seeding;break;
  case 65:case 70:case 71:next=Tag::Stopped;break;
  case 75:next=change.complete?Tag::QueuedSeeding:Tag::QueuedDownloading;break;
  default:next=Tag::Error;break;
  }
  if(old!=next){
    if(old){set(value,*old,false);}
    set(value,*next);value.main=next;
    if(next!=Tag::Seeding&&next!=Tag::Downloading){
      set(value,Tag::Active,false);set(value,Tag::Inactive);
    }else if(!value.contains(Tag::Active)&&!value.contains(Tag::Inactive)){
      set(value,Tag::Inactive);
    }
    if(next==Tag::Stopped&&change.paused){set(value,Tag::Paused);}
    else if(old==Tag::Stopped){set(value,Tag::Paused,false);}
  }else if(change.state==70){set(value,Tag::Paused,change.paused);}
  if(!value.complete||*value.complete!=change.complete){
    const auto present=change.complete?Tag::Complete:Tag::Incomplete;
    if(!value.contains(present)){
      set(value,present);set(value,change.complete?Tag::Incomplete:Tag::Complete,false);
    }
    value.complete=change.complete;
  }
}
bool updateActive(State& value,Activity activity)noexcept{
  bool active=false;
  if(value.contains(Tag::Downloading)||value.contains(Tag::Seeding)){
    const auto total=add(activity.received,activity.sent);
    const auto old=value.activityPrefix;
    if(!old||*old!=total){value.activityPrefix=total;}
    active=(add(activity.receiveRate,activity.sendRate)>0||(old&&*old!=total))&&!activity.destroyed;
    if(active){set(value,Tag::Active);set(value,Tag::Inactive,false);}
  }
  if(!active&&value.contains(Tag::Active)){
    set(value,Tag::Active,false);
    if(!activity.destroyed){set(value,Tag::Inactive);}
  }
  return active;
}
void operation(State& value,int type,bool added,bool hasDownload)noexcept{
  if(!hasDownload){return;}
  // Original callbacks intentionally do not count overlapping operations.
  if(type==6){set(value,Tag::Checking,added);}
  else if(type==2){set(value,Tag::Moving,added);}
}
void removeInitiated(State& value)noexcept{set(value,Tag::Deleting);}
} // namespace biglybtc::core::tags::download_state
