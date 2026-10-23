#pragma once
#include "biglybtc/tracker_statistics.hpp"
#include <QMetaType>
#include <memory>
namespace biglybtc::ui {
struct TrackerStatisticsPacket {
  quint64 session=0,epoch=0,sequence=0;
  std::shared_ptr<const core::TrackerStatistics> snapshot;
  bool current=true;
};
using TrackerStatisticsPresentation=std::shared_ptr<const TrackerStatisticsPacket>;
inline bool acceptTrackerStatistics(const TrackerStatisticsPresentation& previous,
    const TrackerStatisticsPresentation& incoming,quint64 session) noexcept {
  if(!incoming||!session||incoming->session!=session||!incoming->sequence
      ||(incoming->snapshot&&!core::validTrackerStatistics(*incoming->snapshot))){return false;}
  if(incoming->snapshot&&incoming->snapshot->state==core::TrackerReadState::Ready&&!incoming->epoch){return false;}
  return !previous||(previous->session==session&&incoming->sequence>previous->sequence&&incoming->epoch>=previous->epoch);
}
} // namespace biglybtc::ui
Q_DECLARE_METATYPE(biglybtc::ui::TrackerStatisticsPresentation)
