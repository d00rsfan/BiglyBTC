#include "biglybtc/queue_observations.hpp"
#include <array>
#include <charconv>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace biglybtc::core;
namespace {
std::int64_t number(const std::string& s){std::int64_t v=0;const auto r=std::from_chars(s.data(),s.data()+s.size(),v);if(r.ec!=std::errc{}||r.ptr!=s.data()+s.size()){throw std::runtime_error("Bad tape integer");}return v;}
void output(const std::string& id,const char* field,std::int64_t value){std::cout<<"OBS\t"<<id<<'\t'<<field<<'\t'<<value<<'\n';}
}
int main(){try{
  unsigned cases=0;
  for(std::string line;std::getline(std::cin,line);){
    if(line.empty()||line[0]=='#'){continue;}if(line.size()>8192||++cases>128){throw std::runtime_error("Tape bound");}
    std::vector<std::string> f;std::stringstream row(line);for(std::string field;std::getline(row,field,'\t');){f.push_back(std::move(field));}
    if(f.size()<3){throw std::runtime_error("Tape arity");}
    if(f[0]=="T"){
      if(f.size()!=9){throw std::runtime_error("Original response arity");}
      std::optional<QueueOriginalScrapeContent> content;
      if(number(f[2])){content=QueueOriginalScrapeContent{static_cast<std::int32_t>(number(f[3])),static_cast<std::int32_t>(number(f[4])),number(f[5])};}
      const auto value=sourceQueueOriginalScrapeResponse(content);
      // f[6] is original transport status. It never determines isValid().
      const auto own=number(f[7])?std::optional{number(f[8])}:std::nullopt;
      output(f[1],"responseType",value.type==QueueOriginalScrapeType::Success?1:2);
      output(f[1],"seeds",content?content->seeds:-1);output(f[1],"peers",content?content->peers:-1);
      output(f[1],"start",content?content->startedWallMillis:-1);
      output(f[1],"eligible",sourceQueueQueuedScrapeEligible(value,own));continue;
    }
    if(f[0]=="I"){
      if(f.size()!=6){throw std::runtime_error("Idle arity");}
      auto last=static_cast<std::int32_t>(number(f[2]));
      for(std::size_t i=3;i<6;++i){const auto field="idle"+std::to_string(i-3);output(f[1],field.c_str(),sourceQueuePeerIdleSeconds(number(f[i]),last));}
      continue;
    }
    if(f[0]=="R"){
      if(f.size()!=6){throw std::runtime_error("Ratio arity");}
      output(f[1],"ratio",sourceQueueShareRatio(number(f[2]),number(f[3]),number(f[4]),number(f[5])));continue;
    }
    const bool seed=number(f[2])==5;QueueOwnerObservations source(1);
    std::vector<std::array<unsigned char,32>> keys;
    if(f[0]=="A"){
      if(f.size()<6||(f.size()-3)%3){throw std::runtime_error("Aggregate arity");}
      for(std::size_t i=3;i<f.size();i+=3){std::array<unsigned char,32> key{};key[0]=static_cast<unsigned char>(keys.size()+1);keys.push_back(key);}
    }else{keys.resize(1);keys[0][0]=1;}
    if(!source.trackerOrder(keys)){throw std::runtime_error("Tracker order");}
    if(f[0]=="C"){
      if(f.size()!=5){throw std::runtime_error("Cache arity");}
      if(f[3]!="-"){if(!source.restoreAggregate(f[3])){throw std::runtime_error("Cache invalid");}}
      else{source.restoreLegacy(number(f[4]));}
    }
    std::uint64_t seq=0;
    const auto event=[&](QueueObservationKind kind,std::int64_t wall,int seeds=-1,int peers=-1,std::int64_t requestWall=0,std::array<unsigned char,32> key={}){
      QueueObservation e;e.sequence=++seq;e.kind=kind;e.observedNanos=(1000+static_cast<std::int64_t>(seq))*1000000;e.observedWallMillis=wall;e.seeds=seeds;e.peers=peers;e.tracker=key;
      if(static_cast<unsigned>(kind)>=6){e.protocol=1;e.request=seq;e.requestStartedNanos=1;e.requestStartedWallMillis=requestWall;}
      if(!source.observe(e)){throw std::runtime_error("Native production reducer rejected tape");}
    };
    event(QueueObservationKind::StartedDownload,100000);
    if(seed){event(QueueObservationKind::Finished,110000);}
    if(f[0]=="S"){
      if(f.size()!=9){throw std::runtime_error("Swarm arity");}
      if(number(f[6])){event(number(f[6])==1?QueueObservationKind::Announce:QueueObservationKind::AnnounceFailed,120000,static_cast<int>(number(f[7])),static_cast<int>(number(f[8])),119000,keys[0]);}
      event(QueueObservationKind::Scrape,120001,static_cast<int>(number(f[4])),static_cast<int>(number(f[5])),number(f[3]),keys[0]);
    }else if(f[0]=="A"){
      // Arrival is deliberately reverse of configured source order.
      for(std::size_t i=keys.size();i>0;--i){const auto j=3+(i-1)*3;event(QueueObservationKind::Scrape,120001,static_cast<int>(number(f[j])),static_cast<int>(number(f[j+1])),number(f[j+2]),keys[i-1]);}
    }else if(f[0]!="C"){throw std::runtime_error("Unknown command");}
    const auto observed=source.facts(3000);if(!observed.swarm){throw std::runtime_error("Unknown native source");}
    if(f[0]=="C"||f[0]=="A"){output(f[1],"startupSuccess",observed.swarm->startupSuccess);}
    output(f[1],"peers",observed.swarm->peers);output(f[1],"seeds",observed.swarm->seeds);
  }
  if(!cases){throw std::runtime_error("Empty tape");}std::cout<<"PASS_QUEUE_FACTS_NATIVE\t"<<cases<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
