#include "actual_alert_policy.hpp"
#include "owned_engine_fixture.hpp"
#include "persistent_runtime_config.hpp"

#if !BIGLYBTC_PERSISTENT_RUNTIME_BOUND
#error Actual-alert policy fixtures require the explicitly bound qualification build
#endif

#include <QDebug>
#include <QtTest>

#include <chrono>
#include <optional>
#include <utility>

namespace {
using namespace biglybtc;
using namespace std::chrono_literals;
namespace policy = test::actual_alert_policy;
namespace hook = libtorrent::biglybtc_causal;
using engine::AccountingError;
static_assert(static_cast<int>(AccountingError::DependencyUnqualified) > static_cast<int>(AccountingError::Engine),
    "Runtime guard integration must precede this qualification cohort");

auto owner() {
  auto created = engine::PersistentSession::create({1701, 1702, 1703});
  test::require(created.has_value(), "Exact instrumented runtime binding required; unqualified is not a skipped test");
  return std::move(*created);
}
auto held(const test::OwnedEngineFixture& fixture) {
  auto lease = storage::ProfileLease::acquire(fixture.native());
  test::require(lease.has_value(), "Cannot hold owned actual-alert profile");
  return std::move(*lease);
}
engine::LocalOptions options(const test::OwnedEngineFixture& fixture) {
  return {.profile = fixture.native(), .hash = *fixture.hash,
      .peerPort = fixture.port(), .timeout = 20s, .observer = &policy::observeStage};
}
void rows() {
  QTest::addColumn<int>("fault");
  for (const auto& item : {std::pair{"observed-global-error", hook::Fault::SessionError},
      {"dropped-global-error", hook::Fault::DroppedSessionError},
      {"dropped-removal", hook::Fault::DroppedRemoval},
      {"dropped-stats", hook::Fault::DroppedStats}}) {
    QTest::newRow(item.first) << static_cast<int>(item.second);
  }
}
std::int64_t savedCompleted(const test::OwnedEngineFixture& fixture) {
  const auto parsed = core::Document::parse(test::read(fixture.profile + QStringLiteral("/downloads.config")).toStdString());
  test::require(parsed.has_value(), "Cannot parse generated completion inventory");
  const auto* downloads = parsed->find("downloads");
  test::require(downloads && downloads->children.size() == 1, "Unexpected generated inventory shape");
  const auto* value = downloads->children.front().find("completedbytes");
  test::require(value && value->type == core::Node::Type::Integer, "Missing generated completion counter");
  return value->integer;
}
void logTrace(const char* phase, const policy::State& state) {
  qInfo().noquote() << "ACTUAL_ALERT_POLICY" << phase << static_cast<unsigned>(state.selected)
      << "posts" << state.realPosts << "pops" << state.realPops
      << "barriers" << state.barrierCalls << "matched" << state.matchedFaultBatches
      << "emitted" << state.emittedSequence << "copied" << state.copiedSequence;
  for (std::size_t i = 0; i < state.batchCount; ++i) {
    const auto& batch = state.batches[i];
    qInfo().noquote() << "ACTUAL_ALERT_BATCH" << i << batch.size << batch.stats
        << batch.sessionErrors << batch.droppedAlerts << batch.unexpectedDroppedBits
        << batch.droppedStats << batch.droppedRemoval
        << batch.droppedGlobal << batch.selectedFaultAndStats;
  }
}
class Report final {
public:
  Report(const char* phase, const policy::State& state) : phase_(phase), state_(state) {}
  ~Report() { logTrace(phase_, state_); }
private:
  const char* phase_;
  const policy::State& state_;
};
bool coalesced(const policy::State& state) {
  return !state.pendingPost && !state.expectingBatch && !state.protocolFailed && !state.overflow
      && state.barrierCalls == 1 && state.matchedFaultBatches == 1
      && state.emittedSequence > state.armAfter && state.copiedSequence > state.emittedSequence;
}
} // namespace

class PersistentActualAlertTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QByteArray(hook::build_identity()), QByteArray("biglybtc-causal-lt-2.0.12-v2"));
  }
  void realUnmodifiedQueueControlCompletesAndSettles() {
    test::OwnedEngineFixture fixture(false); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    auto lease = held(fixture); policy::State state; Report report("control", state); policy::Scope scope(state);
    auto service = owner(); policy::Controller controller;
    QVERIFY(engine::continueLocal(*service, lease, options(fixture)));
    const auto receipt = service->lastRetirement();
    QVERIFY(receipt && receipt->storageReleased && receipt->peerCounters && !receipt->accountingFailure);
    QVERIFY(receipt->peerCounters->counters().payloadReceived > 0);
    QVERIFY(state.realPosts > 0 && state.realPops > 0 && state.batchCount > 0);
    QCOMPARE(state.selected, hook::Fault::None); QCOMPARE(state.barrierCalls, std::size_t{0});
    for (std::size_t i = 0; i < state.batchCount; ++i) {
      const auto& batch = state.batches[i];
      QVERIFY(!batch.sessionErrors && !batch.droppedAlerts && !batch.unexpectedDroppedBits);
    }
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased && closed.retirement.peerCounters);
    QVERIFY(state.flushed && state.completionSaved && state.inventorySaved && fixture.intact());
    const auto ledger = hook::snapshot(); QVERIFY(!ledger.failed && !ledger.overflow);
    QVERIFY(!state.protocolFailed && !state.overflow);
  }
  void actualPreAdmissionFaultAndStatsSameBatch_data() { rows(); }
  void actualPreAdmissionFaultAndStatsSameBatch() {
    QFETCH(int, fault); const auto selected = static_cast<hook::Fault>(fault);
    test::OwnedEngineFixture fixture(false); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    auto lease = held(fixture); policy::State state; Report report("pre-admission", state); policy::Scope scope(state);
    auto service = owner(); policy::Controller controller;
    QVERIFY(policy::arm(state, selected));
    const auto result = engine::continueLocal(*service, lease, options(fixture));
    QVERIFY(coalesced(state));
    const auto receipt = service->lastRetirement();
    if (selected == hook::Fault::DroppedStats) {
      // Optional stats loss during prepare does NOT fabricate transfer failure.
      QVERIFY(result); QVERIFY(receipt && receipt->storageReleased && !receipt->peerCounters);
      QCOMPARE(receipt->accountingFailure, std::optional{AccountingError::CounterResponseLost});
      QVERIFY(state.flushed && state.completionSaved && state.inventorySaved && fixture.intact());
      QVERIFY(service->latest() && !service->latest()->observed);
    } else {
      QVERIFY(!result); QCOMPARE(result.error().code, storage::ErrorCode::Io);
      QVERIFY(!state.flushed && !state.completionSaved && !state.inventorySaved);
      QVERIFY(!receipt || (!receipt->storageReleased && !receipt->peerCounters));
      QCOMPARE(savedCompleted(fixture), std::int64_t{0});
      const auto beforePosts = state.realPosts;
      const auto retry = engine::continueLocal(*service, lease, options(fixture));
      QVERIFY(!retry); QCOMPARE(retry.error().code, storage::ErrorCode::Conflict);
      QCOMPARE(state.realPosts, beforePosts);
      const auto polled = service->poll(); QVERIFY(!polled);
      QCOMPARE(polled.error(), AccountingError::OwnerResetRequired);
      if (selected != hook::Fault::DroppedRemoval) {
        // Global error is processed BEFORE the valid stats in this SAME pop.
        QVERIFY(service->latest() && !service->latest()->observed);
      }
      QVERIFY(fixture.intact(false));
    }
    const auto close = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(close.sessionJoined && !close.retirement.peerCounters);
    QCOMPARE(close.retirement.storageReleased, selected == hook::Fault::DroppedStats);
    const auto ledger = hook::snapshot(); QVERIFY(!ledger.failed && !ledger.overflow);
  }
  void actualActivePayloadFaultAndStatsSameBatch_data() { rows(); }
  void actualActivePayloadFaultAndStatsSameBatch() {
    QFETCH(int, fault); const auto selected = static_cast<hook::Fault>(fault);
    test::OwnedEngineFixture fixture(false); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    auto lease = held(fixture); policy::State state; Report report("active-positive-payload", state); policy::Scope scope(state);
    auto service = owner(); policy::Controller controller;
    auto opt = options(fixture);
    opt.progress = [&](const engine::LocalProgress& progress) {
      if (progress.phase == engine::LocalProgress::Phase::Flushing) { state.reachedFlushing = true; }
    };
    opt.statistics = [&](const core::EngineStatistics& actual) {
      if (state.selected == hook::Fault::None && actual.counters.payloadReceived > 0
          && actual.counters.payloadReceived < test::OwnedEngineFixture::PayloadBytes) {
        state.payloadAtArm = actual.counters.payloadReceived;
        const auto retained = service->latest();
        if (retained) { state.observedBeforeFault = retained->observed; }
        state.armedFromPositiveIncompletePayload = policy::arm(state, selected);
        if (!state.armedFromPositiveIncompletePayload) { state.protocolFailed = true; }
      }
    };
    const auto result = engine::continueLocal(*service, lease, opt);
    QVERIFY(state.armedFromPositiveIncompletePayload);
    QVERIFY(state.payloadAtArm > 0 && state.payloadAtArm < test::OwnedEngineFixture::PayloadBytes);
    QVERIFY(coalesced(state) && state.faultBatchBeforeFlushing);
    const auto receipt = service->lastRetirement(); QVERIFY(receipt && !receipt->peerCounters);
    QVERIFY(service->latest() && service->latest()->observed);
    QVERIFY(service->latest()->observed->counters.payloadReceived > 0);
    if (selected != hook::Fault::DroppedRemoval) {
      // Includes elapsed/rates, so equal counters from a newly accepted stats
      // object cannot masquerade as the previously retained observation.
      QVERIFY(state.observedBeforeFault);
      QCOMPARE(*service->latest()->observed, *state.observedBeforeFault);
    }
    if (selected == hook::Fault::DroppedStats) {
      QVERIFY(result); QVERIFY(receipt->storageReleased);
      QCOMPARE(receipt->accountingFailure, std::optional{AccountingError::CounterResponseLost});
      QVERIFY(state.flushed && state.completionSaved && state.inventorySaved && fixture.intact());
    } else {
      QVERIFY(!result); QCOMPARE(result.error().code, storage::ErrorCode::Io);
      QVERIFY(!receipt->storageReleased);
      QCOMPARE(receipt->accountingFailure, std::optional{AccountingError::StorageFenceLost});
      QVERIFY(!state.flushed && !state.completionSaved && !state.inventorySaved);
      QCOMPARE(savedCompleted(fixture), std::int64_t{0}); QVERIFY(fixture.intact(false));
      const auto beforePosts = state.realPosts;
      const auto retry = engine::continueLocal(*service, lease, opt); QVERIFY(!retry);
      QCOMPARE(retry.error().code, storage::ErrorCode::Conflict); QCOMPARE(state.realPosts, beforePosts);
      const auto polled = service->poll(); QVERIFY(!polled);
      QCOMPARE(polled.error(), AccountingError::OwnerResetRequired);
    }
    const auto close = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(close.sessionJoined && !close.retirement.peerCounters);
    QCOMPARE(close.retirement.storageReleased, selected == hook::Fault::DroppedStats);
    const auto ledger = hook::snapshot(); QVERIFY(!ledger.failed && !ledger.overflow);
  }
  void actualCloseFaultCannotUpgradeEarlierSettledCheckpoint_data() { rows(); }
  void actualCloseFaultCannotUpgradeEarlierSettledCheckpoint() {
    QFETCH(int, fault); const auto selected = static_cast<hook::Fault>(fault);
    test::OwnedEngineFixture fixture(false); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    auto lease = held(fixture); policy::State state; Report report("close-after-settlement", state); policy::Scope scope(state);
    auto service = owner(); policy::Controller controller;
    QVERIFY(engine::continueLocal(*service, lease, options(fixture)));
    const auto prior = service->lastRetirement(); QVERIFY(prior && prior->storageReleased && prior->peerCounters);
    const auto earlierCounters = prior->peerCounters->counters();
    QVERIFY(earlierCounters.payloadReceived > 0);
    const auto earlierObservation = service->latest();
    QVERIFY(earlierObservation && earlierObservation->observed);
    const auto beforePosts = state.realPosts;
    QVERIFY(policy::arm(state, selected));
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(coalesced(state)); QCOMPARE(state.realPosts, beforePosts + 1);
    QVERIFY(closed.sessionJoined && !closed.retirement.peerCounters);
    QCOMPARE(closed.retirement.storageReleased, selected == hook::Fault::DroppedStats);
    QCOMPARE(closed.retirement.accountingFailure, std::optional{selected == hook::Fault::DroppedStats
        ? AccountingError::CounterResponseLost : AccountingError::StorageFenceLost});
    const auto retained = service->lastRetirement(); QVERIFY(retained && !retained->peerCounters);
    QVERIFY(service->latest() && service->latest()->observed);
    QCOMPARE(service->latest()->observed->counters, earlierCounters);
    if (selected != hook::Fault::DroppedRemoval) {
      QCOMPARE(*service->latest()->observed, *earlierObservation->observed);
    }
    // prior remains only the earlier value-copy checkpoint. It is neither the
    // uncertain close result nor a receipt for a future admitted operation.
    QCOMPARE(prior->peerCounters->counters(), earlierCounters);
    QVERIFY(!service->poll());
    QVERIFY(!engine::continueLocal(*service, lease, options(fixture)));
    QVERIFY(fixture.intact());
    const auto ledger = hook::snapshot(); QVERIFY(!ledger.failed && !ledger.overflow);
  }
};
QTEST_GUILESS_MAIN(PersistentActualAlertTests)
#include "persistent_actual_alert_tests.moc"
