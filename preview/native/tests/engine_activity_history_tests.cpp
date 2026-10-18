#include "engine_activity_history.hpp"

#include <QtTest>

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace {
using namespace biglybtc;
using Packet = std::shared_ptr<ui::EngineStatisticsPacket>;

Packet preparing(quint64 operation = 1, quint64 session = 7) {
  auto value = std::make_shared<ui::EngineStatisticsPacket>();
  value->session = session; value->operation = operation; value->sequence = 1;
  value->target = {QStringLiteral("OWNED-ENGINE"), 11};
  value->name = QStringLiteral("Actual owned engine");
  return value;
}
Packet observed(quint64 sequence = 2, quint64 operation = 1, quint64 session = 7) {
  auto value = preparing(operation, session);
  value->sequence = sequence; value->state = ui::EngineStatisticsState::Observed;
  const auto count = static_cast<std::int64_t>(sequence);
  value->sample = core::EngineStatistics{{count * 100, count * 60, count * 80, count * 40},
      count * 1000000000LL, core::EngineRates{100, 60, 80, 40, 20, 20}};
  return value;
}
Packet ended(const Packet& prior, quint64 sequence, bool withSample = true) {
  auto value = std::make_shared<ui::EngineStatisticsPacket>(*prior);
  value->sequence = sequence; value->state = ui::EngineStatisticsState::Ended;
  value->successful = true;
  if (!withSample) { value->sample.reset(); }
  return value;
}
bool unchanged(const ui::EngineActivityPresentation& a,
    const ui::EngineActivityPresentation& b) {
  if (!a || !b) { return a == b; }
  if (a->latest() != b->latest() || a->points().size() != b->points().size()) { return false; }
  for (std::size_t i = 0; i < a->points().size(); ++i) {
    if (a->points()[i] != b->points()[i]) { return false; }
  }
  return true;
}
} // namespace

