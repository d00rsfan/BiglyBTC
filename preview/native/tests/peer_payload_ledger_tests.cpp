#include "biglybtc/peer_payload_ledger.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {
using namespace biglybtc::core;
std::size_t cases = 0, assertions = 0;
void check(bool result, std::string_view message) {
  ++assertions; if (!result) { throw std::runtime_error(std::string(message)); }
}
template<class F> void run(std::string_view name, F body) {
  try { body(); ++cases; }
  catch (const std::exception& e) { throw std::runtime_error(std::string(name) + ": " + e.what()); }
}
PeerNumericEndpoint loopback(std::uint16_t port = 4321) {
  PeerNumericEndpoint p; p.family = PeerAddressFamily::V4; p.address[0] = 127;
  p.address[3] = 1; p.port = port; return p;
}
PeerPayloadSnapshot snapshot(const PeerPayloadLedger& ledger) {
  auto result = ledger.snapshot(); check(result.has_value(), "uncontended snapshot absent"); return *result;
}
PeerPayloadSample value(std::int64_t received = 100, std::int64_t sent = 20) {
  return {received, sent, 79, 39};
}
} // namespace

int main() {
  try {
    run("identity-and-empty-ledger-are-not-observed-zero", [] {
      PeerPayloadLedger ledger({1,2,3}); const auto empty = snapshot(ledger);
      check(empty.identity == PeerPayloadIdentity{1,2,3} && empty.count == 0 && empty.sequence == 0
          && empty.issues == 0, "empty owner fabricated observations");
      for (auto id : {PeerPayloadIdentity{0,2,3}, PeerPayloadIdentity{1,0,3}, PeerPayloadIdentity{1,2,0}}) {
        PeerPayloadLedger invalid(id);
        check(invalid.attach(1,loopback(),PeerNetworkClass::Local,0) == 0, "invalid identity admitted");
        check(hasPeerPayloadIssue(snapshot(invalid).issues, PeerPayloadIssue::InvalidIdentity), "invalid identity not explicit");
      }
    });
    run("attach-copies-endpoint-without-making-zero-sample", [] {
      PeerPayloadLedger ledger({1,2,3}); auto endpoint = loopback();
      const auto token = ledger.attach(7,endpoint,PeerNetworkClass::Local,123);
      endpoint.address.fill(99); const auto s = snapshot(ledger); const auto& r = s.records[0];
      check(token == 1 && s.count == 1 && r.operation == 7 && r.remote == loopback(), "input endpoint borrowed or identity lost");
      check(r.network == PeerNetworkClass::Local && !r.lastObserved && !r.observedNanoseconds
          && !r.latestSampleAvailable && r.tailUncertain, "attachment invented a measurement/certainty");
    });
    run("actual-zero-and-full-signed-range-retained-literally", [] {
      PeerPayloadLedger ledger({1,2,3}); const auto t = ledger.attach(1,loopback(),PeerNetworkClass::Local,1);
      ledger.observe(t,2,PeerPayloadSample{}); auto s = snapshot(ledger);
      check(s.records[0].lastObserved == PeerPayloadSample{} && s.records[0].latestSampleAvailable,
          "known zero changed to missing");
      const auto high = PeerPayloadSample{std::numeric_limits<std::int64_t>::max(),
          std::numeric_limits<std::int64_t>::max(),std::numeric_limits<std::int32_t>::max(),
          std::numeric_limits<std::int32_t>::max()};
      ledger.observe(t,3,high); s = snapshot(ledger);
      check(s.records[0].lastObserved == high && s.records[0].observedNanoseconds == 3, "large values narrowed or arithmetic inferred");
    });
    run("reconnect-and-next-operation-never-merge-same-endpoint", [] {
      PeerPayloadLedger ledger({1,2,3});
      const auto a = ledger.attach(1,loopback(),PeerNetworkClass::Local,0);
      ledger.observe(a,1,value(2048,1024),true); ledger.released(a);
      const auto b = ledger.attach(2,loopback(),PeerNetworkClass::Local,2);
      // LT can restore a rounded amount. It is retained raw, not subtracted or summed.
      ledger.observe(b,3,value(2048,1024)); const auto s = snapshot(ledger);
      check(a != b && s.count == 2 && s.records[0].operation == 1 && s.records[1].operation == 2,
          "reconnect overwrote identity");
      check(s.records[0].lastObserved == s.records[1].lastObserved && s.records[0].tailUncertain
          && s.records[1].tailUncertain, "carry-over converted to invented fresh bytes/finality");
    });
    run("ipv6-scope-and-lan-classification-are-independent-scalars", [] {
      PeerPayloadLedger ledger({1,2,3}); PeerNumericEndpoint p;
      p.family = PeerAddressFamily::V6; p.address[0] = 0xfe; p.address[1] = 0x80;
      p.address[15] = 9; p.scope = std::numeric_limits<std::uint64_t>::max(); p.port = 65535;
      ledger.attach(1,p,PeerNetworkClass::Unknown,0); const auto s = snapshot(ledger);
      check(s.records[0].remote == p && s.records[0].network == PeerNetworkClass::Unknown,
          "endpoint scope narrowed or geography inferred");
    });
    run("invalid-endpoint-retains-record-with-explicit-unknown", [] {
      PeerPayloadLedger ledger({1,2,3}); auto p = loopback(); p.address[15] = 1;
      check(ledger.attach(1,p,PeerNetworkClass::NonLocal,0) != 0, "bad optional endpoint failed entire record");
      const auto s = snapshot(ledger);
      check(s.records[0].remote.family == PeerAddressFamily::Unknown
          && hasPeerPayloadIssue(s.issues,PeerPayloadIssue::EndpointUnavailable), "malformed endpoint presented as known");
    });
    run("missing-and-invalid-sample-preserve-prior-observation-as-stale", [] {
      PeerPayloadLedger ledger({1,2,3}); const auto t = ledger.attach(1,loopback(),PeerNetworkClass::Local,0);
      ledger.observe(t,1,value()); ledger.observe(t,2,std::nullopt);
      auto s = snapshot(ledger);
      check(s.records[0].lastObserved == value() && !s.records[0].latestSampleAvailable
          && s.records[0].observedNanoseconds == 1 && hasPeerPayloadIssue(s.issues,PeerPayloadIssue::SampleUnavailable),
          "missing sample erased history or invented current data");
      for (const auto bad : {PeerPayloadSample{-1,20,79,39}, PeerPayloadSample{100,-1,79,39},
          PeerPayloadSample{100,20,-1,39}, PeerPayloadSample{100,20,79,-1}}) { ledger.observe(t,3,bad); }
      s = snapshot(ledger); check(s.records[0].lastObserved == value()
          && !s.records[0].latestSampleAvailable && hasPeerPayloadIssue(s.issues,PeerPayloadIssue::InvalidSample),
          "negative counters/rates admitted");
    });
    run("counter-and-clock-regressions-never-rebase-to-zero", [] {
      PeerPayloadLedger ledger({1,2,3}); const auto t = ledger.attach(1,loopback(),PeerNetworkClass::Local,4);
      ledger.observe(t,5,value()); ledger.observe(t,6,value(99,20)); ledger.observe(t,7,value(100,19));
      ledger.observe(t,3,value(101,21)); const auto s = snapshot(ledger);
      check(s.records[0].lastObserved == value() && s.records[0].observedNanoseconds == 5
          && hasPeerPayloadIssue(s.issues,PeerPayloadIssue::CounterRegression)
          && hasPeerPayloadIssue(s.issues,PeerPayloadIssue::Clock), "regression was reset/narrowed/accepted");
    });
    run("connected-disconnected-released-retain-uncertified-tail", [] {
      PeerPayloadLedger ledger({1,2,3}); const auto t = ledger.attach(1,loopback(),PeerNetworkClass::Local,0);
      ledger.connected(t,1); ledger.observe(t,2,value()); ledger.observe(t,3,value(130,40),true);
      auto s = snapshot(ledger); check(s.records[0].phase == PeerPayloadPhase::Disconnected
          && s.records[0].disconnectObserved && s.records[0].tailUncertain, "disconnect upgraded to final accounting");
      ledger.released(t); s = snapshot(ledger);
      check(s.records[0].phase == PeerPayloadPhase::Released && s.records[0].lastObserved == value(130,40)
          && !hasPeerPayloadIssue(s.issues,PeerPayloadIssue::MissingDisconnect), "release lost observed tail");
      const auto retained = s.records[0].lastObserved; ledger.observe(t,4,value(999,999)); ledger.connected(t,5);
      s = snapshot(ledger); check(s.records[0].lastObserved == retained
          && hasPeerPayloadIssue(s.issues,PeerPayloadIssue::Lifecycle), "released token was resurrected");
    });
    run("disconnect-with-missing-read-is-known-lifecycle-unknown-tail", [] {
      PeerPayloadLedger ledger({1,2,3}); const auto t = ledger.attach(1,loopback(),PeerNetworkClass::Local,0);
      ledger.observe(t,1,std::nullopt,true); ledger.released(t); const auto s = snapshot(ledger);
      check(s.records[0].disconnectObserved && !s.records[0].lastObserved && s.records[0].tailUncertain
          && s.records[0].phase == PeerPayloadPhase::Released, "missing disconnect sample invented zero/final");
    });
    run("destructor-only-loss-and-invalid-token-explicit", [] {
      PeerPayloadLedger ledger({1,2,3}); const auto t = ledger.attach(1,loopback(),PeerNetworkClass::Local,0);
      ledger.released(t); ledger.observe(0,2,value()); ledger.released(99); const auto s = snapshot(ledger);
      check(hasPeerPayloadIssue(s.issues,PeerPayloadIssue::MissingDisconnect)
          && hasPeerPayloadIssue(s.issues,PeerPayloadIssue::InvalidToken) && !s.records[0].lastObserved,
          "destroyed/foreign token silently accepted");
    });
    run("capacity-never-evicts-old-connection-or-reuses-token", [] {
      PeerPayloadLedger ledger({1,2,3});
      for (std::size_t i=0;i<MaximumPeerPayloadRecords;++i) {
        check(ledger.attach(i+1,loopback(),PeerNetworkClass::Local,0) == i+1, "unexpected capacity/token");
      }
      ledger.observe(1,1,value(1234,567)); ledger.released(1);
      check(ledger.attach(9999,loopback(),PeerNetworkClass::Local,2) == 0, "old released record silently reused");
      const auto s = snapshot(ledger);
      check(s.count == MaximumPeerPayloadRecords && s.records[0].operation == 1
          && s.records[0].lastObserved == value(1234,567) && hasPeerPayloadIssue(s.issues,PeerPayloadIssue::Capacity),
          "overflow evicted raw history or hid loss");
    });
    run("copied-snapshot-owned-and-loss-flags-sticky", [] {
      PeerPayloadLedger ledger({1,2,3}); const auto t = ledger.attach(1,loopback(),PeerNetworkClass::Local,0);
      ledger.observe(t,1,value()); auto before = snapshot(ledger);
      ledger.noteLoss(PeerPayloadIssue::Factory); ledger.noteLoss(PeerPayloadIssue::UnsupportedConnection);
      ledger.observe(t,2,value(400,30)); const auto after = snapshot(ledger);
      check(before.records[0].lastObserved == value() && after.records[0].lastObserved == value(400,30)
          && hasPeerPayloadIssue(after.issues,PeerPayloadIssue::Factory)
          && hasPeerPayloadIssue(after.issues,PeerPayloadIssue::UnsupportedConnection), "snapshot aliases live state or loss vanished");
      before.records[0].remote.address.fill(77); check(snapshot(ledger).records[0].remote == loopback(), "caller mutated ledger");
    });
    run("bounded-concurrent-publication-is-either-accepted-or-explicitly-lossy", [] {
      PeerPayloadLedger ledger({1,2,3}); const auto t = ledger.attach(1,loopback(),PeerNetworkClass::Local,0);
      // Real threads exercise synchronization, not synthetic callback/causality credit.
      std::jthread publisher([&] { for (int i=1;i<=10000;++i) { ledger.observe(t,i,value(i,i)); } });
      std::uint64_t sequence=0;
      for (int i=0;i<10000;++i) { if (const auto s=ledger.snapshot()) {
        check(s->count==1 && s->sequence>=sequence && s->records[0].operation==1, "torn identity/sequence"); sequence=s->sequence;
      } }
      publisher.join(); const auto s=snapshot(ledger);
      check(s.records[0].lastObserved.has_value() || hasPeerPayloadIssue(s.issues,PeerPayloadIssue::Contention),
          "missed publications had no loss flag");
      check(s.count==1 && s.records[0].tailUncertain, "concurrency changed cap/finality");
    });
    std::cout << "PASS " << cases << " peer payload ledger policy cases; " << assertions
              << " assertions; synthetic domain policy only; no Country/tail/runtime equality credit\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << "FAIL peer payload ledger: " << e.what() << '\n'; return 1; }
}
