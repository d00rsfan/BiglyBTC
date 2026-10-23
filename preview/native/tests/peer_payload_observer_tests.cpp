#include "persistent_session.hpp"
#include "peer_payload_observer.hpp"
#include "owned_engine_fixture.hpp"

#include <libtorrent/extensions.hpp>
#include <libtorrent/peer_connection_handle.hpp>
#include <libtorrent/address.hpp>

#include <QtTest>

#include <chrono>
#include <algorithm>
#include <memory>
#include <thread>
#include <utility>

namespace {
using namespace biglybtc;
namespace lt = libtorrent;
using namespace std::chrono_literals;
}
class PeerPayloadObserverTests final : public QObject {
  Q_OBJECT
private slots:
  void numericLocalPolicyMatchesPinnedIpv4AndIpv6Boundaries() {
    using Class=core::PeerNetworkClass;
    const std::pair<const char*,Class> cases[]{
      {"10.0.0.0",Class::Local},{"10.255.255.255",Class::Local},{"11.0.0.0",Class::NonLocal},
      {"172.15.255.255",Class::NonLocal},{"172.16.0.0",Class::Local},{"172.31.255.255",Class::Local},{"172.32.0.0",Class::NonLocal},
      {"192.168.255.255",Class::Local},{"192.169.0.0",Class::NonLocal},{"169.254.0.1",Class::Local},
      {"127.255.255.255",Class::Local},{"100.63.255.255",Class::NonLocal},{"100.64.0.0",Class::Local},{"100.127.255.255",Class::Local},{"100.128.0.0",Class::NonLocal},
      {"8.8.8.8",Class::NonLocal},{"0.0.0.0",Class::NonLocal},{"::1",Class::Local},{"::",Class::NonLocal},
      {"fe80::1",Class::Local},{"febf::1",Class::Local},{"fec0::1",Class::Local},{"feff::1",Class::Local},{"fe7f::1",Class::NonLocal},
      {"fc00::1",Class::Local},{"fdff::1",Class::Local},{"fe00::1",Class::NonLocal},
      {"ff02::1",Class::Local},{"ff05::1",Class::Local},{"ff01::1",Class::NonLocal},{"ff0e::1",Class::NonLocal},
      {"2001:4860:4860::8888",Class::NonLocal},{"::ffff:127.0.0.1",Class::NonLocal}};
    for(const auto& [literal,expected]:cases){
      lt::error_code error;const auto address=lt::make_address(literal,error);QVERIFY2(!error,literal);
      core::PeerNumericEndpoint numeric;
      if(address.is_v4()){numeric.family=core::PeerAddressFamily::V4;const auto bytes=address.to_v4().to_bytes();std::copy(bytes.begin(),bytes.end(),numeric.address.begin());}
      else{numeric.family=core::PeerAddressFamily::V6;const auto bytes=address.to_v6().to_bytes();std::copy(bytes.begin(),bytes.end(),numeric.address.begin());numeric.scope=77;}
      QCOMPARE(engine::detail::classifyPeerPayloadEndpoint(numeric),expected);
    }
    QCOMPARE(engine::detail::classifyPeerPayloadEndpoint({}),Class::Unknown);
    core::PeerNumericEndpoint invalid;invalid.family=static_cast<core::PeerAddressFamily>(255);
    QCOMPARE(engine::detail::classifyPeerPayloadEndpoint(invalid),Class::Unknown);
  }
  void missingPeerAndDefaultHandlersCannotAlterProtocolOrRequest() {
    auto ledger=std::make_shared<core::PeerPayloadLedger>(core::PeerPayloadIdentity{1,2,3});
    lt::add_torrent_params parameters;
    parameters.save_path="generated-sentinel";
    const auto flags=parameters.flags;
    engine::detail::attachPeerPayloadObserver(parameters,ledger,7);
    QCOMPARE(parameters.extensions.size(),std::size_t{1});
    QCOMPARE(parameters.save_path,std::string("generated-sentinel"));QVERIFY(parameters.flags==flags);
    auto plugin=parameters.extensions.front()(lt::torrent_handle{},lt::client_data_t{});QVERIFY(plugin);
    lt::peer_connection_handle missing(std::weak_ptr<lt::peer_connection>{});
    QVERIFY(!plugin->new_connection(missing));
    const auto s=ledger->snapshot();QVERIFY(s);QCOMPARE(s->count,std::size_t{0});
    QVERIFY(core::hasPeerPayloadIssue(s->issues,core::PeerPayloadIssue::SampleUnavailable));
    // No peer hook exists for an unavailable handle; no dereference/fake row.
    QVERIFY(!plugin->on_pause());QVERIFY(!plugin->on_resume());
  }
  void actualOwnedPeerHasLiteralPayloadObservationButNeverCertifiedCountryTail_data() {
    QTest::addColumn<bool>("multifile");QTest::newRow("single")<<false;QTest::newRow("multi")<<true;
  }
  void actualOwnedPeerHasLiteralPayloadObservationButNeverCertifiedCountryTail() {
    QFETCH(bool,multifile);
    test::OwnedEngineFixture fixture(multifile);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);
    auto lease=storage::ProfileLease::acquire(fixture.native());QVERIFY(lease);
    auto created=engine::PersistentSession::create({11,22,33});QVERIFY2(created,"Requires the actual qualified source/runtime binding");
    auto owner=std::move(*created);
    auto empty=owner->peerPayloads();QVERIFY(empty);QCOMPARE(empty->count,std::size_t{0});
    QVERIFY(owner->startIdle(*lease));
    QVERIFY(owner->peerPayloads());QCOMPARE(owner->peerPayloads()->count,std::size_t{0});
    // Same actual transfer/admission/retirement path. No second alert pop,
    // injected counts, synthesized peer, factory bypass or extra network source.
    const auto result=engine::continueLocal(*owner,*lease,{.profile=fixture.native(),.hash=*fixture.hash,
        .peerPort=fixture.port(),.timeout=20s});QVERIFY(result);QVERIFY(result->received>0);
    const auto retirement=owner->lastRetirement();QVERIFY(retirement&&retirement->storageReleased&&retirement->peerCounters);
    auto observed=owner->peerPayloads();QVERIFY(observed);QVERIFY(observed->count>0);
    QVERIFY((observed->identity==core::PeerPayloadIdentity{11,22,33}));
    bool positive=false, disconnected=false;
    for(std::size_t i=0;i<observed->count;++i) {
      const auto& r=observed->records[i];QCOMPARE(r.operation,retirement->operation);QVERIFY(r.connection>0);
      QCOMPARE(r.remote.family,core::PeerAddressFamily::V4);QCOMPARE(r.remote.address[0],std::uint8_t{127});
      QCOMPARE(r.remote.address[3],std::uint8_t{1});QVERIFY(r.remote.port>0);
      QCOMPARE(r.network,core::PeerNetworkClass::Local);QVERIFY(r.tailUncertain);
      positive|=r.lastObserved&&r.lastObserved->received>0;
      disconnected|=r.disconnectObserved;
      if(r.lastObserved) {QVERIFY(r.lastObserved->received>=0&&r.lastObserved->sent>=0);
        QVERIFY(r.lastObserved->receivedRate>=0&&r.lastObserved->sentRate>=0);}
    }
    QVERIFY(positive);QVERIFY(disconnected);
    // Copy remains immutable while joining may publish lifecycle metadata only.
    const auto retained=observed->records[0].lastObserved;
    std::optional<core::PeerPayloadSnapshot> foreign;
    std::thread other([&]{foreign=owner->peerPayloads();});other.join();QVERIFY(!foreign);
    const auto closed=owner->close(std::chrono::steady_clock::now()+5s);
    QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&closed.retirement.peerCounters);
    QCOMPARE(closed.retirement.peerCounters->counters(),retirement->peerCounters->counters());
    const auto final=owner->peerPayloads();QVERIFY(final);QCOMPARE(final->count,observed->count);
    QVERIFY(observed->records[0].lastObserved==retained);
    for(std::size_t i=0;i<final->count;++i) {QVERIFY(final->records[i].tailUncertain);}
    QVERIFY(fixture.intact());
  }
};
QTEST_GUILESS_MAIN(PeerPayloadObserverTests)
#include "peer_payload_observer_tests.moc"
