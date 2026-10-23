#include "biglybtc/disk_statistics.hpp"
#include <limits>
#include <string_view>

namespace biglybtc::core {
namespace {
using I=std::int64_t;using U=std::uint64_t;
constexpr U Scale=1000000000,Maximum=static_cast<U>(std::numeric_limits<I>::max());
std::optional<I> rate(I delta,I time)noexcept{
  const auto n=static_cast<U>(delta),d=static_cast<U>(time);const auto whole=n/d;
  if(whole>Maximum/Scale){return {};}
  U quotient=0,rest=0;const U remainder=n%d;
  // Exact fractional product/division, as in the existing engine accumulator:
  // no signed overflow and no nonstandard wide integer extension.
  for(unsigned index=30;index>0;--index){quotient*=2;rest*=2;if(rest>=d){rest-=d;++quotient;}
    if((Scale>>(index-1))&U{1}){rest+=remainder;if(rest>=d){rest-=d;++quotient;}}}
  const U base=whole*Scale;if(quotient>Maximum-base){return {};}return static_cast<I>(base+quotient);
}
}
std::expected<DiskStatistics,DiskStatisticsError> DiskStatisticsReducer::observe(const RuntimeMetrics& source) noexcept {
  if(!source.sequence||source.ownerElapsedNanoseconds<0||source.metrics.size()>MaximumRuntimeMetrics){return std::unexpected(DiskStatisticsError::Invalid);}
  DiskStatistics result;result.sequence=source.sequence;result.nanoseconds=source.ownerElapsedNanoseconds;
  struct Definition{std::string_view name;RuntimeMetricType type;};using T=RuntimeMetricType;
  constexpr std::array<Definition,DiskStatisticCount> fields{{
    {"disk.queued_disk_jobs",T::Gauge},{"disk.num_running_disk_jobs",T::Gauge},{"disk.blocked_disk_jobs",T::Gauge},
    {"disk.disk_blocks_in_use",T::Gauge},{"disk.file_pool_size",T::Gauge},{"disk.queued_write_bytes",T::Gauge},
    {"disk.num_read_ops",T::Counter},{"disk.num_write_ops",T::Counter},{"disk.num_blocks_read",T::Counter},
    {"disk.num_blocks_written",T::Counter},{"disk.file_pool_hits",T::Counter},{"disk.file_pool_misses",T::Counter}}};
  for(std::size_t i=0;i<source.metrics.size();++i){const auto& metric=source.metrics[i];
    if(metric.name.empty()||metric.name.size()>MaximumRuntimeMetricNameBytes||metric.name.find('\0')!=std::string::npos
        ||(metric.type!=T::Counter&&metric.type!=T::Gauge)||metric.group!=runtimeMetricGroup(metric.name)){return std::unexpected(DiskStatisticsError::Invalid);}
    for(std::size_t j=0;j<i;++j){if(source.metrics[j].name==metric.name){return std::unexpected(DiskStatisticsError::Invalid);}}
    for(std::size_t field=0;field<fields.size();++field){if(metric.name==fields[field].name&&metric.type==fields[field].type&&metric.value>=0){result.values[field]=metric.value;}}
  }
  if(previous_){const auto& old=*previous_;
    if(result.sequence<old.sequence||result.nanoseconds<old.nanoseconds){return std::unexpected(DiskStatisticsError::Stale);}
    if(result.sequence==old.sequence){
      if(result.nanoseconds!=old.nanoseconds||result.values!=old.values){return std::unexpected(DiskStatisticsError::Stale);}
      auto same=old;same.fresh=false;if(gap_){same.readOperationsPerSecond.reset();same.writeOperationsPerSecond.reset();}return same;
    }
    if(!gap_&&result.nanoseconds>old.nanoseconds){
      const auto project=[&](DiskStatistic field){const auto a=old.get(field),b=result.get(field);
        return a&&b&&*b>=*a?rate(*b-*a,result.nanoseconds-old.nanoseconds):std::optional<I>{};};
      result.readOperationsPerSecond=project(DiskStatistic::ReadOperations);result.writeOperationsPerSecond=project(DiskStatistic::WriteOperations);
      result.connectedToPrevious=old.sequence!=std::numeric_limits<std::uint64_t>::max()&&result.sequence==old.sequence+1;
    }
  }
  result.fresh=true;previous_=result;gap_=false;return result;
}
void DiskActivityHistory::observe(const DiskStatistics& value)noexcept{
  if(!value.fresh){return;}
  if(!value.sequence||value.nanoseconds<0||(value.readOperationsPerSecond&&*value.readOperationsPerSecond<0)
      ||(value.writeOperationsPerSecond&&*value.writeOperationsPerSecond<0)){gap_=true;return;}
  if(count_){const auto& old=points_[(start_+count_-1)%MaximumDiskActivityPoints];
    if(value.sequence<=old.sequence||value.nanoseconds<=old.nanoseconds){gap_=true;return;}}
  if(!value.readOperationsPerSecond&&!value.writeOperationsPerSecond){gap_=true;return;}
  const DiskActivityPoint point{value.sequence,value.nanoseconds,value.readOperationsPerSecond,value.writeOperationsPerSecond,!gap_&&value.connectedToPrevious};
  if(count_<MaximumDiskActivityPoints){points_[(start_+count_)%MaximumDiskActivityPoints]=point;++count_;}
  else{points_[start_]=point;start_=(start_+1)%MaximumDiskActivityPoints;}
  gap_=false;
}
DiskActivityFrame DiskActivityHistory::snapshot()const noexcept{
  DiskActivityFrame result;result.count=count_;for(std::size_t i=0;i<count_;++i){result.points[i]=points_[(start_+i)%MaximumDiskActivityPoints];}
  if(result.count){result.points[0].connectedToPrevious=false;}return result;
}
} // namespace biglybtc::core
