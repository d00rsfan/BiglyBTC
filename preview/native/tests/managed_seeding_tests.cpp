#include "persistent_session.hpp"
#include "file_policy_commands.hpp"
#include "owned_engine_fixture.hpp"

#include <QtTest>

#include <libtorrent/peer_info.hpp>
#include <libtorrent/alert_types.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {
using namespace biglybtc;
using namespace std::chrono_literals;
namespace lt = libtorrent;
using Phase = engine::ManagedPhase;
constexpr int PieceBytes = 16384;

std::unique_ptr<engine::PersistentSession> owner() {
  auto result = engine::PersistentSession::create({701, 702, 703});
  test::require(result.has_value(), "Managed seeding requires the approved source-bound provider");
  return std::move(*result);
}
storage::ProfileLease held(const test::OwnedEngineFixture& fixture) {
  auto result = storage::ProfileLease::acquire(fixture.native());
  test::require(result.has_value(), "Cannot hold generated seeding profile");
  return std::move(*result);
}
core::Document parse(const QByteArray& raw) {
  auto result = core::Document::parse(raw.toStdString(), storage::compatibleConfigLimits());
  test::require(result.has_value(), "Cannot parse generated seeding state");
  return std::move(*result);
}
std::optional<engine::ManagedDownloadStatus> status(engine::PersistentSession& service, const core::TorrentHash& hash) {
  const auto values = service.managedDownloads();
  const auto found = std::ranges::find(values, hash, &engine::ManagedDownloadStatus::hash);
  return found == values.end() ? std::nullopt : std::optional{*found};
}
bool phase(engine::PersistentSession& service, const core::TorrentHash& hash, Phase expected) {
  const auto value = status(service, hash);
  return value && !value->failure && value->phase == expected;
}
bool completeRetained(engine::PersistentSession& service, const core::TorrentHash& hash) {
  const auto value = status(service, hash);
  return value && !value->failure && value->retained && value->checked && value->complete
      && value->selectedComplete && value->phase == Phase::Seeding;
}
const core::Node& entry(const core::Document& document) {
  const auto* rows = document.find("downloads");
  test::require(rows && rows->children.size() == 1, "Expected one generated seeding inventory row");
  return rows->children.front();
}
std::int64_t integer(const core::Node& parent, std::string_view name) {
  const auto* value = parent.find(name);
  test::require(value && value->type == core::Node::Type::Integer, "Expected original saved integer");
  return value->integer;
}
std::string encoded(const core::Node& node) {
  auto result = core::encodeValue(node, storage::compatibleConfigLimits());
  test::require(result.has_value(), "Cannot encode original saved subtree");
  return std::move(*result);
}
struct Resume {
  std::string flags;
  std::int64_t valid = -1, complete = -1;
  std::vector<std::int64_t> downloaded;
};
Resume resume(const test::OwnedEngineFixture& fixture) {
  const auto document = parse(test::read(fixture.stateFile));
  const auto* outer = document.find("resume");
  test::require(outer && outer->find("data"), "Missing original resume data");
  const auto& data = *outer->find("data");
  const auto* flags = data.find("resume data");
  const auto* attributes = document.find("attributes");
  test::require(flags && flags->type == core::Node::Type::Bytes && attributes
      && attributes->find("filedownloaded") && attributes->find("filedownloaded")->find("downloaded"),
      "Missing original per-file progress");
  Resume result{std::string(flags->bytes), integer(data, "valid"), integer(*attributes, "resumecomplete"), {}};
  for (const auto& value : attributes->find("filedownloaded")->find("downloaded")->children) {
    test::require(value.type == core::Node::Type::Integer, "Malformed saved per-file count");
    result.downloaded.push_back(value.integer);
  }
  return result;
}
bool onlyLinearPayloadFiles(const test::OwnedEngineFixture& fixture) {
  std::vector<std::filesystem::path> expected;
  for (const auto& file : fixture.files) { expected.emplace_back(file.relative.toStdString()); }
  std::vector<std::filesystem::path> actual;
  const auto root = std::filesystem::path(fixture.payload.toStdString());
  for (const auto& value : std::filesystem::recursive_directory_iterator(root)) {
    if (value.is_symlink()) { return false; }
    if (value.is_regular_file()) { actual.push_back(value.path().lexically_relative(root)); }
  }
  std::ranges::sort(expected); std::ranges::sort(actual);
  return actual == expected;
}
} // namespace

