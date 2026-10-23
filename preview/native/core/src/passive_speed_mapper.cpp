// SPDX-License-Identifier: GPL-2.0-or-later
// Derived policy: Azureus Software, Inc. SpeedManagerPingMapperImpl (cb8c6cb9).
#include "biglybtc/passive_speed_mapper.hpp"
#include "biglybtc/saved_speed_manager.hpp"
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
  contiguous_=0;recentCount_=0;recentNext_=0;lastUpload_=0;lastDownload_=0;resetWarmup();lastEventWasSpeedOnly_=false;
  if(gaps_!=std::numeric_limits<std::uint64_t>::max()){++gaps_;}
  state_=error==PassiveSpeedError::Missing?PassiveSpeedState::Gap:error==PassiveSpeedError::Clock?PassiveSpeedState::Clock:error==PassiveSpeedError::Overflow?PassiveSpeedState::Overflow:error==PassiveSpeedError::Memory?PassiveSpeedState::Memory:PassiveSpeedState::Invalid;
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
  lastTime_=input.observedMillis;lastAttempt_=input;lastUpload_=rawUpload;lastDownload_=rawDownload;recent_=recent;recentNext_=next;recentCount_=count;contiguous_=std::min(contiguous_+1,2U);++accepted_;state_=PassiveSpeedState::Ready;lastEventWasSpeedOnly_=false;
  try{auto result=updateAfterPing();if(!result){return gap(input,result.error());}return {};}
  catch(const std::bad_alloc&){return gap(input,PassiveSpeedError::Memory);}
}

