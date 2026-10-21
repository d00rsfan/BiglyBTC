#include "persistent_session.hpp"
#include "owned_engine_fixture.hpp"

#include <QCoreApplication>
#include <QtTest>

#include <chrono>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <vector>

namespace {
using namespace biglybtc;
using namespace std::chrono_literals;
using engine::AccountingError;
using engine::PersistentSession;

std::unique_ptr<PersistentSession> owner() {
  auto result = PersistentSession::create({11, 22, 33});
  test::require(result.has_value(), "Cannot allocate persistent test owner");
  return std::move(*result);
}
storage::ProfileLease held(const test::OwnedEngineFixture& fixture) {
  auto result = storage::ProfileLease::acquire(fixture.native());
  test::require(result.has_value(), "Cannot hold generated profile");
  return std::move(*result);
}
engine::LocalOptions options(const test::OwnedEngineFixture& fixture) {
  return {.profile = fixture.native(), .hash = *fixture.hash, .peerPort = fixture.port(), .timeout = 20s};
}
std::string value(const core::Node& node) {
  auto result = core::encodeValue(node, storage::compatibleConfigLimits());
  test::require(result.has_value(), "Cannot encode generated fixture entry"); return std::move(*result);
}
core::Document parse(const QByteArray& raw) {
  auto result = core::Document::parse(raw.toStdString(),
      storage::compatibleConfigLimits());
  test::require(result.has_value(), "Cannot parse generated test inventory"); return std::move(*result);
}
std::string bytes(const QString& text) {
  const auto raw = text.toUtf8(); return std::to_string(raw.size()) + ':' + raw.toStdString();
}
// Assemble a second generated torrent into the FIRST profile without directory
// discovery or an external profile. Both seed sessions keep their private seed
// roots. The engine receives the first profile's one held lease for both layouts.
void appendMultifile(test::OwnedEngineFixture& first, const test::OwnedEngineFixture& second) {
  for (const auto& file : second.files) {
    test::write(first.payload + '/' + file.relative, QByteArray(file.expected.size(), '\0'));
  }
  const auto state = first.profile + QStringLiteral("/active/")
      + QString::fromStdString(second.hash->hex()) + QStringLiteral(".dat");
  test::write(state, test::read(second.stateFile));
  auto a = parse(test::read(first.profile + QStringLiteral("/downloads.config")));
  auto b = parse(test::read(second.profile + QStringLiteral("/downloads.config")));
  auto entry = core::Document::parse(value(b.find("downloads")->children.at(0)), storage::compatibleConfigLimits());
  test::require(entry.has_value(), "Cannot own second generated entry");
  test::require(entry->replaceValue("save_dir", bytes(first.payload)).has_value(), "Cannot set generated directory");
  test::require(entry->replaceValue("torrent", bytes(state)).has_value(), "Cannot set generated torrent path");
  const auto list = "l" + value(a.find("downloads")->children.at(0)) + entry->serialize() + 'e';
  test::require(a.replaceValue("downloads", list).has_value(), "Cannot merge generated inventory");
  test::write(first.profile + QStringLiteral("/downloads.config"), QByteArray::fromStdString(a.serialize()));
}
bool copiedFilesIntact(const test::OwnedEngineFixture& first, const test::OwnedEngineFixture& second) {
  for (const auto& file : second.files) {
    if (test::read(first.payload + '/' + file.relative) != file.expected) { return false; }
  }
  return true;
}
thread_local PersistentSession* publicationOwner = nullptr;
thread_local bool publicationFenced = false;
void observePublication(engine::LocalStage stage) {
  if (stage != engine::LocalStage::Flushed) { return; }
  const auto receipt = publicationOwner ? publicationOwner->lastRetirement() : std::nullopt;
  publicationFenced = receipt && receipt->storageReleased;
  if (!publicationFenced) { throw std::runtime_error("Completion crossed an absent engine retirement fence"); }
}
struct PublicationHook {
  explicit PublicationHook(PersistentSession& service) { publicationOwner = &service; publicationFenced = false; }
  ~PublicationHook() { publicationOwner = nullptr; }
};
} // namespace

