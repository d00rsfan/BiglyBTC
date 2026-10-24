#include "session_bridge.hpp"
#include "library_window.hpp"
#include "owned_engine_fixture.hpp"
#include "persistent_api_faults.hpp"
#include "profile.hpp"

#include <QApplication>
#include <QPointer>
#include <QSignalSpy>
#include <QtTest>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <utility>
#include <vector>

namespace {
using namespace biglybtc;
using Action = core::DownloadAction;
namespace lt = libtorrent;

core::Document parse(const QByteArray& raw) {
  auto result = core::Document::parse(raw.toStdString(), storage::compatibleConfigLimits());
  test::require(result.has_value(), "Cannot parse generated command fixture");
  return std::move(*result);
}
std::string encoded(const core::Node& value) {
  auto result = core::encodeValue(value, storage::compatibleConfigLimits());
  test::require(result.has_value(), "Cannot encode generated command subtree");
  return std::move(*result);
}
std::string bytes(const QString& value) {
  const auto raw = value.toUtf8();
  return std::to_string(raw.size()) + ':' + raw.toStdString();
}
QString hashText(const core::TorrentHash& hash) { return QString::fromStdString(hash.hex()); }
QString inventoryPath(const test::OwnedEngineFixture& fixture) {
  return fixture.profile + QStringLiteral("/downloads.config");
}
void editRow(test::OwnedEngineFixture& fixture, std::size_t index,
    std::initializer_list<std::pair<std::string_view, std::string>> fields) {
  auto document = parse(test::read(inventoryPath(fixture)));
  const auto* list = document.find("downloads");
  test::require(list && index < list->children.size(), "Missing generated command row");
  auto row = core::Document::parse(encoded(list->children[index]), storage::compatibleConfigLimits());
  test::require(row.has_value(), "Cannot own generated command row");
  for (const auto& [key, value] : fields) {
    test::require(row->replaceValue(key, value).has_value(), "Cannot edit generated command row");
  }
  std::string replacement = "l";
  for (std::size_t i = 0; i < list->children.size(); ++i) {
    replacement += i == index ? row->serialize() : encoded(list->children[i]);
  }
  replacement += 'e';
  test::require(document.replaceValue("downloads", replacement).has_value(), "Cannot replace generated command list");
  test::write(inventoryPath(fixture), QByteArray::fromStdString(document.serialize()));
}
// Only generated client files are merged; each fixture's real seed remains
// isolated in its original generated root. No ordinary profile is opened.
void appendMultifile(test::OwnedEngineFixture& first, const test::OwnedEngineFixture& second) {
  for (const auto& file : second.files) {
    const auto path = first.payload + '/' + file.relative;
    test::write(path, QByteArray(file.expected.size(), '\0'));
    auto imported = file;
    test::require(::stat(path.toStdString().c_str(), &imported.before) == 0, "Cannot stat imported generated payload");
    first.files.push_back(std::move(imported));
  }
  const auto state = first.profile + QStringLiteral("/active/") + hashText(*second.hash) + QStringLiteral(".dat");
  test::write(state, test::read(second.stateFile));
  auto a = parse(test::read(inventoryPath(first)));
  auto b = parse(test::read(inventoryPath(second)));
  auto row = core::Document::parse(encoded(b.find("downloads")->children.at(0)), storage::compatibleConfigLimits());
  test::require(row.has_value(), "Cannot own second generated download");
  test::require(row->replaceValue("save_dir", bytes(first.payload)).has_value(), "Cannot bind generated save root");
  test::require(row->replaceValue("torrent", bytes(state)).has_value(), "Cannot bind generated state");
  test::require(a.replaceValue("downloads", "l" + encoded(a.find("downloads")->children.at(0))
      + row->serialize() + 'e').has_value(), "Cannot merge generated command profile");
  test::write(inventoryPath(first), QByteArray::fromStdString(a.serialize()));
  editRow(first, 0, {{"state", "i70e"}, {"position", "i1e"}, {"maxdl", "i16384e"},
      {"opaque.command", "li-8e3:rawe"}});
  editRow(first, 1, {{"state", "i70e"}, {"position", "i2e"}, {"maxdl", "i16384e"},
      {"opaque.command", "li-9e3:rawe"}});
}

// One explicit loopback listener serves BOTH distinct hashes. The Bridge has
// one owned peer port, so two unrelated seed ports would not prove hash routing.
class MixedPartialPeer final {
public:
  MixedPartialPeer(const test::OwnedEngineFixture& first, const test::OwnedEngineFixture& second)
      : session_(test::localSettings()) {
    for (const auto* fixture : {&first, &second}) {
      const auto raw = test::read(fixture->stateFile); lt::error_code error;
      auto metadata = std::make_shared<lt::torrent_info>(
          lt::span<const char>(raw.constData(), static_cast<std::size_t>(raw.size())), error, lt::from_span);
      test::require(!error, "Cannot parse generated mixed peer metadata");
      metadata->clear_trackers();
      lt::add_torrent_params options; options.ti = std::move(metadata);
      options.save_path = (fixture->root.path() + QStringLiteral("/seed")).toStdString();
      options.flags &= ~(lt::torrent_flags::paused | lt::torrent_flags::auto_managed);
      options.flags |= lt::torrent_flags::disable_dht | lt::torrent_flags::disable_lsd | lt::torrent_flags::disable_pex;
      auto handle = session_.add_torrent(std::move(options), error);
      test::require(!error && handle.is_valid(), "Cannot add generated mixed peer torrent");
      handles_.push_back(std::move(handle));
    }
  }
  bool ready() const {
    return session_.listen_port() != 0 && std::ranges::all_of(handles_, [](const auto& handle) {
      const auto status = handle.status();
      return status.state != lt::torrent_status::checking_files
          && status.state != lt::torrent_status::checking_resume_data
          && status.num_pieces == test::OwnedEngineFixture::PayloadBytes / 16384 - 1 && !status.is_seeding;
    });
  }
  unsigned short port() const { return session_.listen_port(); }
private:
  lt::session session_;
  std::vector<lt::torrent_handle> handles_;
};

ui::Presentation latest(const QSignalSpy& snapshots) {
  return snapshots.isEmpty() ? ui::Presentation{} : qvariant_cast<ui::Presentation>(snapshots.last().at(0));
}
const ui::LibraryItem& item(const ui::Presentation& snapshot, const QString& hash) {
  test::require(static_cast<bool>(snapshot), "Missing library presentation");
  const auto found = std::ranges::find(snapshot->items, hash, &ui::LibraryItem::hash);
  test::require(found != snapshot->items.end(), "Missing generated hash in presentation");
  return *found;
}
ui::Target target(const QSignalSpy& snapshots, const core::TorrentHash& hash) {
  const auto snapshot = latest(snapshots);
  return {item(snapshot, hashText(hash)).hash, snapshot->revision};
}
core::LibrarySnapshot saved(const test::OwnedEngineFixture& fixture) {
  const auto document = parse(test::read(inventoryPath(fixture)));
  auto result = core::readLibrary(document);
  test::require(result.has_value(), "Cannot read saved generated command result");
  return std::move(*result);
}
const core::SavedDownload& row(const core::LibrarySnapshot& value, const core::TorrentHash& hash) {
  const auto found = std::ranges::find_if(value.downloads, [&](const auto& download) {
    return download.hash && *download.hash == hash;
  });
  test::require(found != value.downloads.end(), "Missing saved generated hash");
  return *found;
}
qint64 completed(const QSignalSpy& telemetry, const QString& hash, qsizetype first = 0) {
  qint64 result = -1;
  for (qsizetype i = first; i < telemetry.size(); ++i) {
    const auto update = qvariant_cast<ui::Telemetry>(telemetry.at(i).at(0));
    if (update.target.hash == hash) { result = std::max(result, update.completed); }
  }
  return result;
}
bool bothInOneDelivery(const QSignalSpy& telemetry, const QString& first, const QString& second) {
  for (qsizetype i = 1; i < telemetry.size(); ++i) {
    const auto a = qvariant_cast<ui::Telemetry>(telemetry.at(i - 1).at(0));
    const auto b = qvariant_cast<ui::Telemetry>(telemetry.at(i).at(0));
    if (a.target.generation == b.target.generation && a.target.generation
        && ((a.target.hash == first && b.target.hash == second) || (a.target.hash == second && b.target.hash == first))
        && a.completed > 0 && b.completed > 0) { return true; }
  }
  return false;
}
struct Stamp { dev_t device{}; ino_t inode{}; QByteArray raw; };
Stamp stamp(const QString& path) {
  struct stat value{}; test::require(::lstat(path.toStdString().c_str(), &value) == 0, "Cannot stat generated command file");
  return {value.st_dev, value.st_ino, test::read(path)};
}
void wire(ui::LibraryWindow& window, app::SessionBridge& bridge) {
  QObject::connect(&bridge, &app::SessionBridge::snapshotReady, &window, &ui::LibraryWindow::setPresentation);
  QObject::connect(&bridge, &app::SessionBridge::busyChanged, &window, &ui::LibraryWindow::setBusy);
  QObject::connect(&bridge, &app::SessionBridge::progressReady, &window, &ui::LibraryWindow::setTelemetry);
  QObject::connect(&window, &ui::LibraryWindow::downloadActionRequested, &bridge, &app::SessionBridge::downloadAction);
}
} // namespace

class LibraryCommandsBridgeTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
  }

  void rejectsWholeSelectionBeforeFirstSavedOrEngineMutation_data() {
    QTest::addColumn<int>("invalid");
    QTest::newRow("stale-second") << 0;
    QTest::newRow("unknown-second") << 1;
    QTest::newRow("duplicate-second") << 2;
    QTest::newRow("unauthorized-second") << 3;
    QTest::newRow("invalid-action-next") << 4;
    QTest::newRow("invalid-action-large") << 5;
  }
  void rejectsWholeSelectionBeforeFirstSavedOrEngineMutation() {
    QFETCH(int, invalid);
    test::OwnedEngineFixture single(false), multi(true);
    appendMultifile(single, multi);
    editRow(single, 0, {{"state", "i75e"}});
    if (invalid != 3) { editRow(single, 1, {{"state", "i75e"}}); }
    app::SessionBridge bridge;
    QSignalSpy snapshots(&bridge, &app::SessionBridge::snapshotReady);
    QSignalSpy messages(&bridge, &app::SessionBridge::messageReady);
    QSignalSpy telemetry(&bridge, &app::SessionBridge::progressReady);
    QSignalSpy closed(&bridge, &app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(single.native(), single.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(snapshots), 30000);
    // The preview open validates before optional engine creation. Explicit
    // refresh obtains permissions from the actual qualified managed owner.
    const auto opened = latest(snapshots)->revision;
    QVERIFY(bridge.refresh());
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(snapshots)->revision > opened, 5000);
    const auto before = latest(snapshots);
    QVERIFY(item(before, hashText(*single.hash)).actionMask & ui::downloadActionBit(Action::Pause));
    const auto inventory = stamp(inventoryPath(single));
    const auto firstState = stamp(single.stateFile);
    const auto secondPath = single.profile + QStringLiteral("/active/") + hashText(*multi.hash) + QStringLiteral(".dat");
    const auto secondState = stamp(secondPath);
    ui::DownloadTargets selection{target(snapshots, *single.hash), target(snapshots, *multi.hash)};
    Action action = Action::Pause;
    if (invalid == 0) { --selection.back().generation; }
    if (invalid == 1) { selection.back().hash = QString(40, QLatin1Char('F')); }
    if (invalid == 2) { selection.back() = selection.front(); }
    if (invalid == 3) { QVERIFY(!(item(before, hashText(*multi.hash)).actionMask & ui::downloadActionBit(Action::Pause))); }
    if (invalid == 4) { action = static_cast<Action>(10); }
    if (invalid == 5) { action = static_cast<Action>(999); }
    const auto messageCount = messages.count();
    QVERIFY(bridge.downloadAction(selection, action));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(snapshots)->revision > before->revision
        && messages.count() > messageCount, 5000);
    const auto after = stamp(inventoryPath(single));
    QCOMPARE(after.device, inventory.device); QCOMPARE(after.inode, inventory.inode); QCOMPARE(after.raw, inventory.raw);
    const auto firstAfter = stamp(single.stateFile); const auto secondAfter = stamp(secondPath);
    QCOMPARE(firstAfter.device, firstState.device); QCOMPARE(firstAfter.inode, firstState.inode); QCOMPARE(firstAfter.raw, firstState.raw);
    QCOMPARE(secondAfter.device, secondState.device); QCOMPARE(secondAfter.inode, secondState.inode); QCOMPARE(secondAfter.raw, secondState.raw);
    QCOMPARE(telemetry.count(), 0);
    for (const auto& file : single.files) {
      QCOMPARE(test::read(single.payload + '/' + file.relative), QByteArray(file.expected.size(), '\0'));
    }
    QVERIFY(!QFileInfo::exists(inventoryPath(single) + QStringLiteral(".saving")));
    QVERIFY(messages.last().at(0).toString() != QStringLiteral("Download command applied"));
    QVERIFY(bridge.closeSession()); QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 15000);
    QVERIFY(closed.first().at(1).toBool()); QVERIFY(closed.first().at(2).toBool());
    auto released = storage::ProfileLease::acquire(single.native()); QVERIFY(released);
  }

  void mixedTargetsRouteSavedIntentAndActualProgressIndependently() {
    test::OwnedEngineFixture single(false, true), multi(true, true);
    QVERIFY(*single.hash != *multi.hash);
    appendMultifile(single, multi); MixedPartialPeer peer(single, multi);
    QTRY_VERIFY_WITH_TIMEOUT(peer.ready(), 30000);
    ui::LibraryWindow window; app::SessionBridge bridge; wire(window, bridge);
    QSignalSpy snapshots(&bridge, &app::SessionBridge::snapshotReady);
    QSignalSpy telemetry(&bridge, &app::SessionBridge::progressReady);
    QSignalSpy closed(&bridge, &app::SessionBridge::sessionClosed);
    window.show();
    QVERIFY(bridge.open(single.native(), peer.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(snapshots), 30000);
    const auto opened = latest(snapshots)->revision;
    QVERIFY(bridge.refresh()); QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(snapshots)->revision > opened, 5000);
    auto previous = latest(snapshots)->revision;
    QVERIFY(bridge.downloadAction({target(snapshots, *single.hash), target(snapshots, *multi.hash)}, Action::Start));
    QVERIFY(bridge.busy()); QVERIFY(!bridge.refresh());
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(snapshots)->revision > previous, 10000);
    auto intent = saved(single);
    QCOMPARE(row(intent, *single.hash).startup, core::StartupState::Waiting);
    QCOMPARE(row(intent, *multi.hash).startup, core::StartupState::Waiting);
    QCOMPARE(intent.paused.size(), std::size_t{0});
    const auto firstHash = hashText(*single.hash), secondHash = hashText(*multi.hash);
    QTRY_VERIFY_WITH_TIMEOUT(bothInOneDelivery(telemetry, firstHash, secondHash), 30000);
    QVERIFY(completed(telemetry, firstHash) > 0); QVERIFY(completed(telemetry, secondHash) > 0);
    QVERIFY(completed(telemetry, firstHash) < test::OwnedEngineFixture::PayloadBytes);
    QVERIFY(completed(telemetry, secondHash) < test::OwnedEngineFixture::PayloadBytes);
    QVERIFY(window.model().rowFor(firstHash) >= 0 && window.model().rowFor(secondHash) >= 0);
    const auto beforePause = completed(telemetry, secondHash);
    const auto pauseTelemetry = telemetry.size(); previous = latest(snapshots)->revision;
    QVERIFY(bridge.downloadAction({target(snapshots, *single.hash)}, Action::Pause));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(snapshots)->revision > previous, 5000);
    intent = saved(single);
    QCOMPARE(intent.paused.size(), std::size_t{1}); QCOMPARE(intent.paused.front().hash, *single.hash);
    QCOMPARE(row(intent, *single.hash).startup, core::StartupState::Stopped);
    QCOMPARE(row(intent, *multi.hash).startup, core::StartupState::Waiting);
    QTRY_VERIFY_WITH_TIMEOUT(item(latest(snapshots), firstHash).state == QStringLiteral("Paused"), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(completed(telemetry, secondHash, pauseTelemetry) > beforePause, 15000);
    QVERIFY(item(latest(snapshots), secondHash).state != QStringLiteral("Stopped"));
    previous = latest(snapshots)->revision;
    QVERIFY(bridge.downloadAction({target(snapshots, *single.hash)}, Action::Resume));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(snapshots)->revision > previous, 5000);
    intent = saved(single); QVERIFY(intent.paused.empty());
    QCOMPARE(row(intent, *single.hash).startup, core::StartupState::Queued);
    // Stop one hash while the other real handle remains admitted. Completion
    // cannot win this race: BOTH owned peers lack their final piece.
    previous = latest(snapshots)->revision;
    QVERIFY(bridge.downloadAction({target(snapshots, *single.hash)}, Action::Stop));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(snapshots)->revision > previous, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(item(latest(snapshots), firstHash).state == QStringLiteral("Stopped"), 10000);
    QVERIFY(item(latest(snapshots), secondHash).state != QStringLiteral("Stopped"));
    intent = saved(single);
    QCOMPARE(row(intent, *single.hash).startup, core::StartupState::Stopped);
    QCOMPARE(row(intent, *multi.hash).startup, core::StartupState::Waiting);
    QVERIFY(row(intent, *single.hash).downloaded && *row(intent, *single.hash).downloaded > 0);
    previous = latest(snapshots)->revision;
    QVERIFY(bridge.downloadAction({target(snapshots, *multi.hash)}, Action::Stop));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(snapshots)->revision > previous, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(item(latest(snapshots), secondHash).state == QStringLiteral("Stopped"), 10000);
    intent = saved(single);
    QVERIFY(row(intent, *multi.hash).downloaded && *row(intent, *multi.hash).downloaded > 0);
    QCOMPARE(row(intent, *single.hash).position, std::optional<std::int64_t>{1});
    QCOMPARE(row(intent, *multi.hash).position, std::optional<std::int64_t>{2});
    QVERIFY(bridge.closeSession()); QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 15000);
    QVERIFY(closed.first().at(1).toBool()); QVERIFY(closed.first().at(2).toBool());
    QVERIFY(single.accountingIntact(false));
    auto released = storage::ProfileLease::acquire(single.native()); QVERIFY(released);
  }

  void passivePhaseSnapshotCannotReleaseQueuedCommandBusyOwnership() {
    test::OwnedEngineFixture fixture(false, true);
    editRow(fixture, 0, {{"state", "i70e"}, {"maxdl", "i16384e"}});
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    app::SessionBridge bridge;
    // This existing target-private gate releases on destruction BEFORE Bridge
    // joins, including every QVERIFY/exception exit. It does not forge alerts.
    test::accounting_fault::CallGate gate;
    QSignalSpy snapshots(&bridge, &app::SessionBridge::snapshotReady);
    QSignalSpy telemetry(&bridge, &app::SessionBridge::progressReady);
    QSignalSpy closed(&bridge, &app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(), fixture.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(snapshots), 30000);
    const auto opened = latest(snapshots)->revision;
    QVERIFY(bridge.refresh()); QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(snapshots)->revision > opened, 5000);
    auto previous = latest(snapshots)->revision;
    QVERIFY(bridge.downloadAction({target(snapshots, *fixture.hash)}, Action::Start));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(snapshots)->revision > previous, 10000);
    const auto hash = hashText(*fixture.hash);
    QTRY_VERIFY_WITH_TIMEOUT(completed(telemetry, hash) > 0 && gate.bound()
        && item(latest(snapshots), hash).state == QStringLiteral("Downloading"), 30000);
    bool armOnPauseResult = true, gateArmed = false, released = false;
    bool firstPhaseSeen = false, phaseHeldBusy = false;
    quint64 passiveRevision = 0;
    QString passiveState;
    QObject::connect(&bridge, &app::SessionBridge::snapshotReady, &bridge, [&](const ui::Presentation& snapshot) {
      if (armOnPauseResult && !bridge.busy()) {
        armOnPauseResult = false;
        gateArmed = gate.arm(test::accounting_fault::CallGate::Point::Pop);
      } else if (released && !firstPhaseSeen) {
        firstPhaseSeen = true; phaseHeldBusy = bridge.busy(); passiveRevision = snapshot->revision;
        passiveState = item(snapshot, hash).state;
      }
    });
    QVERIFY(bridge.downloadAction({target(snapshots, *fixture.hash)}, Action::Pause));
    QTRY_VERIFY_WITH_TIMEOUT(gateArmed && gate.entered(), 5000);
    QVERIFY(!bridge.busy());
    // pauseManaged sets manual intent synchronously, but sampleManaged changes
    // its phase only in the held poll. Thus the released idle poll MUST queue
    // its real Paused snapshot before this queued refresh can publish ready().
    QVERIFY(bridge.refresh()); QVERIFY(bridge.busy()); QVERIFY(!bridge.refresh());
    released = true; gate.release();
    QTRY_VERIFY_WITH_TIMEOUT(firstPhaseSeen, 5000);
    QVERIFY(phaseHeldBusy);
    QCOMPARE(passiveState, QStringLiteral("Paused"));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(snapshots)->revision > passiveRevision, 5000);
    QCOMPARE(item(latest(snapshots), hash).state, QStringLiteral("Paused"));
    QVERIFY(bridge.closeSession()); QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 15000);
    QVERIFY(closed.first().at(1).toBool()); QVERIFY(closed.first().at(2).toBool());
    auto lease = storage::ProfileLease::acquire(fixture.native()); QVERIFY(lease);
  }

  void closeReportsActualCheckpointConflictAfterJoiningAndReleasingProfile() {
    test::OwnedEngineFixture fixture(false, true);
    editRow(fixture, 0, {{"state", "i70e"}, {"maxdl", "i16384e"}});
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    app::SessionBridge bridge;
    QSignalSpy snapshots(&bridge, &app::SessionBridge::snapshotReady);
    QSignalSpy telemetry(&bridge, &app::SessionBridge::progressReady);
    QSignalSpy messages(&bridge, &app::SessionBridge::messageReady);
    QSignalSpy closed(&bridge, &app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(), fixture.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(snapshots), 30000);
    const auto opened = latest(snapshots)->revision;
    QVERIFY(bridge.refresh()); QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(snapshots)->revision > opened, 5000);
    const auto previous = latest(snapshots)->revision;
    QVERIFY(bridge.downloadAction({target(snapshots, *fixture.hash)}, Action::Start));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy() && latest(snapshots)->revision > previous, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(completed(telemetry, hashText(*fixture.hash)) > 0, 30000);
    const auto stateBefore = test::read(fixture.stateFile);
    // Deliberate non-cooperating edit of this GENERATED fixture: the managed
    // admission captured zero, so its fresh checkpoint must refuse 9001 rather
    // than overwrite it with an old baseline or claim successful persistence.
    editRow(fixture, 0, {{"downloaded", "i9001e"}});
    const auto external = test::read(inventoryPath(fixture));
    const auto messageCount = messages.count();
    bool warnedBeforeAck = false;
    QObject::connect(&bridge, &app::SessionBridge::sessionClosed, &bridge, [&](quint64, bool, bool) {
      for (qsizetype i = messageCount; i < messages.size(); ++i) {
        warnedBeforeAck = warnedBeforeAck || messages.at(i).at(0).toString().contains(
            QStringLiteral("Saved cumulative counters changed during managed admission"));
      }
    });
    QVERIFY(bridge.closeSession()); QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 15000);
    QVERIFY(warnedBeforeAck); QVERIFY(!bridge.busy());
    QVERIFY(closed.first().at(1).toBool()); QVERIFY(closed.first().at(2).toBool());
    QCOMPARE(test::read(inventoryPath(fixture)), external);
    QCOMPARE(test::read(fixture.stateFile), stateBefore); // Failed preflight cannot publish DONE or replace RECHECK.
    auto lease = storage::ProfileLease::acquire(fixture.native()); QVERIFY(lease);
    QVERIFY(!bridge.refresh());
  }
};
QTEST_MAIN(LibraryCommandsBridgeTests)
#include "library_commands_bridge_tests.moc"
