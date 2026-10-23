#include "biglybtc/country_statistics.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace biglybtc::core;
namespace {
std::size_t cases=0,assertions=0;
void check(bool value,const char* message){++assertions;if(!value){throw std::runtime_error(message);}}
template<class F>void run(const char* name,F body){try{body();++cases;}catch(const std::exception& e){throw std::runtime_error(std::string(name)+": "+e.what());}}
PeerNumericEndpoint endpoint(){PeerNumericEndpoint p;p.family=PeerAddressFamily::V4;p.address={127,0,0,1};p.port=1234;return p;}
std::uint64_t add(PeerPayloadLedger& ledger){const auto token=ledger.attach(1,endpoint(),PeerNetworkClass::Local,0);check(token!=0,"attach failed");return token;}
PeerPayloadSnapshot snapshot(const PeerPayloadLedger& ledger){auto s=ledger.snapshot();check(s.has_value(),"snapshot unavailable");return *s;}
void observe(PeerPayloadLedger& ledger,std::uint64_t token,std::int64_t time,std::int64_t received,std::int64_t sent,bool disconnect=false){ledger.observe(token,time,PeerPayloadSample{received,sent,7,3},disconnect);}
CountryStatistics project(const PeerPayloadSnapshot& ledger){auto p=projectCountryStatistics(ledger,{},false);check(bool(p),"projection rejected");check(validCountryStatistics(*p),"projection invalid");return *p;}
}
int main(){try{
  run("restored-first-raw-totals-never-count",[]{
    PeerPayloadLedger l({1,2,3});const auto t=add(l);observe(l,t,1,1000000,2000000);const auto first=snapshot(l);
    check(first.records[0].observedReceivedBytes==0&&first.records[0].observedSentBytes==0,"restored amounts counted");
    observe(l,t,2,1000040,2000017);const auto next=snapshot(l);
    check(next.records[0].observedReceivedBytes==40&&next.records[0].observedSentBytes==17,"valid differences wrong");
    check(first.records[0].observedReceivedBytes==0,"owned snapshot changed");
  });
  run("before-first-sample-is-unknown-not-zero",[]{
    PeerPayloadLedger l({1,2,3});const auto t=add(l);l.observe(t,1,std::nullopt);
    check(!snapshot(l).records[0].observedReceivedBytes,"missing sample became zero");
    observe(l,t,2,-1,7);check(!snapshot(l).records[0].observedReceivedBytes,"invalid first sample became zero");
    observe(l,t,3,9999,8888);check(snapshot(l).records[0].observedReceivedBytes==0,"first good sample invented historical bytes");
  });
  run("gaps-preserve-prefix-and-rebaseline-next-valid",[]{
    PeerPayloadLedger l({1,2,3});const auto t=add(l);observe(l,t,1,100,200);observe(l,t,2,150,250);l.observe(t,3,std::nullopt);
    check(snapshot(l).records[0].observedReceivedBytes==50,"gap erased known prefix");
    observe(l,t,4,1000,2000);check(snapshot(l).records[0].observedReceivedBytes==50,"unknown interval counted");
    observe(l,t,5,1003,2010);const auto s=snapshot(l);
    check(s.records[0].observedReceivedBytes==53&&s.records[0].observedSentBytes==60,"new valid interval not resumed");
    check(hasPeerPayloadIssue(s.issues,PeerPayloadIssue::SampleUnavailable),"gap uncertainty hidden");
  });
  run("invalid-clock-is-a-prefix-gap",[]{
    PeerPayloadLedger l({1,2,3});const auto t=add(l);observe(l,t,1,100,100);observe(l,t,3,120,130);observe(l,t,2,999,999);
    observe(l,t,4,1000,2000);check(snapshot(l).records[0].observedReceivedBytes==20,"clock gap bridged");
    observe(l,t,5,1004,2001);check(snapshot(l).records[0].observedReceivedBytes==24,"valid post-clock interval lost");
  });
  run("counter-regression-cannot-add-or-reset-prefix",[]{
    PeerPayloadLedger l({1,2,3});const auto t=add(l);observe(l,t,1,500,500);observe(l,t,2,550,550);observe(l,t,3,10,10);
    auto s=snapshot(l);check(s.records[0].lastObserved->received==550&&s.records[0].observedReceivedBytes==50,"regression adopted");
    observe(l,t,4,600,600);observe(l,t,5,605,605);s=snapshot(l);check(s.records[0].observedReceivedBytes==55,"regression gap counted");
  });
  run("global-loss-rebaselines-each-record",[]{
    PeerPayloadLedger l({1,2,3});const auto a=add(l),b=add(l);observe(l,a,1,100,100);observe(l,b,1,200,200);
    observe(l,a,2,110,110);observe(l,b,2,210,210);l.noteLoss(PeerPayloadIssue::Contention);
    observe(l,a,3,500,500);observe(l,b,3,600,600);observe(l,a,4,502,503);const auto s=snapshot(l);
    check(s.records[0].observedReceivedBytes==12&&s.records[1].observedReceivedBytes==10,"global loss bridged another peer");
  });
  run("known-zero-increment-is-still-known",[]{
    PeerPayloadLedger l({1,2,3});const auto t=add(l);observe(l,t,1,5000,6000);observe(l,t,2,5000,6000);
    const auto p=project(snapshot(l));check(p.rows[0].observedDownloadBytes==0&&p.rows[0].downloadBytePeers==1,"zero lost presence");
  });
  run("disconnect-and-release-keep-prefix-without-current-rate",[]{
    PeerPayloadLedger l({1,2,3});const auto t=add(l);observe(l,t,1,100,200);observe(l,t,2,110,220);observe(l,t,3,130,250,true);l.released(t);
    const auto p=project(snapshot(l));check(p.rowCount==1&&p.rows[0].retiredPeers==1&&p.retiredConnections==1,"retired row lost");
    check(p.rows[0].observedDownloadBytes==30&&p.rows[0].observedUploadBytes==50,"retired prefix changed");
    check(!p.rows[0].observedDownloadRate&&p.rows[0].rateObservedPeers==0&&p.rows[0].rateMissingPeers==0,"retired rate invented");
    check(p.tailUncertain&&p.rows[0].partial,"disconnect certified missing tail");
  });
  run("release-without-disconnect-retains-only-observed-prefix",[]{
    PeerPayloadLedger l({1,2,3});const auto t=add(l);observe(l,t,1,100,200);observe(l,t,2,107,209);l.released(t);
    const auto p=project(snapshot(l));check(p.rows[0].observedDownloadBytes==7&&p.rows[0].observedUploadBytes==9,"destructor inferred a tail");
    check(hasPeerPayloadIssue(p.ledgerIssues,PeerPayloadIssue::MissingDisconnect),"missing disconnect hidden");
  });
  run("active-and-retired-byte-prefixes-join-with-active-only-rates",[]{
    PeerPayloadLedger l({1,2,3});const auto a=add(l),b=add(l);observe(l,a,1,100,100);observe(l,b,1,1000,1000);
    observe(l,a,2,110,120);observe(l,b,2,1003,1004,true);l.released(b);const auto p=project(snapshot(l));
    check(p.rows[0].recordedPeers==2&&p.rows[0].retiredPeers==1&&p.rows[0].rateObservedPeers==1,"peer domains conflated");
    check(p.rows[0].observedDownloadBytes==13&&p.rows[0].observedUploadBytes==24&&p.rows[0].observedDownloadRate==7,"prefix/rate sums wrong");
  });
  run("maximum-single-prefix-no-signed-overflow",[]{
    PeerPayloadLedger l({1,2,3});const auto t=add(l);observe(l,t,1,0,0);observe(l,t,2,std::numeric_limits<std::int64_t>::max(),42);
    const auto p=project(snapshot(l));check(p.rows[0].observedDownloadBytes==std::numeric_limits<std::int64_t>::max(),"maximum prefix narrowed");
  });
  run("group-overflow-is-unknown-independent-other-direction",[]{
    PeerPayloadSnapshot s;s.identity={1,2,3};s.sequence=1;s.count=2;
    for(std::size_t i=0;i<2;++i){auto& r=s.records[i];r.connection=i+1;r.operation=1;r.network=PeerNetworkClass::Local;
      r.lastObserved=PeerPayloadSample{std::numeric_limits<std::int64_t>::max(),100,0,0};r.latestSampleAvailable=true;
      r.observedReceivedBytes=std::numeric_limits<std::int64_t>::max();r.observedSentBytes=static_cast<std::int64_t>(i+3);}
    const auto p=project(s);check(!p.rows[0].observedDownloadBytes&&p.rows[0].downloadBytesOverflow&&p.rows[0].downloadBytePeers==2,"overflow clamped/wrapped");
    check(p.rows[0].observedUploadBytes==7&&!p.rows[0].uploadBytesOverflow&&p.rows[0].partial,"independent known prefix lost");
  });
  run("forged-prefix-and-count-frames-rejected",[]{
    PeerPayloadLedger l({1,2,3});const auto t=add(l);observe(l,t,1,10,20);auto s=snapshot(l);
    s.records[0].observedReceivedBytes=-1;check(!projectCountryStatistics(s,{},false),"negative prefix accepted");
    s=snapshot(l);s.records[0].observedReceivedBytes=11;check(!projectCountryStatistics(s,{},false),"prefix larger than raw accepted");
    auto p=project(snapshot(l));p.rows[0].downloadBytePeers=2;check(!validCountryStatistics(p),"byte peer count forged");
    p=project(snapshot(l));p.rows[0].retiredPeers=1;check(!validCountryStatistics(p),"retired/rate domains forged");
  });
  std::cout<<"PASS "<<cases<<" observed payload prefix cases; "<<assertions<<" assertions; no restored/final/global/minute accounting credit\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL observed prefixes: "<<e.what()<<'\n';return 1;}}
