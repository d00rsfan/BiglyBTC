#include "biglybtc/tracker_statistics.hpp"
#include <algorithm>

namespace biglybtc::core {
namespace {
std::string origin(std::string_view input){
  const auto split=input.find("://");if(split==std::string_view::npos||split>8){return "Invalid tracker URL";}
  const auto scheme=input.substr(0,split);
  if(scheme!="http"&&scheme!="https"&&scheme!="udp"&&scheme!="ws"&&scheme!="wss"){return "Unsupported tracker URL";}
  auto authority=input.substr(split+3);authority=authority.substr(0,authority.find_first_of("/?#"));
  const auto user=authority.rfind('@');if(user!=std::string_view::npos){authority.remove_prefix(user+1);}
  if(authority.empty()||authority.size()>240){return "Invalid tracker URL";}
  for(unsigned char c:authority){if(c<=32||c>=127||c=='\\'){return "Invalid tracker URL";}}
  return std::string(scheme)+"://"+std::string(authority)+"/[path hidden]";
}
bool count(const std::optional<int>& value){return !value||*value>=0;}
}
TrackerRow projectTrackerRow(std::string_view url,std::uint8_t tier,bool verified,
    const std::optional<TrackerEndpointFacts>& endpoint){
  TrackerRow row;row.displayUrl=origin(url);row.tier=tier;row.verified=verified;
  if(!endpoint){return row;}const auto& p=*endpoint;
  if(p.protocol!=1&&p.protocol!=2){row.partial=true;return row;}
  if(p.localEndpoint.size()<=128&&p.localEndpoint.find_first_of("\r\n\t")==std::string::npos){row.localEndpoint=p.localEndpoint;}
  else{row.partial=true;}
  row.protocol=p.protocol;row.enabled=p.enabled;row.updating=p.updating;
  if(p.failures>=0&&p.failures<=127){row.failures=p.failures;}else{row.partial=true;}
  const auto scrape=[&](int value,std::optional<int>& output){if(value>=0){output=value;}else if(value!=-1){row.partial=true;}};
  scrape(p.seeders,row.seeders);scrape(p.leechers,row.leechers);scrape(p.downloaded,row.downloaded);
  row.errorCode=p.errorCode;row.nextAnnounceSeconds=p.nextAnnounceSeconds;return row;
}
bool validTrackerStatistics(const TrackerStatistics& value) noexcept {
  if(value.rows.size()>MaximumTrackerRows){return false;}
  if(value.state==TrackerReadState::Unavailable||value.state==TrackerReadState::DisabledByIsolation){return value.rows.empty()&&value.torrentHash.empty()&&!value.truncated;}
  if(value.state!=TrackerReadState::Ready||(value.torrentHash.size()!=40&&value.torrentHash.size()!=64)
      ||!std::all_of(value.torrentHash.begin(),value.torrentHash.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');})){return false;}
  for(const auto& row:value.rows){
    if(row.displayUrl.empty()||row.displayUrl.size()>270||row.displayUrl.find_first_of("\r\n\t")!=std::string::npos
        ||row.displayUrl.find('@')!=std::string::npos||row.displayUrl.find('?')!=std::string::npos||row.displayUrl.find('#')!=std::string::npos
        ||row.localEndpoint.size()>128||row.localEndpoint.find_first_of("\r\n\t")!=std::string::npos
        ||row.protocol<0||row.protocol>2||!count(row.failures)||!count(row.seeders)||!count(row.leechers)||!count(row.downloaded)
        ||(row.failures&&*row.failures>127)){return false;}
    if(row.protocol==0&&(!row.localEndpoint.empty()||row.enabled||row.updating||row.failures||row.seeders||row.leechers||row.downloaded||row.errorCode||row.nextAnnounceSeconds)){return false;}
    if(row.protocol!=0&&(!row.enabled||!row.updating||!row.errorCode)){return false;}
  }
  return true;
}
} // namespace biglybtc::core
