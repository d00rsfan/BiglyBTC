#include "biglybtc/country_statistics.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace biglybtc::core;
std::size_t cases=0,assertions=0;
void check(bool value,const char* message){++assertions;if(!value){throw std::runtime_error(message);}}
template<class F>void run(const char* name,F body){try{body();++cases;}catch(const std::exception& e){throw std::runtime_error(std::string(name)+": "+e.what());}}
PeerPayloadSnapshot ledger(std::size_t count=1){
  PeerPayloadSnapshot s;s.identity={1,2,3};s.sequence=count;s.count=count;
  for(std::size_t i=0;i<count;++i){auto& p=s.records[i];p.connection=i+1;p.operation=1;p.sequence=i+1;
    p.network=PeerNetworkClass::NonLocal;p.remote.family=PeerAddressFamily::V4;p.remote.address[0]=8;p.remote.address[3]=static_cast<std::uint8_t>(i+1);p.remote.port=1234;
    p.phase=PeerPayloadPhase::Connected;p.lastObserved=PeerPayloadSample{99999999,88888888,101,23};p.latestSampleAvailable=true;}
  return s;
}
CountryResolvedPeer country(std::uint64_t connection,const char* code="US"){
  CountryResolvedPeer p;p.connection=connection;p.location.kind=CountryLookupKind::Country;p.location.code={code[0],code[1],0};
  return p;
}
CountryStatistics project(const PeerPayloadSnapshot& s,std::span<const CountryResolvedPeer> p={},bool available=false){
  auto value=projectCountryStatistics(s,p,available);check(bool(value),"valid projection rejected");check(validCountryStatistics(*value),"projection fails its admission");return *value;
}
}
int main(){try{
  run("empty-is-not-observed-zero-and-provider-state-is-independent",[]{
    const auto s=ledger(0);auto a=project(s),b=project(s,{},true);
    check(a.rowCount==0&&b.rowCount==0&&!a.providerAvailable&&b.providerAvailable&&a.tailUncertain,"empty fabricated data/provider");
  });
  run("country-rate-sum-never-uses-raw-carried-over-totals",[]{
    auto s=ledger(2);s.records[1].lastObserved=PeerPayloadSample{std::numeric_limits<std::int64_t>::max(),-999,7,3};
    const std::array p{country(2),country(1)};const auto out=project(s,p,true);
    check(out.rowCount==1&&out.rows[0].observedDownloadRate==108&&out.rows[0].observedUploadRate==26
        &&out.rows[0].recordedPeers==2&&out.rows[0].rateObservedPeers==2,"rates replaced by raw totals/deltas/order");
  });
  run("local-unknown-and-unavailable-stay-distinct",[]{
    auto s=ledger(3);s.records[0].network=PeerNetworkClass::Local;
    const std::array p{country(1),CountryResolvedPeer{2,{CountryLookupKind::Unknown,{},{}}}};
    const auto out=project(s,p,true);check(out.rowCount==3,"classes merged");
    check(out.rows[0].kind==CountryRowKind::Local&&out.rows[1].kind==CountryRowKind::Unknown
        &&out.rows[2].kind==CountryRowKind::Unavailable,"LAN allocated to geographic country or absence to Unknown");
    check(out.partial&&out.rows[1].partial&&out.rows[2].partial,"classification uncertainty hidden");
  });
  run("missing-provider-never-adopts-stale-country-resolution",[]{
    auto s=ledger(2);s.records[0].network=PeerNetworkClass::Local;const std::array p{country(2)};
    const auto out=project(s,p,false);check(out.rows[0].kind==CountryRowKind::Local&&out.rows[0].observedDownloadRate==101
        &&out.rows[1].kind==CountryRowKind::Unavailable&&!out.providerAvailable,"cached ready leaked into unavailable DB");
  });
  run("known-zero-and-no-sample-have-distinct-optionals",[]{
    auto s=ledger(2);s.records[0].lastObserved=PeerPayloadSample{};s.records[1].lastObserved.reset();s.records[1].latestSampleAvailable=false;
    const std::array p{country(1),country(2,"DE")};const auto out=project(s,p,true);
    check(out.rows[0].observedDownloadRate==0&&out.rows[0].rateObservedPeers==1
        &&!out.rows[1].observedDownloadRate&&out.rows[1].rateMissingPeers==1,"unknown became fabricated zero");
  });
  run("partial-rate-prefix-retains-explicit-missing-count",[]{
    auto s=ledger(2);s.records[1].latestSampleAvailable=false;const std::array p{country(1),country(2)};
    const auto out=project(s,p,true);check(out.rows[0].observedDownloadRate==101&&out.rows[0].rateMissingPeers==1
        &&out.rows[0].rateObservedPeers==1&&out.rows[0].partial&&out.partial,"partial known prefix advertised complete");
  });
  run("retired-uncertified-tail-is-not-a-current-rate",[]{
    auto s=ledger(2);s.records[0].phase=PeerPayloadPhase::Disconnected;s.records[1].phase=PeerPayloadPhase::Released;
    const auto out=project(s);check(out.rowCount==1&&out.rows[0].retiredPeers==2&&!out.rows[0].observedDownloadRate
        &&!out.rows[0].observedDownloadBytes&&out.includedConnections==0&&out.retiredConnections==2
        &&out.retainedConnections==2&&out.tailUncertain,"retired samples contributed current country rate/finality");
  });
  run("all-ledger-loss-including-capacity-is-sticky-partial",[]{
    auto s=ledger();s.issues=static_cast<std::uint32_t>(PeerPayloadIssue::Capacity)|static_cast<std::uint32_t>(PeerPayloadIssue::Contention);
    const std::array p{country(1)};const auto out=project(s,p,true);
    check(out.ledgerIssues==s.issues&&out.partial&&out.rows[0].partial&&out.rows[0].observedDownloadRate==101,"loss hidden or known observations discarded");
  });
  run("maximum-fixed-cap-and-rates-do-not-overflow",[]{
    auto s=ledger(MaximumPeerPayloadRecords);std::array<CountryResolvedPeer,MaximumPeerPayloadRecords> p{};
    for(std::size_t i=0;i<s.count;++i){s.records[i].lastObserved->receivedRate=std::numeric_limits<std::int32_t>::max();p[i]=country(i+1);}
    const auto out=project(s,p,true);check(out.rows[0].observedDownloadRate==1099511627264LL&&out.rows[0].recordedPeers==512,"bounded rate sum narrowed/overflowed");
  });
  run("duplicate-foreign-and-malformed-resolutions-fail-closed",[]{
    const auto s=ledger();const std::array duplicate{country(1),country(1)};
    check(!projectCountryStatistics(s,duplicate,true),"duplicate token accepted");
    const std::array foreign{country(2)};check(!projectCountryStatistics(s,foreign,true),"foreign peer adopted");
    auto bad=country(1);bad.location.code[2]='X';check(!projectCountryStatistics(s,{&bad,1},true),"unterminated country accepted");
    bad=country(1);bad.location.name.fill('x');check(!projectCountryStatistics(s,{&bad,1},true),"unbounded name accepted");
  });
  run("public-frame-admission-rejects-count-rate-and-finality-forgery",[]{
    const std::array p{country(1)};const auto original=project(ledger(),p,true);
    auto bad=original;bad.tailUncertain=false;check(!validCountryStatistics(bad),"tail certainty forged");
    bad=original;bad.rows[0].observedDownloadRate=-1;check(!validCountryStatistics(bad),"negative rate accepted");
    bad=original;bad.rows[0].rateObservedPeers=0;check(!validCountryStatistics(bad),"optionals lost presence contract");
    bad=original;bad.sequence=0;check(!validCountryStatistics(bad),"nonempty zero ledger revision accepted");
    bad=original;bad.providerAvailable=false;check(!validCountryStatistics(bad),"country accepted without DB availability");
  });
  std::cout<<"PASS "<<cases<<" Country grouping policy cases; "<<assertions<<" assertions; no Java/provider/geography runtime equality credit\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL Country grouping: "<<e.what()<<'\n';return 1;}}
