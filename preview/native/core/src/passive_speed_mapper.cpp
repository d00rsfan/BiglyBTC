// SPDX-License-Identifier: GPL-2.0-or-later
// Derived policy: Azureus Software, Inc. SpeedManagerPingMapperImpl (cb8c6cb9).
#include "biglybtc/passive_speed_mapper.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace biglybtc::core {
namespace {
constexpr std::int32_t Divisor=256,Good=50,Bad=150;
float rating(std::int32_t metric)noexcept{if(metric<Good){return 1;}if(metric>=Bad){return -1;}return static_cast<float>(std::clamp(1-(static_cast<double>(metric)-Good)/50,-1.0,1.0));}
std::uint16_t quantize(std::int64_t value)noexcept{return static_cast<std::uint16_t>(std::min<std::int64_t>(value/Divisor,65535));}
std::pair<std::uint16_t,std::uint16_t> endpoints(std::uint16_t a,std::uint16_t b)noexcept{
  // Deliberately preserve the original signed-short comparison before unsigned
  // getters, including reversed unsigned extents across the short sign boundary.
  if(std::bit_cast<std::int16_t>(b)<std::bit_cast<std::int16_t>(a)){std::swap(a,b);}return {a,b};
}
PassiveSpeedZone zone(const PassiveSpeedPoint& a,const PassiveSpeedPoint& b)noexcept{
  const auto [x1,x2]=endpoints(a.uploadBin,b.uploadBin);const auto [y1,y2]=endpoints(a.downloadBin,b.downloadBin);
  return {x1*Divisor,x2*Divisor+Divisor-1,y1*Divisor,y2*Divisor+Divisor-1,static_cast<std::uint16_t>((static_cast<unsigned>(a.metric)+b.metric)/2),x1>x2,y1>y2};
}
std::expected<PassiveSpeedEstimate,PassiveSpeedError> estimate(const std::vector<PassiveSpeedZone>& zones,bool up,std::int64_t when){
  std::int32_t maxEnd=0;for(const auto& value:zones){maxEnd=std::max(maxEnd,(up?value.uploadEnd:value.downloadEnd)/Divisor);}
  const auto sampleEnd=static_cast<std::size_t>(maxEnd)+1;
  if(sampleEnd>PassiveSpeedMapper::MaximumBins){return std::unexpected(PassiveSpeedError::Overflow);}
  std::vector<std::int32_t> totals(sampleEnd,0);std::vector<std::uint16_t> hits(sampleEnd,0),worst(sampleEnd,0);
  for(const auto& value:zones){const auto start=(up?value.uploadStart:value.downloadStart)/Divisor;const auto end=(up?value.uploadEnd:value.downloadEnd)/Divisor;
    const auto metric=value.metric;const std::int32_t first=metric<Good?0:start,last=metric<Good?end:metric<Bad?end:maxEnd;const std::uint16_t type=metric<Good?0:metric<Bad?Good:Bad;
    for(std::int32_t j=first;j<=last;++j){const auto index=static_cast<std::size_t>(j);
      if(type==Bad&&worst[index]<=type){totals[index]=0;hits[index]=0;worst[index]=type;}
      if(totals[index]>std::numeric_limits<std::int32_t>::max()-metric||hits[index]==std::numeric_limits<std::int16_t>::max()){return std::unexpected(PassiveSpeedError::Overflow);}
      totals[index]+=metric;++hits[index];
    }
  }
  for(std::size_t i=0;i<sampleEnd;++i){if(hits[i]){totals[i]/=hits[i];worst[i]=totals[i]<Good?0:totals[i]<Bad?Good:Bad;}}
  PassiveSpeedEstimate result;result.whenMillis=when;
  std::int32_t lastAverage=-1;std::size_t lastChange=0;std::uint16_t lastWorst=0,lastMaxHits=0,worstValue=0;
  for(std::size_t i=0;i<sampleEnd;++i){const auto var=worst[i],hit=hits[i];worstValue=std::max(worstValue,var);const auto average=totals[i];
    if(i==0){lastAverage=average;}
    else if(lastAverage!=average){result.segments.push_back({lastAverage,static_cast<std::int32_t>(lastChange)*Divisor,static_cast<std::int32_t>(i-1)*Divisor,lastWorst,lastMaxHits});lastAverage=average;lastChange=i;lastWorst=var;lastMaxHits=hit;}
    else{lastWorst=std::max(var,lastWorst);lastMaxHits=std::max(hit,lastMaxHits);}
  }
  // Original policy omits a final singleton segment. Preserve this rather than
  // inventing a repaired endpoint or claiming an independently observed limit.
  if(lastChange!=sampleEnd-1){result.segments.push_back({lastAverage,static_cast<std::int32_t>(lastChange)*Divisor,static_cast<std::int32_t>(sampleEnd-1)*Divisor,lastWorst,lastMaxHits});}
  const PassiveSpeedSegment* selected=nullptr;std::uint16_t estimateVar=0;
  if(worstValue==Bad){for(auto it=result.segments.rbegin();it!=result.segments.rend();++it){if(it->varianceType>=worstValue){selected=&*it;estimateVar=it->varianceType;}}}
  else{for(const auto& segment:result.segments){if(segment.varianceType>=worstValue){selected=&segment;estimateVar=segment.varianceType;}}}
  std::int32_t speed=-1;
  if(selected){speed=worstValue==0?selected->end:worstValue==Good?(selected->start+selected->end)/2:selected->start;result.hits=selected->hits;}
  if(speed<5*1024){estimateVar=Good;if(speed<=0){speed=1;}}
  result.bytesPerSecond=speed;result.metricRating=rating(estimateVar);return result;
}
}
std::expected<void,PassiveSpeedError> PassiveSpeedMapper::gap(PassiveSpeedObservation input,PassiveSpeedError error)noexcept{
  lastAttempt_=input;if(input.observedMillis>=0&&(!lastTime_||input.observedMillis>*lastTime_)){lastTime_=input.observedMillis;}
  contiguous_=0;recentCount_=0;recentNext_=0;lastUpload_=0;lastDownload_=0;
  if(gaps_!=std::numeric_limits<std::uint64_t>::max()){++gaps_;}
  state_=error==PassiveSpeedError::Missing?PassiveSpeedState::Gap:error==PassiveSpeedError::Clock?PassiveSpeedState::Clock:error==PassiveSpeedError::Overflow?PassiveSpeedState::Overflow:PassiveSpeedState::Invalid;
  return std::unexpected(error);
}
std::expected<void,PassiveSpeedError> PassiveSpeedMapper::observe(PassiveSpeedObservation input)noexcept{
  if(input.observedMillis<0||(lastTime_&&input.observedMillis<=*lastTime_)){return gap(input,PassiveSpeedError::Clock);}
  if(!input.uploadBytesPerSecond||!input.downloadBytesPerSecond||!input.rttMillis){return gap(input,PassiveSpeedError::Missing);}
  if(*input.uploadBytesPerSecond<0||*input.downloadBytesPerSecond<0||*input.rttMillis<0){return gap(input,PassiveSpeedError::Invalid);}
  if(*input.rttMillis>std::numeric_limits<std::int32_t>::max()||accepted_==std::numeric_limits<std::uint64_t>::max()){return gap(input,PassiveSpeedError::Overflow);}
  const auto rawUpload=quantize(*input.uploadBytesPerSecond),rawDownload=quantize(*input.downloadBytesPerSecond);
  const auto upload=static_cast<std::uint16_t>((static_cast<unsigned>(rawUpload)+lastUpload_)/2),download=static_cast<std::uint16_t>((static_cast<unsigned>(rawDownload)+lastDownload_)/2);
  const auto rtt=static_cast<std::int32_t>(*input.rttMillis>65535?1500:*input.rttMillis==0?1:*input.rttMillis);
  auto recent=recent_;unsigned next=input.rebase?0:recentNext_,count=input.rebase?0:recentCount_;recent[next]=rtt;next=(next+1)%3;count=std::min(count+1,3U);
  std::int32_t metric=0;
  if(count>1){std::int64_t total=0;for(unsigned i=0;i<count;++i){total+=recent[i];}const auto average=total/count;std::int64_t deviations=0;
    for(unsigned i=0;i<count;++i){const auto delta=recent[i]-average;const auto square=delta*delta;if(square>std::numeric_limits<std::int32_t>::max()-deviations){return gap(input,PassiveSpeedError::Overflow);}deviations+=square;}
    metric=static_cast<std::int32_t>(std::sqrt(static_cast<double>(deviations)));
    if(count==3){const auto mean=static_cast<std::int32_t>(total/3);const auto floor=mean>=500?Bad:mean>350?Good+((Bad-Good)*(mean-350))/(500-350):0;metric=std::max(metric,floor);}
  }
  if(count_==MaximumPoints){constexpr auto discard=MaximumPoints/10;if(dropped_>std::numeric_limits<std::uint64_t>::max()-discard){return gap(input,PassiveSpeedError::Overflow);}
    std::move(points_.begin()+discard,points_.end(),points_.begin());count_-=discard;dropped_+=discard;points_[0].connectedToPrevious=false;
  }
  points_[count_++]={input,upload,download,static_cast<std::uint16_t>(metric),rtt,contiguous_!=0};
  lastTime_=input.observedMillis;lastAttempt_=input;lastUpload_=rawUpload;lastDownload_=rawDownload;recent_=recent;recentNext_=next;recentCount_=count;contiguous_=std::min(contiguous_+1,2U);++accepted_;state_=PassiveSpeedState::Ready;return {};
}
std::expected<PassiveSpeedSnapshot,PassiveSpeedError> PassiveSpeedMapper::snapshot()const{try{
  PassiveSpeedSnapshot value;value.state=state_;value.accepted=accepted_;value.dropped=dropped_;value.gaps=gaps_;value.lastAttempt=lastAttempt_;
  value.history.assign(points_.begin(),points_.begin()+count_);if(count_){value.zones.reserve(count_-1);}for(std::size_t i=1;i<count_;++i){if(points_[i].connectedToPrevious){value.zones.push_back(zone(points_[i-1],points_[i]));}}
  if(state_==PassiveSpeedState::Ready&&count_){value.currentMetricRating=rating(points_[count_-1].metric);
    if(contiguous_>=2&&!value.zones.empty()){auto up=estimate(value.zones,true,points_[count_-1].input.observedMillis);if(!up){return std::unexpected(up.error());}auto down=estimate(value.zones,false,points_[count_-1].input.observedMillis);if(!down){return std::unexpected(down.error());}value.upload=std::move(*up);value.download=std::move(*down);}
  }
  return value;
 }catch(const std::bad_alloc&){return std::unexpected(PassiveSpeedError::Memory);}}
} // namespace biglybtc::core
