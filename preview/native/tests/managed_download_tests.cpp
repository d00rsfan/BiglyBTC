#include "persistent_session.hpp"
#include "owned_engine_fixture.hpp"

#include <QtTest>

#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <unistd.h>

namespace {
using namespace biglybtc;
using namespace std::chrono_literals;
using Phase = engine::ManagedPhase;

std::unique_ptr<engine::PersistentSession> owner() {
  auto result = engine::PersistentSession::create({101, 202, 303});
  test::require(result.has_value(), "Managed tests require the approved source-bound provider");
  return std::move(*result);
}
storage::ProfileLease held(const test::OwnedEngineFixture& fixture) {
  auto result = storage::ProfileLease::acquire(fixture.native());
  test::require(result.has_value(), "Cannot hold generated managed profile");
  return std::move(*result);
}
core::Document parse(const QByteArray& raw) {
  auto result = core::Document::parse(raw.toStdString(), storage::compatibleConfigLimits());
  test::require(result.has_value(), "Cannot parse generated managed document");
  return std::move(*result);
}
std::string encoded(const core::Node& value) {
  auto result = core::encodeValue(value, storage::compatibleConfigLimits());
  test::require(result.has_value(), "Cannot encode generated managed subtree");
  return std::move(*result);
}
std::string bytes(std::string_view text) { return std::to_string(text.size()) + ':' + std::string(text); }
std::string bytes(const QString& text) { return bytes(text.toUtf8().toStdString()); }
std::optional<engine::ManagedDownloadStatus> status(engine::PersistentSession& service, const core::TorrentHash& hash) {
  const auto all = service.managedDownloads();
  const auto found = std::find_if(all.begin(), all.end(), [&](const auto& value) { return value.hash == hash; });
  return found == all.end() ? std::nullopt : std::optional{*found};
}
bool phase(engine::PersistentSession& service, const core::TorrentHash& hash, Phase expected) {
  const auto value = status(service, hash);
  return value && value->phase == expected;
}
bool hasReceived(engine::PersistentSession& service, const core::TorrentHash& hash, std::int64_t after = 0) {
  const auto value = status(service, hash);
  return value && !value->failure && value->received > after && value->pieces > 0 && !value->complete;
}

// Same small merge used by persistent_session_tests: the second seed remains in
// its own generated root, while both client layouts use the FIRST held profile.
void appendMultifile(test::OwnedEngineFixture& first, const test::OwnedEngineFixture& second) {
  for (const auto& file : second.files) {
    const auto path = first.payload + '/' + file.relative;
    test::write(path, QByteArray(file.expected.size(), '\0'));
    auto owned = file;
    test::require(::stat(path.toStdString().c_str(), &owned.before) == 0, "Cannot stat imported generated payload");
    first.files.push_back(std::move(owned));
  }
  const auto state = first.profile + QStringLiteral("/active/")
      + QString::fromStdString(second.hash->hex()) + QStringLiteral(".dat");
  test::write(state, test::read(second.stateFile));
  auto a = parse(test::read(first.profile + QStringLiteral("/downloads.config")));
  auto b = parse(test::read(second.profile + QStringLiteral("/downloads.config")));
  auto entry = core::Document::parse(encoded(b.find("downloads")->children.at(0)), storage::compatibleConfigLimits());
  test::require(entry.has_value(), "Cannot own imported generated entry");
  test::require(entry->replaceValue("save_dir", bytes(first.payload)).has_value(), "Cannot bind imported payload root");
  test::require(entry->replaceValue("torrent", bytes(state)).has_value(), "Cannot bind imported state");
  test::require(a.replaceValue("downloads", "l" + encoded(a.find("downloads")->children.at(0))
      + entry->serialize() + 'e').has_value(), "Cannot merge generated profiles");
  test::write(first.profile + QStringLiteral("/downloads.config"), QByteArray::fromStdString(a.serialize()));
}
void editFirstEntry(storage::ConfigFile& file, std::initializer_list<std::pair<std::string_view, std::string>> changes) {
  const auto* downloads = file.document().find("downloads");
  test::require(downloads && downloads->children.size() == 1, "Expected one generated entry");
  auto entry = core::Document::parse(encoded(downloads->children.front()), storage::compatibleConfigLimits());
  test::require(entry.has_value(), "Cannot own generated entry edit");
  for (const auto& [key, value] : changes) {
    test::require(entry->replaceValue(key, value).has_value(), "Cannot edit generated intent");
  }
  test::require(file.document().replaceValue("downloads", "l" + entry->serialize() + 'e').has_value(),
      "Cannot replace generated entry");
}
const core::Node& firstEntry(const core::Document& document) { return document.find("downloads")->children.at(0); }
std::int64_t integer(const core::Node& node, std::string_view key) {
  const auto* value = node.find(key);
  test::require(value && value->type == core::Node::Type::Integer, "Expected saved generated integer");
  return value->integer;
}
} // namespace

