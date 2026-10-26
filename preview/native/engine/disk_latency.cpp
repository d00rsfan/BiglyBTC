#include "disk_latency.hpp"
#include <algorithm>
#include <chrono>
#include <limits>

namespace biglybtc::engine::detail {
std::int64_t DiskLatency::nowMillis() noexcept {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::uint64_t DiskLatency::begin(DiskIoAxis axis) noexcept {
  try{
    const std::lock_guard lock(mutex_);
    if(lost_){return 0;}
    const auto now=nowMillis();
    if((axis!=DiskIoAxis::Read&&axis!=DiskIoAxis::Write)||now<lastMutation_
        ||pending_.size()>=MaximumPending||issued_==std::numeric_limits<std::uint64_t>::max()){
      lost_=true;return 0;
    }
    const auto token=issued_+1;
    pending_.emplace(token,Request{axis,now});issued_=token;lastMutation_=now;return token;
  }catch(...){lost_=true;return 0;}
}
void DiskLatency::finish(std::uint64_t token,bool success) noexcept {
  if(!token){return;}
  try{
    const std::lock_guard lock(mutex_);
    const auto found=pending_.find(token);
    if(found==pending_.end()){lost_=true;return;}
    const auto request=found->second;pending_.erase(found);
    const auto now=nowMillis();
    if(now<lastMutation_||now<request.started){lost_=true;return;}
    lastMutation_=now;
    if(success){
      auto& duration=request.axis==DiskIoAxis::Read?readMillis_:writeMillis_;
      auto& completed=request.axis==DiskIoAxis::Read?reads_:writes_;
      if(completed==std::numeric_limits<std::uint64_t>::max()){lost_=true;return;}
      duration=now-request.started;++completed;
    }
    // Original cancelled/failed requests disappear without replacing the last
    // successful duration. They do not manufacture a successful zero read.
  }catch(...){lost_=true;}
}
DiskLatencySnapshot DiskLatency::snapshot(std::int64_t observed) const noexcept {
  DiskLatencySnapshot result;result.observedMillis=observed;
  try{
    const std::lock_guard lock(mutex_);
    result.completedReads=reads_;result.completedWrites=writes_;
    auto read=readMillis_,write=writeMillis_;
    // Completion can race an already captured owner timestamp. Do not publish
    // a future completion against that earlier accepted sample; try next tick.
    bool unavailable=lost_||observed<0||observed<lastMutation_;
    for(const auto& [token,request]:pending_){
      static_cast<void>(token);
      if(request.axis==DiskIoAxis::Read){++result.pendingReads;}else{++result.pendingWrites;}
      if(observed<request.started){unavailable=true;}
      else if(request.axis==DiskIoAxis::Read){read=std::max(read,observed-request.started);}
      else{write=std::max(write,observed-request.started);}
    }
    result.measurementLost=lost_;
    if(!unavailable){result.readMillis=read;result.writeMillis=write;}
  }catch(...){lost_=true;result.measurementLost=true;}
  return result;
}
} // namespace biglybtc::engine::detail
