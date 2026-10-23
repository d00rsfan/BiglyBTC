#pragma once
#include "biglybtc/passive_speed_mapper.hpp"
#include <QMetaType>
#include <QString>
#include <cmath>
#include <memory>

namespace biglybtc::ui {
struct LiveSpeedManagerPacket {
  quint64 session=0,epoch=0,sequence=0;
  std::shared_ptr<const core::PassiveSpeedSnapshot> snapshot;
  bool current=false;
  QString failure;
};
using LiveSpeedManagerPresentation=std::shared_ptr<const LiveSpeedManagerPacket>;
// Shape/identity admission only, not source authentication. All observedMillis
// and estimate.whenMillis values are STEADY milliseconds, never epoch dates.
inline bool acceptLiveSpeedManager(const LiveSpeedManagerPresentation& previous,
    const LiveSpeedManagerPresentation& incoming,quint64 session)noexcept{
  if(!incoming||!session||incoming->session!=session||!incoming->sequence||incoming->failure.size()>8192
      ||(!incoming->epoch&&(incoming->snapshot||incoming->current))
      ||(incoming->epoch&&!incoming->snapshot)){return false;}
  if(previous&&previous->session==session&&(incoming->sequence<=previous->sequence||incoming->epoch<previous->epoch)){return false;}
  if(!incoming->snapshot){return true;}
  const auto& value=*incoming->snapshot;
  if(value.history.size()>core::PassiveSpeedMapper::MaximumPoints||value.zones.size()>value.history.size()
      ||value.accepted<value.dropped||value.accepted-value.dropped!=value.history.size()
      ||(value.currentMetricRating&&(!std::isfinite(*value.currentMetricRating)||*value.currentMetricRating < -1||*value.currentMetricRating>1))
      ||bool(value.upload)!=bool(value.download)){return false;}
  switch(value.state){
    case core::PassiveSpeedState::Empty:case core::PassiveSpeedState::Ready:case core::PassiveSpeedState::Gap:
    case core::PassiveSpeedState::Invalid:case core::PassiveSpeedState::Clock:case core::PassiveSpeedState::Overflow:
    case core::PassiveSpeedState::Memory:break;
    default:return false;
  }
  if(value.state!=core::PassiveSpeedState::Ready&&(value.currentMetricRating||value.upload||value.download)){return false;}
  std::int64_t last=-1;
  for(const auto& point:value.history){
    const auto& input=point.input;
    if(input.observedMillis<=last||!input.uploadBytesPerSecond||!input.downloadBytesPerSecond||!input.rttMillis
        ||*input.uploadBytesPerSecond<0||*input.downloadBytesPerSecond<0||*input.rttMillis<0){return false;}
    last=input.observedMillis;
  }
  for(const auto* estimate:{value.upload?&*value.upload:nullptr,value.download?&*value.download:nullptr,
      value.persistentUpload?&*value.persistentUpload:nullptr,value.persistentDownload?&*value.persistentDownload:nullptr}){
    if(estimate&&(estimate->bytesPerSecond<0||estimate->whenMillis<0||!std::isfinite(estimate->metricRating)
        ||estimate->metricRating < -1||estimate->metricRating>1||!std::isfinite(estimate->estimateType)
        ||estimate->segments.size()>core::PassiveSpeedMapper::MaximumBins)){return false;}
  }
  const auto limit=[](const core::PassiveSpeedLimitRecord& record){
    return std::isfinite(record.metricRating)&&record.metricRating>=-1&&record.metricRating<=1&&std::isfinite(record.estimateType);
  };
  if(value.badUpload.size()>core::PassiveSpeedMapper::MaximumBadHistory||value.badDownload.size()>core::PassiveSpeedMapper::MaximumBadHistory
      ||value.badUploadCountdown>5||value.badDownloadCountdown>5||value.sustainedSamples>core::PassiveSpeedMapper::SustainedSamples){return false;}
  for(const auto& row:value.badUpload){if(!limit(row)){return false;}}
  for(const auto& row:value.badDownload){if(!limit(row)){return false;}}
  for(const auto* row:{value.lastBadUpload?&*value.lastBadUpload:nullptr,value.lastBadDownload?&*value.lastBadDownload:nullptr,
      value.bestGoodUpload?&*value.bestGoodUpload:nullptr,value.bestGoodDownload?&*value.bestGoodDownload:nullptr}){
    if(row&&!limit(*row)){return false;}
  }
  for(const auto* capacity:{value.uploadCapacity?&*value.uploadCapacity:nullptr,value.downloadCapacity?&*value.downloadCapacity:nullptr}){
    if(!capacity){continue;}if(!limit(capacity->value)||(capacity->lastDerivedMillis&&*capacity->lastDerivedMillis<0)){return false;}
    switch(capacity->origin){case core::PassiveCapacityOrigin::Saved:case core::PassiveCapacityOrigin::Sustained:case core::PassiveCapacityOrigin::Choke:break;default:return false;}
  }
  if(previous&&previous->session==session&&previous->epoch==incoming->epoch&&previous->snapshot){
    const auto& before=*previous->snapshot;
    if(value.accepted<before.accepted||value.dropped<before.dropped||value.gaps<before.gaps||value.speedOnlyAccepted<before.speedOnlyAccepted){return false;}
  }
  return true;
}
} // namespace biglybtc::ui
Q_DECLARE_METATYPE(biglybtc::ui::LiveSpeedManagerPresentation)