void PassiveSpeedMapper::resetWarmup()noexcept{
  uploadSpeeds_.fill(0);downloadSpeeds_.fill(0);speedNext_=0;speedCount_=0;
  rawUpload_.reset();rawDownload_.reset();
  // A discontinuity cannot stand in for the five actual good observations.
  if(up_.lastBad){up_.countdown=5;}if(down_.lastBad){down_.countdown=5;}
  if(up_.capacity){up_.capacity->current=false;}if(down_.capacity){down_.capacity->current=false;}
}
void PassiveSpeedMapper::sustain(Axis& axis,std::uint16_t minimum,std::int64_t when,bool qualified)noexcept{
  const auto speed=static_cast<std::int64_t>(minimum)*Divisor;
  if(axis.capacity&&axis.capacity->value.estimateType==1){return;}
  const auto previous=axis.capacity?axis.capacity->value.bytesPerSecond:0;
  if(speed>previous){
    if(!axis.capacity){axis.capacity=PassiveSpeedCapacity{};}
    auto& capacity=*axis.capacity;capacity.value.bytesPerSecond=speed;
    capacity.value.metricRating=0;capacity.value.estimateType=0;capacity.value.fromSaved=false;
    capacity.origin=PassiveCapacityOrigin::Sustained;capacity.lastDerivedMillis=when;capacity.current=true;
  }else if(qualified&&axis.capacity&&axis.capacity->origin!=PassiveCapacityOrigin::Saved&&speed>=previous){
    axis.capacity->current=true;axis.capacity->lastDerivedMillis=when;
  }
}
std::expected<void,PassiveSpeedError> PassiveSpeedMapper::decrease(Axis& axis,std::int64_t when){
  if((axis.capacity&&axis.capacity->value.estimateType==1)||axis.badCount<MaximumBadHistory){return {};}
  const auto cap=axis.capacity?axis.capacity->value.bytesPerSecond:0;
  if(cap>0&&cap<10*1024){return {};}
  auto sorted=axis.bad;std::sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.bytesPerSecond<b.bytesPerSecond;});
  constexpr std::size_t Trim=MaximumBadHistory/4,End=MaximumBadHistory-Trim,Count=End-Trim;
  std::int64_t total=0;
  for(std::size_t i=Trim;i<End;++i){const auto speed=sorted[i].bytesPerSecond;
    if(speed<0||speed>std::numeric_limits<std::int32_t>::max()-total){return std::unexpected(PassiveSpeedError::Overflow);}total+=speed;
  }
  const auto average=total/static_cast<std::int64_t>(Count);
  // Preserve this early exit before evaluating the source int variance.
  if(cap>0&&average>=cap){return {};}
  std::int64_t squares=0;
  for(std::size_t i=Trim;i<End;++i){const auto delta=sorted[i].bytesPerSecond-average;
    // Every admitted speed/average fits a nonnegative Java int, so square fits
    // int64; Java's compound int narrowing is deliberately NOT wrapped here.
    const auto square=delta*delta;
    if(square>std::numeric_limits<std::int32_t>::max()-squares){return std::unexpected(PassiveSpeedError::Overflow);}squares+=square;
  }
  const auto deviation=static_cast<std::int64_t>(std::sqrt(static_cast<double>(squares)/Count));
  if(cap<=0||(deviation<cap/2&&average<cap)){
    if(!axis.capacity){axis.capacity=PassiveSpeedCapacity{};}
    auto& capacity=*axis.capacity;capacity.value.bytesPerSecond=average;capacity.value.estimateType=0.5f;
    // Source setters retain the previous metric/hits/when metadata.
    capacity.value.fromSaved=false;capacity.origin=PassiveCapacityOrigin::Choke;
    capacity.lastDerivedMillis=when;capacity.current=true;
    std::move(axis.bad.begin()+Trim,axis.bad.begin()+axis.badCount,axis.bad.begin());axis.badCount-=Trim;
  }
  return {};
}
std::expected<void,PassiveSpeedError> PassiveSpeedMapper::updateAxis(Axis& axis,const std::optional<PassiveSpeedEstimate>& raw,float current,std::int64_t when){
  if(!raw){return {};}
  const PassiveSpeedLimitRecord record{raw->bytesPerSecond,raw->metricRating,raw->estimateType,raw->hits,raw->whenMillis,false};
  if(record.metricRating==-1){
    if(axis.countdown==0&&(!axis.lastBad||axis.lastBad->bytesPerSecond!=record.bytesPerSecond)){
      axis.countdown=5;
      if(axis.badCount==MaximumBadHistory){std::move(axis.bad.begin()+1,axis.bad.end(),axis.bad.begin());--axis.badCount;}
      axis.bad[axis.badCount++]=record;
      if(const auto result=decrease(axis,when);!result){return result;}
    }
    axis.lastBad=record;
  }else if(record.metricRating==1&&(!axis.bestGood||axis.bestGood->bytesPerSecond<record.bytesPerSecond)){
    axis.bestGood=record;
  }
  if(axis.countdown){if(current==-1){axis.countdown=5;}else if(current==1){--axis.countdown;}}
  return {};
}
std::expected<void,PassiveSpeedError> PassiveSpeedMapper::updateAfterPing(){
  std::optional<PassiveSpeedEstimate> upload,download;
  const auto& point=points_[count_-1];
  if(contiguous_>=2){
    std::vector<PassiveSpeedZone> zones;zones.reserve(count_-1);
    for(std::size_t i=1;i<count_;++i){if(points_[i].connectedToPrevious){zones.push_back(zone(points_[i-1],points_[i]));}}
    if(!zones.empty()){
      auto result=estimate(zones,true,point.input.observedMillis);if(!result){return std::unexpected(result.error());}upload=std::move(*result);
      result=estimate(zones,false,point.input.observedMillis);if(!result){return std::unexpected(result.error());}download=std::move(*result);
    }
  }
  // Stage both axes/ring before commit: one failed arithmetic policy cannot
  // publish a half-updated capacity. The actual point above remains recorded.
  auto up=up_,down=down_;auto uploads=uploadSpeeds_,downloads=downloadSpeeds_;
  uploads[speedNext_]=point.metric<Bad?point.uploadBin:0;downloads[speedNext_]=point.metric<Bad?point.downloadBin:0;
  const auto qualified=std::min(speedCount_+1,static_cast<unsigned>(SustainedSamples));
  sustain(up,*std::min_element(uploads.begin(),uploads.end()),point.input.observedMillis,qualified==SustainedSamples);
  sustain(down,*std::min_element(downloads.begin(),downloads.end()),point.input.observedMillis,qualified==SustainedSamples);
  auto result=updateAxis(up,upload,rating(point.metric),point.input.observedMillis);if(!result){return result;}
  result=updateAxis(down,download,rating(point.metric),point.input.observedMillis);if(!result){return result;}
  up_=up;down_=down;uploadSpeeds_=uploads;downloadSpeeds_=downloads;speedCount_=qualified;speedNext_=(speedNext_+1)%SustainedSamples;
  rawUpload_=std::move(upload);rawDownload_=std::move(download);return {};
}
std::expected<void,PassiveSpeedError> PassiveSpeedMapper::observeSpeed(PassiveSpeedRateObservation value)noexcept{
  const PassiveSpeedObservation input{value.observedMillis,value.uploadBytesPerSecond,value.downloadBytesPerSecond,{},false};
  if(value.observedMillis<0||(lastTime_&&value.observedMillis<=*lastTime_)){return gap(input,PassiveSpeedError::Clock);}
  if(!value.uploadBytesPerSecond||!value.downloadBytesPerSecond){return gap(input,PassiveSpeedError::Missing);}
  if(*value.uploadBytesPerSecond<0||*value.downloadBytesPerSecond<0){return gap(input,PassiveSpeedError::Invalid);}
  if(speedOnlyAccepted_==std::numeric_limits<std::uint64_t>::max()){return gap(input,PassiveSpeedError::Overflow);}
  uploadSpeeds_[speedNext_]=quantize(*value.uploadBytesPerSecond);downloadSpeeds_[speedNext_]=quantize(*value.downloadBytesPerSecond);
  speedNext_=(speedNext_+1)%SustainedSamples;speedCount_=std::min(speedCount_+1,static_cast<unsigned>(SustainedSamples));
  sustain(up_,*std::min_element(uploadSpeeds_.begin(),uploadSpeeds_.end()),value.observedMillis,speedCount_==SustainedSamples);
  sustain(down_,*std::min_element(downloadSpeeds_.begin(),downloadSpeeds_.end()),value.observedMillis,speedCount_==SustainedSamples);
  ++speedOnlyAccepted_;lastAttempt_=input;lastTime_=value.observedMillis;lastEventWasSpeedOnly_=true;state_=PassiveSpeedState::Ready;return {};
}
std::expected<void,PassiveSpeedError> PassiveSpeedMapper::restoreSaved(std::string_view bytes){try{
  if(restored_||accepted_||speedOnlyAccepted_||lastAttempt_){return std::unexpected(PassiveSpeedError::Invalid);}
  auto source=readSavedSpeedHistory(bytes);if(!source){return std::unexpected(source.error()==SavedSpeedError::Memory?PassiveSpeedError::Memory:PassiveSpeedError::Invalid);}
  const auto convert=[](const SavedSpeedEstimate& estimate)->std::expected<PassiveSpeedLimitRecord,PassiveSpeedError>{
    if(!estimate.present||estimate.malformed||!estimate.bytesPerSecond||*estimate.bytesPerSecond<0||!estimate.hits||!estimate.whenMillis||!estimate.metric||!estimate.type||!std::isfinite(*estimate.metric)||!std::isfinite(*estimate.type)){return std::unexpected(PassiveSpeedError::Invalid);}
    return PassiveSpeedLimitRecord{*estimate.bytesPerSecond,*estimate.metric,*estimate.type,*estimate.hits,*estimate.whenMillis,true};
  };
  const auto record=[&](const Node& node)->std::expected<PassiveSpeedLimitRecord,PassiveSpeedError>{
    const auto& raw=source->source->originalBytes();
    if(node.type!=Node::Type::Dictionary||node.begin>node.end||node.end>raw.size()){return std::unexpected(PassiveSpeedError::Invalid);}
    std::string selected="d5:upcap";selected.append(raw,node.begin,node.end-node.begin);selected+='e';
    auto parsed=readSavedSpeedHistory(selected);if(!parsed){return std::unexpected(parsed.error()==SavedSpeedError::Memory?PassiveSpeedError::Memory:PassiveSpeedError::Invalid);}return convert(parsed->upload);
  };
  const auto load=[&](Axis& axis,const SavedSpeedEstimate& capacity,std::string_view badKey,std::string_view bestKey)->std::expected<void,PassiveSpeedError>{
    if(capacity.present){auto value=convert(capacity);if(!value){return std::unexpected(value.error());}axis.capacity=PassiveSpeedCapacity{*value,PassiveCapacityOrigin::Saved,{},false};}
    if(const auto* bads=source->source->find(badKey)){
      if(bads->type!=Node::Type::List||bads->children.size()>MaximumBadHistory){return std::unexpected(PassiveSpeedError::Invalid);}
      for(const auto& node:bads->children){auto value=record(node);if(!value){return std::unexpected(value.error());}axis.bad[axis.badCount++]=*value;axis.lastBad=*value;}
    }
    if(const auto* good=source->source->find(bestKey)){auto value=record(*good);if(!value){return std::unexpected(value.error());}axis.bestGood=*value;}
    return {};
  };
  Axis up,down;auto result=load(up,source->upload,"lbus","bgu");if(!result){return result;}
  result=load(down,source->download,"lbds","bgd");if(!result){return result;}
  up_=up;down_=down;savedSource_=source->source;restored_=true;return {};
 }catch(const std::bad_alloc&){return std::unexpected(PassiveSpeedError::Memory);}}
