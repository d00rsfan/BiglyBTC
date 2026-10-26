#include "selected_swarm_fixture.hpp"
#include "../engine/managed_payload_counters.hpp"
#include "../engine/managed_storage.hpp"
#include "../engine/network_policy.hpp"
#include "../engine/persistent_session.hpp"

#include <libtorrent/biglybtc_swarm.hpp>
#include <libtorrent/biglybtc_payload_counters.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/disabled_disk_io.hpp>
#include <libtorrent/torrent_status.hpp>
#include <QtTest>

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <chrono>
#include <cstring>
#include <dlfcn.h>

// Negative reader is never invoked; keep its pointer type opaque on v1 SDKs.
namespace libtorrent { struct BiglybtcTrafficCountersV2; }

namespace {
using namespace biglybtc;
namespace fixture = test::selected_swarm;
namespace lt = libtorrent;
using namespace std::chrono_literals;
enum class Fault { None, Missing, Foreign, ZeroAfterProof };
thread_local Fault fault = Fault::None;
thread_local unsigned markerLookups = 0, zeroCalls = 0;
unsigned foreignMarker() noexcept { return 1; }
unsigned zeroMarker() noexcept { ++zeroCalls; return 0; }
struct FaultScope final {
  explicit FaultScope(Fault value) { fault = value; markerLookups = zeroCalls = 0; }
  ~FaultScope() { fault = Fault::None; }
};
// Optional ABI faults affect only lookup policy. Genuine provider byte/hash,
// mapping, descriptor and all nine mandatory exports remain unchanged.
enum class TrafficFault { None, Absent, ForeignMarker, ForeignReader,
    WrongMarkerAfterProof, MarkerOnly, ReaderOnly };
thread_local TrafficFault trafficFault = TrafficFault::None;
thread_local unsigned trafficMarkerLookups = 0, trafficReaderLookups = 0;
thread_local unsigned wrongTrafficCalls = 0, foreignTrafficCalls = 0;
constexpr std::array mandatorySymbols{
    "_ZN10libtorrent7versionEv", "_ZN10libtorrent21session_stats_metricsEv",
    "_ZN10libtorrent14session_handle18post_session_statsEv",
    "_ZN10libtorrent14session_handle10pop_alertsEPSt6vectorIPNS_5alertESaIS3_EE",
    "_ZNK10libtorrent19session_stats_alert8countersEv",
    "_ZN10libtorrent14session_handle14remove_torrentERKNS_14torrent_handleENS_5flags13bitfield_flagIhNS_16remove_flags_tagEvEE",
    "biglybtc_payload_counter_abi_v1", "biglybtc_read_payload_counters_v1",
    "biglybtc_swarm_mask_abi_v1"};
thread_local std::array<unsigned, mandatorySymbols.size()> mandatoryLookups{};
unsigned foreignTrafficMarker() noexcept { ++foreignTrafficCalls; return 2; }
std::uint32_t foreignTrafficReader(const lt::torrent_handle*, lt::BiglybtcTrafficCountersV2*, std::uint32_t) noexcept {
  ++foreignTrafficCalls; return 0;
}
unsigned wrongTrafficMarker() noexcept { ++wrongTrafficCalls; return 1; }
struct ProviderPins final {
  std::atomic_bool enabled{false};
  std::atomic<void*> object{nullptr};
  std::atomic<int> live{0};
  std::atomic<unsigned> opens{0}, closes{0}, mismatches{0};
} providerPins;
struct TrafficScope final {
  explicit TrafficScope(TrafficFault value) {
    trafficFault=value;trafficMarkerLookups=trafficReaderLookups=wrongTrafficCalls=foreignTrafficCalls=0;
    mandatoryLookups.fill(0);providerPins.object=nullptr;providerPins.live=0;
    providerPins.opens=0;providerPins.closes=0;providerPins.mismatches=0;providerPins.enabled=true;
  }
  ~TrafficScope() { providerPins.enabled=false;trafficFault=TrafficFault::None; }
};
std::optional<engine::ManagedDownloadStatus> status(engine::PersistentSession& owner,
    const core::TorrentHash& key) {
  const auto rows = owner.managedDownloads();
  const auto at = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.hash == key; });
  return at == rows.end() ? std::nullopt : std::optional{*at};
}
} // namespace

