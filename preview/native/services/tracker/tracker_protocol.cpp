// SPDX-License-Identifier: GPL-2.0-or-later
#include "tracker_protocol.hpp"
#include <algorithm>
#include <charconv>
#include <limits>
#include <set>
namespace biglybtc::tracker::detail {
namespace {
struct Invalid{std::string text;};
void need(bool condition,const char* text){if(!condition){throw Invalid{text};}}
std::string lower(std::string_view s){std::string out(s);for(auto& c:out){if(c>='A'&&c<='Z'){c=static_cast<char>(c-'A'+'a');}}return out;}
int hex(char c){if(c>='0'&&c<='9'){return c-'0';}if(c>='a'&&c<='f'){return c-'a'+10;}if(c>='A'&&c<='F'){return c-'A'+10;}return -1;}
std::string decoded(std::string_view raw){std::string out;need(raw.size()<=4096,"Tracker field exceeds bound");for(std::size_t i=0;i<raw.size();++i){char c=raw[i];if(c=='%'){need(i+2<raw.size()&&hex(raw[i+1])>=0&&hex(raw[i+2])>=0,"Malformed percent escape");c=static_cast<char>((hex(raw[i+1])<<4)|hex(raw[i+2]));i+=2;}else if(c=='+'){c=' ';}out+=c;}return out;}
std::int64_t number(std::string_view value){need(!value.empty()&&value.size()<=20,"Malformed tracker integer");if(value.starts_with('+')){value.remove_prefix(1);need(!value.empty(),"Malformed tracker integer");}std::int64_t out=0;const auto p=std::from_chars(value.data(),value.data()+value.size(),out);need(p.ec==std::errc{}&&p.ptr==value.data()+value.size(),"Malformed tracker integer");return out;}
std::string bytes(std::string_view s){return std::to_string(s.size())+':'+std::string(s);}
std::string integer(std::int64_t n){return 'i'+std::to_string(n)+'e';}
std::string dictionary(std::initializer_list<std::pair<std::string,std::string>> values){std::map<std::string,std::string> sorted(values.begin(),values.end());std::string out="d";for(const auto& [k,v]:sorted){out+=bytes(k)+v;}return out+'e';}
void add(std::int64_t& value,std::int64_t amount){need(value>=0&&amount>=0&&amount<=std::numeric_limits<std::int64_t>::max()-value,"Tracker counter exceeds bound");value+=amount;}
void counts(WireTorrent& torrent){torrent.stats.seeds=torrent.stats.leechers=0;for(const auto& [id,p]:torrent.peers){(void)id;if(p.left==0){++torrent.stats.seeds;}else{++torrent.stats.leechers;}}}
std::string ip(std::array<unsigned char,4> a){return std::to_string(a[0])+'.'+std::to_string(a[1])+'.'+std::to_string(a[2])+'.'+std::to_string(a[3]);}
std::string scrapeEntry(const WireTorrent* torrent){return dictionary({{"complete",integer(torrent?torrent->stats.seeds:0)},{"downloaded",integer(torrent?torrent->stats.completed:0)},{"incomplete",integer(torrent?torrent->stats.leechers:0)}});}
std::int64_t delta(std::int64_t value,std::int64_t previous,std::chrono::steady_clock::duration elapsed){if(value<previous){return 0;}const auto amount=value-previous;const auto millis=std::max<std::int64_t>(25,std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());const auto limit=(std::int64_t{3*1024*1024}+1)*std::min<std::int64_t>(millis,10800000);return amount<=(limit-1)/1000?amount:0;}
}
std::expected<WireRequest,std::string> parseTrackerRequest(std::string_view raw){try{
  need(raw.size()<=8192,"Tracker request exceeds bound");const auto end=raw.find("\r\n\r\n");need(end!=raw.npos&&end+4==raw.size(),"One complete bodyless tracker request required");const auto line=raw.find("\r\n");need(line!=raw.npos&&line<=4096,"Tracker request line exceeds bound");const auto first=raw.substr(0,line);need(first.starts_with("GET "),"Tracker requires GET");const auto version=first.rfind(' ');need(version>4&&(first.substr(version+1)=="HTTP/1.0"||first.substr(version+1)=="HTTP/1.1"),"Unsupported HTTP version");const auto target=first.substr(4,version-4);need(std::ranges::all_of(target,[](unsigned char c){return c>=33&&c<=126;})&&target.find('#')==target.npos,"Invalid tracker request target");
  std::set<std::string> headerKeys;for(std::size_t at=line+2;at<end;){const auto next=raw.find("\r\n",at);need(next!=raw.npos,"Malformed HTTP header");const auto header=raw.substr(at,next-at);const auto colon=header.find(':');need(colon!=header.npos&&colon>0,"Malformed HTTP header");const auto key=lower(header.substr(0,colon));need(headerKeys.insert(key).second,"Duplicate HTTP header");auto value=header.substr(colon+1);while(!value.empty()&&value.front()==' '){value.remove_prefix(1);}need(key!="transfer-encoding"&&(key!="content-length"||value=="0"),"Tracker request bodies unsupported");at=next+2;}
  WireRequest out;std::string_view query;if(target.starts_with("/announce?")){out.announce=true;query=target.substr(10);}else if(target.starts_with("/scrape?")){query=target.substr(8);}else if(target=="/scrape"){out.fullScrape=true;return out;}else{throw Invalid{"Tracker only supports announce and scrape"};}
  std::set<std::string> seen;unsigned fields=0;for(std::size_t at=0;at<query.size();){need(++fields<=64,"Too many tracker fields");const auto amp=query.find('&',at);const auto field=query.substr(at,amp==query.npos?amp:amp-at);const auto equal=field.find('=');need(equal!=field.npos&&equal>0,"Malformed tracker query field");const auto key=lower(field.substr(0,equal));const auto value=decoded(field.substr(equal+1));if(key=="info_hash"){need(value.size()==20&&out.hashes.size()<32,"Tracker requires a20-byte info_hash");out.hashes.push_back(value);}else{
    need(seen.insert(key).second,"Duplicate tracker field");
    if(key=="peer_id"){need(value.size()==20,"Tracker requires a20-byte peer_id");out.peerId=value;}
    else if(key=="port"){const auto n=number(value);need(n>0&&n<=65535,"Invalid tracker peer port");out.port=static_cast<std::uint16_t>(n);}
    else if(key=="uploaded"||key=="downloaded"||key=="left"){const auto n=number(value);need(n>=0,"Negative tracker counter unsupported");if(key=="uploaded"){out.uploaded=n;}else if(key=="downloaded"){out.downloaded=n;}else{out.left=n;}}
    else if(key=="numwant"){out.numWant=number(value);need(out.numWant>=-1&&out.numWant<=10000,"Tracker numwant exceeds bound");}
    else if(key=="event"){need(value.empty()||value=="started"||value=="completed"||value=="stopped","Unsupported tracker event");out.event=value;}
    else if(key=="compact"||key=="no_peer_id"){need(value=="0"||value=="1","Unsupported compact tracker mode");if(key=="compact"){out.compact=value=="1";}else{out.noPeerId=value=="1";}}
    else if(key=="key"){need(value.size()<=128&&std::ranges::all_of(value,[](unsigned char c){return c>=32&&c<127;}),"Invalid tracker peer key");out.key=value;}
    else if(key=="ip"){out.ip=value;}
    else if(key=="ipv6"||key=="ipv4"||key=="cryptoport"||key=="azudp"||key=="azhttp"||key=="azq"||(key=="requirecrypto"&&value!="0")){throw Invalid{"Advanced tracker routing unsupported"};}
  }if(amp==query.npos){break;}at=amp+1;}
  need(!out.hashes.empty(),"Hash missing from request");if(out.announce){need(out.hashes.size()==1&&!out.peerId.empty()&&out.port,"Announce requires one hash, peer_id and port");for(const auto* key:{"uploaded","downloaded","left"}){need(seen.contains(key),"Announce counters missing");}}return out;
}catch(const Invalid& e){return std::unexpected(e.text);}}
std::string trackerFailure(std::string_view reason){return dictionary({{"failure reason",bytes(reason.substr(0,256))}});}
std::string trackerHttpResponse(std::string_view body){return "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "+std::to_string(body.size())+"\r\nConnection: close\r\n\r\n"+std::string(body);}
WireResponse trackerResponse(const WireRequest& request,std::array<unsigned char,4> address,std::map<std::string,WireTorrent>& registry,const WirePolicy& policy,std::chrono::steady_clock::time_point now){try{
  need(policy.interval>=60&&policy.interval<=3600&&policy.maxPeers<=200,"Unsupported tracker response policy");for(auto& [hash,t]:registry){(void)hash;for(auto i=t.peers.begin();i!=t.peers.end();){if(now-i->second.seen>std::chrono::seconds(policy.interval*3)){i=t.peers.erase(i);}else{++i;}}counts(t);}
  if(request.fullScrape||!request.announce){need(!request.fullScrape||policy.fullScrape,"Full scrape disabled");std::string files="d";std::set<std::string> hashes(request.hashes.begin(),request.hashes.end());if(request.fullScrape){for(const auto& [hash,t]:registry){if(t.serving){hashes.insert(hash);}}}need(hashes.size()<=128,"Scrape exceeds tracker bound");std::optional<std::string> account;
    for(const auto& hash:hashes){auto found=registry.find(hash);WireTorrent* torrent=found!=registry.end()&&found->second.serving?&found->second:nullptr;if(torrent){add(torrent->stats.scrapes,1);if(hashes.size()==1){account=hash;}}files+=bytes(hash)+scrapeEntry(torrent);}files+='e';return {dictionary({{"files",files},{"flags",dictionary({{"min_request_interval",integer(policy.scrapeInterval)}})}}),std::move(account)};
  }
  const auto found=registry.find(request.hashes.front());need(found!=registry.end()&&found->second.serving,"Torrent unauthorised");auto& actual=found->second;auto next=actual;need(!request.ip||*request.ip==ip(address),"IP override unsupported");need(!request.compact||policy.compact,"Compact replies disabled");auto old=next.peers.find(request.peerId);
  if(old!=next.peers.end()){need(old->second.address==address&&(!policy.keyEnabled||old->second.key==request.key),"Peer identity or key changed");}
  const bool existed=old!=next.peers.end();const bool completed=existed&&old->second.completed;
  if(existed){add(next.stats.uploaded,delta(request.uploaded,old->second.uploaded,now-old->second.seen));add(next.stats.downloaded,delta(request.downloaded,old->second.downloaded,now-old->second.seen));}
  add(next.stats.announces,1);
  if(request.event=="stopped"){next.peers.erase(request.peerId);}else{
    need(existed||next.peers.size()<4096,"Tracker peer bound reached");
    for(auto i=next.peers.begin();i!=next.peers.end();){if(i->first!=request.peerId&&i->second.address==address&&i->second.port==request.port){i=next.peers.erase(i);}else{++i;}}
    const bool isComplete=completed||request.event=="completed";if(request.event=="completed"&&!completed){add(next.stats.completed,1);}next.peers.insert_or_assign(request.peerId,WirePeer{request.peerId,request.key,address,request.port,request.uploaded,request.downloaded,request.left,isComplete,now});
  }counts(next);std::string peers=request.compact?"":"l";const auto wanted=request.event=="stopped"?0:std::min<unsigned>(policy.maxPeers,request.numWant<0?policy.maxPeers:static_cast<unsigned>(request.numWant));unsigned sent=0;
  for(const auto& [id,peer]:next.peers){if(id==request.peerId||sent==wanted){continue;}if(request.compact){for(const auto c:peer.address){peers+=static_cast<char>(c);}peers+=static_cast<char>(peer.port>>8);peers+=static_cast<char>(peer.port&255);}else{std::map<std::string,std::string> fields{{"ip",bytes(ip(peer.address))},{"port",integer(peer.port)}};if(policy.sendPeerIds&&!request.noPeerId){fields.emplace("peer id",bytes(peer.id));}std::string encoded="d";for(const auto& [key,value]:fields){encoded+=bytes(key)+value;}peers+=encoded+'e';}++sent;}
  if(request.compact){peers=bytes(peers);}else{peers+='e';}actual=std::move(next);return {dictionary({{"complete",integer(actual.stats.seeds)},{"incomplete",integer(actual.stats.leechers)},{"interval",integer(policy.interval)},{"min interval",integer(policy.minInterval)},{"peers",peers}}),request.hashes.front()};
}catch(const Invalid& e){return {trackerFailure(e.text),std::nullopt};}}
} // namespace biglybtc::tracker::detail