std::expected<PassiveSpeedSnapshot,PassiveSpeedError> PassiveSpeedMapper::snapshot()const{try{
  PassiveSpeedSnapshot value;value.state=state_;value.accepted=accepted_;value.dropped=dropped_;value.gaps=gaps_;value.lastAttempt=lastAttempt_;
  value.history.assign(points_.begin(),points_.begin()+count_);if(count_){value.zones.reserve(count_-1);}for(std::size_t i=1;i<count_;++i){if(points_[i].connectedToPrevious){value.zones.push_back(zone(points_[i-1],points_[i]));}}
  if(state_==PassiveSpeedState::Ready){
    if(count_&&contiguous_){value.currentMetricRating=rating(points_[count_-1].metric);}value.upload=rawUpload_;value.download=rawDownload_;
  }
  const auto persistent=[](const std::optional<PassiveSpeedEstimate>& raw,const Axis& axis,bool& fromSaved)->std::optional<PassiveSpeedEstimate>{
    if(!raw||raw->metricRating==-1){return raw;}
    const PassiveSpeedLimitRecord* chosen=nullptr;
    if(axis.bestGood&&axis.lastBad){chosen=axis.lastBad->whenMillis>axis.bestGood->whenMillis?&*axis.lastBad:axis.bestGood->bytesPerSecond>axis.lastBad->bytesPerSecond?&*axis.bestGood:&*axis.lastBad;}
    else if(axis.bestGood){chosen=&*axis.bestGood;}else if(axis.lastBad){chosen=&*axis.lastBad;}
    if(!chosen||raw->bytesPerSecond>chosen->bytesPerSecond){return raw;}
    auto result=*raw;result.bytesPerSecond=static_cast<std::int32_t>(chosen->bytesPerSecond);fromSaved=chosen->fromSaved;return result;
  };
  value.persistentUpload=persistent(value.upload,up_,value.persistentUploadFromSaved);value.persistentDownload=persistent(value.download,down_,value.persistentDownloadFromSaved);
  value.uploadCapacity=up_.capacity;value.downloadCapacity=down_.capacity;
  value.badUpload.assign(up_.bad.begin(),up_.bad.begin()+up_.badCount);value.badDownload.assign(down_.bad.begin(),down_.bad.begin()+down_.badCount);
  value.lastBadUpload=up_.lastBad;value.lastBadDownload=down_.lastBad;value.bestGoodUpload=up_.bestGood;value.bestGoodDownload=down_.bestGood;
  value.badUploadCountdown=up_.countdown;value.badDownloadCountdown=down_.countdown;value.sustainedSamples=speedCount_;
  value.speedOnlyAccepted=speedOnlyAccepted_;value.lastEventWasSpeedOnly=lastEventWasSpeedOnly_;value.savedSource=savedSource_;
  return value;
 }catch(const std::bad_alloc&){return std::unexpected(PassiveSpeedError::Memory);}}
} // namespace biglybtc::core
