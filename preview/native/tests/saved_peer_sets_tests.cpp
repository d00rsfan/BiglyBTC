// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/saved_peer_sets.hpp>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
namespace b=biglybtc::core;namespace p=b::tags::peer_sets;
namespace {
void need(bool ok,const char* reason){if(!ok){throw std::runtime_error(reason);}}
std::string bytes(std::string_view s){return std::to_string(s.size())+":"+std::string(s);}
std::string key(std::string_view s){constexpr std::string_view alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";std::string out="speed.limit.handler.ipset_n.";unsigned value=0,bits=0;for(unsigned char c:s){value=(value<<8)|c;bits+=8;while(bits>=5){bits-=5;out+=alphabet[(value>>bits)&31];}}if(bits){out+=alphabet[(value<<(5-bits))&31];}return out;}
std::string dictionary(const std::map<std::string,std::string>& values){std::string out="d";for(const auto& [k,v]:values){out+=bytes(k)+v;}return out+"e";}
std::string integer(std::int64_t n){return "i"+std::to_string(n)+"e";}
void actualOriginalSavedSource(){
 std::ifstream file(SAVED_PEER_SETS_FIXTURE,std::ios::binary);auto fixture=b::Document::parse(std::string{std::istreambuf_iterator<char>(file),{}});need(bool(fixture),"Actual pinned source fixture");unsigned rows=0,known=0,unknown=0,ids=0;
 const std::set<std::string> refused{"case-9","case-13","case-14","case-16","case-17","case-18","case-19","case-20","case-21","case-22","case-23","case-24","case-25","case-29","case-30","case-33","case-34","case-35"};
 for(const auto& row:fixture->find("rows")->children){
  ++rows;std::map<std::string,std::string> values;values["speed.limit.handler.schedule.lines"]=*b::encodeValue(*row.find("lines"));const auto& supplied=row.find("ids")->children;for(std::size_t i=0;i<supplied.size();i+=2){values[key(supplied[i].bytes)]=*b::encodeValue(supplied[i+1]);}
  const auto before=dictionary(values);auto config=b::Document::parse(before);need(bool(config),"Generated original config");auto result=p::SavedPeerSets::read(*config);need(bool(result),"Generated source read");need(config->serialize()==before,"Read-only source changed config");const auto& actual=row.find("sets")->children;need(result->sets().size()==actual.size(),"Declaration name census differs");need(result->enabled()==(row.find("enabled")->integer!=0),"Saved enable differs");
  for(const auto& set:result->sets()){
   const b::Node* source=nullptr;for(const auto& candidate:actual){if(candidate.find("name")->bytes==set.name){source=&candidate;break;}}need(source,"Native invented source declaration");
   if(set.predicate){++known;need(set.name!="Generated"||!refused.contains(std::string(row.find("name")->bytes)),"Incomplete selectors became known predicates");const auto& ranges=source->find("ranges")->children;need(ranges.size()==set.predicate->ranges().size(),"Original reference/duplicate range count differs");for(std::size_t i=0;i<ranges.size();++i){need(ranges[i].children[0].integer==set.predicate->ranges()[i].first&&ranges[i].children[1].integer==set.predicate->ranges()[i].last,"Actual original saved ranges differ");}for(auto address:{0xcb007101U,0xc6336401U,0xcb007201U}){bool hit=false;for(const auto& range:ranges){hit=hit||(address>=range.children[0].integer&&address<=range.children[1].integer);}const bool inverse=source->find("inverse")->integer!=0;need(set.predicate->match(address)==(hit!=inverse?b::tags::runtime::Truth::Yes:b::tags::runtime::Truth::No),"Saved inverse/default/reference semantics differ");}}
   else{++unknown;need(refused.contains(std::string(row.find("name")->bytes)),"Supported saved source unexpectedly unknown");need(!result->complete(),"Unknown source reported complete census");}
   if(set.id){++ids;need(*set.id==source->find("id")->integer,"Saved Java Long narrowing/ID differs");}else{need(!result->complete(),"Missing/clashing saved ID reported complete");}
  }
  if(row.find("name")->bytes=="id--1"||row.find("name")->bytes=="clash"){for(const auto& set:result->sets()){need(!set.id,"Reader allocated or arbitrarily repaired saved IDs");}}
 }
 need(rows==43&&known>20&&unknown>10&&ids>25,"Actual saved-source coverage lost");std::cout<<"actual original saved peer sources rows="<<rows<<" known="<<known<<" unknown="<<unknown<<" identities="<<ids<<'\n';
}
void boundsAndMalformedRetainNoPrefix(){
 auto make=[](std::string encoded){return b::Document::parse(std::move(encoded));};auto config=make(dictionary({{"speed.limit.handler.schedule.lines","l"+bytes("peer_set Generated=203.0.113.0/24")+"e"},{key("Generated"),integer(17)}}));need(bool(config),"Generated limits fixture");const auto before=config->serialize();
 p::SavedLimits limits;limits.sets=0;auto value=p::SavedPeerSets::read(*config,limits);need(!value&&value.error()==p::Error::Bounds,"Set overflow admitted partial source");limits=p::SavedLimits{};limits.ranges=0;need(!p::SavedPeerSets::read(*config,limits),"Range overflow admitted prefix");limits=p::SavedLimits{};limits.totalBytes=1;need(!p::SavedPeerSets::read(*config,limits),"Input quota ignored");limits=p::SavedLimits{};limits.nameBytes=2;need(!p::SavedPeerSets::read(*config,limits),"Name quota ignored");
 auto malformed=make(dictionary({{"speed.limit.handler.schedule.lines","li1ee"}}));need(!p::SavedPeerSets::read(*malformed),"Nontext lines became trusted");malformed=make(dictionary({{"speed.limit.handler.schedule.lines","l"+bytes(std::string("peer_set ")+char(0xff)+"=203.0.113.0/24")+"e"}}));need(!p::SavedPeerSets::read(*malformed),"Invalid UTF8 replaced silently");need(config->serialize()==before,"Refused reader changed source");
 auto snapshot=p::SavedPeerSets::read(*config);need(bool(snapshot),"Owning snapshot");config->setInteger(key("Generated"),99);need(snapshot->sets()[0].id==17&&snapshot->sets()[0].name=="Generated","Snapshot borrowed edited source");
 std::map<std::string,std::string> data{{"speed.limit.handler.schedule.lines","l"+bytes("peer_set Generated=203.0.113.0/24")+bytes("peer_set Base=Generated 198.51.100.0/24")+"e"},{key("Generated"),integer(17)},{key("Base"),integer(18)}};auto chained=make(dictionary(data));limits=p::SavedLimits{};limits.ranges=1;need(!p::SavedPeerSets::read(*chained,limits),"Copied range quota produced partial referenced source");
}
}
int main(){try{actualOriginalSavedSource();boundsAndMalformedRetainNoPrefix();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}std::cout<<"PASS saved peer declarations: actual parser comparisons, unknown selectors/IDs, immutable source and whole-result bounds\n";}
