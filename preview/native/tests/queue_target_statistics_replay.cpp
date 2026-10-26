#include "biglybtc/queue_target_statistics.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace biglybtc::core;
int main(){try{
  QueueTargetEta subject;std::string name,line;std::uint64_t admission=0;
  while(std::getline(std::cin,line)){
    if(line.empty()||line[0]=='#'){continue;}
    std::vector<std::string> fields;std::istringstream split(line);std::string item;
    while(std::getline(split,item,'\t')){fields.push_back(item);}
    const auto integer=[&](std::size_t index){return std::stoll(fields.at(index));};
    if(fields.at(0)=="RESET"&&fields.size()==5){
      subject=QueueTargetEta{};name=fields[1];++admission;
      const auto baseline=subject.observe({admission,1,integer(3),integer(4),{},{},static_cast<int>(integer(2))});
      if(!baseline){throw std::runtime_error("invalid baseline");}continue;
    }
    if(fields.at(0)!="OBS"||fields.size()!=8||!admission){throw std::runtime_error("invalid admitted tape");}
    const auto completed=integer(6);
    const auto result=subject.observe({admission,static_cast<std::uint64_t>(integer(1)),integer(2),integer(3),
        std::max(integer(4),integer(5)),completed<0?std::nullopt:std::optional<std::int64_t>{completed},static_cast<int>(integer(7))});
    if(!result||!result->etaSeconds||!result->smoothedPayloadReceiveBytesPerSecond){throw std::runtime_error("admitted observation unavailable");}
    std::cout<<"OBS\t"<<name<<'\t'<<fields[1]<<"\tsmooth\t"<<*result->smoothedPayloadReceiveBytesPerSecond
        <<"\teta\t"<<*result->etaSeconds<<"\tpolicy\t"<<result->smoothing.windowSeconds<<','<<result->smoothing.intervalSeconds<<'\n';
  }
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
