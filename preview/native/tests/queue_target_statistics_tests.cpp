#include "biglybtc/queue_target_statistics.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace biglybtc::core;
namespace {
void check(bool value,const char* reason){if(!value){throw std::runtime_error(reason);}}
QueueTargetStatistics take(std::expected<QueueTargetStatistics,QueueTargetStatisticsError> result){
  if(!result){throw std::runtime_error("target observation rejected");}return *result;
}
void sourceEtaBoundaries(){
  check(sourceSmoothedEta(1,0)==SourceEtaInfinity,"actual Java int-expression infinity");
  check(sourceSmoothedEta(1,2)==1,"incomplete ETA cannot flicker to finished");
  check(sourceSmoothedEta(601,30)==20,"integer division");
  check(sourceSmoothedEta(0,0,1999)==0&&sourceSmoothedEta(0,0,2000)==-2,"observed completion elapsed rounding");
  check(!sourceSmoothedEta(0,0),"missing completion lifetime is not a guessed zero");
  check(!sourceSmoothedEta(-1,1)&&!sourceSmoothedEta(1,-1),"negative primitives reject");
  check(sourceSmoothedEta(std::numeric_limits<std::int64_t>::max(),1)==std::numeric_limits<std::int64_t>::max(),"full signed remaining no float loss");
}
void payloadWindowAndSourceCache(){
  QueueTargetEta subject;
  auto a=take(subject.observe({17,1,100000,1000,600,{},10}));check(!a.etaSeconds&&!a.smoothedPayloadReceiveBytesPerSecond,"first prefix is only a baseline");
  a=take(subject.observe({17,2,101000,1300,600,{},10}));check(a.etaSeconds==SourceEtaInfinity&&a.smoothedPayloadReceiveBytesPerSecond==0,"actual zero window differs from unavailable");
  a=take(subject.observe({17,3,102000,1600,600,{},10}));check(a.smoothedPayloadReceiveBytesPerSecond==30&&a.etaSeconds==20,"fixed original window includes prior bucket only");
  QueueTargetEta cached;take(cached.observe({19,1,100000,0,1,{},30}));
  a=take(cached.observe({19,2,101000,0,1,{},30}));check(a.etaSeconds==SourceEtaInfinity,"zero-rate original ETA");
  a=take(cached.observe({19,3,101900,0,0,2000,30}));check(a.etaSeconds==SourceEtaInfinity,"strict cache boundary900ms retains original result");
  a=take(cached.observe({19,4,101901,0,0,2000,30}));check(a.etaSeconds==-2,"cache recomputes only after900ms with observed completion");
}
void configuredIntervalChangesOnOldGate(){
  QueueTargetEta subject;take(subject.observe({23,1,100000,0,1200,{},120}));
  auto a=take(subject.observe({23,2,101000,600,1200,{},10}));check(a.smoothing.windowSeconds==120,"old interval governs configuration change");
  a=take(subject.observe({23,3,102000,1200,1200,{},10}));check(a.smoothing.windowSeconds==10&&a.smoothedPayloadReceiveBytesPerSecond==0,"new window keeps actual pending delta");
  a=take(subject.observe({23,4,103000,1200,1200,{},10}));check(a.smoothedPayloadReceiveBytesPerSecond==120&&a.etaSeconds==10,"configured original receive smoothing drives target ETA");
}
void gapsAndAdmissionFences(){
  QueueTargetEta subject;take(subject.observe({31,1,100000,100,1000,{},30}));
  const QueueTargetSample accepted{31,2,101000,200,1000,{},30};const auto known=take(subject.observe(accepted));
  const auto repeated=take(subject.observe(accepted));check(repeated.sequence==known.sequence&&repeated.etaSeconds==known.etaSeconds,"same actual sample is never charged twice");
  auto invalid=accepted;invalid.remainingBytes=999;check(!subject.observe(invalid),"same identity with different bytes rejected");
  invalid.sequence=3;invalid.payloadReceived=199;check(!subject.observe(invalid),"regression is not an idle zero");
  invalid=accepted;invalid.sequence=3;invalid.admission=32;check(!subject.observe(invalid),"admission cannot inherit a rate window");
  auto a=take(subject.observe({31,3,102000,{},{},{},30}));check(!a.etaSeconds&&!a.smoothedPayloadReceiveBytesPerSecond,"missing actual status is a gap");
  a=take(subject.observe({31,4,103000,9000000,1000,{},30}));check(!a.etaSeconds&&!a.smoothedPayloadReceiveBytesPerSecond,"gap rebaseline never turns old prefix into burst");
  a=take(subject.observe({31,5,104000,9000000,1000,{},30}));check(a.etaSeconds==SourceEtaInfinity&&a.smoothedPayloadReceiveBytesPerSecond==0,"new observed idle is known zero");
  invalid={31,6,103999,9000000,1000,{},30};check(!subject.observe(invalid),"clock regression fail closed");
}
void authoritativeStaleTimingOnly(){
  QueueStaleSeedTiming subject;
  auto value=subject.observe({60000,true,true,true,true,false});check(value&&*value==0,"strict first stale refresh boundary");
  value=subject.observe({60001,true,true,true,false,false});check(value&&*value==1,"source last-refresh epoch, not manufactured stale duration");
  value=subject.observe({120001,true,true,true,false,false});check(value&&*value==1,"strict >60000 boundary");
  value=subject.observe({120002,true,true,true,false,false});check(value&&*value==2,"whole source refresh increments");
  subject.clearAppliedOffset();value=subject.observe({120003,true,true,true,false,false});check(value&&*value==0,"source rank feedback clears applied offset");
  value=subject.observe({180003,false,true,true,false,false});check(value&&!value->has_value(),"missing activity provider is not zero stale rank");
  value=subject.observe({180004,true,true,true,false,false});check(value&&!value->has_value(),"unobserved inactive interval cannot regain authority");
  value=subject.observe({180005,true,true,true,false,true});check(value&&*value==0,"actual active transition resets source stale state");
  check(!subject.observe({180004,true,true,true,true,false}),"stale clock rejected");
}
}
int main(){try{sourceEtaBoundaries();payloadWindowAndSourceCache();configuredIntervalChangesOnOldGate();gapsAndAdmissionFences();authoritativeStaleTimingOnly();
  std::cout<<"queue target statistics: five focused families passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
