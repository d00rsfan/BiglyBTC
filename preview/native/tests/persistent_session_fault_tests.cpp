#include "persistent_api_faults.hpp"
#include "persistent_session.hpp"
#include "owned_engine_fixture.hpp"

#include <QtTest>

#include <algorithm>
#include <chrono>
#include <optional>
#include <stop_token>
#include <utility>

namespace {
using namespace biglybtc;
using namespace std::chrono_literals;
namespace fault = test::accounting_fault;
using engine::AccountingError;
auto owner() {
  auto value = engine::PersistentSession::create({101, 202, 303});
  test::require(value.has_value(), "Cannot create generated accounting owner");
  return std::move(*value);
}
auto held(const test::OwnedEngineFixture& fixture) {
  auto value = storage::ProfileLease::acquire(fixture.native());
  test::require(value.has_value(), "Cannot hold generated accounting profile");
  return std::move(*value);
}
engine::LocalOptions options(const test::OwnedEngineFixture& fixture) {
  return {.profile=fixture.native(), .hash=*fixture.hash, .peerPort=fixture.port(), .timeout=20s};
}
std::size_t find(const fault::State& state, fault::EventKind kind, std::size_t begin = 0) {
  for (auto i = begin; i < state.eventCount; ++i) { if (state.events[i].kind == kind) { return i; } }
  return state.eventCount;
}
thread_local bool flushed = false;
void publication(engine::LocalStage stage) { if (stage == engine::LocalStage::Flushed) { flushed = true; } }
std::int64_t savedCompleted(const test::OwnedEngineFixture& fixture) {
  const auto parsed = core::Document::parse(test::read(fixture.profile + QStringLiteral("/downloads.config")).toStdString());
  test::require(parsed.has_value(), "Cannot parse generated completion inventory");
  return parsed->find("downloads")->children.at(0).find("completedbytes")->integer;
}
} // namespace

