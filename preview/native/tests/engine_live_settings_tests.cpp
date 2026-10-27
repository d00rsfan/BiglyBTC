#include "persistent_session.hpp"
#include "live_settings.hpp"
#include "owned_engine_fixture.hpp"

#include <QtTest>

#include <chrono>
#include <algorithm>
#include <array>
#include <thread>

namespace {
using namespace biglybtc;
using namespace std::chrono_literals;
namespace lt = libtorrent;
std::unique_ptr<engine::PersistentSession> owner() {
  auto value = engine::PersistentSession::create({801, 802, 803});
  test::require(value.has_value(), "Live settings tests require the approved source-bound provider");
  return std::move(*value);
}
storage::ProfileLease held(const test::OwnedEngineFixture& fixture) {
  auto value = storage::ProfileLease::acquire(fixture.native());
  test::require(value.has_value(), "Cannot hold generated live settings profile");
  return std::move(*value);
}
std::optional<engine::ManagedDownloadStatus> status(engine::PersistentSession& service, const core::TorrentHash& hash) {
  const auto all = service.managedDownloads();
  const auto found = std::ranges::find(all, hash, &engine::ManagedDownloadStatus::hash);
  return found == all.end() ? std::nullopt : std::optional{*found};
}
bool phase(engine::PersistentSession& service, const core::TorrentHash& hash, engine::ManagedPhase expected) {
  const auto value = status(service, hash);
  return value && !value->failure && value->phase == expected;
}
void routesEqual(const engine::LiveSettingsSnapshot& before, const engine::LiveSettingsSnapshot& after) {
  QCOMPARE(after.listenInterfaces, before.listenInterfaces);
  QCOMPARE(after.outgoingInterfaces, before.outgoingInterfaces);
  QCOMPARE(after.proxyType, before.proxyType);
  QCOMPARE(after.dht, before.dht); QCOMPARE(after.localDiscovery, before.localDiscovery);
  QCOMPARE(after.upnp, before.upnp); QCOMPARE(after.natpmp, before.natpmp);
}
} // namespace