class EngineActivityHistoryTests final : public QObject {
  Q_OBJECT
private slots:
  void emptyClearAndPreparingNeverInventMeasurements() {
    ui::EngineActivityHistory history;
    QVERIFY(!history.snapshot()); history.clear(); QVERIFY(!history.snapshot());
    auto value = preparing(); QVERIFY(history.observe(value, 7));
    auto snapshot = history.snapshot(); QVERIFY(snapshot); QCOMPARE(snapshot->latest(), value);
    QVERIFY(snapshot->points().empty());
    history.clear(); QVERIFY(!history.snapshot());
    QCOMPARE(snapshot->latest()->state, ui::EngineStatisticsState::Preparing);
    QVERIFY(snapshot->points().empty());
  }
  void firstUnknownAndKnownZeroAreDistinct() {
    ui::EngineActivityHistory history;
    auto first = observed(); first->sample->elapsedNanoseconds = 0; first->sample->rates.reset();
    QVERIFY(history.observe(first, 7)); QVERIFY(history.snapshot()->points().empty());
    auto zero = observed(3); zero->sample->rates = core::EngineRates{};
    QVERIFY(history.observe(zero, 7)); auto snapshot = history.snapshot();
    QCOMPARE(snapshot->points().size(), std::size_t{1});
    QCOMPARE(snapshot->points().front().rates, core::EngineRates{});
    QVERIFY(!snapshot->points().front().connectedToPrevious);
  }
  void consecutiveObservedRatesConnectExactly() {
    ui::EngineActivityHistory history;
    QVERIFY(history.observe(observed(2), 7)); QVERIFY(history.observe(observed(3), 7));
    QVERIFY(history.observe(observed(4), 7)); const auto snapshot = history.snapshot();
    QCOMPARE(snapshot->points().size(), std::size_t{3});
    QVERIFY(!snapshot->points()[0].connectedToPrevious);
    QVERIFY(snapshot->points()[1].connectedToPrevious); QVERIFY(snapshot->points()[2].connectedToPrevious);
    QCOMPARE(snapshot->points()[1].elapsedNanoseconds, std::int64_t{3000000000LL});
    QCOMPARE(snapshot->points()[1].rates, (core::EngineRates{100, 60, 80, 40, 20, 20}));
  }
  void coalescedSequenceGapBreaksOnlyItsConnection() {
    ui::EngineActivityHistory history;
    QVERIFY(history.observe(observed(2), 7)); QVERIFY(history.observe(observed(8), 7));
    QVERIFY(history.observe(observed(9), 7)); const auto snapshot = history.snapshot();
    QCOMPARE(snapshot->points().size(), std::size_t{3});
    QVERIFY(!snapshot->points()[1].connectedToPrevious); QVERIFY(snapshot->points()[2].connectedToPrevious);
  }
  void unavailableRateGapRetainsPriorPointsWithoutZeroFill() {
    ui::EngineActivityHistory history;
    QVERIFY(history.observe(observed(2), 7)); auto unavailable = observed(3); unavailable->sample->rates.reset();
    QVERIFY(history.observe(unavailable, 7)); QCOMPARE(history.snapshot()->points().size(), std::size_t{1});
    QCOMPARE(history.snapshot()->latest(), unavailable);
    QVERIFY(history.observe(observed(4), 7)); QVERIFY(history.observe(observed(5), 7));
    auto snapshot = history.snapshot(); QCOMPARE(snapshot->points().size(), std::size_t{3});
    QCOMPARE(snapshot->points()[1].elapsedNanoseconds, std::int64_t{4000000000LL});
    QVERIFY(!snapshot->points()[1].connectedToPrevious); QVERIFY(snapshot->points()[2].connectedToPrevious);
  }
  void equalTerminalSnapshotDoesNotDuplicateLastPoint_data() {
    QTest::addColumn<bool>("successful");
    QTest::newRow("success") << true; QTest::newRow("failure") << false;
  }
  void equalTerminalSnapshotDoesNotDuplicateLastPoint() {
    QFETCH(bool, successful);
    ui::EngineActivityHistory history; auto first = observed(); QVERIFY(history.observe(first, 7));
    auto terminal = ended(first, 3); terminal->successful = successful;
    QVERIFY(history.observe(terminal, 7)); auto snapshot = history.snapshot();
    QCOMPARE(snapshot->points().size(), std::size_t{1}); QCOMPARE(snapshot->latest(), terminal);
    QCOMPARE(snapshot->latest()->successful, successful);
    QVERIFY(!history.observe(observed(4), 7));
    QVERIFY(!history.observe(ended(terminal, 4), 7));
  }
  void newerTerminalSampleAddsStandaloneActualPoint() {
    ui::EngineActivityHistory history; QVERIFY(history.observe(observed(2), 7));
    auto terminal = ended(observed(7), 8); QVERIFY(history.observe(terminal, 7));
    auto snapshot = history.snapshot(); QCOMPARE(snapshot->points().size(), std::size_t{2});
    QCOMPARE(snapshot->points().back().elapsedNanoseconds, std::int64_t{7000000000LL});
    QVERIFY(!snapshot->points().back().connectedToPrevious); QCOMPARE(snapshot->latest(), terminal);
  }
  void adjacentSequenceNewerTerminalStillHasNoConnection() {
    ui::EngineActivityHistory history; QVERIFY(history.observe(observed(2), 7));
    auto terminal = ended(observed(3), 3); QVERIFY(history.observe(terminal, 7));
    auto snapshot = history.snapshot(); QCOMPARE(snapshot->points().size(), std::size_t{2});
    QCOMPARE(snapshot->latest()->sequence, quint64{3});
    QCOMPARE(snapshot->points().back().elapsedNanoseconds, std::int64_t{3000000000LL});
    QVERIFY(!snapshot->points().back().connectedToPrevious);
  }
  void independentlyFlooredProtocolRatesAreCopiedWithoutRecomputation() {
    ui::EngineActivityHistory history; auto value = observed();
    value->sample->rates = core::EngineRates{100, 60, 79, 39, 20, 20};
    QVERIFY(history.observe(value, 7)); auto snapshot = history.snapshot();
    QCOMPARE(snapshot->points().size(), std::size_t{1});
    const auto& rates = snapshot->points().front().rates;
    QCOMPARE(rates, (core::EngineRates{100, 60, 79, 39, 20, 20}));
    QCOMPARE(rates.received - rates.payloadReceived, std::int64_t{21});
    QCOMPARE(rates.protocolReceived, std::int64_t{20});
    QCOMPARE(rates.sent - rates.payloadSent, std::int64_t{21});
    QCOMPARE(rates.protocolSent, std::int64_t{20});
  }
  void terminalWithoutSampleNormalizesOnlySameOperation() {
    ui::EngineActivityHistory history; auto first = observed(); QVERIFY(history.observe(first, 7));
    auto terminal = ended(first, 3, false); QVERIFY(history.observe(terminal, 7));
    auto snapshot = history.snapshot(); QCOMPARE(snapshot->points().size(), std::size_t{1});
    QVERIFY(snapshot->latest() != terminal); QCOMPARE(snapshot->latest()->sample, first->sample);
    QCOMPARE(snapshot->latest()->sequence, quint64{3}); QCOMPARE(snapshot->latest()->state, ui::EngineStatisticsState::Ended);
    QVERIFY(!terminal->sample); // Caller packet is never edited for retention.
    auto another = ended(preparing(2), 2, false); QVERIFY(history.observe(another, 7));
    QVERIFY(history.snapshot()->points().empty()); QVERIFY(!history.snapshot()->latest()->sample);
  }
  void unknownLatestSampleSurvivesMissingTerminalWithoutPoint() {
    ui::EngineActivityHistory history; QVERIFY(history.observe(observed(2), 7));
    auto unknown = observed(3); unknown->sample->rates.reset(); QVERIFY(history.observe(unknown, 7));
    QVERIFY(history.observe(ended(unknown, 4, false), 7)); auto snapshot = history.snapshot();
    QCOMPARE(snapshot->points().size(), std::size_t{1}); QCOMPARE(snapshot->latest()->sample, unknown->sample);
    QVERIFY(!snapshot->latest()->sample->rates);
  }
  void terminalAsFirstCachedPacketUsesActualSampleWithoutConnection() {
    ui::EngineActivityHistory history;
    auto terminal = ended(observed(9), 10); QVERIFY(history.observe(terminal, 7));
    QCOMPARE(history.snapshot()->points().size(), std::size_t{1});
    QVERIFY(!history.snapshot()->points().front().connectedToPrevious);
    history.clear(); QVERIFY(history.observe(ended(preparing(), 2, false), 7));
    QVERIFY(history.snapshot()->points().empty());
  }
  void newOperationClearsPointsAndRetainsIndependentOldSnapshot() {
    ui::EngineActivityHistory history; QVERIFY(history.observe(observed(2), 7));
    QVERIFY(history.observe(observed(3), 7)); auto before = history.snapshot();
    auto next = preparing(2); next->target = {QStringLiteral("ANOTHER-ENGINE"), 12};
    QVERIFY(history.observe(next, 7)); QVERIFY(history.snapshot()->points().empty());
    auto actual = observed(2, 2); actual->target = next->target; QVERIFY(history.observe(actual, 7));
    QCOMPARE(history.snapshot()->points().size(), std::size_t{1});
    QVERIFY(!history.snapshot()->points().front().connectedToPrevious);
    QCOMPARE(before->points().size(), std::size_t{2}); QCOMPARE(before->latest()->operation, quint64{1});
  }
  void wrongSessionRequiresExplicitClearEvenWithHigherOperation() {
    ui::EngineActivityHistory history; QVERIFY(history.observe(observed(2), 7)); auto before = history.snapshot();
    QVERIFY(!history.observe(observed(2, 10, 8), 8)); QVERIFY(unchanged(before, history.snapshot()));
    QVERIFY(!history.observe(observed(2, 10, 8), 7)); QVERIFY(unchanged(before, history.snapshot()));
    history.clear(); QVERIFY(history.observe(observed(2, 10, 8), 8));
    QCOMPARE(history.snapshot()->latest()->session, quint64{8}); QCOMPARE(before->latest()->session, quint64{7});
  }
  void invalidPacketsCannotMutateHistory_data() {
    QTest::addColumn<int>("kind");
    for (int kind = 0; kind < 19; ++kind) { QTest::newRow(qPrintable(QString::number(kind))) << kind; }
  }
  void invalidPacketsCannotMutateHistory() {
    QFETCH(int, kind);
    ui::EngineActivityHistory history; QVERIFY(history.observe(observed(2), 7)); auto before = history.snapshot();
    auto incoming = observed(3); quint64 expected = 7;
    switch (kind) {
      case 0: incoming.reset(); break;
      case 1: expected = 0; break;
      case 2: incoming->session = 8; break;
      case 3: incoming->operation = 0; break;
      case 4: incoming->sequence = 0; break;
      case 5: incoming->target.generation = 0; break;
      case 6: incoming->target.hash.clear(); break;
      case 7: incoming->target.hash = QString(257, QLatin1Char('x')); break;
      case 8: incoming->name = QString(8193, QLatin1Char('x')); break;
      case 9: incoming->state = static_cast<ui::EngineStatisticsState>(99); break;
      case 10: incoming->sample.reset(); break;
      case 11: incoming->successful = true; break;
      case 12: incoming->sample->counters.received = -1; break;
      case 13: incoming->sample->rates->protocolReceived = -1; break;
      case 14: incoming->sample->rates->received = 79; break;
      case 15: incoming->sample->elapsedNanoseconds = -1; break;
      case 16: incoming->state = ui::EngineStatisticsState::Preparing; break;
      case 17: incoming->target.hash = QStringLiteral("FOREIGN-TARGET"); break;
      case 18: incoming->target.generation = 12; break;
    }
    QVERIFY(!history.observe(incoming, expected)); QVERIFY(unchanged(before, history.snapshot()));
  }
  void staleSequenceOperationAndRegressionsLeavePointsUntouched_data() {
    QTest::addColumn<int>("kind");
    for (int kind = 0; kind < 11; ++kind) { QTest::newRow(qPrintable(QString::number(kind))) << kind; }
  }
  void staleSequenceOperationAndRegressionsLeavePointsUntouched() {
    QFETCH(int, kind);
    ui::EngineActivityHistory history; auto current = observed(4, 2); QVERIFY(history.observe(current, 7));
    auto before = history.snapshot(); auto incoming = observed(5, 2);
    switch (kind) {
      case 0: incoming->sequence = 4; break;
      case 1: incoming->sequence = 3; break;
      case 2: incoming->operation = 1; break;
      case 3: incoming->sample->elapsedNanoseconds = 4000000000LL; break;
      case 4: incoming->sample->elapsedNanoseconds = 3000000000LL; break;
      case 5: incoming->sample->counters.received = 399; incoming->sample->counters.payloadReceived = 300; break;
      case 6: incoming->sample->counters.sent = 239; incoming->sample->counters.payloadSent = 150; break;
      case 7: incoming->sample->counters.payloadReceived = 319; break;
      case 8: incoming->sample->counters.payloadSent = 159; break;
      case 9: incoming->sample->counters.received = 410; incoming->sample->counters.payloadReceived = 400; break;
      case 10: incoming->sample->counters.sent = 250; incoming->sample->counters.payloadSent = 200; break;
    }
    QVERIFY(!history.observe(incoming, 7)); QVERIFY(unchanged(before, history.snapshot()));
  }
  void equalTimeTerminalMutationIsRejectedAndOriginalStillRetained() {
    ui::EngineActivityHistory history; auto first = observed(); QVERIFY(history.observe(first, 7));
    auto terminal = ended(first, 3); terminal->sample->rates = core::EngineRates{};
    auto before = history.snapshot(); QVERIFY(!history.observe(terminal, 7));
    QVERIFY(unchanged(before, history.snapshot()));
  }
  void exactCapacityAndEvictionDisconnectRetainedLeftEdge() {
    ui::EngineActivityHistory history;
    for (quint64 sequence = 2; sequence < 602; ++sequence) { QVERIFY(history.observe(observed(sequence), 7)); }
    auto full = history.snapshot(); QCOMPARE(full->points().size(), ui::EngineActivityHistory::MaximumPoints);
    QCOMPARE(full->points().front().elapsedNanoseconds, std::int64_t{2000000000LL});
    QVERIFY(!full->points().front().connectedToPrevious); QVERIFY(full->points().back().connectedToPrevious);
    QVERIFY(history.observe(observed(602), 7)); auto clipped = history.snapshot();
    QCOMPARE(clipped->points().size(), std::size_t{600});
    QCOMPARE(clipped->points().front().elapsedNanoseconds, std::int64_t{3000000000LL});
    QVERIFY(!clipped->points().front().connectedToPrevious); QVERIFY(clipped->points()[1].connectedToPrevious);
    QCOMPARE(clipped->points().back().elapsedNanoseconds, std::int64_t{602000000000LL});
    QCOMPARE(full->points().front().elapsedNanoseconds, std::int64_t{2000000000LL});
    QVERIFY(history.observe(observed(604), 7)); clipped = history.snapshot();
    QVERIFY(!clipped->points().front().connectedToPrevious); QVERIFY(!clipped->points().back().connectedToPrevious);
  }
  void repeatedRingWrapNeverExceedsSixHundredOrChangesSnapshots() {
    ui::EngineActivityHistory history; QVERIFY(history.observe(observed(2), 7)); auto first = history.snapshot();
    for (quint64 sequence = 3; sequence < 1803; ++sequence) { QVERIFY(history.observe(observed(sequence), 7)); }
    auto current = history.snapshot(); QCOMPARE(current->points().size(), std::size_t{600});
    QCOMPARE(current->points().front().elapsedNanoseconds, std::int64_t{1203000000000LL});
    QCOMPARE(current->points().back().elapsedNanoseconds, std::int64_t{1802000000000LL});
    QVERIFY(!current->points().front().connectedToPrevious);
    for (std::size_t i = 1; i < current->points().size(); ++i) { QVERIFY(current->points()[i].connectedToPrevious); }
    QCOMPARE(first->points().size(), std::size_t{1}); QCOMPARE(first->latest()->sequence, quint64{2});
  }
  void extremeRatesTimesAndSequenceNeverOverflowOrInferConnection() {
    ui::EngineActivityHistory history; auto first = observed();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    first->sample->counters = {maximum, maximum, maximum, maximum};
    first->sample->elapsedNanoseconds = maximum - 1;
    first->sample->rates = core::EngineRates{maximum, maximum, maximum, maximum, 0, 0};
    QVERIFY(history.observe(first, 7));
    auto last = std::make_shared<ui::EngineStatisticsPacket>(*first);
    last->sequence = std::numeric_limits<quint64>::max(); last->sample->elapsedNanoseconds = maximum;
    QVERIFY(history.observe(last, 7)); auto snapshot = history.snapshot();
    QCOMPARE(snapshot->points().size(), std::size_t{2}); QVERIFY(!snapshot->points().back().connectedToPrevious);
    QCOMPARE(snapshot->points().back().rates.received, maximum);
    QCOMPARE(snapshot->points().back().elapsedNanoseconds, maximum);
    auto reused = std::make_shared<ui::EngineStatisticsPacket>(*last); reused->sequence = 2;
    QVERIFY(!history.observe(reused, 7));
    QVERIFY(!history.observe(ended(last, std::numeric_limits<quint64>::max()), 7));
  }
  void snapshotSurvivesOwnerDestructionAndIndependentCopies() {
    ui::EngineActivityPresentation retained;
    {
      ui::EngineActivityHistory history; QVERIFY(history.observe(observed(2), 7)); retained = history.snapshot();
      auto copy = history; QVERIFY(copy.observe(observed(3), 7));
      QCOMPARE(history.snapshot()->points().size(), std::size_t{1});
      QCOMPARE(copy.snapshot()->points().size(), std::size_t{2});
      auto moved = std::move(copy); QCOMPARE(moved.snapshot()->points().size(), std::size_t{2});
      copy.clear(); QVERIFY(!copy.snapshot());
      QVERIFY(copy.observe(preparing(9), 7)); QVERIFY(copy.snapshot()->points().empty());
      history.clear(); QVERIFY(!history.snapshot());
    }
    QVERIFY(retained); QCOMPARE(retained->points().size(), std::size_t{1});
    QCOMPARE(retained->latest()->target.hash, QStringLiteral("OWNED-ENGINE"));
    QCOMPARE(retained->points().front().rates.payloadReceived, std::int64_t{80});
  }
};

QTEST_APPLESS_MAIN(EngineActivityHistoryTests)
#include "engine_activity_history_tests.moc"
