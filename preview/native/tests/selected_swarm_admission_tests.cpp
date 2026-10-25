#include "selected_swarm_fixture.hpp"
#include "../engine/managed_payload_counters.hpp"
#include "../engine/managed_storage.hpp"
#include "../engine/network_policy.hpp"
#include "../engine/persistent_session.hpp"

#include <libtorrent/biglybtc_swarm.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_status.hpp>
#include <QtTest>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <dlfcn.h>

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
std::optional<engine::ManagedDownloadStatus> status(engine::PersistentSession& owner,
    const core::TorrentHash& key) {
  const auto rows = owner.managedDownloads();
  const auto at = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.hash == key; });
  return at == rows.end() ? std::nullopt : std::optional{*at};
}
} // namespace

// Test-only ELF seam, scoped to this thread and this single symbol. Actual LT
// object/hash/descriptor proof and every other export remain unmodified.
// ZeroAfterProof is a synthetic ABI-policy fault, not a different approved DSO.
extern "C" void* __real_dlsym(void*, const char*) noexcept;
extern "C" void* __wrap_dlsym(void* library, const char* name) noexcept {
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
