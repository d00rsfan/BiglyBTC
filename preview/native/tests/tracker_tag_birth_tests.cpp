// SPDX-License-Identifier: GPL-2.0-or-later
#include "../app/tracker_tag_births.hpp"
#include <iostream>
#include <stdexcept>
using Queue=biglybtc::app::detail::TrackerTagBirths;
namespace {
void check(bool value,const char* why){if(!value){throw std::runtime_error(why);}}
biglybtc::core::TorrentHash hash(unsigned id){std::string bytes(20,'Q');bytes[0]=static_cast<char>(id);auto result=biglybtc::core::TorrentHash::parse(bytes);check(bool(result),"Generated typed hash");return *result;}
Queue::Cohort rules(Queue& queue,int id=1){auto result=queue.cohort({{id,{"example.test"}},{id+1,{"example.test:80"}}});check(bool(result),"Generated immutable cohort");return *result;}
Queue::Request next(Queue& queue){auto result=queue.next();check(result&&result->has_value(),"Expected queued birth request");return **result;}
void sourceVersionsReplayProgressAndDeferral(){
 Queue queue;auto cohort=rules(queue);auto same=rules(queue);check(same==cohort,"Equivalent current definitions copied immutable cohorts");
 check(queue.record(hash(1),100,1,cohort)==Queue::Recorded::Added,"Genuine value event was not queued");auto first=next(queue);auto repeated=next(queue);check(first.token==repeated.token&&first.index==0&&first.rules==cohort,"Repeated request lost the pending sequence");
 auto forged=first;++forged.index;check(!queue.accept(forged),"Wrong rule cursor acknowledged an effect");forged=first;++forged.addedTime;check(!queue.withdraw(forged),"Wrong durable saved time withdrew a different birth");
 check(queue.record(hash(2),200,2,cohort)==Queue::Recorded::Added,"Second actual value event");check(bool(queue.defer(first)),"Deferred unpublished request");check(!queue.accept(first),"Deferred sequence replay accepted");auto second=next(queue);check(second.hash==hash(2),"An unpublished unavailable target blocked every later birth");
 check(bool(queue.accept(second)),"First effect ack");check(!queue.accept(second),"Successful ack replay accepted");auto secondTail=next(queue);check(secondTail.index==1&&secondTail.event==2,"Later tag cursor changed event");check(bool(queue.withdraw(secondTail)),"Actual removed target withdrawal");
 auto old=next(queue);check(old.hash==hash(1),"Deferred birth was lost");check(queue.record(hash(1),100,3,cohort)==Queue::Recorded::Added&&!queue.current(old),"A distinct genuine same-millisecond creation reused an old request");old=next(queue);check(queue.record(hash(1),100,3,cohort)==Queue::Recorded::Duplicate&&queue.current(old),"Replay of the actual same event invalidated its current request");
 check(queue.record(hash(1),999,4,cohort)==Queue::Recorded::Added,"Replacement saved birth not retained");check(!queue.accept(old)&&queue.size()==1,"Old saved-time generation survived replacement or duplicate queue entry");auto fresh=next(queue);check(fresh.addedTime==999&&fresh.event==4&&fresh.index==0,"Replacement inherited the old request cursor");
 Queue foreign;auto foreignRules=rules(foreign);check(!queue.record(hash(3),300,5,foreignRules),"Another queue's source cohort was accepted");check(!queue.record(hash(3),300,0,cohort),"Missing actual event sequence was accepted");
 check(bool(queue.accept(fresh)),"Replacement first ack");check(bool(queue.accept(next(queue))),"Replacement last ack");check(queue.size()==0&&queue.next()->has_value()==false,"Completed birth retained work");
 check(queue.record(hash(8),1000,4,cohort)==Queue::Recorded::Duplicate,"Old event number admitted another hash");
 auto empty=queue.cohort({});check(bool(empty)&&queue.record(hash(9),0,5,*empty)==Queue::Recorded::NoRules&&queue.size()==0,"Empty cohort fabricated pending tag work");queue.clear();check(!queue.current(fresh)&&!queue.record(hash(1),999,6,cohort),"Clear retained a source grant/request");
}
void capacityAndSharedCohortVersions(){
 Queue queue;auto cohort=rules(queue);for(unsigned i=0;i<64;++i){check(queue.record(hash(i),i+100,i+1,cohort)==Queue::Recorded::Added,"Bounded birth capacity lost an input");}
 auto overflow=queue.record(hash(100),1000,65,cohort);check(!overflow&&overflow.error()==Queue::Error::Bounds&&queue.size()==64,"Overflow truncated or replaced valid queued events");check(bool(queue.withdraw(next(queue))),"Capacity withdrawal");check(queue.record(hash(100),1000,65,cohort)==Queue::Recorded::Added&&queue.size()==64,"Failed admission consumed its real event number");
 Queue versions;std::vector<Queue::Cohort> keep;for(int i=0;i<8;++i){keep.push_back(rules(versions,i*2));}auto existing=rules(versions,0);check(existing==keep.front(),"Older retained equal cohort needlessly exhausted source versions");auto ninth=versions.cohort({{100,{"example.test"}}});check(!ninth&&ninth.error()==Queue::Error::Bounds,"Retained source versions exceeded the hard bound");existing.reset();keep[1].reset();ninth=versions.cohort({{100,{"example.test"}}});check(bool(ninth),"Released source version did not recover capacity");
 Queue invalid;check(!invalid.cohort({{1,{"a"}},{1,{"b"}}}),"Duplicate tag IDs became a cohort");check(!invalid.cohort({{-1,{"a"}}}),"Negative tag ID accepted");check(!invalid.cohort({{1,{}}}),"Missing matching property invented a rule");check(!invalid.cohort({{1,{std::string(2049,'a')}}}),"Oversized property copied");check(!invalid.cohort({{1,{std::string(1,char(0xff))}}}),"Unknown name encoding became a rule");
 Queue::Rules huge;for(int i=0;i<129;++i){huge.push_back({i,{std::string(2048,'a')}});}auto bounded=invalid.cohort(std::move(huge));check(!bounded&&bounded.error()==Queue::Error::Bounds,"Aggregate cohort text was truncated into a valid plan");
}
}
int main(){try{sourceVersionsReplayProgressAndDeferral();capacityAndSharedCohortVersions();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}std::cout<<"PASS tracker birth value queue: immutable cohorts, saved-time generations, replay, deferral, close,64 events,8 versions and refusal boundaries\n";return 0;}
