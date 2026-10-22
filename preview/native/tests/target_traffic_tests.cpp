#include "target_traffic.hpp"
#include "persistent_session.hpp"
#include "owned_engine_fixture.hpp"

#include <QCoreApplication>
#include <QtTest>

#include <libtorrent/error_code.hpp>
#include <libtorrent/info_hash.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace biglybtc;
using namespace std::chrono_literals;
namespace lt = libtorrent;

core::TorrentHash hash(char value = 'a', std::size_t size = 20) {
  const auto parsed = core::TorrentHash::parse(std::string(size, value));
  test::require(parsed.has_value(), "Cannot parse synthetic target hash");
  return *parsed;
}
lt::torrent_status status(char value = 'a') {
  lt::torrent_status result;
  const std::string raw(20, value);
  result.info_hashes = lt::info_hash_t(lt::sha1_hash(raw.data()));
  result.download_rate = 113;
  result.upload_rate = 59;
  result.download_payload_rate = 97;
  result.upload_payload_rate = 43;
  return result;
}
} // namespace

class TargetTrafficTests final : public QObject {
  Q_OBJECT
private slots:
  void legacyPayloadFieldsAndUnknownDefaultRemainDistinct() {
    const engine::LocalProgress progress{engine::LocalProgress::Phase::Checking, 17, 23, 31, 1, 4};
    QCOMPARE(progress.completedBytes, std::int64_t{17});
    QCOMPARE(progress.downloadRate, std::int64_t{23});
    QCOMPARE(progress.uploadRate, std::int64_t{31});
    QVERIFY(!progress.targetTraffic);
  }
  void oneStatusCopiesCombinedAndPayloadSeparatelyAndOwnsItsHash() {
    auto sample = status(); const auto target = hash();
    const auto projected = engine::detail::targetTraffic(target, sample);
    QVERIFY(projected); QVERIFY(projected->hash == target);
    QCOMPARE(projected->downloadRate, std::int64_t{113});
    QCOMPARE(projected->uploadRate, std::int64_t{59});
    QCOMPARE(projected->payloadDownloadRate, std::int64_t{97});
    QCOMPARE(projected->payloadUploadRate, std::int64_t{43});
    // These are the same status's protocol components, not global-owner deltas.
    QCOMPARE(projected->downloadRate - projected->payloadDownloadRate, std::int64_t{16});
    QCOMPARE(projected->uploadRate - projected->payloadUploadRate, std::int64_t{16});
    sample = status('b'); sample.download_rate = 991;
    QVERIFY(projected->hash == hash());
    QCOMPARE(projected->downloadRate, std::int64_t{113});
  }
  void absentWrongTargetAndFailedOrInvalidSnapshotsStayUnknown() {
    const auto target = hash();
    QVERIFY(!engine::detail::targetTraffic(target, lt::torrent_status{}));
    QVERIFY(!engine::detail::targetTraffic(hash('b'), status()));
    for (int invalid = 0; invalid < 7; ++invalid) {
      auto sample = status();
      switch (invalid) {
      case 0: sample.download_rate = -1; break;
      case 1: sample.upload_rate = -1; break;
      case 2: sample.download_payload_rate = -1; break;
      case 3: sample.upload_payload_rate = -1; break;
      case 4: sample.download_rate = 96; break;
      case 5: sample.upload_rate = 42; break;
      case 6: sample.errc = lt::error_code(EIO, lt::generic_category()); break;
      }
      QVERIFY(!engine::detail::targetTraffic(target, sample));
    }
  }
  void actualZeroAndMaximumStatusRatesRemainKnown() {
    auto sample = status();
    sample.download_rate = sample.upload_rate = 0;
    sample.download_payload_rate = sample.upload_payload_rate = 0;
    const auto zero = engine::detail::targetTraffic(hash(), sample);
    QVERIFY(zero); QVERIFY(core::validTargetTraffic(*zero));
    QCOMPARE(zero->downloadRate, std::int64_t{0});
    QCOMPARE(zero->payloadUploadRate, std::int64_t{0});
    sample.download_rate = sample.upload_rate = std::numeric_limits<int>::max();
    sample.download_payload_rate = std::numeric_limits<int>::max() - 1;
    sample.upload_payload_rate = 0;
    const auto maximum = engine::detail::targetTraffic(hash(), sample);
    QVERIFY(maximum);
    QCOMPARE(maximum->downloadRate, std::int64_t{2147483647});
    QCOMPARE(maximum->payloadDownloadRate, std::int64_t{2147483646});
  }
  void v2TargetRequiresTheFullSameStatusHash() {
    auto sample = status();
    const std::string raw(32, 'c');
    sample.info_hashes = lt::info_hash_t(lt::sha256_hash(raw.data()));
    const auto result = engine::detail::targetTraffic(hash('c', 32), sample);
    QVERIFY(result); QVERIFY(result->hash == hash('c', 32));
    QVERIFY(!engine::detail::targetTraffic(hash('d', 32), sample));
    QVERIFY(!engine::detail::targetTraffic(hash(), sample));
  }
  void realOwnedSingleAndMultifileProgress_data() {
    QTest::addColumn<bool>("multifile");
    QTest::newRow("single") << false;
    QTest::newRow("multifile") << true;
  }
  void realOwnedSingleAndMultifileProgress() {
    QFETCH(bool, multifile);
    test::OwnedEngineFixture fixture(multifile);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    auto held = storage::ProfileLease::acquire(fixture.native()); QVERIFY(held);
    auto created = engine::PersistentSession::create({71, 72, 73});
    // Required source-bound target: absence of the real provider is a failure,
    // never a skip or synthetic substitute for the actual loopback path.
    QVERIFY(created); auto owner = std::move(*created);
    engine::LocalOptions options{.profile = fixture.native(), .hash = *fixture.hash,
        .peerPort = fixture.port(), .timeout = 20s};
    std::vector<engine::LocalProgress> received;
    options.progress = [&](const auto& progress) { received.push_back(progress); };
    const auto result = engine::continueLocal(*owner, *held, options);
    QVERIFY(result); QVERIFY(fixture.intact()); QVERIFY(!received.empty());
    bool positiveDownload = false;
    for (const auto& progress : received) {
      QVERIFY(progress.targetTraffic);
      const auto& traffic = *progress.targetTraffic;
      QVERIFY(traffic.hash == *fixture.hash); QVERIFY(core::validTargetTraffic(traffic));
      // Original progress fields keep their payload-only interpretation.
      QCOMPARE(traffic.payloadDownloadRate, progress.downloadRate);
      QCOMPARE(traffic.payloadUploadRate, progress.uploadRate);
      if (progress.phase == engine::LocalProgress::Phase::Downloading
          && traffic.payloadDownloadRate > 0) { positiveDownload = true; }
    }
    QVERIFY(positiveDownload);
    QCOMPARE(received.back().phase, engine::LocalProgress::Phase::Flushing);
    const auto count = received.size(); const auto last = received.back().targetTraffic;
    const auto closed = owner->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined); QVERIFY(closed.retirement.storageReleased);
    QVERIFY(closed.retirement.peerCounters);
    // Retirement/finality are separate authority. Close cannot republish an
    // invented zero-rate target sample or invoke the former operation callback.
    QCOMPARE(received.size(), count); QVERIFY(received.back().targetTraffic == last);
    QVERIFY(fixture.intact());
  }
};
QTEST_GUILESS_MAIN(TargetTrafficTests)
#include "target_traffic_tests.moc"
