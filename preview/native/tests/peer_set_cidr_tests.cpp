// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/peer_set_cidrs.hpp>
#include <biglybtc/bencode.hpp>
#include <arpa/inet.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace b=biglybtc::core;namespace p=b::tags::peer_sets;namespace r=b::tags::runtime;
namespace {
void require(bool ok,const char* why){if(!ok){throw std::runtime_error(why);}}
std::optional<std::uint32_t> numeric(std::string_view text){in_addr address{};const std::string name(text);if(inet_pton(AF_INET,name.c_str(),&address)!=1){return {};}return ntohl(address.s_addr);}
std::vector<std::string> tokens(std::string_view value){std::vector<std::string> out;for(std::size_t at=0;;){const auto end=value.find(';',at);out.emplace_back(value.substr(at,end==value.npos?value.npos:end-at));if(end==value.npos){return out;}at=end+1;}}
void actualOriginalRangesAndMatches(){
 std::ifstream file(PEER_SET_CIDR_FIXTURE,std::ios::binary);const std::string bytes{std::istreambuf_iterator<char>(file),{}};auto source=b::Document::parse(bytes);require(bool(source),"Pinned actual CIDR source fixture");unsigned cases=0,known=0,unknown=0,context=0,refused=0;
 for(const auto& row:source->find("rows")->children){++cases;auto input=tokens(row.find("token")->bytes);auto predicate=p::Ipv4CidrSet::create(input,row.find("inverse")->integer!=0);
  if(!predicate){require(predicate.error()==p::Error::Unsupported,"Unexpected valid-source resource refusal");++refused;continue;}
  require(row.find("accepted")->integer==1,"Native accepted a source-refused CIDR");const auto& ranges=row.find("ranges")->children;require(predicate->ranges().size()==ranges.size(),"Range declaration order/count differs");for(std::size_t i=0;i<ranges.size();++i){require(predicate->ranges()[i].first==ranges[i].children[0].integer&&predicate->ranges()[i].last==ranges[i].children[1].integer,"Native mask/range differs from actual source");}
  if(row.find("needs_download")->integer){++context;continue;}auto address=numeric(row.find("address")->bytes);const auto result=predicate->match(address);
  if(!address||!*address){require(result==r::Truth::Unknown,"Other-family/unresolved/zero source manufactured a positive or negative");++unknown;}
  else{require(result==(row.find("match")->integer?r::Truth::Yes:r::Truth::No),"Known IPv4 inverse/union match differs from actual Java");++known;}
 }require(cases==252&&known==36&&unknown==48&&context==84&&refused==84,"Actual source/subset census changed");
}
void immutableBoundsAndUnsupportedSyntax(){
 std::vector<std::string> input{"203.0.113.0/24"};auto value=p::Ipv4CidrSet::create(input);require(bool(value),"Generated subset");input[0]="198.51.100.0/24";require(value->match(numeric("203.0.113.1"))==r::Truth::Yes,"Predicate retained borrowed mutable input");
 for(const auto& bad:{"203.0.113.1/33","203.0.113.1/-1","203.0.113.256/24","203.0.113/24","example.invalid/24","203.0.113.1/4294967296","2001:db8::/32"}){const std::vector<std::string> one{bad};require(!p::Ipv4CidrSet::create(one),"Unsupported or malformed selector admitted");}
 require(!p::Ipv4CidrSet::create({}),"Empty input became a universal match");const std::vector<std::string> two{"203.0.113.0/24","203.0.114.0/24"};p::Limits lower;lower.ranges=1;require(!p::Ipv4CidrSet::create(two,false,lower),"Lower-only caller range cap ignored");lower.ranges=999;require(!p::Ipv4CidrSet::create(std::vector<std::string>(257,"203.0.113.0/24"),false,lower),"Maximum cap silently widened/truncated");lower.tokenBytes=2;require(!p::Ipv4CidrSet::create(two,false,lower),"Lower token bound ignored");
}
}
int main(){try{actualOriginalRangesAndMatches();immutableBoundsAndUnsupportedSyntax();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}std::cout<<"PASS252 actual peer CIDR source rows:36 IPv4 inverse/union comparisons,48 unknown endpoints,84 context-only rows,84 refused shapes;immutable bounds\n";return 0;}
