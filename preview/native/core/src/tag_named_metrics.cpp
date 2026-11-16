// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/tag_named_metrics.hpp>
#include <algorithm>
#include <bit>
#include <new>
#include <set>
namespace biglybtc::core::tags::constraints {
namespace {
std::int64_t add(std::int64_t time,std::int32_t position)noexcept{return std::bit_cast<std::int64_t>(std::uint64_t(time)+std::uint64_t(std::int64_t(position)));}
}
std::expected<NamedTagMetric,runtime::Error> namedMetricFromMembers(
 std::int64_t uid,std::span<const NamedTagMember> members,std::optional<std::uint64_t> subject,bool complete,NamedMetricLimits limit){
 try{
  const NamedMetricLimits cap;if(members.size()>std::min(limit.members,cap.members)){return std::unexpected(runtime::Error::Bounds);}
  std::set<std::uint64_t> identities;const NamedTagMember* selected=nullptr;
  for(const auto& member:members){if(!identities.insert(member.identity).second){return std::unexpected(runtime::Error::Invalid);}if(subject&&member.identity==*subject){selected=&member;}}
  NamedTagMetric out;out.uid=uid;if(complete){out.count=static_cast<std::int32_t>(members.size());}
  if(!subject){return out;}
  if(!selected){if(complete){out.queuePosition=-1;out.addedPosition=-1;}return out;}
  out.addedMilliseconds=selected->addedMilliseconds;if(!complete||selected->taggableType!=2){return out;}
  bool queueKnown=bool(selected->position),timeKnown=bool(selected->addedMilliseconds);
  std::int32_t byQueue=0,byTime=0;
  if(timeKnown&&*selected->addedMilliseconds==-1){out.addedPosition=-1;timeKnown=false;}
  else if(timeKnown&&!selected->position){timeKnown=false;}
  for(const auto& member:members){if(member.identity==selected->identity){continue;}
   if(queueKnown){if(member.taggableType!=2||!member.position){queueKnown=false;}else if(*member.position<*selected->position){++byQueue;}}
   if(timeKnown){if(!member.addedMilliseconds){timeKnown=false;}
    else if(*member.addedMilliseconds!=-1){if(member.taggableType!=2||!member.position){timeKnown=false;}else if(add(*member.addedMilliseconds,*member.position)<add(*selected->addedMilliseconds,*selected->position)){++byTime;}}}
  }
  if(queueKnown){out.queuePosition=byQueue;}if(timeKnown){out.addedPosition=byTime;}return out;
 }catch(const std::bad_alloc&){return std::unexpected(runtime::Error::Memory);}
}
std::expected<NamedTagMetrics,runtime::Error> NamedTagMetrics::create(std::vector<NamedTagMetric> values,NamedMetricLimits limit){
 try{
  const NamedMetricLimits cap;if(values.size()>std::min(limit.entries,cap.entries)){return std::unexpected(runtime::Error::Bounds);}
  std::ranges::sort(values,{},&NamedTagMetric::uid);
  for(std::size_t i=0;i<values.size();++i){const auto& value=values[i];if((i&&values[i-1].uid==value.uid)||(value.count&&*value.count<0)||(value.queuePosition&&*value.queuePosition<-1)||(value.addedPosition&&*value.addedPosition<-1)){return std::unexpected(runtime::Error::Invalid);}}
  return NamedTagMetrics(std::move(values));
 }catch(const std::bad_alloc&){return std::unexpected(runtime::Error::Memory);}
}
const NamedTagMetric* NamedTagMetrics::find(std::int64_t uid)const noexcept{
 const auto found=std::ranges::lower_bound(entries_,uid,{},&NamedTagMetric::uid);return found==entries_.end()||found->uid!=uid?nullptr:&*found;
}
}
