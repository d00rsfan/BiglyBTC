// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/peer_set_predicate.hpp>
#include <algorithm>
#include <new>
namespace biglybtc::core::tags::peer_sets {
namespace {constexpr unsigned publicNet=1,i2p=2,tor=4,ipv4=8,ipv6=16;
std::string lower(std::string text){for(char& c:text){if(c>='A'&&c<='Z'){c+=32;}}return text;}
}
std::expected<Predicate,Error> Predicate::create(std::span<const std::string> tokens,bool inverse,Limits limits){
 try{
  const Limits cap;limits.ranges=std::min(limits.ranges,cap.ranges);limits.tokenBytes=std::min(limits.tokenBytes,cap.tokenBytes);
  if(tokens.empty()){return std::unexpected(Error::Invalid);}if(tokens.size()>limits.ranges||!limits.tokenBytes){return std::unexpected(Error::Bounds);}
  Predicate out;out.inverse_=inverse;std::vector<std::string> ranges;
  for(const auto& token:tokens){if(token.size()>limits.tokenBytes){return std::unexpected(Error::Bounds);}const auto name=lower(token);
   if(name=="public"){out.networks_|=publicNet;}else if(name=="i2p"){out.networks_|=i2p;}else if(name=="tor"){out.networks_|=tor;}else if(name=="all"){out.networks_|=publicNet|i2p|tor;}else if(name=="ipv4"){out.networks_|=ipv4;}else if(name=="ipv6"){out.networks_|=ipv6;}else{ranges.push_back(token);}
  }
  if(!ranges.empty()){auto parsed=Ipv4CidrSet::create(ranges,inverse,limits);if(!parsed){return std::unexpected(parsed.error());}out.ranges_=std::move(*parsed);}
  return out;
 }catch(const std::bad_alloc&){return std::unexpected(Error::Memory);}
}
runtime::Truth Predicate::matchNetwork(const Endpoint& endpoint)const noexcept{
 using runtime::Truth;
 std::optional<bool> hit;
 if(endpoint.network==Network::I2p){hit=(networks_&i2p)!=0;}else if(endpoint.network==Network::Tor){hit=(networks_&tor)!=0;}
 else if(endpoint.network==Network::Public){
  if(networks_&publicNet){hit=true;}else if(endpoint.family==Family::Ipv4){hit=(networks_&ipv4)!=0;}else if(endpoint.family==Family::Ipv6){hit=(networks_&ipv6)!=0;}else if(!(networks_&(ipv4|ipv6))){hit=false;}
 }
 return hit?(*hit!=inverse_?Truth::Yes:Truth::No):Truth::Unknown;
}
runtime::Truth Predicate::match(const Endpoint& endpoint)const noexcept{
 using runtime::Truth;
 const auto range=ranges_?ranges_->match(endpoint.network==Network::Public&&endpoint.family==Family::Ipv4?endpoint.ipv4:std::nullopt):Truth::Yes;
 const auto network=networks_?matchNetwork(endpoint):Truth::Yes;
 if(range==Truth::No||network==Truth::No){return Truth::No;}return range==Truth::Yes&&network==Truth::Yes?Truth::Yes:Truth::Unknown;
}
runtime::Truth Predicate::matchLivePeer(const Endpoint& endpoint)const noexcept{
 using runtime::Truth;
 const auto network=networks_?matchNetwork(endpoint):Truth::No;
 if(network==Truth::Yes){return Truth::Yes;}
 auto cidr=Truth::No;
 if(ranges_){
  if(endpoint.network==Network::Public&&endpoint.family==Family::Ipv4){
   // Original peersAdded skips the CIDR loop for an unspecified IPv4.
   if(!endpoint.ipv4||*endpoint.ipv4){cidr=ranges_->match(endpoint.ipv4);}
  }else if(endpoint.network==Network::Unknown||
      (endpoint.network==Network::Public&&endpoint.family==Family::Unknown)){
   cidr=Truth::Unknown;
  }
 }
 if(cidr==Truth::Yes){return Truth::Yes;}
 return cidr==Truth::Unknown||network==Truth::Unknown?Truth::Unknown:Truth::No;
}
runtime::Truth Predicate::match(std::optional<std::uint32_t> address)const noexcept{
 return match(address?Endpoint{Network::Public,Family::Ipv4,address}:Endpoint{});
}
}
