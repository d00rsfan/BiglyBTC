#include "library_commands.hpp"
#include "network_policy.hpp"
#include "owned_mainline_krpc.hpp"
#include "persistent_session.hpp"

#include <QElapsedTimer>
#include <QtTest>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <sys/inotify.h>
#include <unistd.h>

namespace {
using namespace biglybtc;
using namespace std::chrono_literals;
namespace lt = libtorrent;
using test::mainline::OwnedKrpc;

// Authentication reads metadata by design. This snapshot detects writes or
// replacement without mistaking an ordinary atime change for data mutation.
struct SavedFile {
  QString path;
  QByteArray bytes;
  struct stat stamp{};
  explicit SavedFile(QString value) : path(std::move(value)), bytes(test::read(path)) {
    test::require(::lstat(path.toStdString().c_str(), &stamp) == 0 && S_ISREG(stamp.st_mode),
                  "Cannot snapshot generated queued owner file");
  }
  bool unchanged() const {
    struct stat current{};
    return ::lstat(path.toStdString().c_str(), &current) == 0
        && current.st_dev == stamp.st_dev && current.st_ino == stamp.st_ino
        && current.st_mode == stamp.st_mode && current.st_nlink == stamp.st_nlink
        && current.st_size == stamp.st_size
        && current.st_mtim.tv_sec == stamp.st_mtim.tv_sec && current.st_mtim.tv_nsec == stamp.st_mtim.tv_nsec
        && current.st_ctim.tv_sec == stamp.st_ctim.tv_sec && current.st_ctim.tv_nsec == stamp.st_ctim.tv_nsec
        && test::read(path) == bytes;
  }
};

class UnopenedPayload final {
  int descriptor_ = -1;
public:
  explicit UnopenedPayload(const test::OwnedEngineFixture& fixture) {
    descriptor_ = ::inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    test::require(descriptor_ >= 0, "Cannot observe owned payload access");
    constexpr auto mask = IN_OPEN | IN_ACCESS | IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE
        | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_MOVE_SELF | IN_DELETE_SELF;
    try {
      test::require(::inotify_add_watch(descriptor_, fixture.payload.toStdString().c_str(), mask) >= 0,
                    "Cannot watch generated payload directory");
      for (const auto& file : fixture.files) {
        test::require(::inotify_add_watch(descriptor_, (fixture.payload + '/' + file.relative).toStdString().c_str(), mask) >= 0,
                      "Cannot watch generated real payload file");
      }
    } catch (...) { ::close(descriptor_); descriptor_ = -1; throw; }
  }
  ~UnopenedPayload() { if (descriptor_ >= 0) { ::close(descriptor_); } }
  UnopenedPayload(const UnopenedPayload&) = delete;
  UnopenedPayload& operator=(const UnopenedPayload&) = delete;
  void stop() { if (descriptor_ >= 0) { ::close(descriptor_); descriptor_ = -1; } }
  QString failure() const {
    alignas(inotify_event) std::array<char, 4096> events{};
    const auto count = ::read(descriptor_, events.data(), events.size());
    const auto readError = errno;
    if (count < 0 && readError == EAGAIN) { return {}; }
    if (count < 0) { return QStringLiteral("Payload inotify read failed: %1").arg(QString::fromLocal8Bit(std::strerror(readError))); }
    if (count == 0) { return QStringLiteral("Payload inotify unexpectedly closed"); }
    const auto* first = reinterpret_cast<const inotify_event*>(events.data());
    return QStringLiteral("Queued-only owner touched payload: mask=0x%1, watch=%2, bytes=%3")
        .arg(first->mask, 0, 16).arg(first->wd).arg(count);
  }
};

QByteArray originalCache(unsigned short port) {
  using namespace test::mainline;
  const auto node = dictionary({{"addr", string(compact(port))}, {"id", string(QByteArray(20, 'K'))},
      {"created", "i800e"}, {"lastSeen", "i995e"}, {"failedCount", "i0e"}, {"verified", "i1e"}});
  return dictionary({{"mainEntries", list({node})}, {"replacements", list({})},
      {"oldKey", string(QByteArray(20, 'R'))}, {"timestamp", "i1000e"},
      {"opaque", string(QByteArray::fromHex("00ff51"))}});
}

engine::NetworkPolicy localPolicy() {
  engine::NetworkPolicy result;
  result.mode = engine::NetworkMode::Normal; result.dht = true; result.ipv6 = false;
  result.bindAddresses = {"127.0.0.1"}; result.mainlineIpv4 = true; result.mainlineIpv6 = false;
  result.mainlinePeerBootstrapOnly = true;
  QTcpServer transfer, discovery;
  test::require(transfer.listen(QHostAddress::LocalHost, 0) && discovery.listen(QHostAddress::LocalHost, 0),
                "Cannot reserve distinct owned transfer/discovery ports");
  QUdpSocket reserved;
  test::require(reserved.bind(QHostAddress::LocalHost, discovery.serverPort(), QUdpSocket::DontShareAddress),
                "Cannot reserve actual owned Mainline UDP port");
  result.listenPort = transfer.serverPort(); result.mainlinePort = discovery.serverPort();
  return result;
}
} // namespace