class EngineLiveSettingsTests final : public QObject {
  Q_OBJECT
private slots:
  void originalLanRateClassesAreAppliedAndSafeLimitsRemainLive() {
    test::OwnedEngineFixture fixture(false); auto lease = held(fixture); auto service = owner();
    engine::NetworkPolicy policy; policy.mode = engine::NetworkMode::Normal;
    policy.lanRates = true; policy.lanUploadBytesPerSecond = 17408; policy.lanDownloadBytesPerSecond = 23552;
    policy.uploadBytesPerSecond = 1024; policy.downloadBytesPerSecond = 2048;
    QVERIFY(service->configureNetwork(policy)); QVERIFY(service->startIdle(lease));
    const auto initial = service->liveSettings(); QVERIFY(initial); QVERIFY(initial->separateLanClass);
    QCOMPARE(initial->lanUpload, core::effectiveSessionRateLimit(17408));
    QCOMPARE(initial->lanDownload, core::effectiveSessionRateLimit(23552));
    QCOMPARE(initial->rateLimits.upload, core::effectiveSessionRateLimit(1024));
    auto desired = policy; desired.lanRates = false;
    desired.lanUploadBytesPerSecond = 3072; desired.lanDownloadBytesPerSecond = 8192;
    const auto changed = service->applyLiveSettings(desired); QVERIFY(changed);
    QCOMPARE(changed->restartFields, static_cast<std::uint32_t>(engine::LiveRestartField::LanClassification));
    const auto actual = service->liveSettings(); QVERIFY(actual); QVERIFY(actual->separateLanClass);
    QCOMPARE(actual->lanUpload, core::effectiveSessionRateLimit(3072));
    QCOMPARE(actual->lanDownload, core::effectiveSessionRateLimit(8192));
    routesEqual(*initial, *actual);
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased);
  }
  void disabledLanRatesDoNotInheritLibtorrentsUnlimitedLocalClass() {
    test::OwnedEngineFixture fixture(false); auto lease = held(fixture); auto service = owner();
    engine::NetworkPolicy policy; policy.mode = engine::NetworkMode::Normal;
    policy.uploadBytesPerSecond = 1024; policy.downloadBytesPerSecond = 2048;
    QVERIFY(service->configureNetwork(policy)); QVERIFY(service->startIdle(lease));
    const auto actual = service->liveSettings(); QVERIFY(actual); QVERIFY(!actual->separateLanClass);
    QCOMPARE(actual->rateLimits.upload, core::effectiveSessionRateLimit(1024));
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased);
  }
  void purePlanPreservesAllActiveRoutingAndEmitsOnlyNarrowKeys() {
    engine::NetworkPolicy active, desired;
    desired.mode = engine::NetworkMode::Normal;
    desired.bindAddresses = {"0.0.0.0"}; desired.listenPort = 34567;
    desired.incomingTcp = false; desired.outgoingUtp = true;
    desired.httpTrackers = true; desired.pex = true; desired.webSeeds = true;
    desired.lsd = true; desired.upnp = true; desired.natpmp = true; desired.requireEncryption = true;
    desired.proxy.kind = engine::NetworkProxyKind::Socks5; desired.proxy.host = "127.0.0.1";
    desired.proxy.port = 9; desired.proxy.trackers = true;
    desired.uploadBytesPerSecond = 12345; desired.downloadBytesPerSecond = 54321;
    desired.seedingOnlyUpload = true; desired.seedingUploadBytesPerSecond = 2345;
    const auto plan = engine::prepareLiveSettings(active, desired, engine::LiveQueueLimits{0, 4});
    QVERIFY(plan); QCOMPARE(plan->restartFields, std::uint32_t{127});
    QCOMPARE(plan->effectivePolicy.mode, engine::NetworkMode::IsolatedLoopback);
    QCOMPARE(plan->effectivePolicy.bindAddresses, active.bindAddresses);
    QCOMPARE(plan->effectivePolicy.listenPort, active.listenPort);
    QCOMPARE(plan->effectivePolicy.proxy.kind, engine::NetworkProxyKind::None);
    QVERIFY(!plan->effectivePolicy.lsd && !plan->effectivePolicy.upnp && !plan->effectivePolicy.natpmp);
    QVERIFY(!plan->effectivePolicy.httpTrackers && !plan->effectivePolicy.pex && !plan->effectivePolicy.webSeeds);
    QCOMPARE(active.uploadBytesPerSecond, 0); // Pure plan changed neither input.
    QCOMPARE(desired.listenPort, 34567);
    const auto pack = engine::makeLiveSettingsPack(plan->effectivePolicy, plan->queues, true); QVERIFY(pack);
    QCOMPARE(pack->get_int(lt::settings_pack::upload_rate_limit), 2345);
    QCOMPARE(pack->get_int(lt::settings_pack::download_rate_limit), 54321);
    QCOMPARE(pack->get_int(lt::settings_pack::active_downloads), -1);
    QCOMPARE(pack->get_int(lt::settings_pack::active_limit), 4);
    QCOMPARE(pack->get_int(lt::settings_pack::active_seeds), -1);
    QVERIFY(!pack->get_bool(lt::settings_pack::dont_count_slow_torrents));
    for (const int key : std::array<int, 11>{lt::settings_pack::listen_interfaces, lt::settings_pack::outgoing_interfaces,
        lt::settings_pack::proxy_hostname, lt::settings_pack::proxy_password, lt::settings_pack::dht_bootstrap_nodes,
        lt::settings_pack::proxy_type, lt::settings_pack::out_enc_policy, lt::settings_pack::enable_dht,
        lt::settings_pack::enable_lsd, lt::settings_pack::enable_upnp, lt::settings_pack::enable_natpmp}) {
      QVERIFY(!pack->has_val(key));
    }
    const auto ratesOnly = engine::makeLiveSettingsPack(active); QVERIFY(ratesOnly);
    QVERIFY(!ratesOnly->has_val(lt::settings_pack::active_limit));
    QVERIFY(!ratesOnly->has_val(lt::settings_pack::dont_count_slow_torrents));
    QVERIFY(!engine::prepareLiveSettings(active, desired, engine::LiveQueueLimits{-1, 4}));
    QVERIFY(!engine::prepareLiveSettings(active, desired, engine::LiveQueueLimits{4, engine::LiveQueueLimits::Maximum + 1}));
    desired.downloadBytesPerSecond = -1; QVERIFY(!engine::prepareLiveSettings(active, desired));
  }

  void cachedBeforeIdleCreatesNoSessionAndStartupUsesOriginalQueueDefaults() {
    test::OwnedEngineFixture fixture(false);
    auto lease = held(fixture); auto service = owner(); engine::NetworkPolicy desired;
    desired.uploadBytesPerSecond = 12000; desired.downloadBytesPerSecond = 24000;
    QVERIFY(service->configureNetwork({}));
    const auto planned = engine::prepareLiveSettings({}, desired, engine::LiveQueueLimits{}); QVERIFY(planned);
    QVERIFY(!service->liveSettings()); QVERIFY(!service->tcpListenPort()); QVERIFY(!service->latest());
    const auto cached = service->applyLiveSettings(desired, engine::LiveQueueLimits{}); QVERIFY(cached);
    QVERIFY(!cached->appliedToLiveSession); QCOMPARE(cached->restartFields, std::uint32_t{0});
    QVERIFY(!service->liveSettings()); QVERIFY(!service->tcpListenPort()); QVERIFY(!service->latest());
    QVERIFY(service->startIdle(lease));
    QTRY_VERIFY_WITH_TIMEOUT(service->tcpListenPort().has_value(), 5000);
    const auto actual = service->liveSettings(); QVERIFY(actual);
    QCOMPARE(actual->activeDownloads, 4); QCOMPARE(actual->activeLimit, 4); QCOMPARE(actual->activeSeeds, -1);
    QVERIFY(actual->countSlowTorrents);
    QCOMPARE(actual->rateLimits.upload, core::effectiveSessionRateLimit(12000));
    QCOMPARE(actual->rateLimits.download, core::effectiveSessionRateLimit(24000));
    QVERIFY(!actual->dht && !actual->localDiscovery && !actual->upnp && !actual->natpmp);
    QCOMPARE(actual->proxyType, int{lt::settings_pack::none});
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased);
    QVERIFY(fixture.intact(false));
  }

  void liveRatesAndQueueCapsKeepActualRoutesAndManualPauseAcrossActiveTransfer() {
    test::OwnedEngineFixture fixture(false, true);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    auto lease = held(fixture); auto service = owner(); engine::NetworkPolicy active;
    QVERIFY(service->configureNetwork(active));
    QVERIFY(service->startManaged(lease, {.hash = *fixture.hash, .ownedLoopbackPeerPort = fixture.port(), .forceStart = true}));
    QTRY_VERIFY_WITH_TIMEOUT(([&] { const auto p = service->poll(); const auto s = status(*service, *fixture.hash);
      return p && s && !s->failure && s->received > 0 && s->phase == engine::ManagedPhase::Downloading; })(), 30000);
    const auto before = service->liveSettings(); QVERIFY(before);
    const auto port = service->tcpListenPort(); QVERIFY(port);
    const auto running = status(*service, *fixture.hash); QVERIFY(running);
    auto desired = active;
    desired.mode = engine::NetworkMode::Normal; desired.listenPort = 34567; desired.bindAddresses = {"0.0.0.0"};
    desired.proxy.kind = engine::NetworkProxyKind::Socks5; desired.proxy.host = "127.0.0.1";
    desired.proxy.port = 9; desired.proxy.trackers = true; desired.httpTrackers = true;
    desired.lsd = true; desired.upnp = true; desired.natpmp = true;
    desired.uploadBytesPerSecond = 17000; desired.downloadBytesPerSecond = 19000;
    const auto prepared = engine::prepareLiveSettings(active, desired, engine::LiveQueueLimits{1, 2}); QVERIFY(prepared);
    QCOMPARE(service->liveSettings(), before); // Validation before a hypothetical failed save has no effect.
    const auto applied = service->applyLiveSettings(desired, engine::LiveQueueLimits{1, 2}); QVERIFY(applied);
    QVERIFY(applied->appliedToLiveSession); QCOMPARE(applied->restartFields, prepared->restartFields); QVERIFY(applied->restartFields);
    const auto after = service->liveSettings(); QVERIFY(after); routesEqual(*before, *after);
    QCOMPARE(service->tcpListenPort(), port);
    QCOMPARE(after->rateLimits.upload, core::effectiveSessionRateLimit(17000));
    QCOMPARE(after->rateLimits.download, core::effectiveSessionRateLimit(19000));
    QCOMPARE(after->activeDownloads, 1); QCOMPARE(after->activeLimit, 2); QCOMPARE(after->activeSeeds, -1);
    QVERIFY(after->countSlowTorrents);
    QVERIFY(service->pauseManaged(*fixture.hash, true));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && phase(*service, *fixture.hash, engine::ManagedPhase::Paused), 5000);
    auto safe = prepared->effectivePolicy; safe.uploadBytesPerSecond = 0; safe.downloadBytesPerSecond = 0;
    const auto unlimited = service->applyLiveSettings(safe, engine::LiveQueueLimits{0, 0}); QVERIFY(unlimited);
    QCOMPARE(unlimited->restartFields, std::uint32_t{0});
    const auto uncapped = service->liveSettings(); QVERIFY(uncapped);
    QCOMPARE(uncapped->activeDownloads, -1); QCOMPARE(uncapped->activeLimit, -1);
    QCOMPARE(uncapped->rateLimits.upload.state, core::RateLimitState::Unlimited);
    QVERIFY(service->poll()); QVERIFY(phase(*service, *fixture.hash, engine::ManagedPhase::Paused));
    QVERIFY(service->pauseManaged(*fixture.hash, false));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && phase(*service, *fixture.hash, engine::ManagedPhase::Downloading), 5000);
    const auto resumed = status(*service, *fixture.hash); QVERIFY(resumed);
    QCOMPARE(resumed->admission, running->admission); QVERIFY(resumed->received >= running->received);
    const auto retainedRoutes = service->liveSettings(); QVERIFY(retainedRoutes);
    routesEqual(*before, *retainedRoutes); QCOMPARE(service->tcpListenPort(), port);
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased && !closed.checkpointFailure);
    QVERIFY(fixture.intact(false));
  }

  void seedingUploadTransitionPreservesNewSafeRatesAndQueueCaps() {
    test::OwnedEngineFixture fixture(false);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    auto lease = held(fixture); auto service = owner(); engine::NetworkPolicy desired;
    desired.uploadBytesPerSecond = 16000; desired.seedingUploadBytesPerSecond = 4000; desired.seedingOnlyUpload = true;
    QVERIFY(service->configureNetwork({}));
    QVERIFY(service->applyLiveSettings(desired, engine::LiveQueueLimits{2, 3}));
    QVERIFY(service->startManaged(lease, {.hash = *fixture.hash, .ownedLoopbackPeerPort = fixture.port(), .forceStart = true}));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && phase(*service, *fixture.hash, engine::ManagedPhase::Seeding), 30000);
    const auto actual = service->liveSettings(); QVERIFY(actual);
    QCOMPARE(actual->rateLimits.upload, core::effectiveSessionRateLimit(4000));
    QCOMPARE(actual->activeDownloads, 2); QCOMPARE(actual->activeLimit, 3); QCOMPARE(actual->activeSeeds, -1);
    desired.uploadBytesPerSecond = 18000; desired.seedingUploadBytesPerSecond = 6000;
    QVERIFY(service->applyLiveSettings(desired)); // Omitted queue argument preserves cached caps.
    const auto reseeded = service->liveSettings(); QVERIFY(reseeded);
    QCOMPARE(reseeded->rateLimits.upload, core::effectiveSessionRateLimit(6000));
    desired.seedingOnlyUpload = false; QVERIFY(service->applyLiveSettings(desired));
    const auto final = service->liveSettings(); QVERIFY(final);
    QCOMPARE(final->rateLimits.upload, core::effectiveSessionRateLimit(18000));
    QCOMPARE(final->activeDownloads, 2); QCOMPARE(final->activeLimit, 3);
    routesEqual(*actual, *final);
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased && !closed.checkpointFailure);
    QVERIFY(fixture.intact());
  }

  void invalidOrForeignThreadRequestLeavesEveryActualSettingUnchanged() {
    test::OwnedEngineFixture fixture(false); auto lease = held(fixture); auto service = owner();
    engine::NetworkPolicy active; QVERIFY(service->configureNetwork(active)); QVERIFY(service->startIdle(lease));
    const auto before = service->liveSettings(); QVERIFY(before);
    auto invalid = active; invalid.downloadBytesPerSecond = -1;
    QVERIFY(!service->applyLiveSettings(invalid));
    QVERIFY(!service->applyLiveSettings(active, engine::LiveQueueLimits{-1, 4}));
    invalid = active; invalid.bindAddresses = {"not-a-numeric-address"};
    QVERIFY(!service->applyLiveSettings(invalid));
    QCOMPARE(service->liveSettings(), before);
    bool denied = false, getterDenied = false;
    std::jthread foreign([&] { denied = !service->applyLiveSettings(active); getterDenied = !service->liveSettings(); });
    foreign.join(); QVERIFY(denied && getterDenied); QCOMPARE(service->liveSettings(), before);
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased);
    QVERIFY(!service->applyLiveSettings(active)); QVERIFY(!service->liveSettings());
    QVERIFY(fixture.intact(false));
  }
};
QTEST_GUILESS_MAIN(EngineLiveSettingsTests)
#include "engine_live_settings_tests.moc"
