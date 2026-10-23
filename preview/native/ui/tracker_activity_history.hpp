#pragma once
#include "runtime_service_summary.hpp"
#include <algorithm>
#include <array>
#include <limits>

namespace biglybtc::ui {
struct TrackerActivityPoint {
  std::uint64_t sequence=0;
  std::int64_t nanoseconds=0;
  std::optional<std::int64_t> queuedHttpAnnounces,receivedBytesPerSecond,sentBytesPerSecond;
  bool queueConnected=false,receivedConnected=false,sentConnected=false;
};
struct TrackerActivityValues {
  std::optional<std::int64_t> queuedHttpAnnounces,receivedBytes,sentBytes,receivedBytesPerSecond,sentBytesPerSecond;
};
struct TrackerActivityFrame {
  static constexpr std::size_t MaximumPoints=600;
  std::array<TrackerActivityPoint,MaximumPoints> points{};
  std::size_t count=0;
};
// Fixed value-only history of actual accepted engine responses, not announces
// reconstructed from torrent endpoint flags or next_announce deadlines.
class TrackerActivityHistory final {
public:
  static std::optional<std::int64_t> rate(std::int64_t before,std::int64_t after,std::int64_t nanos)noexcept{
    if(before<0||after<before||nanos<=0){return {};}
    constexpr std::uint64_t scale=1000000000,maximum=std::numeric_limits<std::int64_t>::max();
    const auto delta=static_cast<std::uint64_t>(after-before),divisor=static_cast<std::uint64_t>(nanos);
    const auto whole=delta/divisor,remainder=delta%divisor;if(whole>maximum/scale){return {};}
    // Exact floor(remainder*1e9/divisor), without a nonstandard wide integer.
    // Both residues are <divisor<=INT64_MAX, so each unsigned double/add fits.
    std::uint64_t fraction=0,residue=0;
    for(int bit=29;bit>=0;--bit){
      fraction*=2;residue*=2;if(residue>=divisor){residue-=divisor;++fraction;}
      if(scale&(std::uint64_t{1}<<bit)){residue+=remainder;if(residue>=divisor){residue-=divisor;++fraction;}}
    }
    const auto base=whole*scale;if(fraction>maximum-base){return {};}return static_cast<std::int64_t>(base+fraction);
  }
  bool observe(const core::RuntimeMetrics& source,bool current)noexcept{
    if(!source.sequence||source.ownerElapsedNanoseconds<0||source.metrics.size()>core::MaximumRuntimeMetrics
        ||source.sequence<sequence_||(sequence_&&source.ownerElapsedNanoseconds<nanoseconds_)){return false;}
    if(source.sequence==sequence_){if(!current){pause();}return true;}
    const auto summary=projectRuntimeServiceSummary(&source);
    const auto nonnegative=[](std::optional<std::int64_t> value){return value&&*value>=0?value:std::nullopt;};
    TrackerActivityValues next;next.queuedHttpAnnounces=nonnegative(summary.get(RuntimeServiceMetric::QueuedHttpAnnounces));
    next.receivedBytes=nonnegative(summary.get(RuntimeServiceMetric::TrackerReceived));next.sentBytes=nonnegative(summary.get(RuntimeServiceMetric::TrackerSent));
    const bool consecutive=sequence_&&source.sequence-sequence_==1&&source.ownerElapsedNanoseconds>nanoseconds_&&!gap_;
    const bool regression=(received_&&next.receivedBytes&&*next.receivedBytes<*received_)||(sent_&&next.sentBytes&&*next.sentBytes<*sent_);
    if(consecutive&&!regression){
      const auto elapsed=source.ownerElapsedNanoseconds-nanoseconds_;
      if(received_&&next.receivedBytes){next.receivedBytesPerSecond=rate(*received_,*next.receivedBytes,elapsed);}
      if(sent_&&next.sentBytes){next.sentBytesPerSecond=rate(*sent_,*next.sentBytes,elapsed);}
    }
    TrackerActivityPoint point{source.sequence,source.ownerElapsedNanoseconds,next.queuedHttpAnnounces,next.receivedBytesPerSecond,next.sentBytesPerSecond};
    if(frame_.count&&consecutive&&!regression&&current){const auto& old=frame_.points[frame_.count-1];
      point.queueConnected=bool(old.queuedHttpAnnounces&&point.queuedHttpAnnounces);
      point.receivedConnected=bool(old.receivedBytesPerSecond&&point.receivedBytesPerSecond);
      point.sentConnected=bool(old.sentBytesPerSecond&&point.sentBytesPerSecond);}
    if(frame_.count==frame_.MaximumPoints){std::move(frame_.points.begin()+1,frame_.points.end(),frame_.points.begin());--frame_.count;}
    frame_.points[frame_.count++]=point;frame_.points[0].queueConnected=false;frame_.points[0].receivedConnected=false;frame_.points[0].sentConnected=false;
    sequence_=source.sequence;nanoseconds_=source.ownerElapsedNanoseconds;received_=next.receivedBytes;sent_=next.sentBytes;latest_=next;gap_=false;
    if(!current){pause();}return true;
  }
  void gap()noexcept{pause();latest_={};}
  const TrackerActivityValues& latest()const noexcept{return latest_;}
  const TrackerActivityFrame& frame()const noexcept{return frame_;}
private:
  void pause()noexcept{gap_=true;received_.reset();sent_.reset();}
  TrackerActivityFrame frame_;
  TrackerActivityValues latest_;
  std::uint64_t sequence_=0;
  std::int64_t nanoseconds_=0;
  std::optional<std::int64_t> received_,sent_;
  bool gap_=true;
};
} // namespace biglybtc::ui