class ManagedDownloadTests final : public QObject {
  Q_OBJECT
private slots:
  void concurrentMixedHandlesRemainControllableUnderOneOwner() {
    test::OwnedEngineFixture single(false, true), multi(true, true);
    QTRY_VERIFY_WITH_TIMEOUT(single.ready() && multi.ready(), 30000);
    QVERIFY(*single.hash != *multi.hash);
    appendMultifile(single, multi);
    auto lease = held(single);
    auto service = owner();
    QVERIFY(service->startManaged(lease, {.hash = *single.hash, .ownedLoopbackPeerPort = single.port(), .forceStart = true}));
    QVERIFY(service->startManaged(lease, {.hash = *multi.hash, .ownedLoopbackPeerPort = multi.port(), .forceStart = true}));
    // Hold the second actual handle before pumping either admission. It can
    // progress after resumption while the first handle remains manually paused.
    QVERIFY(service->pauseManaged(*multi.hash, true));
    QCOMPARE(service->managedDownloads().size(), std::size_t{2});
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && hasReceived(*service, *single.hash), 30000);
    QVERIFY(service->pauseManaged(*single.hash, true));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && phase(*service, *single.hash, Phase::Paused), 5000);
    const auto singlePaused = status(*service, *single.hash); QVERIFY(singlePaused);
    const auto multiPaused = status(*service, *multi.hash); QVERIFY(multiPaused);
    QCOMPARE(multiPaused->phase, Phase::Paused);
    QVERIFY(singlePaused->admission != multiPaused->admission);
    QVERIFY(service->pauseManaged(*multi.hash, false));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value()
        && hasReceived(*service, *multi.hash, multiPaused->received)
        && phase(*service, *single.hash, Phase::Paused), 30000);
    QVERIFY(service->queueManaged(*single.hash, true, false));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value()
        && phase(*service, *single.hash, Phase::Downloading), 5000);
    QVERIFY(service->stopManaged(*single.hash));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && phase(*service, *single.hash, Phase::Stopped), 10000);
    const auto stopped = status(*service, *single.hash); QVERIFY(stopped && !stopped->failure);
    const auto other = status(*service, *multi.hash); QVERIFY(other && !other->failure);
    QVERIFY(!other->complete && other->received > multiPaused->received);
    QVERIFY(other->phase != Phase::Stopped && other->phase != Phase::Stopping);
    QCOMPARE(stopped->hash, *single.hash); QCOMPARE(other->hash, *multi.hash);
    QCOMPARE(stopped->totalPieces, test::OwnedEngineFixture::PayloadBytes / 16384);
    QCOMPARE(other->totalPieces, stopped->totalPieces);
    // Both peers deliberately lack the final piece: completion cannot race the
    // pause/stop assertions or silently retire the second admission.
    QVERIFY(!stopped->complete && !other->complete);
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased && closed.retirement.peerCounters);
    QCOMPARE(closed.retirement.peerCounters->identity(), (engine::AccountingIdentity{101, 202, 303}));
    QVERIFY(closed.retirement.peerCounters->counters().payloadReceived >= stopped->received + other->received);
    QCOMPARE(service->managedDownloads().size(), std::size_t{2});
    QVERIFY(phase(*service, *multi.hash, Phase::Stopped));
    QVERIFY(single.intact(false));
  }

  void partialStopAndRepeatedHashCheckpointsKeepFreshSavedIntent() {
    test::OwnedEngineFixture partial(false, true), full(false);
    QTRY_VERIFY_WITH_TIMEOUT(partial.ready() && full.ready(), 30000);
    QCOMPARE(*partial.hash, *full.hash);
    const auto initialState = parse(test::read(partial.stateFile));
    auto lease = held(partial);
    {
      auto baseline = storage::ConfigFile::load(lease, "downloads.config"); QVERIFY(baseline);
      editFirstEntry(*baseline, {{"downloaded", "i1000e"}, {"uploaded", "i200e"},
          {"opaque.managed", bytes(std::string_view("\0\xffmanaged", 9))}});
      QVERIFY(baseline->save(lease));
    }
    auto service = owner();
    QVERIFY(service->startManaged(lease, {.hash = *partial.hash, .ownedLoopbackPeerPort = partial.port(), .forceStart = true}));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && hasReceived(*service, *partial.hash), 30000);
    const auto originalAdmission = status(*service, *partial.hash); QVERIFY(originalAdmission);
    QByteArray requestedIntent;
    {
      // This models a later worker-side saved-intent command under the SAME
      // held lease. Checkpoint must read it fresh, not restore admission state.
      auto intent = storage::ConfigFile::load(lease, "downloads.config"); QVERIFY(intent);
      editFirstEntry(*intent, {{"state", "i70e"}, {"forceStart", "i1e"}, {"position", "i8e"}});
      const auto rawHash = partial.hash->bytes();
      const std::string hash(reinterpret_cast<const char*>(rawHash.data()), rawHash.size());
      QVERIFY(intent->document().replaceValue("pause_data", "ld4:hash" + bytes(hash)
          + "5:forcei1e5:owneri7eee"));
      QVERIFY(intent->save(lease));
      requestedIntent = test::read(partial.profile + QStringLiteral("/downloads.config"));
    }
    QVERIFY(service->stopManaged(*partial.hash));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && phase(*service, *partial.hash, Phase::Stopped), 10000);
    const auto stopped = status(*service, *partial.hash); QVERIFY(stopped && !stopped->failure);
    QVERIFY(stopped->received > 0 && !stopped->complete);
    auto requested = parse(requestedIntent);
    auto saved = parse(test::read(partial.profile + QStringLiteral("/downloads.config")));
    const auto& entry = firstEntry(saved);
    for (const auto key : {"state", "forceStart", "position", "file_priorities", "opaque.managed"}) {
      QVERIFY(firstEntry(requested).find(key) && entry.find(key));
      QCOMPARE(encoded(*entry.find(key)), encoded(*firstEntry(requested).find(key)));
    }
    QCOMPARE(encoded(*saved.find("pause_data")), encoded(*requested.find("pause_data")));
    QCOMPARE(integer(entry, "downloaded"), std::int64_t{1000} + stopped->received);
    QCOMPARE(integer(entry, "uploaded"), std::int64_t{200} + stopped->sent);
    auto state = parse(test::read(partial.stateFile));
    const auto* resume = state.find("resume"); QVERIFY(resume);
    const auto* data = resume->find("data"); QVERIFY(data);
    const auto* pieces = data->find("resume data"); QVERIFY(pieces);
    QCOMPARE(pieces->type, core::Node::Type::Bytes);
    QCOMPARE(pieces->bytes.size(), std::size_t{test::OwnedEngineFixture::PayloadBytes / 16384});
    // Every original piece remains RECHECK after a partial stop, including
    // payload not yet checked in this admission. Zero would hide earlier good
    // payload from the original reader. Verified accounting is independent.
    QCOMPARE(std::string(pieces->bytes), std::string(pieces->bytes.size(), '\2'));
    QVERIFY(data->find("valid"));
    QCOMPARE(data->find("valid")->type, core::Node::Type::Integer);
    QCOMPARE(data->find("valid")->integer, std::int64_t{0});
    const auto verified = static_cast<std::int64_t>(stopped->pieces) * 16384;
    QVERIFY(verified > 0 && verified < test::OwnedEngineFixture::PayloadBytes);
    QCOMPARE(integer(entry, "completedbytes"), verified);
    QVERIFY(state.find("opaque.fixture") && initialState.find("opaque.fixture"));
    QCOMPARE(encoded(*state.find("opaque.fixture")), encoded(*initialState.find("opaque.fixture")));
    const auto* attributes = state.find("attributes"); QVERIFY(attributes);
    QVERIFY(attributes->find("resumecomplete"));
    QCOMPARE(attributes->find("resumecomplete")->type, core::Node::Type::Integer);
    QCOMPARE(attributes->find("resumecomplete")->integer, std::int64_t{1});
    QVERIFY(attributes->find("opaque.fixture") && initialState.find("attributes")->find("opaque.fixture"));
    QCOMPARE(encoded(*attributes->find("opaque.fixture")), encoded(*initialState.find("attributes")->find("opaque.fixture")));
    const auto* fileDownloaded = attributes->find("filedownloaded"); QVERIFY(fileDownloaded);
    const auto* perFile = fileDownloaded->find("downloaded"); QVERIFY(perFile);
    QCOMPARE(perFile->children.size(), std::size_t{1});
    QCOMPARE(perFile->children.front().type, core::Node::Type::Integer);
    QCOMPARE(perFile->children.front().integer, verified);
    QVERIFY(partial.intact(false));

    // Resume on the same persistent session from the actual partial payload.
    QVERIFY(service->startManaged(lease, {.hash = *partial.hash, .ownedLoopbackPeerPort = full.port(), .forceStart = true}));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && phase(*service, *partial.hash, Phase::Seeding), 30000);
    const auto resumed = status(*service, *partial.hash); QVERIFY(resumed && resumed->complete && !resumed->failure);
    QVERIFY(resumed->admission > originalAdmission->admission);
    QVERIFY(partial.intact());
    QVERIFY(service->stopManaged(*partial.hash));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && phase(*service, *partial.hash, Phase::Stopped), 10000);
    const auto finished = status(*service, *partial.hash); QVERIFY(finished && !finished->failure);
    const auto complete = parse(test::read(partial.profile + QStringLiteral("/downloads.config")));
    QCOMPARE(integer(firstEntry(complete), "downloaded"), integer(entry, "downloaded") + finished->received);
    QCOMPARE(integer(firstEntry(complete), "completedbytes"), std::int64_t{test::OwnedEngineFixture::PayloadBytes});
    QCOMPARE(encoded(*complete.find("pause_data")), encoded(*requested.find("pause_data")));
    QCOMPARE(encoded(*firstEntry(complete).find("forceStart")), encoded(*firstEntry(requested).find("forceStart")));
    // A third, checked-complete admission must not charge its restored bytes
    // again as new network traffic.
    const auto beforeRepeat = integer(firstEntry(complete), "downloaded");
    QVERIFY(service->startManaged(lease, {.hash = *partial.hash, .ownedLoopbackPeerPort = full.port(), .forceStart = true}));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && phase(*service, *partial.hash, Phase::Seeding), 30000);
    QCOMPARE(status(*service, *partial.hash)->received, std::int64_t{0});
    QVERIFY(service->stopManaged(*partial.hash));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && phase(*service, *partial.hash, Phase::Stopped), 10000);
    QVERIFY(!status(*service, *partial.hash)->failure);
    const auto repeated = parse(test::read(partial.profile + QStringLiteral("/downloads.config")));
    QCOMPARE(integer(firstEntry(repeated), "downloaded"), beforeRepeat);
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased && closed.retirement.peerCounters);
    QVERIFY(partial.intact());
  }

  void failedAndForeignAdmissionsDoNotReplaceTheOtherActiveTorrent() {
    test::OwnedEngineFixture first(false, true), foreign(true);
    QTRY_VERIFY_WITH_TIMEOUT(first.ready() && foreign.ready(), 30000);
    auto lease = held(first);
    auto otherLease = held(foreign);
    auto service = owner();
    const auto foreignInventory = test::read(foreign.profile + QStringLiteral("/downloads.config"));
    const auto foreignState = test::read(foreign.stateFile);
    QVERIFY(service->startManaged(lease, {.hash = *first.hash, .ownedLoopbackPeerPort = first.port(), .forceStart = true}));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && hasReceived(*service, *first.hash), 30000);
    const auto original = status(*service, *first.hash); QVERIFY(original);
    const auto duplicate = service->startManaged(lease, {.hash = *first.hash, .ownedLoopbackPeerPort = first.port()});
    QVERIFY(!duplicate); QCOMPARE(duplicate.error().code, storage::ErrorCode::Conflict);
    const auto wrongProfile = service->startManaged(otherLease, {.hash = *foreign.hash, .ownedLoopbackPeerPort = foreign.port()});
    QVERIFY(!wrongProfile); QCOMPARE(wrongProfile.error().code, storage::ErrorCode::Conflict);
    const auto missingHash = service->startManaged(lease, {.hash = *foreign.hash, .ownedLoopbackPeerPort = foreign.port()});
    QVERIFY(!missingHash);
    QCOMPARE(service->managedDownloads().size(), std::size_t{1});
    QCOMPARE(status(*service, *first.hash)->admission, original->admission);
    QVERIFY(!status(*service, *first.hash)->failure);
    QCOMPARE(test::read(foreign.profile + QStringLiteral("/downloads.config")), foreignInventory);
    QCOMPARE(test::read(foreign.stateFile), foreignState);
    QVERIFY(foreign.intact(false));
    QVERIFY(service->stopManaged(*first.hash));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && phase(*service, *first.hash, Phase::Stopped), 10000);
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased && closed.retirement.peerCounters);
    const auto rejected = service->startManaged(lease, {.hash = *first.hash, .ownedLoopbackPeerPort = first.port()});
    QVERIFY(!rejected); QCOMPARE(rejected.error().code, storage::ErrorCode::Conflict);
    QVERIFY(!service->pauseManaged(*first.hash, false));
    QVERIFY(!service->queueManaged(*first.hash, true, false));
    QVERIFY(!service->stopManaged(*first.hash));
    QVERIFY(first.intact(false));
  }

  void unsafePayloadPreparationDoesNotAdmitOrRewriteAProfile() {
    test::OwnedEngineFixture fixture(false);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    const auto path = fixture.payload + '/' + fixture.files.front().relative;
    const auto saved = path + QStringLiteral(".owned-original");
    const auto outside = fixture.root.path() + QStringLiteral("/owned-sentinel");
    const QByteArray sentinel("untouched generated sentinel");
    test::write(outside, sentinel);
    QVERIFY(QFile::rename(path, saved));
    QVERIFY(::symlink(outside.toStdString().c_str(), path.toStdString().c_str()) == 0);
    const auto inventory = test::read(fixture.profile + QStringLiteral("/downloads.config"));
    const auto state = test::read(fixture.stateFile);
    auto lease = held(fixture);
    auto service = owner();
    const auto denied = service->startManaged(lease, {.hash = *fixture.hash, .ownedLoopbackPeerPort = fixture.port()});
    QVERIFY(!denied);
    QVERIFY(service->managedDownloads().empty());
    QCOMPARE(test::read(fixture.profile + QStringLiteral("/downloads.config")), inventory);
    QCOMPARE(test::read(fixture.stateFile), state);
    QCOMPARE(test::read(outside), sentinel);
    QVERIFY(QFileInfo(path).isSymLink());
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased);
    QVERIFY(!closed.retirement.peerCounters);
    QVERIFY(QFile::remove(path));
    QVERIFY(QFile::rename(saved, path));
    QVERIFY(fixture.intact(false));
  }
};
QTEST_GUILESS_MAIN(ManagedDownloadTests)
#include "managed_download_tests.moc"
