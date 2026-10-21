#include "causal_worker.hpp"
#include <QDebug>
#include <algorithm>
#include <QtTest>

namespace {
using namespace biglybtc;
namespace h = libtorrent::biglybtc_causal;
using namespace std::chrono_literals;
std::optional<h::Event> first(const h::Snapshot& ledger, h::Kind kind, std::uint64_t after = 0) {
  for (std::size_t i = 0; i < ledger.count; ++i) {
    if (ledger.events[i].kind == kind && ledger.events[i].sequence > after) { return ledger.events[i]; }
  }
  return {};
}
std::size_t count(const h::Snapshot& ledger, h::Kind kind, std::uint64_t after = 0, std::uint64_t before = std::numeric_limits<std::uint64_t>::max()) {
  std::size_t result = 0;
  for (std::size_t i = 0; i < ledger.count; ++i) {
    const auto& e = ledger.events[i]; if (e.kind == kind && e.sequence > after && e.sequence < before) { ++result; }
  }
  return result;
}
void trace(const char* name, const h::Snapshot& ledger) {
  qInfo().noquote() << "CAUSAL_CASE" << name << "events" << ledger.count << "failed" << ledger.failed << "overflow" << ledger.overflow;
  for (std::size_t i = 0; i < ledger.count; ++i) {
    const auto& e = ledger.events[i];
    qInfo().noquote() << "CAUSAL_EVENT" << e.sequence << static_cast<unsigned>(e.kind)
        << e.bytes << e.detail << e.hasOwner << e.ownerExpired
        << e.counters[0] << e.counters[1] << e.counters[2] << e.counters[3];
  }
}
// Construct after Gates and before Worker so failure returns destroy/join the
// worker before printing the bounded actual ledger, then finish the controller.
class TraceOnExit final {
public:
  explicit TraceOnExit(const char* name) noexcept : name_(name) {}
  ~TraceOnExit() { trace(name_, h::snapshot()); }
  TraceOnExit(const TraceOnExit&) = delete;
  TraceOnExit& operator=(const TraceOnExit&) = delete;
private:
  const char* name_;
};
} // namespace

class PersistentCausalTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QByteArray(h::build_identity()), QByteArray("biglybtc-causal-lt-2.0.12-v2"));
  }
  void actualWriteJobRetainsTorrentAfterRemovalUntilRealPoolFree() {
    test::OwnedEngineFixture fixture(false, true);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    test::causal::Gates gates(false, true);
    const TraceOnExit traceOnExit("real-disk-job-exit");
    test::causal::Worker worker(fixture); QVERIFY(worker.wait_ready());
    QVERIFY(h::wait_for(h::Kind::DiskHeld, 1, 0, 10s));
    auto held = h::snapshot(); QVERIFY(!held.failed && !held.overflow && held.diskHeld && !held.ownerExpired);
    const auto disk = first(held, h::Kind::DiskHeld); QVERIFY(disk && disk->bytes > 0 && disk->hasOwner && !disk->ownerExpired);
    worker.cancel();
    QVERIFY(h::wait_for(h::Kind::RemovalPosted, 1, disk->sequence, 3s));
    const auto removed = first(h::snapshot(), h::Kind::RemovalPosted); QVERIFY(removed && !removed->ownerExpired);
    QVERIFY(h::wait_for(h::Kind::RemovalDequeued, 1, 0, 3s));
    const auto delivered = first(h::snapshot(), h::Kind::RemovalDequeued); QVERIFY(delivered && !delivered->ownerExpired);
    const auto blockedAfter = std::max(removed->sequence, delivered->sequence);
    // Actual owner pump turns after the actual removal receipt, while the
    // completed real job is STILL owned. This is event-driven, not a sleep.
    QVERIFY(h::wait_for(h::Kind::OwnerPop, 3, blockedAfter, 1s));
    held = h::snapshot(); QVERIFY(held.diskHeld && !held.ownerExpired);
    QCOMPARE(count(held, h::Kind::StatsRequested, removed->sequence), std::size_t{0});
    QCOMPARE(count(held, h::Kind::OwnerReturned), std::size_t{0}); QVERIFY(!worker.state().returned);
    QVERIFY(h::release(h::Gate::Disk));
    QVERIFY(h::wait_for(h::Kind::DiskFreed, 1, removed->sequence, 2s));
    QVERIFY(worker.wait_returned());
    auto final = h::snapshot();
    const auto running = first(final, h::Kind::ReleaseRunning, removed->sequence);
    const auto expired = first(final, h::Kind::WeakExpired); QVERIFY(running && expired && expired->sequence > running->sequence);
    const auto requested = first(final, h::Kind::StatsRequested, expired->sequence); QVERIFY(requested && requested->ownerExpired);
    const auto copied = first(final, h::Kind::StatsCopied, requested->sequence); QVERIFY(copied && copied->ownerExpired);
    const auto dequeued = first(final, h::Kind::StatsDequeued, copied->sequence); QVERIFY(dequeued && dequeued->ownerExpired);
    QCOMPARE(dequeued->counters, copied->counters);
    const auto returned = first(final, h::Kind::OwnerReturned, dequeued->sequence); QVERIFY(returned);
    const auto state = worker.state(); QVERIFY(!state.successful && state.failure == storage::ErrorCode::Cancelled);
    QVERIFY(state.receipt && state.receipt->storageReleased && state.receipt->peerCounters);
    QCOMPARE(test::causal::counters(state.receipt->peerCounters->counters()), copied->counters);
    QCOMPARE(returned->counters, copied->counters); QVERIFY(fixture.intact(false));
    worker.shutdown(); final = h::snapshot(); trace("real-disk-job", final); QVERIFY(!final.failed && !final.overflow);
    QVERIFY(worker.state().joined);
  }
  void actualPositivePeerCompletionRunsAfterWeakExpiryWithoutChangingTheFourCounters() {
    test::OwnedEngineFixture fixture(false, true);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    test::causal::Gates gates(true, false);
    const TraceOnExit traceOnExit("positive-peer-after-expiry-exit");
    test::causal::Worker worker(fixture); QVERIFY(worker.wait_ready());
    QVERIFY(h::wait_for(h::Kind::SocketHeld, 1, 0, 10s));
    auto initial = h::snapshot();
    const auto socket = first(initial, h::Kind::SocketHeld); QVERIFY(socket && socket->bytes > 0 && socket->hasOwner && !socket->ownerExpired);
    QVERIFY(initial.socketHeld && !initial.ownerExpired);
    worker.cancel(); QVERIFY(worker.wait_returned());
    auto settled = h::snapshot(); QVERIFY(settled.socketHeld && settled.ownerExpired);
    const auto state = worker.state(); QVERIFY(!state.successful && state.failure == storage::ErrorCode::Cancelled);
    QVERIFY(state.receipt && state.receipt->storageReleased && state.receipt->peerCounters && state.latest);
    const auto checkpoint = test::causal::counters(state.receipt->peerCounters->counters());
    const auto expired = first(settled, h::Kind::WeakExpired); QVERIFY(expired);
    const auto returned = first(settled, h::Kind::OwnerReturned); QVERIFY(returned && returned->sequence > expired->sequence);
    QCOMPARE(count(settled, h::Kind::SocketFinished), std::size_t{0});
    QVERIFY(h::release(h::Gate::Socket));
    QVERIFY(h::wait_for(h::Kind::SocketFinished, 1, returned->sequence, 2s));
    auto released = h::snapshot();
    const auto finished = first(released, h::Kind::SocketFinished); QVERIFY(finished && finished->ownerExpired);
    QCOMPARE(finished->bytes, socket->bytes); QCOMPARE(finished->detail, socket->detail);
    // Ask the SAME owner for later actual observations after this real peer-only
    // callback ran. No reused seal/zero fixture or second alert consumer.
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
      if (!worker.poll_once()) { return false; }
      const auto now = worker.state(); return now.latest && now.latest->sequence > state.latest->sequence;
    })(), 5000);
    const auto now = worker.state(); QVERIFY(now.latest && now.latest->observed);
    QCOMPARE(test::causal::counters(now.latest->observed->counters), checkpoint);
    const auto observed = first(h::snapshot(), h::Kind::StatsDequeued, finished->sequence); QVERIFY(observed);
    QCOMPARE(observed->counters, checkpoint);
    QCOMPARE(count(h::snapshot(), h::Kind::SocketFinished), std::size_t{1});
    QVERIFY(fixture.intact(false)); worker.shutdown();
    const auto final = h::snapshot(); trace("positive-peer-after-expiry", final); QVERIFY(!final.failed && !final.overflow);
  }
  void releaseAllDisarmsBothActualHoldsBeforeAnyOwnerJoin() {
    test::OwnedEngineFixture fixture(false, true);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    test::causal::Gates gates(true, true);
    const TraceOnExit traceOnExit("release-all-cleanup-exit");
    test::causal::Worker worker(fixture); QVERIFY(worker.wait_ready());
    QVERIFY(h::wait_for(h::Kind::DiskHeld, 1, 0, 10s));
    QVERIFY(h::wait_for(h::Kind::SocketHeld, 1, 0, 10s));
    const auto occupied = h::snapshot(); QVERIFY(occupied.diskHeld && occupied.socketHeld && !occupied.ownerExpired);
    trace("release-all-both-held", occupied);
    QVERIFY(!occupied.failed && !occupied.overflow);
    const auto diskHeld = first(occupied, h::Kind::DiskHeld);
    const auto socketHeld = first(occupied, h::Kind::SocketHeld);
    QVERIFY(diskHeld && socketHeld);
    QVERIFY(diskHeld->bytes > 0 && socketHeld->bytes > 0);
    QVERIFY(diskHeld->hasOwner && socketHeld->hasOwner);
    QVERIFY(!diskHeld->ownerExpired && !socketHeld->ownerExpired);
    QVERIFY(diskHeld->sequence < socketHeld->sequence);
    QCOMPARE(count(occupied, h::Kind::DiskHeld), std::size_t{1});
    QCOMPARE(count(occupied, h::Kind::SocketHeld), std::size_t{1});
    QCOMPARE(count(occupied, h::Kind::ReleasePosted), std::size_t{0});
    QCOMPARE(count(occupied, h::Kind::ReleaseRunning), std::size_t{0});
    worker.cancel(); QVERIFY(h::wait_for(h::Kind::RemovalPosted, 1, 0, 3s));
    const auto removed = first(h::snapshot(), h::Kind::RemovalPosted); QVERIFY(removed);
    QVERIFY(h::wait_for(h::Kind::RemovalDequeued, 1, 0, 3s));
    const auto delivered = first(h::snapshot(), h::Kind::RemovalDequeued); QVERIFY(delivered && !delivered->ownerExpired);
    QVERIFY(h::wait_for(h::Kind::OwnerPop, 3, std::max(removed->sequence, delivered->sequence), 1s));
    QVERIFY(!worker.state().returned);
    // Exercise the SAME cleanup used when any earlier assertion/exception exits.
    worker.shutdown();
    const auto final = h::snapshot();
    QVERIFY(!final.socketHeld && !final.diskHeld && !final.socketPending && !final.diskPending);
    QCOMPARE(count(final, h::Kind::SocketFinished), std::size_t{1});
    QCOMPARE(count(final, h::Kind::DiskFreed), std::size_t{1});
    const auto socketDone = first(final, h::Kind::SocketFinished); const auto diskDone = first(final, h::Kind::DiskFreed);
    const auto returned = first(final, h::Kind::OwnerReturned); QVERIFY(socketDone && diskDone && returned);
    QVERIFY(returned->sequence > socketDone->sequence && returned->sequence > diskDone->sequence);
    QVERIFY(worker.state().joined); QVERIFY(fixture.intact(false));
    trace("release-all-cleanup", final); QVERIFY(!final.failed && !final.overflow);
  }
};
QTEST_GUILESS_MAIN(PersistentCausalTests)
#include "persistent_causal_tests.moc"
