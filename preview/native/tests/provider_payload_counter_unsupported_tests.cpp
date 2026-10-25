#include "managed_payload_counters.hpp"
#include "persistent_session.hpp"
#include "owned_engine_fixture.hpp"
#include "selected_swarm_fixture.hpp"

#include <QtTest>

#include <chrono>
#include <memory>

using namespace biglybtc;
using namespace std::chrono_literals;

// Separate NEGATIVE lane: run against the ordinary default build or the old
// approved c35f SDK/provider. Running it on the new approved bridge must FAIL.
class ProviderPayloadCounterUnsupportedTests final : public QObject {
  Q_OBJECT
private slots:
  void defaultOrStockRejectsBeforePayloadAndProfileMutation() {
    QVERIFY(!engine::detail::managedPayloadCounterBridge());
    libtorrent::add_torrent_params options;
    const auto counters = std::make_shared<engine::detail::ManagedPayloadCounters>();
    counters->installed(); QVERIFY(counters->epoch());
    const auto extensions = options.extensions.size();
    QVERIFY(!engine::detail::attachManagedPayloadCounters(options, counters, {}));
    QVERIFY(!counters->epoch()); QCOMPARE(options.extensions.size(), extensions);
    test::OwnedEngineFixture fixture(false);
    const auto inventory = test::read(fixture.profile + QStringLiteral("/downloads.config"));
    const auto state = test::read(fixture.stateFile);
    const auto payload = test::read(fixture.payload + '/' + fixture.files.front().relative);
    auto lease = storage::ProfileLease::acquire(fixture.native()); QVERIFY(lease);
    auto result = engine::PersistentSession::create({701, 702, 703});
    if (result) {
      // The old SOURCE-BOUND provider can still own a legacy session, but it
      // cannot enter the new managed path without the additional capability.
      auto owner = std::move(*result);
      const auto admitted = owner->startManaged(*lease, {.hash = *fixture.hash});
      QVERIFY(!admitted); QCOMPARE(admitted.error().code, storage::ErrorCode::Unsupported);
      QVERIFY(owner->managedDownloads().empty());
      const auto closed = owner->close(std::chrono::steady_clock::now() + 5s);
      QVERIFY(closed.sessionJoined);
      qInfo("Old source-bound provider rejected managed counter ABI before storage preparation");
    } else {
      QCOMPARE(result.error(), engine::AccountingError::DependencyUnqualified);
      qInfo("Default unbound factory rejected before LT creation; stock-bound managed path not exercised in this lane");
    }
    QCOMPARE(test::read(fixture.profile + QStringLiteral("/downloads.config")), inventory);
    QCOMPARE(test::read(fixture.stateFile), state);
    QCOMPARE(test::read(fixture.payload + '/' + fixture.files.front().relative), payload);
    QVERIFY(!QFileInfo::exists(fixture.stateFile + QStringLiteral(".saving")));
    QVERIFY(!QFileInfo::exists(fixture.profile + QStringLiteral("/downloads.config.saving")));
    QVERIFY(fixture.intact(false));
  }
  void selectedHybridAlsoRejectsBeforeProfileAndPayload_data() {
    QTest::addColumn<bool>("v2");
    QTest::newRow("original-v1") << false; QTest::newRow("original-v2") << true;
  }
  void selectedHybridAlsoRejectsBeforeProfileAndPayload() {
    QFETCH(bool, v2);
    test::selected_swarm::Fixture fixture(v2);
    QVERIFY(!engine::detail::managedPayloadCounterBridge());
    auto created = engine::PersistentSession::create({801, 802, 803});
    if (created) {
      auto owner = std::move(*created);
      const auto admitted = owner->startManaged(*fixture.lease, {.hash = *fixture.key});
      QVERIFY(!admitted); QCOMPARE(admitted.error().code, storage::ErrorCode::Unsupported);
      QVERIFY(owner->managedDownloads().empty());
      const auto closed = owner->close(std::chrono::steady_clock::now() + 5s);
      QVERIFY(closed.sessionJoined);
    } else {
      QCOMPARE(created.error(), engine::AccountingError::DependencyUnqualified);
    }
    QVERIFY(fixture.untouched());
  }
};
QTEST_GUILESS_MAIN(ProviderPayloadCounterUnsupportedTests)
#include "provider_payload_counter_unsupported_tests.moc"
