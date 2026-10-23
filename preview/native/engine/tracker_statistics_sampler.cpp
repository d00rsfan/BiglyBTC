#include "tracker_statistics_sampler.hpp"
#include <libtorrent/announce_entry.hpp>
#include <libtorrent/info_hash.hpp>
#include <libtorrent/time.hpp>
#include <chrono>

namespace biglybtc::engine {
namespace lt=libtorrent;
namespace {
template<class Hash>std::string hex(const Hash& h){
  constexpr char digits[]="0123456789abcdef";std::string out;out.reserve(static_cast<std::size_t>(h.size())*2);
  for(int i=0;i<h.size();++i){const auto b=static_cast<unsigned char>(h[i]);out.push_back(digits[b>>4]);out.push_back(digits[b&15]);}return out;
}
}
core::TrackerStatistics sampleTrackerStatistics(const lt::torrent_handle& handle,core::TrackerPolicy policy) noexcept {
  core::TrackerStatistics result;
  if(policy==core::TrackerPolicy::DisabledByIsolation){result.state=core::TrackerReadState::DisabledByIsolation;return result;}
  if(policy!=core::TrackerPolicy::Permitted){return result;}
  try{
    if(!handle.is_valid()){return result;}const auto hashes=handle.info_hashes();
    if(hashes.has_v1()){result.torrentHash=hex(hashes.v1);}else if(hashes.has_v2()){result.torrentHash=hex(hashes.v2);}else{return {};}
    const auto trackers=handle.trackers(); // Actual snapshot; never add/reannounce/scrape.
    const auto now=std::chrono::duration_cast<std::chrono::seconds>(lt::clock_type::now().time_since_epoch()).count();
    result.state=core::TrackerReadState::Ready;
    const auto append=[&](core::TrackerRow row){if(result.rows.size()==core::MaximumTrackerRows){result.truncated=true;return false;}result.rows.push_back(std::move(row));return true;};
    for(const auto& tracker:trackers){
      if(tracker.endpoints.empty()){
        if(!append(core::projectTrackerRow(tracker.url,tracker.tier,tracker.verified))){break;}
      }else for(const auto& endpoint:tracker.endpoints){
        for(const auto version:lt::all_versions){if(!hashes.has(version)){continue;}const auto& source=endpoint.info_hashes[version];
          core::TrackerEndpointFacts facts;facts.protocol=version==lt::protocol_version::V1?1:2;
          const auto address=endpoint.local_endpoint.address().to_string();
          facts.localEndpoint=(endpoint.local_endpoint.address().is_v6()?"["+address+"]":address)+":"+std::to_string(endpoint.local_endpoint.port());
          facts.enabled=endpoint.enabled;facts.updating=source.updating;facts.failures=source.fails;
          facts.seeders=source.scrape_complete;facts.leechers=source.scrape_incomplete;facts.downloaded=source.scrape_downloaded;facts.errorCode=source.last_error.value();
          if(source.next_announce!=(lt::time_point32::min)()){
            // Widen BOTH counts before subtraction; this is LT's own clock,
            // not app uptime or a guaranteed announce-queue deadline.
            facts.nextAnnounceSeconds=static_cast<std::int64_t>(source.next_announce.time_since_epoch().count())-static_cast<std::int64_t>(now);
          }
          if(!append(core::projectTrackerRow(tracker.url,tracker.tier,tracker.verified,facts))){break;}
        }
        if(result.truncated){break;}
      }
      if(result.truncated){break;}
    }
    if(!core::validTrackerStatistics(result)){return {};}return result;
  }catch(...){return {};}
}
} // namespace biglybtc::engine