class PersistentSessionFaultTests final : public QObject {
  Q_OBJECT
private slots:
  void realPassThroughHasAllRequiredWrapperHitsAndActualCounterReceipt() {
    test::OwnedEngineFixture fixture(false); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    auto lease = held(fixture); fault::State state; fault::Scope seam(state); auto service = owner();
    QVERIFY(engine::continueLocal(*service, lease, options(fixture))); QVERIFY(fixture.intact());
    const auto receipt = service->lastRetirement(); QVERIFY(receipt && receipt->storageReleased && receipt->peerCounters);
    QVERIFY(state.receiver && state.metricCalls == 1 && state.realPosts > 0 && state.realPops > 0);
    QVERIFY(state.actualStats > 0 && state.deliveredStats == state.actualStats);
    QCOMPARE(state.removals, std::size_t{1}); QCOMPARE(state.deliveredRemovals, std::size_t{1});
    QVERIFY(!state.removing.is_valid()); QVERIFY(state.lastActualCounters);
    QCOMPARE(receipt->peerCounters->counters(), *state.lastActualCounters);
    QVERIFY(receipt->peerCounters->counters().payloadReceived > 0);
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased && closed.retirement.peerCounters);
    QCOMPARE(closed.retirement.peerCounters->counters(), *state.lastActualCounters);
    QVERIFY(!state.overflow && !state.mappingFaults && !state.syntheticSpans && !state.postThrows);
  }
  void oldIdleRequestMustBeDeliveredBeforeDistinctCloseProbe() {
    test::OwnedEngineFixture fixture(false); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    auto lease = held(fixture); fault::State state; fault::Scope seam(state); auto service = owner();
    QVERIFY(engine::continueLocal(*service, lease, options(fixture)));
    const auto before = service->lastRetirement(); QVERIFY(before && before->peerCounters);
    const auto oldCheckpoint = before->peerCounters->counters();
    state.mode = fault::Mode::HoldIdleResponse; state.holdNextPost = true;
    const auto beforePosts = state.realPosts, beforeEvents = state.eventCount;
    // Wait for an ACTUAL cadence post, never infer an outstanding response from
    // elapsed sleeps. The first poll that posts ends this wait immediately.
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && state.realPosts > beforePosts, 3000);
    QCOMPARE(state.realPosts, beforePosts + 1); QVERIFY(state.oldPending);
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased && closed.retirement.peerCounters);
    QCOMPARE(state.totalHeldTurns, std::size_t{3}); QVERIFY(!state.oldPending && !state.extraPostBeforeOld);
    QCOMPARE(state.realPosts, beforePosts + 2); QVERIFY(state.oldDeliveredEvent > beforeEvents);
    const auto oldDelivered = state.oldDeliveredEvent - 1;
    const auto freshPost = find(state, fault::EventKind::RealPost, oldDelivered + 1);
    const auto freshDelivered = find(state, fault::EventKind::DeliveredStats, freshPost + 1);
    QVERIFY(oldDelivered < freshPost && freshPost < freshDelivered && freshDelivered < state.eventCount);
    QCOMPARE(closed.retirement.peerCounters->counters(), *state.lastActualCounters);
    QVERIFY(closed.retirement.peerCounters->sequence() > before->peerCounters->sequence());
    QCOMPARE(before->peerCounters->counters(), oldCheckpoint); QVERIFY(!state.overflow && fixture.intact());
  }
  void losingActualCloseResponseCannotPromoteAnOlderCheckpoint() {
    test::OwnedEngineFixture fixture(false); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    auto lease = held(fixture); fault::State state; fault::Scope seam(state); auto service = owner();
    QVERIFY(engine::continueLocal(*service, lease, options(fixture)));
    const auto before = service->lastRetirement(); QVERIFY(before && before->peerCounters);
    const auto checkpoint = before->peerCounters->counters();
    state.mode = fault::Mode::DropStats; const auto posts = state.realPosts;
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased); QVERIFY(!closed.retirement.peerCounters);
    QCOMPARE(closed.retirement.accountingFailure, std::optional{AccountingError::CounterResponseLost});
    QCOMPARE(state.droppedStats, std::size_t{1}); QCOMPARE(state.realPosts, posts + 1);
    QCOMPARE(before->peerCounters->counters(), checkpoint);
    QVERIFY(service->latest() && service->latest()->observed);
    QCOMPARE(service->latest()->observed->counters, checkpoint);
    QVERIFY(!state.overflow && fixture.intact());
  }
  void attributablePostExceptionDoesNotBlockRealStorageOrInventZeros() {
    test::OwnedEngineFixture fixture(false); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    auto lease = held(fixture); fault::State state; state.mode = fault::Mode::ThrowPost;
    fault::Scope seam(state); auto service = owner(); flushed = false;
    auto opt = options(fixture); opt.observer = &publication;
    QVERIFY(engine::continueLocal(*service, lease, opt)); QVERIFY(flushed && fixture.intact());
    QCOMPARE(state.postThrows, std::size_t{1}); QCOMPARE(state.realPosts, std::size_t{0});
    const auto receipt = service->lastRetirement(); QVERIFY(receipt && receipt->storageReleased);
    QVERIFY(!receipt->peerCounters); QCOMPARE(receipt->accountingFailure, std::optional{AccountingError::Engine});
    QVERIFY(service->latest()); QVERIFY(!service->latest()->observed);
    QCOMPARE(state.deliveredRemovals, std::size_t{1}); QVERIFY(!state.removing.is_valid());
    const auto close = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(close.sessionJoined && !close.retirement.peerCounters); QVERIFY(!state.overflow);
  }
  void ownedRuntimeMetricMappingFaultsCannotMintCounters_data() {
    QTest::addColumn<int>("mode");
    for (const auto& entry : {std::pair{"missing", fault::Mode::MissingMetric},
        {"duplicate-name", fault::Mode::DuplicateMetric}, {"wrong-type", fault::Mode::GaugeMetric},
        {"negative-index", fault::Mode::NegativeIndex}, {"colliding-index", fault::Mode::CollidingIndex},
        {"outside-counter-span", fault::Mode::OutsideIndex}}) {
      QTest::newRow(entry.first) << static_cast<int>(entry.second);
    }
  }
  void ownedRuntimeMetricMappingFaultsCannotMintCounters() {
    QFETCH(int, mode); test::OwnedEngineFixture fixture(false); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    auto lease = held(fixture); fault::State state; state.mode = static_cast<fault::Mode>(mode);
    fault::Scope seam(state); auto service = owner();
    QVERIFY(engine::continueLocal(*service, lease, options(fixture))); QVERIFY(fixture.intact());
    QCOMPARE(state.metricCalls, std::size_t{1}); QCOMPARE(state.mappingFaults, std::size_t{1});
    QVERIFY(std::all_of(state.realIndices.begin(), state.realIndices.end(), [](int i) { return i >= 0; }));
    const auto receipt = service->lastRetirement(); QVERIFY(receipt && receipt->storageReleased && !receipt->peerCounters);
    QCOMPARE(receipt->accountingFailure, std::optional{AccountingError::CounterMapping});
    QVERIFY(service->latest() && !service->latest()->observed);
    QCOMPARE(state.realPosts, state.mode == fault::Mode::OutsideIndex ? std::size_t{1} : std::size_t{0});
    QCOMPARE(state.deliveredRemovals, std::size_t{1}); QVERIFY(!state.overflow && !state.syntheticSpans);
    const auto close = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(close.sessionJoined && !close.retirement.peerCounters);
  }
  void filteringActualMatchingRemovalDeniesCompletionAndReuse() {
    test::OwnedEngineFixture fixture(false); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    auto lease = held(fixture); const auto statistics = test::read(fixture.profile + QStringLiteral("/BiglyBT.statistics"));
    fault::State state; state.mode = fault::Mode::DropRemoval; fault::Scope seam(state); auto service = owner();
    auto opt = options(fixture); opt.observer = &publication; flushed = false;
    const auto result = engine::continueLocal(*service, lease, opt);
    QVERIFY(!result); QCOMPARE(result.error().code, storage::ErrorCode::Io); QVERIFY(!flushed);
    QCOMPARE(state.removals, std::size_t{1}); QCOMPARE(state.droppedRemovals, std::size_t{1});
    QCOMPARE(state.deliveredRemovals, std::size_t{0}); QVERIFY(!state.removing.is_valid());
    const auto receipt = service->lastRetirement(); QVERIFY(receipt && !receipt->storageReleased && !receipt->peerCounters);
    QCOMPARE(receipt->accountingFailure, std::optional{AccountingError::StorageFenceLost});
    QCOMPARE(savedCompleted(fixture), std::int64_t{0});
    QCOMPARE(test::read(fixture.profile + QStringLiteral("/BiglyBT.statistics")), statistics);
    QVERIFY(!engine::continueLocal(*service, lease, opt));
    const auto poll = service->poll(); QVERIFY(!poll); QCOMPARE(poll.error(), AccountingError::OwnerResetRequired);
    const auto close = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(close.sessionJoined && !close.retirement.peerCounters); QVERIFY(!state.overflow && fixture.intact());
  }
  void syntheticAccessorPolicyFaultsCannotCertifyActualPositiveEpoch_data() {
    QTest::addColumn<int>("mode"); QTest::addColumn<int>("expected");
    QTest::newRow("regression-test-copy") << static_cast<int>(fault::Mode::Regression) << static_cast<int>(AccountingError::CounterRegression);
    QTest::newRow("negative-test-copy") << static_cast<int>(fault::Mode::NegativeCounter) << static_cast<int>(AccountingError::CounterInvalid);
    QTest::newRow("payload-exceeds-combined-test-copy") << static_cast<int>(fault::Mode::PayloadExceedsCombined) << static_cast<int>(AccountingError::CounterInvalid);
    QTest::newRow("empty-test-span") << static_cast<int>(fault::Mode::EmptySpan) << static_cast<int>(AccountingError::CounterMapping);
  }
  void syntheticAccessorPolicyFaultsCannotCertifyActualPositiveEpoch() {
    QFETCH(int, mode); QFETCH(int, expected);
    test::OwnedEngineFixture fixture(false); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    auto lease = held(fixture); fault::State state; fault::Scope seam(state); auto service = owner();
    QVERIFY(engine::continueLocal(*service, lease, options(fixture)));
    const auto prior = service->lastRetirement(); QVERIFY(prior && prior->peerCounters);
    const auto actual = prior->peerCounters->counters(); QVERIFY(actual.payloadReceived > 0);
    state.mode = static_cast<fault::Mode>(mode); const auto posts = state.realPosts;
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased && !closed.retirement.peerCounters);
    QCOMPARE(closed.retirement.accountingFailure, std::optional{static_cast<AccountingError>(expected)});
    QCOMPARE(state.syntheticSpans, std::size_t{1}); QCOMPARE(state.realPosts, posts + 1);
    QVERIFY(state.lastActualCounters && state.lastActualCounters->payloadReceived > 0);
    QCOMPARE(prior->peerCounters->counters(), actual);
    QVERIFY(service->latest() && service->latest()->observed); QCOMPARE(service->latest()->observed->counters, actual);
    QVERIFY(!state.overflow && fixture.intact());
  }
  void expiredDeadlineNeverPostsOrUpgradesTheActualPriorCheckpoint() {
    test::OwnedEngineFixture fixture(false); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    auto lease = held(fixture); fault::State state; fault::Scope seam(state); auto service = owner();
    QVERIFY(engine::continueLocal(*service, lease, options(fixture)));
    const auto before = service->lastRetirement(); QVERIFY(before && before->peerCounters); const auto posts = state.realPosts;
    const auto closed = service->close(std::chrono::steady_clock::now());
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased && !closed.retirement.peerCounters);
    QCOMPARE(closed.retirement.accountingFailure, std::optional{AccountingError::Deadline});
    QCOMPARE(state.realPosts, posts); QVERIFY(before->peerCounters->counters().payloadReceived > 0);
    QVERIFY(!state.overflow && fixture.intact());
  }
  void actualIdlePostFailurePublishesOneDiagnosticWithUnchangedSample() {
    test::OwnedEngineFixture fixture(false); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    auto lease = held(fixture); fault::State state; fault::Scope seam(state); auto service = owner();
    QVERIFY(engine::continueLocal(*service, lease, options(fixture)));
    const auto before = service->latest(); QVERIFY(before && before->observed);
    QVERIFY(before->observed->counters.payloadReceived > 0);
    std::optional<engine::ApplicationPeerObservation> diagnostic;
    std::size_t callbacks = 0;
    QVERIFY(service->setObservationSink([&](const auto& value) { diagnostic = value; ++callbacks; }));
    state.mode = fault::Mode::ThrowPost;
    // poll's actual cadence decides when to post; the wrapper proves the call
    // threw. Repeated QTRY polls before that may consume a real idle response.
    QTRY_VERIFY_WITH_TIMEOUT(!service->poll().has_value() && state.postThrows == 1, 3000);
    const auto failed = service->latest(); QVERIFY(failed && failed->observed && diagnostic);
    QCOMPARE(failed->state, engine::AccountingState::Uncertified);
    QVERIFY(failed->sequence > before->sequence);
    QCOMPARE(diagnostic->sequence, failed->sequence);
    // Any actual idle response preceding the throw is legitimate. The
    // diagnostic retains that latest sample, never making a new one itself.
    const auto sample = *failed->observed;
    const auto sequence = failed->sequence;
    const auto delivered = state.deliveredStats;
    const auto calls = callbacks;
    QVERIFY(state.lastActualCounters);
    QCOMPARE(sample.counters, *state.lastActualCounters);
    for (int turn = 0; turn < 4; ++turn) { QVERIFY(!service->poll()); }
    QCOMPARE(service->latest()->sequence, sequence);
    QCOMPARE(*service->latest()->observed, sample);
    QCOMPARE(state.deliveredStats, delivered); QCOMPARE(callbacks, calls);
    QCOMPARE(state.postThrows, std::size_t{1});
    service->clearObservationSink();
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased && !closed.retirement.peerCounters);
    QVERIFY(!state.overflow && fixture.intact());
  }
  void reusableCallGateBlocksOnlyActualOwnerAndControllerReleasesBeforeJoin() {
    test::OwnedEngineFixture fixture(false); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    auto lease = held(fixture); fault::State state; fault::Scope seam(state); auto service = owner();
    fault::CallGate gate; // Destroy/release BEFORE service and its held lease.
    QVERIFY(engine::continueLocal(*service, lease, options(fixture))); QVERIFY(gate.bound());
    QVERIFY(gate.arm(fault::CallGate::Point::Pop)); bool controllerObserved = false;
    std::jthread controller([&] { controllerObserved = gate.waitEntered(3000ms); gate.release(); });
    const auto polled = service->poll(); controller.join();
    QVERIFY(controllerObserved && gate.entered()); QVERIFY(polled);
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased && closed.retirement.peerCounters);
    QVERIFY(!state.overflow && fixture.intact());
  }
};
QTEST_GUILESS_MAIN(PersistentSessionFaultTests)
#include "persistent_session_fault_tests.moc"