class ManagedSeedingTests final : public QObject {
  Q_OBJECT
private slots:
  void completedNativeHandleUploadsActualPayloadToAnIndependentOwnedLeecher_data() {
    QTest::addColumn<bool>("multifile");
    QTest::newRow("single") << false;
    QTest::newRow("multifile") << true;
  }
  void completedNativeHandleUploadsActualPayloadToAnIndependentOwnedLeecher() {
    QFETCH(bool, multifile);
    test::OwnedEngineFixture fixture(multifile);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    auto lease = held(fixture); auto service = owner();
    QVERIFY(service->startManaged(lease, {.hash = *fixture.hash,
        .ownedLoopbackPeerPort = fixture.port(), .forceStart = true}));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && completeRetained(*service, *fixture.hash), 30000);
    const auto before = status(*service, *fixture.hash); QVERIFY(before);
    QCOMPARE(before->completed, std::int64_t{test::OwnedEngineFixture::PayloadBytes});
    QVERIFY(before->received > 0); QVERIFY(fixture.intact());
    const auto port = service->tcpListenPort(); QVERIFY(port && *port != 0);
    const auto admission = before->admission;
    {
      QTemporaryDir leecherRoot(QStringLiteral("/tmp/biglybtc-managed-leecher-XXXXXX"));
      QVERIFY(leecherRoot.isValid());
      auto leecherSettings = test::localSettings();
      leecherSettings.set_int(lt::settings_pack::alert_mask,
          leecherSettings.get_int(lt::settings_pack::alert_mask) | static_cast<int>(lt::alert_category::connect));
      lt::session leecher(leecherSettings);
      const auto raw = test::read(fixture.stateFile); lt::error_code error;
      auto metadata = std::make_shared<lt::torrent_info>(
          lt::span<const char>(raw.constData(), static_cast<std::size_t>(raw.size())), error, lt::from_span);
      QVERIFY(!error); metadata->clear_trackers();
      lt::add_torrent_params options; options.ti = std::move(metadata);
      options.save_path = leecherRoot.path().toStdString();
      options.flags &= ~(lt::torrent_flags::paused | lt::torrent_flags::auto_managed);
      options.flags |= lt::torrent_flags::disable_dht | lt::torrent_flags::disable_lsd | lt::torrent_flags::disable_pex;
      auto handle = leecher.add_torrent(std::move(options), error);
      QVERIFY(!error && handle.is_valid());
      const lt::tcp::endpoint native(lt::address_v4::loopback(), *port);
      handle.connect_peer(native); // The ONLY peer submitted to this leecher.
      bool sawNative = false, sawOtherPeer = false;
      const auto downloadedFromNative = [&] {
        const auto result = service->poll();
        // The small loopback transfer may finish and disconnect between polls.
        // Retain actual connection alerts as well as current peer snapshots.
        std::vector<lt::alert*> alerts; leecher.pop_alerts(&alerts);
        for (const auto* alert : alerts) {
          if (const auto* connected = lt::alert_cast<lt::peer_connect_alert>(alert)) {
            if (connected->endpoint == native) { sawNative = true; }
            else { sawOtherPeer = true; }
          }
        }
        std::vector<lt::peer_info> peers; handle.get_peer_info(peers);
        for (const auto& peer : peers) {
          if (peer.ip == native) { sawNative = true; }
          else { sawOtherPeer = true; }
        }
        const auto nativeStatus = status(*service, *fixture.hash);
        const auto received = handle.status();
        return result && nativeStatus && nativeStatus->sent > before->sent
            && nativeStatus->retained && received.is_seeding && received.total_payload_download > 0;
      };
      QTRY_VERIFY_WITH_TIMEOUT(downloadedFromNative(), 30000);
      QVERIFY(sawNative); QVERIFY(!sawOtherPeer);
      for (const auto& file : fixture.files) {
        QCOMPARE(test::read(leecherRoot.path() + '/' + file.relative), file.expected);
      }
      const auto uploaded = status(*service, *fixture.hash); QVERIFY(uploaded);
      QCOMPARE(uploaded->admission, admission);
      QVERIFY(uploaded->retained && uploaded->complete && uploaded->selectedComplete);
      QCOMPARE(uploaded->phase, Phase::Seeding);
      QVERIFY(uploaded->sent > before->sent);
      QCOMPARE(uploaded->received, before->received);
      QVERIFY(onlyLinearPayloadFiles(fixture));
    } // Actual independent leecher aborts/joins before native retirement.
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased && closed.retirement.peerCounters);
    QVERIFY(!closed.checkpointFailure);
    QVERIFY(closed.retirement.peerCounters->counters().payloadSent > 0);
    const auto saved = parse(test::read(fixture.profile + QStringLiteral("/downloads.config")));
    QVERIFY(integer(entry(saved), "uploaded") > 0);
    const auto state = resume(fixture);
    QCOMPARE(state.flags, std::string(test::OwnedEngineFixture::PayloadBytes / PieceBytes, '\1'));
    QCOMPARE(state.valid, std::int64_t{1}); QCOMPARE(state.complete, std::int64_t{2});
    QVERIFY(fixture.intact());
  }

  void selectedCompletionRetainsSparseSkippedLinearStorageAndOriginalPolicyOnReopen() {
    test::OwnedEngineFixture fixture(true);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    auto lease = held(fixture);
    const std::array<compat::FilePolicy, 2> policy{{{false, 2}, {true, 0}}};
    QVERIFY(storage::setFilePolicies(lease, *fixture.hash, policy));
    const auto policyBefore = parse(test::read(fixture.profile + QStringLiteral("/downloads.config")));
    const auto* storedPolicy = entry(policyBefore).find("file_priorities_c"); QVERIFY(storedPolicy);
    const auto rawPolicy = encoded(*storedPolicy);
    const auto decoded = compat::originalFilePolicy(entry(policyBefore), 2); QVERIFY(decoded);
    QVERIFY(std::ranges::equal(*decoded, policy));
    const auto skippedPath = fixture.payload + '/' + fixture.files[1].relative;
    QVERIFY(QFile::remove(skippedPath)); // A missing skipped file must not become LT partfile storage.
    auto service = owner();
    QVERIFY(service->startManaged(lease, {.hash = *fixture.hash,
        .ownedLoopbackPeerPort = fixture.port(), .forceStart = true}));
    struct stat sparse{}; QVERIFY(::stat(skippedPath.toStdString().c_str(), &sparse) == 0);
    QVERIFY(S_ISREG(sparse.st_mode));
    QCOMPARE(sparse.st_size, static_cast<off_t>(fixture.files[1].expected.size()));
    QCOMPARE(sparse.st_blocks, blkcnt_t{0});
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
      const auto polled = service->poll(); const auto current = status(*service, *fixture.hash);
      return polled && current && !current->failure && current->checked && current->selectedComplete
          && !current->complete && current->retained && current->phase == Phase::Seeding;
    })(), 30000);
    const auto selected = status(*service, *fixture.hash); QVERIFY(selected);
    QVERIFY(selected->received > 0);
    QCOMPARE(selected->completed, static_cast<std::int64_t>(fixture.files[0].expected.size()));
    QCOMPARE(test::read(fixture.payload + '/' + fixture.files[0].relative), fixture.files[0].expected);
    QCOMPARE(test::read(skippedPath), QByteArray(fixture.files[1].expected.size(), '\0'));
    QVERIFY(onlyLinearPayloadFiles(fixture));
    QVERIFY(service->stopManaged(*fixture.hash));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && phase(*service, *fixture.hash, Phase::Stopped), 10000);
    const auto partial = resume(fixture);
    // Selected-complete is NOT all-piece-complete. Every original piece stays
    // RECHECK, including unselected pieces; verified byte counts are separate.
    QCOMPARE(partial.flags, std::string(test::OwnedEngineFixture::PayloadBytes / PieceBytes, '\2'));
    QCOMPARE(partial.valid, std::int64_t{0}); QCOMPARE(partial.complete, std::int64_t{1});
    QCOMPARE(partial.downloaded, (std::vector<std::int64_t>{fixture.files[0].expected.size(), 0}));
    const auto saved = parse(test::read(fixture.profile + QStringLiteral("/downloads.config")));
    QVERIFY(entry(saved).find("file_priorities_c"));
    QCOMPARE(encoded(*entry(saved).find("file_priorities_c")), rawPolicy);
    QCOMPARE(integer(entry(saved), "completedbytes"), static_cast<std::int64_t>(fixture.files[0].expected.size()));
    QVERIFY(service->startManaged(lease, {.hash = *fixture.hash, .forceStart = true})); // No peer needed for retained pieces.
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
      const auto polled = service->poll(); const auto current = status(*service, *fixture.hash);
      return polled && current && !current->failure && current->selectedComplete && !current->complete
          && current->retained && current->phase == Phase::Seeding;
    })(), 30000);
    const auto reopened = status(*service, *fixture.hash); QVERIFY(reopened);
    QVERIFY(reopened->admission > selected->admission);
    QCOMPARE(reopened->received, std::int64_t{0});
    struct stat after{}; QVERIFY(::stat(skippedPath.toStdString().c_str(), &after) == 0);
    QCOMPARE(after.st_dev, sparse.st_dev); QCOMPARE(after.st_ino, sparse.st_ino); QCOMPARE(after.st_blocks, blkcnt_t{0});
    QVERIFY(onlyLinearPayloadFiles(fixture));
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased && !closed.checkpointFailure);
    const auto final = parse(test::read(fixture.profile + QStringLiteral("/downloads.config")));
    QVERIFY(entry(final).find("file_priorities_c"));
    QCOMPARE(encoded(*entry(final).find("file_priorities_c")), rawPolicy);
    const auto finalPolicy = compat::originalFilePolicy(entry(final), 2); QVERIFY(finalPolicy);
    QVERIFY(std::ranges::equal(*finalPolicy, policy));
    QCOMPARE(resume(fixture).flags, partial.flags);
  }

  void immediateStopBeforeFirstPieceSampleRetainsExistingDataForRestart() {
    test::OwnedEngineFixture fixture(false);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    const auto path = fixture.payload + '/' + fixture.files.front().relative;
    auto original = QByteArray(test::OwnedEngineFixture::PayloadBytes, '\0');
    original.replace(0, PieceBytes, fixture.files.front().expected.first(PieceBytes));
    test::write(path, original); // One genuine valid piece already exists before admission.
    auto lease = held(fixture); auto service = owner();
    QVERIFY(service->startManaged(lease, {.hash = *fixture.hash, .forceStart = true})); // No peer: no data can arrive.
    const auto initial = status(*service, *fixture.hash); QVERIFY(initial);
    QCOMPARE(initial->phase, Phase::Checking); QVERIFY(!initial->checked);
    QCOMPARE(initial->pieces, 0); QVERIFY(initial->retained);
    // Deliberately no poll between admission and Stop. A missing first sample
    // must not turn pre-existing valid payload into durable invalid-piece flags.
    QVERIFY(service->stopManaged(*fixture.hash));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && phase(*service, *fixture.hash, Phase::Stopped), 10000);
    const auto stopped = status(*service, *fixture.hash); QVERIFY(stopped && !stopped->failure);
    QVERIFY(!stopped->retained && !stopped->complete);
    QCOMPARE(stopped->received, std::int64_t{0}); QCOMPARE(test::read(path), original);
    const auto partial = resume(fixture);
    QCOMPARE(partial.flags, std::string(test::OwnedEngineFixture::PayloadBytes / PieceBytes, '\2'));
    QCOMPARE(partial.valid, std::int64_t{0}); QCOMPARE(partial.complete, std::int64_t{1});
    QVERIFY(partial.downloaded.size() == 1);
    QVERIFY(partial.downloaded.front() == 0 || partial.downloaded.front() == PieceBytes);
    QVERIFY(service->startManaged(lease, {.hash = *fixture.hash,
        .ownedLoopbackPeerPort = fixture.port(), .forceStart = true}));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && completeRetained(*service, *fixture.hash), 30000);
    const auto restarted = status(*service, *fixture.hash); QVERIFY(restarted);
    QVERIFY(restarted->admission > initial->admission);
    QCOMPARE(restarted->received, std::int64_t{test::OwnedEngineFixture::PayloadBytes - PieceBytes});
    QVERIFY(fixture.intact());
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased && !closed.checkpointFailure);
    QCOMPARE(resume(fixture).flags, std::string(test::OwnedEngineFixture::PayloadBytes / PieceBytes, '\1'));
    QVERIFY(fixture.intact());
  }
};
QTEST_GUILESS_MAIN(ManagedSeedingTests)
#include "managed_seeding_tests.moc"