class PersistentSessionTests final : public QObject {
  Q_OBJECT
private slots:
  void lazyOwnerAndPermanentCloseNeverFabricateAZeroSample() {
    for (const engine::AccountingIdentity id : {engine::AccountingIdentity{0, 2, 3},
        engine::AccountingIdentity{1, 0, 3}, engine::AccountingIdentity{1, 2, 0}}) {
      const auto invalid = PersistentSession::create(id); QVERIFY(!invalid); QCOMPARE(invalid.error(), AccountingError::InvalidIdentity);
    }
    auto service = owner();
    for (int i = 0; i < 5; ++i) { QVERIFY(service->poll()); }
    QVERIFY(!service->latest()); QVERIFY(!service->lastRetirement());
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined); QVERIFY(closed.retirement.storageReleased);
    QVERIFY(!closed.retirement.peerCounters); QVERIFY(!closed.retirement.accountingFailure);
    const auto poll = service->poll(); QVERIFY(!poll); QCOMPARE(poll.error(), AccountingError::Closing);
    const auto again = service->close(std::chrono::steady_clock::now());
    QVERIFY(again.sessionJoined); QVERIFY(!again.retirement.peerCounters);
  }
  void foreignThreadCannotPollOrCloseTheOwner() {
    auto service = owner();
    std::optional<AccountingError> rejected; engine::CloseReceipt closed;
    std::jthread other([&] {
      const auto result = service->poll(); if (!result) { rejected = result.error(); }
      closed = service->close(std::chrono::steady_clock::now() + 5s);
    });
    other.join();
    QCOMPARE(rejected, std::optional{AccountingError::WrongThread});
    QVERIFY(!closed.sessionJoined); QCOMPARE(closed.retirement.accountingFailure, std::optional{AccountingError::WrongThread});
    QVERIFY(service->poll()); QVERIFY(!service->latest());
  }
  void sequentialMixedLayoutsAndRepeatedHashUseOneAbsoluteEpoch() {
    test::OwnedEngineFixture single(false), multi(true);
    QTRY_VERIFY_WITH_TIMEOUT(single.ready() && multi.ready(), 30000);
    appendMultifile(single, multi);
    auto lease = held(single); auto service = owner();
    std::vector<engine::ApplicationPeerObservation> updates;
    QVERIFY(service->setObservationSink([&](auto snapshot) { updates.push_back(snapshot); }));
    auto firstOptions = options(single); PublicationHook hook(*service); firstOptions.observer = &observePublication;
    const auto first = engine::continueLocal(*service, lease, firstOptions); QVERIFY(first); QVERIFY(publicationFenced);
    const auto checkpoint = service->lastRetirement(); QVERIFY(checkpoint && checkpoint->storageReleased && checkpoint->peerCounters);
    QCOMPARE(checkpoint->operation, std::uint64_t{1});
    const auto firstCounters = checkpoint->peerCounters->counters(); QVERIFY(firstCounters.payloadReceived > 0);
    const auto firstIdentity = checkpoint->peerCounters->identity(); const auto firstSequence = checkpoint->peerCounters->sequence();
    QVERIFY(single.intact());
    const auto last = service->latest(); QVERIFY(last);
    QTRY_VERIFY_WITH_TIMEOUT(([&] { const auto result = service->poll(); const auto next = service->latest();
      return result && next && next->sequence > last->sequence; })(), 5000);
    const auto idle = service->latest(); QVERIFY(idle && idle->observed && idle->observed->rates);
    QCOMPARE(idle->observed->counters, firstCounters); QCOMPARE(idle->observed->rates->received, std::int64_t{0});
    QVERIFY(idle->ownerElapsedNanoseconds > last->ownerElapsedNanoseconds);
    auto secondOptions = options(single); secondOptions.hash = *multi.hash; secondOptions.peerPort = multi.port();
    publicationFenced = false; secondOptions.observer = &observePublication;
    const auto second = engine::continueLocal(*service, lease, secondOptions); QVERIFY(second); QVERIFY(publicationFenced);
    const auto secondReceipt = service->lastRetirement(); QVERIFY(secondReceipt && secondReceipt->peerCounters && secondReceipt->storageReleased);
    QCOMPARE(secondReceipt->operation, std::uint64_t{2});
    QVERIFY(secondReceipt->peerCounters->counters().payloadReceived > firstCounters.payloadReceived);
    QCOMPARE(secondReceipt->peerCounters->identity(), firstIdentity); QVERIFY(copiedFilesIntact(single, multi));
    std::vector<core::EngineStatistics> relative;
    firstOptions.statistics = [&](const auto& sample) { relative.push_back(sample); };
    const auto third = engine::continueLocal(*service, lease, firstOptions); QVERIFY(third);
    QCOMPARE(third->received, std::int64_t{0}); QCOMPARE(third->reusedPieces, test::OwnedEngineFixture::PayloadBytes / 16384);
    const auto thirdReceipt = service->lastRetirement(); QVERIFY(thirdReceipt && thirdReceipt->peerCounters);
    QCOMPARE(thirdReceipt->operation, std::uint64_t{3});
    QCOMPARE(thirdReceipt->peerCounters->counters().payloadReceived, secondReceipt->peerCounters->counters().payloadReceived);
    for (const auto& sample : relative) { QVERIFY(core::validEngineStatistics(sample)); QCOMPARE(sample.counters.payloadReceived, std::int64_t{0}); }
    // Earlier copied evidence cannot grow when the SAME engine admits later work.
    QCOMPARE(checkpoint->peerCounters->counters(), firstCounters); QCOMPARE(checkpoint->peerCounters->sequence(), firstSequence);
    std::uint64_t sequence = 0;
    for (const auto& update : updates) {
      QCOMPARE(update.identity, firstIdentity); QVERIFY(update.sequence > sequence); sequence = update.sequence;
      if (update.observed) { QVERIFY(core::validEngineStatistics(*update.observed)); }
    }
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased && closed.retirement.peerCounters);
    QCOMPARE(closed.retirement.peerCounters->counters(), thirdReceipt->peerCounters->counters());
    const auto afterClose = engine::continueLocal(*service, lease, firstOptions); QVERIFY(!afterClose);
    QCOMPARE(afterClose.error().code, storage::ErrorCode::Conflict); QVERIFY(single.intact() && copiedFilesIntact(single, multi));
  }
  void activeCancellationRetiresBeforeCallbackStorageUnwindsAndCanRetrySameHash() {
    test::OwnedEngineFixture partial(false, true), full(false);
    QTRY_VERIFY_WITH_TIMEOUT(partial.ready() && full.ready(), 30000);
    QCOMPARE(*partial.hash, *full.hash);
    auto lease = held(partial); auto service = owner(); std::stop_source stop;
    auto opt = options(partial); opt.stop = stop.get_token();
    int callbacks = 0; bool actualPiece = false;
    opt.statistics = [&](const auto&) { ++callbacks; };
    opt.progress = [&](const auto& progress) {
      if (progress.phase == engine::LocalProgress::Phase::Downloading && progress.pieces > 0) {
        actualPiece = true; stop.request_stop();
      }
    };
    const auto result = engine::continueLocal(*service, lease, opt);
    QVERIFY(actualPiece); QVERIFY(!result); QCOMPARE(result.error().code, storage::ErrorCode::Cancelled);
    const auto receipt = service->lastRetirement(); QVERIFY(receipt && receipt->storageReleased && receipt->peerCounters);
    QVERIFY(receipt->peerCounters->counters().payloadReceived > 0); QVERIFY(partial.intact(false));
    const auto saved = core::Document::parse(test::read(partial.profile + QStringLiteral("/downloads.config")).toStdString());
    QVERIFY(saved); QCOMPARE(saved->find("downloads")->children.at(0).find("completedbytes")->integer, std::int64_t{0});
    const int before = callbacks; opt.statistics = {}; opt.progress = {};
    const auto last = service->latest(); QVERIFY(last);
    QTRY_VERIFY_WITH_TIMEOUT(([&] { const auto poll = service->poll(); const auto next = service->latest();
      return poll && next && next->sequence > last->sequence; })(), 5000);
    QCOMPARE(callbacks, before); // A callback owned by the old operation is gone.
    auto retry = options(partial); retry.peerPort = full.port();
    const auto complete = engine::continueLocal(*service, lease, retry); QVERIFY(complete); QVERIFY(partial.intact());
    const auto next = service->lastRetirement(); QVERIFY(next && next->peerCounters && next->storageReleased);
    QVERIFY(next->operation > receipt->operation); QVERIFY(next->peerCounters->sequence() > receipt->peerCounters->sequence());
  }
  void optionalThrowingSinksDoNotDisableEssentialRetentionOrSafeTransfer() {
    test::OwnedEngineFixture fixture(false); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    auto lease = held(fixture); auto service = owner(); int globalCalls = 0, localCalls = 0;
    QVERIFY(service->setObservationSink([&](auto) { ++globalCalls; throw std::runtime_error("Optional application observer"); }));
    auto opt = options(fixture);
    opt.statistics = [&](const auto&) { ++localCalls; throw std::runtime_error("Optional operation observer"); };
    const auto result = engine::continueLocal(*service, lease, opt); QVERIFY(result); QVERIFY(fixture.intact());
    QCOMPARE(globalCalls, 1); QCOMPARE(localCalls, 1);
    const auto receipt = service->lastRetirement(); QVERIFY(receipt && receipt->storageReleased && receipt->peerCounters);
    const auto last = service->latest(); QVERIFY(last && last->observed); QVERIFY(last->observed->counters.payloadReceived > 0);
    int restored = 0; QVERIFY(service->setObservationSink([&](auto) { ++restored; }));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && restored > 0, 5000);
    QCOMPARE(globalCalls, 1); QCOMPARE(localCalls, 1);
  }
  void preparationFailureCannotExposeEarlierSettlementOrEraseAbsoluteTotals() {
    test::OwnedEngineFixture fixture(false); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    auto lease = held(fixture); auto service = owner(); auto opt = options(fixture);
    QVERIFY(engine::continueLocal(*service, lease, opt));
    const auto receipt = service->lastRetirement(); QVERIFY(receipt && receipt->peerCounters);
    const auto totals = service->latest(); QVERIFY(totals && totals->observed);
    auto invalid = opt; invalid.peerPort = 0;
    const auto failed = engine::continueLocal(*service, lease, invalid); QVERIFY(!failed); QVERIFY(!service->lastRetirement());
    QCOMPARE(service->latest()->observed->counters, totals->observed->counters);
    QVERIFY(engine::continueLocal(*service, lease, opt));
    const auto next = service->lastRetirement(); QVERIFY(next && next->peerCounters);
    QCOMPARE(next->operation, receipt->operation + 2);
    QCOMPARE(next->peerCounters->counters().payloadReceived, receipt->peerCounters->counters().payloadReceived);
  }
  void foreignProfileIsRejectedBeforeCompatibleCacheOrPayloadAccess() {
    test::OwnedEngineFixture first(false), foreign(true);
    QTRY_VERIFY_WITH_TIMEOUT(first.ready() && foreign.ready(), 30000);
    auto lease = held(first); auto otherLease = held(foreign); auto service = owner();
    QVERIFY(engine::continueLocal(*service, lease, options(first)));
    const auto receipt = service->lastRetirement(); QVERIFY(receipt && receipt->peerCounters);
    const auto inventory = test::read(foreign.profile + QStringLiteral("/downloads.config"));
    const auto state = test::read(foreign.stateFile);
    const auto failed = engine::continueLocal(*service, otherLease, options(foreign));
    QVERIFY(!failed); QCOMPARE(failed.error().code, storage::ErrorCode::Conflict);
    QCOMPARE(test::read(foreign.profile + QStringLiteral("/downloads.config")), inventory);
    QCOMPARE(test::read(foreign.stateFile), state); QVERIFY(foreign.intact(false));
    QCOMPARE(service->lastRetirement()->operation, receipt->operation);
    QCOMPARE(service->lastRetirement()->peerCounters->counters(), receipt->peerCounters->counters());
  }
  void throwingProgressRetiresAnAlreadyRegisteredTorrentBeforeReturningFailure() {
    test::OwnedEngineFixture fixture(false); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    auto lease = held(fixture); auto service = owner(); auto opt = options(fixture);
    opt.progress = [](const auto&) { throw std::runtime_error("Requested observer failure"); };
    const auto failed = engine::continueLocal(*service, lease, opt); QVERIFY(!failed);
    const auto retired = service->lastRetirement(); QVERIFY(retired && retired->storageReleased && retired->peerCounters);
    const auto saved = core::Document::parse(test::read(fixture.profile + QStringLiteral("/downloads.config")).toStdString());
    QVERIFY(saved); QCOMPARE(saved->find("downloads")->children.at(0).find("completedbytes")->integer, std::int64_t{0});
    opt.progress = {};
    QVERIFY(engine::continueLocal(*service, lease, opt)); QVERIFY(fixture.intact());
    QVERIFY(service->lastRetirement()->operation > retired->operation);
  }
  void expiredCloseDeadlineCannotUpgradeAnEarlierCheckpointToEpochFinal() {
    test::OwnedEngineFixture fixture(false); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    auto lease = held(fixture); auto service = owner();
    QVERIFY(engine::continueLocal(*service, lease, options(fixture)));
    const auto settled = service->lastRetirement(); QVERIFY(settled && settled->peerCounters);
    const auto oldCounters = settled->peerCounters->counters();
    const auto closed = service->close(std::chrono::steady_clock::now());
    QVERIFY(closed.sessionJoined); QVERIFY(closed.retirement.storageReleased);
    QVERIFY(!closed.retirement.peerCounters); QVERIFY(closed.retirement.accountingFailure);
    QCOMPARE(settled->peerCounters->counters(), oldCounters); // Remains a checkpoint.
    const auto rejected = service->poll(); QVERIFY(!rejected); QCOMPARE(rejected.error(), AccountingError::Closing);
    QVERIFY(fixture.intact());
  }
  void callbackReentryCannotAdmitCloseOrRetargetTheActiveOwner() {
    test::OwnedEngineFixture fixture(false); QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    auto lease = held(fixture); auto service = owner(); auto opt = options(fixture);
    bool attempted = false, rejected = false;
    opt.statistics = [&](const auto&) {
      if (attempted) { return; } attempted = true;
      const auto nested = engine::continueLocal(*service, lease, opt);
      const auto poll = service->poll();
      const auto close = service->close(std::chrono::steady_clock::now() + 5s);
      const auto setter = service->setObservationSink({});
      rejected = !nested && nested.error().code == storage::ErrorCode::Conflict
          && !poll && poll.error() == AccountingError::ActiveOperation
          && !close.sessionJoined && close.retirement.accountingFailure == AccountingError::ActiveOperation
          && !setter && setter.error() == AccountingError::ActiveOperation;
    };
    const auto result = engine::continueLocal(*service, lease, opt);
    QVERIFY(result); QVERIFY(attempted && rejected); QVERIFY(fixture.intact());
    const auto receipt = service->lastRetirement(); QVERIFY(receipt && receipt->storageReleased && receipt->peerCounters);
    QCOMPARE(receipt->operation, std::uint64_t{1});
  }
};
QTEST_GUILESS_MAIN(PersistentSessionTests)
#include "persistent_session_tests.moc"
