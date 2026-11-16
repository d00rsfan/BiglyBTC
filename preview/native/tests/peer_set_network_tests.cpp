// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/peer_set_predicate.hpp>
#include <biglybtc/bencode.hpp>
#include <arpa/inet.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <type_traits>
namespace b=biglybtc::core;namespace p=b::tags::peer_sets;namespace r=b::tags::runtime;
namespace {
void need(bool value,const char* message){if(!value){throw std::runtime_error(message);}}
std::vector<std::string> tokens(std::string_view text){std::vector<std::string> out;for(std::size_t at=0;;){auto end=text.find(';',at);out.emplace_back(text.substr(at,end==text.npos?text.npos:end-at));if(end==text.npos){return out;}at=end+1;}}
p::Endpoint endpoint(std::string_view text){if(text.ends_with(".i2p")){return {p::Network::I2p,p::Family::Unknown,{}};}if(text.ends_with(".onion")){return {p::Network::Tor,p::Family::Unknown,{}};}in_addr address{};std::string name(text);if(inet_pton(AF_INET,name.c_str(),&address)==1){return {p::Network::Public,p::Family::Ipv4,ntohl(address.s_addr)};}in6_addr address6{};need(inet_pton(AF_INET6,name.c_str(),&address6)==1,"Generated numeric peer endpoint");return {p::Network::Public,p::Family::Ipv6,{}};}
void actualOriginalNetworkSelectors(){std::ifstream file(PEER_SET_NETWORKS_FIXTURE,std::ios::binary);auto source=b::Document::parse(std::string{std::istreambuf_iterator<char>(file),{}});need(bool(source),"Actual network fixture");unsigned compared=0,unknown=0;
 for(const auto& row:source->find("rows")->children){auto input=tokens(row.find("definition")->bytes);auto predicate=p::Predicate::create(input,row.find("inverse")->integer!=0);need(predicate&&row.find("accepted")->integer,"Actual accepted selector refused");auto peer=endpoint(row.find("address")->bytes);const auto result=predicate->match(peer);
  if(result==r::Truth::Unknown){need(!predicate->ranges().empty()&&(peer.family!=p::Family::Ipv4||!peer.ipv4||!*peer.ipv4),"Known supported network source unexpectedly unavailable");++unknown;}
  else{need(result==(row.find("match")->integer?r::Truth::Yes:r::Truth::No),"Actual Java network/range inverse composition differs");++compared;}
 }
 need(compared+unknown==160&&compared>=128&&unknown>0,"Original network fixture coverage lost");std::cout<<"actual network selectors rows=160 compared="<<compared<<" conservative_unknown="<<unknown<<'\n';
}
void unavailableUnsupportedAndOwnership(){static_assert(!std::is_default_constructible_v<p::Predicate>);std::vector<std::string> names{"IPv6"};auto predicate=p::Predicate::create(names);need(bool(predicate),"Supported network selector");names[0]="IPv4";need(predicate->match(endpoint("2001:db8::1"))==r::Truth::Yes&&predicate->match(endpoint("203.0.113.1"))==r::Truth::No,"Network selector borrowed caller storage");need(predicate->match(p::Endpoint{})==r::Truth::Unknown,"Unknown network became negative");auto inverse=p::Predicate::create(std::vector<std::string>{"IPv6"},true);need(inverse&&inverse->match(p::Endpoint{})==r::Truth::Unknown,"Inverse unknown source became positive");
 for(const auto* bad:{"LAN","WAN","US","XX","2001:db8::/32","unknown","203.0.113.0/33"}){need(!p::Predicate::create(std::vector<std::string>{bad}),"Unsupported selector ignored");}need(!p::Predicate::create({}),"Empty selectors became universal");p::Limits lower;lower.ranges=1;need(!p::Predicate::create(std::vector<std::string>{"IPv4","IPv6"},false,lower),"Network token quota ignored");lower={};lower.tokenBytes=2;need(!p::Predicate::create(std::vector<std::string>{"Public"},false,lower),"Network text quota ignored");auto mixed=p::Predicate::create(std::vector<std::string>{"203.0.113.0/24","IPv6"});need(mixed&&mixed->match(endpoint("2001:db8::1"))==r::Truth::Unknown,"IPv6 bypassed configured CIDR");
}
}
int main(){try{actualOriginalNetworkSelectors();unavailableUnsupportedAndOwnership();}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}std::cout<<"PASS160 actual peer network selectors: independent inverse dimensions, unknown cross-family CIDRs, immutable ownership and quotas\n";}