// Test-only ELF seams: original swarm fault plus the optional traffic pair.
// Actual object/hash/descriptor proof and all other exports remain unmodified.
// ZeroAfterProof is a synthetic ABI-policy fault, not a different approved DSO.
extern "C" void* __real_dlsym(void*, const char*) noexcept;
extern "C" void* __wrap_dlsym(void* library, const char* name) noexcept {
  if (providerPins.enabled.load()) {
    for (std::size_t i=0;i<mandatorySymbols.size();++i) {
      if (std::strcmp(name,mandatorySymbols[i])==0) { ++mandatoryLookups[i];break; }
    }
  }
  const bool trafficMarker=std::strcmp(name,"biglybtc_traffic_counter_abi_v2")==0;
  const bool trafficReader=std::strcmp(name,"biglybtc_read_traffic_counters_v2")==0;
  if (providerPins.enabled.load() && (trafficMarker||trafficReader)) {
    if (trafficMarker) { ++trafficMarkerLookups; } else { ++trafficReaderLookups; }
    switch(trafficFault) {
    case TrafficFault::Absent:return nullptr;
    case TrafficFault::MarkerOnly:if(trafficReader){return nullptr;}break;
    case TrafficFault::ReaderOnly:if(trafficMarker){return nullptr;}break;
    case TrafficFault::ForeignMarker:if(trafficMarker){return reinterpret_cast<void*>(&foreignTrafficMarker);}break;
    case TrafficFault::ForeignReader:if(trafficReader){return reinterpret_cast<void*>(&foreignTrafficReader);}break;
    case TrafficFault::WrongMarkerAfterProof:
      // Real handle/default pointer + image/digest proof finish FIRST.
      // Only the later callable marker is replaced; no new DSO is approved.
      if(trafficMarker&&trafficMarkerLookups>2){return reinterpret_cast<void*>(&wrongTrafficMarker);}break;
    case TrafficFault::None:break;
    }
  }
  if (fault != Fault::None && std::strcmp(name, "biglybtc_swarm_mask_abi_v1") == 0) {
    ++markerLookups;
    if (fault == Fault::Missing) { return nullptr; }
    if (fault == Fault::Foreign) { return reinterpret_cast<void*>(&foreignMarker); }
    // The full proof resolves handle+RTLD_DEFAULT first. Only the subsequent
    // capability call is replaced, preserving real preceding identity checks.
    if (fault == Fault::ZeroAfterProof && markerLookups > 2) { return reinterpret_cast<void*>(&zeroMarker); }
  }
  return __real_dlsym(library, name);
}

// Observe references obtained by the actual guard; never replace handles,
// dladdr, source hashes, mappings or the actual close operation. Atomic fixed
// primitives allow the final plugin release to occur on the real LT thread.
extern "C" void* __real_dlopen(const char*, int) noexcept;
extern "C" int __real_dlclose(void*) noexcept;
extern "C" void* __wrap_dlopen(const char* name,int flags) noexcept {
  void* result=__real_dlopen(name,flags);
  if(result&&providerPins.enabled.load()&&name
      &&std::strcmp(name,"libtorrent-rasterbar.so.2.0")==0&&(flags&RTLD_NOLOAD)){
    void* empty=nullptr;
    if(!providerPins.object.compare_exchange_strong(empty,result)&&empty!=result){++providerPins.mismatches;}
    ++providerPins.opens;++providerPins.live;
  }
  return result;
}
extern "C" int __wrap_dlclose(void* handle) noexcept {
  const bool observed=providerPins.enabled.load()&&handle==providerPins.object.load();
  const int result=__real_dlclose(handle);
  if(observed){
    if(result!=0||providerPins.live.fetch_sub(1)<=0){++providerPins.mismatches;}
    ++providerPins.closes;
  }
  return result;
}

class SelectedSwarmAdmissionTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(lt::biglybtc_swarm_mask_abi_v1(), 1u);
    QVERIFY2(engine::detail::managedPayloadCounterBridge(), "Requires exact approved full provider; no unqualified fallback or skip");
  }
  void selectedFlagsAreActualImmutableSessionIndices_data() {
    QTest::addColumn<bool>("v2"); QTest::newRow("original-v1") << false; QTest::newRow("original-v2") << true;
  }
  void selectedFlagsAreActualImmutableSessionIndices() {
    QFETCH(bool, v2); fixture::Fixture generated(v2);
    auto bridge = engine::detail::managedPayloadCounterBridge(); QVERIFY(bridge);
    auto prepared = engine::detail::ManagedStorage::prepare(*generated.lease, *generated.key, {}, {}, {}, bridge);
    QVERIFY2(prepared.has_value(), prepared ? "" : prepared.error().message.c_str());
    auto parameters = prepared->parameters();
    const auto full = parameters.ti->info_hashes(); QVERIFY(full.has_v1() && full.has_v2());
    const auto selected = v2 ? lt::biglybtc_v2_only : lt::biglybtc_v1_only;
    QCOMPARE(parameters.flags & lt::biglybtc_swarm_mask, selected);
    QCOMPARE(std::string(parameters.ti->info_section().data(), parameters.ti->info_section().size()), generated.rawInfo);
    QCOMPARE(prepared->identity().originalKey, *generated.key);
    QVERIFY(prepared->identity().full == generated.identity->full);
    const auto counters = std::make_shared<engine::detail::ManagedPayloadCounters>();
    QVERIFY(engine::detail::attachManagedPayloadCounters(parameters, counters, bridge));
    bridge.reset(); // Parameters/plugin and prepared storage retain the proven module pin.
    lt::session session(fixture::isolated());
    lt::error_code error;
    const auto handle = session.add_torrent(std::move(parameters), error);
    QVERIFY2(!error, error.message().c_str()); QVERIFY(handle.is_valid());
    QCOMPARE(session.find_torrent(full.v1).is_valid(), !v2);
    QCOMPARE(session.find_torrent(full.v2).is_valid(), v2);
    QVERIFY(handle.info_hashes() == full && handle.status().info_hashes == full);
    handle.set_flags(lt::torrent_flags_t{}, lt::biglybtc_swarm_mask);
    QCOMPARE(handle.status().flags & lt::biglybtc_swarm_mask, selected);
    const auto retained = handle.torrent_file()->info_section();
    QCOMPARE(std::string(retained.data(), retained.size()), generated.rawInfo);
  }
  void missingPinOrWrongFullIdentityLeavesParametersUnchanged() {
    fixture::Fixture generated(true);
    auto bridge = engine::detail::managedPayloadCounterBridge(); QVERIFY(bridge);
    lt::add_torrent_params parameters;
    lt::error_code error;
    parameters.ti = std::make_shared<lt::torrent_info>(
        lt::span<const char>(generated.source.data(), generated.source.size()), error, lt::from_span);
    QVERIFY(!error); const auto flags = parameters.flags;
    QVERIFY(!engine::detail::selectManagedSwarm(parameters, *generated.identity, {}));
    QCOMPARE(parameters.flags, flags);
    auto changed = *generated.identity; (*changed.full.v2)[0] ^= std::byte{1};
    QVERIFY(!engine::detail::selectManagedSwarm(parameters, changed, bridge));
    QCOMPARE(parameters.flags, flags);
    changed = *generated.identity; changed.selected = static_cast<storage::OriginalSelectedHash>(77);
    QVERIFY(!engine::detail::selectManagedSwarm(parameters, changed, bridge));
    QCOMPARE(parameters.flags, flags);
    auto offline = engine::detail::ManagedStorage::prepare(*generated.lease, *generated.key, {});
    QVERIFY(!offline); QCOMPARE(offline.error().code, storage::ErrorCode::Unsupported);
    QVERIFY(generated.untouched());
  }
  void originalSelectedHybridManagedAdmissionKeepsManualPause_data() {
    selectedFlagsAreActualImmutableSessionIndices_data();
  }
  void originalSelectedHybridManagedAdmissionKeepsManualPause() {
    QFETCH(bool, v2); fixture::Fixture generated(v2);
    auto created = engine::PersistentSession::create({1001, 1002, 1003}); QVERIFY(created);
    auto owner = std::move(*created);
    const auto admitted = owner->startManaged(*generated.lease,
        {.hash = *generated.key, .manuallyPaused = true});
    QVERIFY2(admitted.has_value(), admitted ? "" : admitted.error().message.c_str());
    QTRY_VERIFY_WITH_TIMEOUT(owner->poll().has_value() && status(*owner, *generated.key)
        && !status(*owner, *generated.key)->failure
        && status(*owner, *generated.key)->checked
        && status(*owner, *generated.key)->phase == engine::ManagedPhase::Paused, 10000);
    const auto actual = status(*owner, *generated.key); QVERIFY(actual);
    QVERIFY(actual->retained && actual->manuallyPaused); QCOMPARE(actual->bytes, std::int64_t{16384});
    QCOMPARE(actual->received, std::int64_t{0}); QCOMPARE(actual->sent, std::int64_t{0});
    const auto closed = owner->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased && closed.retirement.peerCounters);
    QVERIFY(!closed.checkpointFailure);
    auto saved = core::Document::parse(fixture::read(generated.state), storage::compatibleConfigLimits()); QVERIFY(saved);
    QCOMPARE(std::string(fixture::take(storage::originalTorrentInfoSubject(*saved))), generated.rawInfo);
    const auto* properties = saved->find("azureus_private_properties"); QVERIFY(properties);
    const auto* selector = properties->find("hybrid_hash_v2"); QVERIFY(selector);
    QCOMPARE(selector->integer, std::int64_t{v2 ? 1 : 0});
    const auto* future = saved->find("future.root"); QVERIFY(future);
    QCOMPARE(std::string(future->bytes), std::string("opaque-selected-swarm"));
  }
  void companionRequiresActualRetirementAndOwnerSuppliesSharingProof() {
    fixture::Fixture generated(false);
    auto created=engine::PersistentSession::create({1901,1902,1903});QVERIFY(created);auto owner=std::move(*created);
    QVERIFY(owner->startManaged(*generated.lease,{.hash=*generated.key,.manuallyPaused=true}));
    QTRY_VERIFY_WITH_TIMEOUT(owner->poll().has_value()&&status(*owner,*generated.key)
        &&status(*owner,*generated.key)->phase==engine::ManagedPhase::Paused,10000);
    const auto before=fixture::read(generated.profile/"downloads.config");
    const auto refused=owner->createHybridCompanion(*generated.lease,*generated.key,storage::OriginalSelectedHash::V2);
    QVERIFY(!refused);QCOMPARE(refused.error().code,storage::ErrorCode::Conflict);
    QCOMPARE(fixture::read(generated.profile/"downloads.config"),before);
    QVERIFY(owner->stopManaged(*generated.key));
    QTRY_VERIFY_WITH_TIMEOUT(owner->poll().has_value()&&status(*owner,*generated.key)
        &&status(*owner,*generated.key)->phase==engine::ManagedPhase::Stopped&&!status(*owner,*generated.key)->retained,10000);
    const auto counterpart=owner->createHybridCompanion(*generated.lease,*generated.key,storage::OriginalSelectedHash::V2);
    QVERIFY2(counterpart,counterpart?"":counterpart.error().message.c_str());QVERIFY(*counterpart!=*generated.key);
    QVERIFY(owner->startManaged(*generated.lease,{.hash=*counterpart,.manuallyPaused=true}));
    const auto oldState=fixture::read(generated.state);
    const auto overlap=owner->startManaged(*generated.lease,{.hash=*generated.key,.manuallyPaused=true});
    QVERIFY(!overlap);QCOMPARE(overlap.error().code,storage::ErrorCode::Conflict);QCOMPARE(fixture::read(generated.state),oldState);
    const auto closed=owner->close(std::chrono::steady_clock::now()+5s);
    QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);
  }
  void optionalTrafficFaultsRetainOnlyTheProvenPayloadBridge_data() {
    QTest::addColumn<int>("mode");
    QTest::newRow("actual-v2-positive-control")<<static_cast<int>(TrafficFault::None);
    QTest::newRow("optional-pair-absent")<<static_cast<int>(TrafficFault::Absent);
    QTest::newRow("optional-marker-in-other-image")<<static_cast<int>(TrafficFault::ForeignMarker);
    QTest::newRow("optional-reader-in-other-image")<<static_cast<int>(TrafficFault::ForeignReader);
    QTest::newRow("optional-marker-wrong-after-real-proof")<<static_cast<int>(TrafficFault::WrongMarkerAfterProof);
    QTest::newRow("partial-marker-only")<<static_cast<int>(TrafficFault::MarkerOnly);
    QTest::newRow("partial-reader-only")<<static_cast<int>(TrafficFault::ReaderOnly);
  }
  void optionalTrafficFaultsRetainOnlyTheProvenPayloadBridge() {
    QFETCH(int,mode);fixture::Fixture generated(true);
    const bool combined=static_cast<TrafficFault>(mode)==TrafficFault::None;
    TrafficScope scope(static_cast<TrafficFault>(mode));
    std::weak_ptr<const engine::detail::ManagedPayloadCounterBridge> retained;
    const auto counters=std::make_shared<engine::detail::ManagedPayloadCounters>();
    {
      auto bridge=engine::detail::managedPayloadCounterBridge();
      QVERIFY2(bridge,"Optional ABI faults must retain the real full nine-export payload proof");
      QVERIFY(trafficMarkerLookups>0);
      const unsigned attempts=combined?1:2;
      // The failed optional proof/marker attempt is released; exactly ONE
      // provider reference is transferred to the resulting opaque bridge.
      QCOMPARE(providerPins.opens.load(),attempts);
      QCOMPARE(providerPins.closes.load(),attempts-1);
      QCOMPARE(providerPins.live.load(),1);QCOMPARE(providerPins.mismatches.load(),0u);
      for(std::size_t i=0;i<mandatorySymbols.size();++i){
        const unsigned factoryLookups=i>=6?2:0;
        QCOMPARE(mandatoryLookups[i],attempts*2+factoryLookups);
      }
      if(static_cast<TrafficFault>(mode)==TrafficFault::WrongMarkerAfterProof){QCOMPARE(wrongTrafficCalls,1u);}
      // Foreign executable symbols are rejected by the actual image proof,
      // before any callable marker/reader in that foreign image is invoked.
      QCOMPARE(foreignTrafficCalls,0u);
      retained=bridge;
      lt::add_torrent_params options;lt::error_code error;
      options.ti=std::make_shared<lt::torrent_info>(
          lt::span<const char>(generated.source.data(),generated.source.size()),error,lt::from_span);
      QVERIFY2(!error,error.message().c_str());options.save_path=generated.destination.string();
      options.flags&=~(lt::torrent_flags::paused|lt::torrent_flags::auto_managed);
      QVERIFY(engine::detail::selectManagedSwarm(options,*generated.identity,bridge));
      QVERIFY(engine::detail::attachManagedPayloadCounters(options,counters,bridge));
      bridge.reset();QVERIFY(!retained.expired());QCOMPARE(providerPins.live.load(),1);
      {
        lt::session_params settings(fixture::isolated());settings.disk_io_constructor=lt::disabled_disk_io_constructor;
        lt::session session(std::move(settings));
        const auto handle=session.add_torrent(std::move(options),error);
        QVERIFY2(!error,error.message().c_str());QVERIFY(handle.is_valid());
        QTRY_VERIFY_WITH_TIMEOUT(counters->epoch().has_value()
            &&handle.status().state!=lt::torrent_status::checking_resume_data
            &&handle.status().state!=lt::torrent_status::checking_files,10000);
        const auto before=counters->epoch();QVERIFY(before);const auto actual=handle.status();
        QVERIFY(actual.total_payload_download>=0&&actual.total_payload_upload>=0);
        QVERIFY(actual.total_download>=actual.total_payload_download&&actual.total_upload>=actual.total_payload_upload);
        const auto result=counters->combine(*before,actual.total_payload_download,actual.total_payload_upload,
            engine::detail::ManagedPayloadCounters::Protocol{actual.total_download-actual.total_payload_download,
                actual.total_upload-actual.total_payload_upload});
        QCOMPARE(result.state,engine::detail::ManagedPayloadCounters::Read::Accepted);
        QCOMPARE(result.received,actual.total_payload_download);QCOMPARE(result.sent,actual.total_payload_upload);
        if(combined){
          QVERIFY(result.combinedReceived&&result.combinedSent);
          QCOMPARE(*result.combinedReceived,actual.total_download);QCOMPARE(*result.combinedSent,actual.total_upload);
        }else{QVERIFY(!result.combinedReceived&&!result.combinedSent);}
        // The real plugin is usable after the factory caller released it;
        // neither callback reading nor optional failure creates another pin.
        QVERIFY(!retained.expired());QCOMPARE(providerPins.live.load(),1);
        QCOMPARE(providerPins.opens.load(),attempts);
      } // Actual abort/join and plugin destruction, with all gates absent.
    } // Moved-from options/factories cannot keep a second authority alive.
    QVERIFY(retained.expired());QCOMPARE(providerPins.live.load(),0);
    QCOMPARE(providerPins.opens.load(),providerPins.closes.load());QCOMPARE(providerPins.mismatches.load(),0u);
    QVERIFY(generated.untouched());
  }
  void missingForeignAndBadMarkersFailBeforeManagedStorage_data() {
    QTest::addColumn<int>("mode");
    QTest::newRow("missing-marker") << static_cast<int>(Fault::Missing);
    QTest::newRow("marker-one-in-foreign-image") << static_cast<int>(Fault::Foreign);
    QTest::newRow("zero-marker-after-real-proof") << static_cast<int>(Fault::ZeroAfterProof);
  }
  void missingForeignAndBadMarkersFailBeforeManagedStorage() {
    QFETCH(int, mode); fixture::Fixture generated(true);
    auto created = engine::PersistentSession::create({1101, 1102, 1103}); QVERIFY(created);
    auto owner = std::move(*created);
    {
      FaultScope scope(static_cast<Fault>(mode));
      const auto admitted = owner->startManaged(*generated.lease, {.hash = *generated.key});
      QVERIFY(!admitted); QCOMPARE(admitted.error().code, storage::ErrorCode::Unsupported);
      QVERIFY(owner->managedDownloads().empty()); QVERIFY(markerLookups != 0);
      if (fault == Fault::ZeroAfterProof) { QCOMPARE(zeroCalls, 1u); QVERIFY(markerLookups >= 3); }
      QVERIFY(generated.untouched());
    }
    const auto closed = owner->close(std::chrono::steady_clock::now() + 5s); QVERIFY(closed.sessionJoined);
    QVERIFY(generated.untouched());
  }
};
QTEST_GUILESS_MAIN(SelectedSwarmAdmissionTests)
#include "selected_swarm_admission_tests.moc"
