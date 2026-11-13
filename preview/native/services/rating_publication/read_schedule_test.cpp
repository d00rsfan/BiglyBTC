// SPDX-License-Identifier: GPL-2.0-only
#include "read_schedule.hpp"
#include <array>
#include <cstdlib>
#include <iostream>
using namespace biglybtc::plugins::rating::publication;
void check(bool value){if(!value){std::cerr<<"Read schedule assertion failed\n";std::exit(1);}}
int main(){
 constexpr std::int64_t h=3600000;ReadHint complete{"complete",true,true,false,0,0},partial{"partial",false,false,false,{},1},scored{"scored",false,false,true,{},0};
 check(ReadSchedule::period(complete,30*h)==2*h);complete.scrapeSeeds.reset();check(ReadSchedule::period(complete,30*h)==8*h);check(ReadSchedule::period(partial,25*h+2)==4*h);check(ReadSchedule::period(partial,24*h+1)==h);
 ReadSchedule schedule;const std::array hints{complete,partial,scored};check(bool(schedule.update(hints)));check(schedule.take(30*h)==std::optional<std::string>{"partial"});check(!schedule.take(30*h));schedule.complete(30*h);check(!schedule.take(30*h+19999));check(schedule.take(30*h+20000)==std::optional<std::string>{"complete"});schedule.complete(30*h+20000);check(bool(schedule.request("scored",true)));check(schedule.take(30*h+20001)==std::optional<std::string>{"scored"});schedule.complete(30*h+20001);
 auto due=30*h+20001+h;check(schedule.take(due)==std::optional<std::string>{"scored"});schedule.complete(due);check(!schedule.request("missing",true));const std::array duplicate{partial,partial};check(!schedule.update(duplicate));check(bool(schedule.update({})));check(!schedule.take(100*h));schedule.reset();std::cout<<"Read periods, cold selection, priority, wakeup, delay and removal passed\n";
}