class MainlineQueuedOwnerTests final : public QObject {
  Q_OBJECT
private slots:
  void savedQueuedProductionPumpUsesRealClockAndFreshGenerationWithoutPayload() {
    test::OwnedEngineFixture saved(false);
    const SavedFile initialInventory(saved.profile + "/downloads.config"), state(saved.stateFile);
    const SavedFile statistics(saved.profile + "/BiglyBT.statistics"), tables(saved.profile + "/tables.config");
    const auto initialPayload = test::read(saved.payload + "/sample.bin");
    QTcpServer unusedPeer;
    QVERIFY(unusedPeer.listen(QHostAddress::LocalHost, 0));
    OwnedKrpc wire(unusedPeer.serverPort());
    const auto wanted = lt::sha1_hash(reinterpret_cast<const char*>(saved.hash->bytes().data()));
    wire.allowWithoutPeers(wanted); wire.hold(wanted);
    const auto cachePath = saved.profile + "/plugins/mldht/IPv4-table.cache";
    test::write(cachePath, originalCache(wire.port()));
    const SavedFile cache(cachePath);
    auto lease = storage::ProfileLease::acquire(saved.native()); QVERIFY(lease);
    // The unused real seed in the shared profile fixture has a separate root;
    // it is never connected to this owner and cannot touch the watched payload.
    UnopenedPayload untouched(saved);
    QElapsedTimer elapsed; elapsed.start();
    auto created = engine::PersistentSession::create({7601, 7602, 7603});
    QVERIFY2(created.has_value(), "Requires the actual approved owner runtime");
    auto owner = std::move(*created);
    const auto policy = localPolicy();
    QVERIFY(owner->configureNetwork(policy));
    QVERIFY(owner->configureMainlineBootstrap({})); // Never use public DNS/router fallback.
    QVERIFY(owner->startIdle(*lease));
    QString failure;
    const auto pump = [&] {
      const auto result = owner->poll();
      if (!result) { failure = QStringLiteral("Actual owner poll failed (%1)").arg(static_cast<int>(result.error())); return false; }
      const auto status = owner->mainlineDiscovery();
      if (status.failure || status.queuedScrapeFailure || status.state == engine::MainlineDiscoveryStatus::State::Unavailable) {
        failure = QStringLiteral("Actual Mainline owner failed: %1; scrape: %2")
            .arg(status.failure ? QString::fromStdString(status.failure->message) : QString())
            .arg(status.queuedScrapeFailure ? QString::fromStdString(status.queuedScrapeFailure->message) : QString());
        return false;
      }
      if (!owner->managedDownloads().empty() || !owner->pendingMagnets().empty()) {
        failure = QStringLiteral("Saved scrape unexpectedly acquired a payload/metadata admission"); return false;
      }
      const auto payloadFailure = untouched.failure();
      if (!payloadFailure.isEmpty()) { failure = payloadFailure; return false; }
      if (!wire.failure.isEmpty()) { failure = wire.failure; return false; }
      return true;
    };
    QTRY_VERIFY_WITH_TIMEOUT(([&] { return !pump() || wire.scrapes() >= 1; }()), 135000);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    const auto firstWireMillis = elapsed.elapsed();
    // Coordinator construction truncates its steady-clock epoch to millis;
    // that loses at most one millisecond relative to this real elapsed timer.
    QVERIFY2(firstWireMillis >= 99999, "The production 100-second startup barrier was bypassed");
    QCOMPARE(wire.scrapes(), 1); QCOMPARE(wire.gets(wanted), 1);
    QVERIFY(wire.bootstrapQueries > 0);
    QCOMPARE(owner->mainlineDiscovery().legacyHints, std::size_t{1});
    QVERIFY(!owner->mainlineDiscovery().checkpointLoaded);
    QCOMPARE(owner->tcpListenPort().value_or(0), static_cast<unsigned short>(policy.listenPort));
    QVERIFY(owner->mainlineScrapeEstimates().empty()); // First reply is really held by the owned socket.
    QVERIFY(initialInventory.unchanged()); QVERIFY(state.unchanged()); QVERIFY(cache.unchanged());

    // Same worker/controller boundary as Host: withdraw before durable intent,
    // acknowledge only after the real saved publication and controller succeed.
    owner->revokeMainlineScrape(*saved.hash);
    const auto stopped = storage::applyLibraryCommand(*lease, *saved.hash, core::DownloadAction::Stop); QVERIFY(stopped);
    QVERIFY(owner->stopManaged(*saved.hash));
    QVERIFY(owner->rearmMainlineScrape(*saved.hash));
    const auto queued = storage::applyLibraryCommand(*lease, *saved.hash, core::DownloadAction::Queue); QVERIFY(queued);
    QVERIFY(owner->rearmMainlineScrape(*saved.hash));
    QCOMPARE(test::read(initialInventory.path), initialInventory.bytes);
    const SavedFile queuedInventory(initialInventory.path);
    wire.release(); // Actual old transaction is sent after its scope was revoked.
    const auto requeuedAt = elapsed.elapsed();
    bool oldEstimate = false;
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
      if (!pump()) { return true; }
      const auto estimates = owner->mainlineScrapeEstimates();
      if (!estimates.empty() && wire.scrapes() < 2) { oldEstimate = true; return true; }
      return !estimates.empty();
    }()), 135000);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QVERIFY2(!oldEstimate, "A revoked held reply published before a fresh wire round");
    const auto estimates = owner->mainlineScrapeEstimates(); QCOMPARE(estimates.size(), std::size_t{1});
    const auto& estimate = estimates.front();
    QCOMPARE(estimate.hash, *saved.hash); QCOMPARE(estimate.scope.size(), std::size_t{64});
    QCOMPARE(estimate.round, std::uint64_t{2});
    QCOMPARE(estimate.counts.seeds, 1); QCOMPARE(estimate.counts.peers, 1);
    QCOMPARE(estimate.counts.responses, std::size_t{1});
    QVERIFY(estimate.startedWallMillis > 0);
    QVERIFY2(elapsed.elapsed() - requeuedAt >= 59999, "A new saved generation bypassed its real initial delay");
    QCOMPARE(wire.scrapes(), 2); QCOMPARE(wire.gets(wanted), 2); QCOMPARE(wire.announced(wanted), 0);
    QCOMPARE(wire.sourcePorts, std::set<unsigned short>{static_cast<unsigned short>(policy.mainlinePort)});
    QCOMPARE(owner->mainlineDiscovery().announces, std::uint64_t{0});
    QCOMPARE(owner->mainlineDiscovery().lookups, std::uint64_t{0});
    QCOMPARE(owner->mainlineDiscovery().deliveredPeers, std::uint64_t{0});
    QVERIFY(owner->managedDownloads().empty()); QVERIFY(owner->pendingMagnets().empty());
    QVERIFY(queuedInventory.unchanged()); QVERIFY(state.unchanged()); QVERIFY(cache.unchanged());
    QVERIFY(statistics.unchanged()); QVERIFY(tables.unchanged());
    const auto closed = owner->close(std::chrono::steady_clock::now() + 10s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased && !closed.checkpointFailure);
    QVERIFY2(untouched.failure().isEmpty(), "Actual owner close touched queued-only payload");
    QVERIFY(queuedInventory.unchanged()); QVERIFY(state.unchanged()); QVERIFY(cache.unchanged());
    QVERIFY(statistics.unchanged()); QVERIFY(tables.unchanged());
    QVERIFY(saved.intact(false));
    untouched.stop(); // Only the test's final independent byte comparison may open it now.
    QCOMPARE(test::read(saved.payload + "/sample.bin"), initialPayload);
    qInfo("actual queued-owner first-wire=%lldms, fresh-round=%llu, total=%lldms",
          static_cast<long long>(firstWireMillis), static_cast<unsigned long long>(estimate.round),
          static_cast<long long>(elapsed.elapsed()));
  }
};

QTEST_GUILESS_MAIN(MainlineQueuedOwnerTests)
#include "mainline_queued_owner_tests.moc"
