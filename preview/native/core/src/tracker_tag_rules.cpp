// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/tracker_tag_rules.hpp>
#include <algorithm>
#include <limits>
#include <optional>
#include <set>
#include <utility>
namespace biglybtc::core::tags::tracker {
namespace {
bool bounded(Limits l){return l.endpoints<=64&&l.properties<=64&&l.textBytes<=2048&&l.hosts<=4096&&l.totalBytes<=262144;}
bool ascii(std::string_view value){return std::ranges::all_of(value,[](unsigned char c){return c<128;});}
bool ipv6(std::string_view value){
  const auto groups=[](std::string_view side)->std::optional<unsigned>{
    if(side.empty()){return 0;}unsigned count=0;
    for(;;){const auto end=side.find(':');const auto part=side.substr(0,end);
      if(part.empty()||part.size()>4||!std::ranges::all_of(part,[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');})){return {};}
      ++count;if(end==side.npos){return count;}side.remove_prefix(end+1);
    }
  };
  const auto compression=value.find("::");
  if(compression==value.npos){auto count=groups(value);return count&&*count==8;}
  if(value.find("::",compression+2)!=value.npos){return false;}
  auto left=groups(value.substr(0,compression)),right=groups(value.substr(compression+2));
  return left&&right&&*left+*right<8;
}
std::string lower(std::string_view value){std::string out(value);for(auto& c:out){if(c>='A'&&c<='Z'){c=static_cast<char>(c+('a'-'A'));}}return out;}
struct ParsedUrl {
  Endpoint endpoint;
  std::string protocol;
  bool pseudoTracker=false;
};
Result<ParsedUrl> parseUrl(std::string_view url,Limits limits,bool trackerSelection=false){
  try{
    if(!bounded(limits)||url.size()>limits.textBytes){return std::unexpected(Error::Bounds);}
    if(!ascii(url)||std::ranges::any_of(url,[](unsigned char c){return c<=32||c==127||c=='\\';})){return std::unexpected(Error::Unsupported);}
    const auto separator=url.find(':');if(separator==url.npos){return std::unexpected(Error::Unsupported);}
    ParsedUrl parsed;
    parsed.protocol=lower(url.substr(0,separator));
    auto& result=parsed.endpoint;
    if(parsed.protocol=="http"||parsed.protocol=="udp"){result.defaultPort=80;}
    else if(parsed.protocol=="https"){result.defaultPort=443;}
    else if(parsed.protocol!="wss"&&!(trackerSelection&&parsed.protocol=="dht")){
      return std::unexpected(Error::Unsupported);
    }
    auto authority=url.substr(separator+1);
    if(!authority.starts_with("//")){return parsed;}
    authority.remove_prefix(2);authority=authority.substr(0,authority.find_first_of("/?#"));
    if(const auto at=authority.find('@');at!=authority.npos){if(authority.find('@',at+1)!=authority.npos){return std::unexpected(Error::Unsupported);}authority.remove_prefix(at+1);}
    std::string_view host,port;bool explicitPort=false;
    if(authority.starts_with('[')){
      const auto end=authority.find(']');if(end==authority.npos||end==1){return std::unexpected(Error::Unsupported);}
      host=authority.substr(0,end+1);const auto address=host.substr(1,host.size()-2);
      if(!ipv6(address)){return std::unexpected(Error::Unsupported);}
      if(end+1<authority.size()){if(authority[end+1]!=':'){return std::unexpected(Error::Unsupported);}port=authority.substr(end+2);explicitPort=true;}
    }else{
      const auto colon=authority.find(':');host=authority.substr(0,colon);
      if(host.find_first_of("[]%")!=host.npos){return std::unexpected(Error::Unsupported);}
      if(colon!=authority.npos){port=authority.substr(colon+1);explicitPort=true;}
    }
    if(explicitPort&&!port.empty()){
      bool negative=false;if(port.front()=='+'||port.front()=='-'){negative=port.front()=='-';port.remove_prefix(1);}
      if(port.empty()){return std::unexpected(Error::Unsupported);}std::uint64_t value=0;
      for(unsigned char c:port){if(c<'0'||c>'9'||value>(std::uint64_t(std::numeric_limits<std::int32_t>::max())-(c-'0'))/10){return std::unexpected(Error::Unsupported);}value=value*10+c-'0';}
      if(negative&&value>1){return std::unexpected(Error::Unsupported);}
      result.port=negative?-static_cast<std::int32_t>(value):static_cast<std::int32_t>(value);
    }
    parsed.pseudoTracker=host.ends_with(".dht");
    result.host=lower(host);
    return parsed;
  }catch(...){return std::unexpected(Error::Memory);}
}
}
Result<Endpoint> endpoint(std::string_view url,Limits limits){
  return parseUrl(url,limits).transform([](ParsedUrl&& parsed){return std::move(parsed.endpoint);});
}
Result<SelectionUrl> selectionUrl(std::string_view url,Limits limits){
  try{
    auto parsed=parseUrl(url,limits,true);
    if(!parsed){return std::unexpected(parsed.error());}
    if(parsed->pseudoTracker){return SelectionUrl{std::move(parsed->endpoint),std::nullopt};}
    auto selector=parsed->endpoint.host;
    const auto port=parsed->endpoint.port==-1?parsed->endpoint.defaultPort:parsed->endpoint.port;
    if(port>0){selector+=':';selector+=std::to_string(port);}
    auto name=parsed->protocol+"://"+selector;
    return SelectionUrl{std::move(parsed->endpoint),Origin{std::move(name),std::move(selector)}};
  }catch(...){return std::unexpected(Error::Memory);}
}
Result<std::vector<std::string>> augmentedHosts(std::span<const Endpoint> endpoints,Limits limits){
  try{
    if(!bounded(limits)||endpoints.size()>limits.endpoints){return std::unexpected(Error::Bounds);}
    std::set<std::string> hosts;std::size_t bytes=0;
    const auto insert=[&](std::string value){if(hosts.contains(value)){return true;}if(hosts.size()==limits.hosts||value.size()>limits.totalBytes-bytes){return false;}bytes+=value.size();hosts.insert(std::move(value));return true;};
    const auto expand=[&](std::string value){
      if(!insert(value)){return false;}
      // Java split("\\.") discards trailing empty strings. Empty interior
      // labels survive, and the first label never becomes a suffix by itself.
      while(!value.empty()&&value.back()=='.'){value.pop_back();}
      auto dot=value.find('.');while(dot!=value.npos){if(!insert(value.substr(dot+1))){return false;}dot=value.find('.',dot+1);}return true;
    };
    for(const auto& e:endpoints){
      if(e.host.size()>limits.textBytes){return std::unexpected(Error::Bounds);}
      if(!ascii(e.host)||e.port< -1||e.defaultPort< -1){return std::unexpected(Error::Unsupported);}
      auto host=lower(e.host);if(!expand(host)){return std::unexpected(Error::Bounds);}
      const auto port=e.port==-1?e.defaultPort:e.port;
      if(port>0&&!expand(host+':'+std::to_string(port))){return std::unexpected(Error::Bounds);}
    }
    return std::vector<std::string>{hosts.begin(),hosts.end()};
  }catch(...){return std::unexpected(Error::Memory);}
}
Result<bool> matches(std::span<const Endpoint> endpoints,std::span<const std::string> property,Limits limits){
  try{
    if(!bounded(limits)||property.size()>limits.properties){return std::unexpected(Error::Bounds);}
    auto hosts=augmentedHosts(endpoints,limits);if(!hosts){return std::unexpected(hosts.error());}
    bool match=false;std::size_t bytes=0;
    for(const auto& name:property){if(name.size()>limits.textBytes||name.size()>limits.totalBytes-bytes){return std::unexpected(Error::Bounds);}bytes+=name.size();
      if(!ascii(name)){return std::unexpected(Error::Unsupported);}match=std::ranges::binary_search(*hosts,lower(name))||match;
    }return match;
  }catch(...){return std::unexpected(Error::Memory);}
}
Membership project(Event event,bool configured,bool member,bool match)noexcept{
  if(event==Event::Startup||(event==Event::AnnounceSync&&!configured)){return Membership::Keep;}
  if(match&&!member){return Membership::Add;}
  if(event!=Event::DownloadCreated&&!match&&member){return Membership::Remove;}
  return Membership::Keep;
}
}
