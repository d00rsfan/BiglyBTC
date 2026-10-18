#include "library_window.hpp"
#include "profile.hpp"
#include "session_bridge.hpp"

#include <QAbstractItemModelTester>
#include <QAction>
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QEvent>
#include <QDir>
#include <QDirIterator>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QtTest>

namespace {
using namespace biglybtc;
std::shared_ptr<ui::LibraryPresentation> presentation(quint64 revision, int count) {
  auto value = std::make_shared<ui::LibraryPresentation>();
  value->revision = revision;
  for (int i = 0; i < count; ++i) {
    const auto hash = QStringLiteral("%1").arg(i + 1, 40, 16, QChar('0')).toUpper();
    value->items.push_back({.key = hash, .hash = hash, .name = QStringLiteral("test-%1").arg(i, 5, 10, QChar('0')),
        .state = QStringLiteral("Waiting"), .bytes = 131072, .completed = 16384, .received = 16384,
        .sent = 0, .canContinue = true});
  }
  return value;
}
void wire(ui::LibraryWindow& window, app::SessionBridge& bridge) {
  QObject::connect(&bridge, &app::SessionBridge::snapshotReady, &window, &ui::LibraryWindow::setPresentation);
  QObject::connect(&bridge, &app::SessionBridge::progressReady, &window, &ui::LibraryWindow::setTelemetry);
  QObject::connect(&bridge, &app::SessionBridge::detailsReady, &window, &ui::LibraryWindow::setDetails);
  QObject::connect(&bridge, &app::SessionBridge::busyChanged, &window, &ui::LibraryWindow::setBusy);
  QObject::connect(&bridge, &app::SessionBridge::messageReady, &window, &ui::LibraryWindow::setMessage);
  QObject::connect(&window, &ui::LibraryWindow::refreshRequested, &bridge, &app::SessionBridge::refresh);
  QObject::connect(&window, &ui::LibraryWindow::continueRequested, &bridge, &app::SessionBridge::continueDownload);
  QObject::connect(&window, &ui::LibraryWindow::detailsRequested, &bridge, &app::SessionBridge::details);
  QObject::connect(&window, &ui::LibraryWindow::cancelRequested, &bridge, &app::SessionBridge::cancel);
}
QByteArray read(const QString& name) {
  QFile file(name);
  if (!file.open(QIODevice::ReadOnly)) { return {}; }
  return file.readAll();
}
}

class DesktopTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
  }
  void modelValuesSelectionAndStaleTargets() {
    ui::LibraryWindow window;
    QAbstractItemModelTester tester(&window.model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    auto first = presentation(1, 10000);
    window.setPresentation(first);
    QCOMPARE(window.model().rowCount(), 10000);
    QCOMPARE(window.model().data(window.model().index(0, 3), Qt::UserRole).toDouble(), 12.5);
    QVERIFY(!window.model().data(window.model().index(0, 4), Qt::UserRole).isValid());
    auto* proxy = qobject_cast<QSortFilterProxyModel*>(window.table().model());
    QVERIFY(proxy);
    window.table().setCurrentIndex(proxy->mapFromSource(window.model().index(7, 0)));
    const auto target = window.model().targetAt(7).value();
    QSignalSpy resets(&window.model(), &QAbstractItemModel::modelReset);
    QVERIFY(window.model().apply(ui::Telemetry{target, 32768, 1000, 200, QStringLiteral("Downloading")}));
    QCOMPARE(window.model().data(window.model().index(7, 4), Qt::UserRole).toLongLong(), 1000);
    window.setPresentation(presentation(2, 10000));
    QCOMPARE(resets.count(), 0);
    const auto selected = proxy->mapToSource(window.table().currentIndex());
    QCOMPARE(window.model().targetAt(selected.row())->hash, target.hash);
    QVERIFY(!window.model().apply(ui::Telemetry{target, 65536, 9, 9, QStringLiteral("stale")}));
    QVERIFY(!window.model().canContinue(target));
    const auto current = window.model().targetAt(7).value();
    QVERIFY(window.model().canContinue(current));
    auto bad = presentation(3, 2);
    bad->items[1].key = bad->items[0].key;
    QVERIFY(!window.model().apply(bad));
    QVERIFY(window.model().accepts(current));
    auto unknown = presentation(3, 1);
    unknown->items[0].bytes.reset();
    window.setPresentation(unknown);
    const auto query = window.model().targetAt(0).value();
    QVERIFY(!window.model().hasDetails(query));
    QVERIFY(window.model().apply(ui::Details{query, QStringLiteral("original custom name"), 131072}));
    QVERIFY(window.model().hasDetails(query));
    QCOMPARE(window.model().data(window.model().index(0, 0), Qt::DisplayRole).toString(), QStringLiteral("original custom name"));
  }
  void closeWaitsForCancellationAndWorkerTeardown() {
    ui::LibraryWindow window;
    window.setPresentation(presentation(1, 1));
    window.show();
    QSignalSpy cancel(&window, &ui::LibraryWindow::cancelRequested);
    window.setBusy(true);
    QVERIFY(!window.close());
    QCOMPARE(cancel.count(), 1);
    QVERIFY(window.isVisible());
    window.setBusy(false);
    QTRY_VERIFY(!window.isVisible());
    for (int i = 0; i < 50; ++i) {
      auto* bridge = new app::SessionBridge;
      QVERIFY(bridge->refresh());
      bridge->cancel();
      delete bridge;
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
  }
  void cancellationFromBusyNotificationCompletes() {
    app::SessionBridge bridge;
    QSignalSpy messages(&bridge, &app::SessionBridge::messageReady);
    QObject::connect(&bridge, &app::SessionBridge::busyChanged, &bridge, [&](bool busy) { if (busy) { bridge.cancel(); } });
    QVERIFY(bridge.refresh());
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(), 30000);
    QVERIFY(!messages.isEmpty());
  }
  void filesValuesSelectionAndStaleResults() {
    ui::LibraryWindow window;
    QAbstractItemModelTester tester(&window.filesModel(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    window.setPresentation(presentation(1, 2));
    window.show();
    const auto target = window.model().targetAt(0).value();
    auto files = std::make_shared<ui::FileDetails>();
    files->target = target;
    files->items = {{0, QStringLiteral("01-first.bin"), 20000, -3, false, false},
        {1, QStringLiteral(".pad/1_12768"), 12768, 0, false, true},
        {2, QString::fromUtf8("nested/02-שب.bin"), 111072, 5, false, false},
        {3, QStringLiteral(".pad/2_3616"), 3616, 0, false, true},
        {4, QStringLiteral("nested/03-empty.bin"), 0, 0, true, false}};
    QSignalSpy requests(&window, &ui::LibraryWindow::detailsRequested);
    window.filesAction().trigger();
    QCOMPARE(requests.count(), 1);
    window.setDetails({target, QStringLiteral("test-00000"), 147456, files});
    QCOMPARE(window.filesModel().rowCount(), 5);
    QCOMPARE(window.filesTable().model()->rowCount(), 3);
    QCOMPARE(window.filesModel().data(window.filesModel().index(2, 0), Qt::DisplayRole).toString(), QString::fromUtf8("nested/02-שب.bin"));
    QCOMPARE(window.filesModel().data(window.filesModel().index(0, 2), Qt::UserRole).toInt(), -3);
    QCOMPARE(window.filesModel().data(window.filesModel().index(4, 3), Qt::UserRole).toBool(), false);
    window.paddingAction().setChecked(true);
    QCOMPARE(window.filesTable().model()->rowCount(), 5);
    auto* proxy = qobject_cast<QSortFilterProxyModel*>(window.filesTable().model());
    QVERIFY(proxy);
    window.filesTable().setCurrentIndex(proxy->mapFromSource(window.filesModel().index(2, 0)));
    QSignalSpy resets(&window.filesModel(), &QAbstractItemModel::modelReset);
    auto update = std::make_shared<ui::FileDetails>(*files);
    update->items[2].priority = 7;
    window.setDetails({target, QStringLiteral("test-00000"), 147456, update});
    QCOMPARE(resets.count(), 0);
    QCOMPARE(window.filesModel().originalIndex(proxy->mapToSource(window.filesTable().currentIndex()).row()).value(), 2);
    window.setPresentation(presentation(2, 2));
    QCOMPARE(requests.count(), 2);
    QCOMPARE(window.filesModel().rowCount(), 0);
    window.setDetails({target, QStringLiteral("stale"), 147456, files});
    QCOMPARE(window.filesModel().rowCount(), 0);
    const auto current = window.model().targetAt(0).value();
    update->target = current;
    window.setDetails({current, QStringLiteral("test-00000"), 147456, update});
    QCOMPARE(window.filesModel().rowCount(), 5);
    QCOMPARE(window.filesModel().originalIndex(proxy->mapToSource(window.filesTable().currentIndex()).row()).value(), 2);
    auto* libraryProxy = qobject_cast<QSortFilterProxyModel*>(window.table().model());
    QVERIFY(libraryProxy);
    window.table().setCurrentIndex(libraryProxy->mapFromSource(window.model().index(1, 0)));
    QCOMPARE(window.filesModel().rowCount(), 0);
    window.setDetails({current, QStringLiteral("test-00000"), 147456, update});
    QCOMPARE(window.filesModel().rowCount(), 0);
    const auto beforeCancel = requests.count();
    window.setBusy(true);
    window.cancelAction().trigger();
    window.setBusy(false);
    window.setPresentation(presentation(3, 2));
    QCoreApplication::processEvents();
    QCOMPARE(requests.count(), beforeCancel);
    window.filesAction().trigger();
    QCOMPARE(requests.count(), beforeCancel + 1);
    ui::FileModel bounded;
    bounded.select(target);
    auto many = std::make_shared<ui::FileDetails>();
    many->target = target;
    many->items.reserve(20000);
    for (int i = 0; i < 20000; ++i) {
      many->items.push_back({i, QStringLiteral("file-%1").arg(i, 5, 10, QChar('0')), i * qint64{1024}, i % 7 - 3, false, false});
    }
    QVERIFY(bounded.apply(many));
    QCOMPARE(bounded.rowCount(), 20000);
    auto ambiguous = std::make_shared<ui::FileDetails>(*many);
    ambiguous->items.back().index = 0;
    QVERIFY(!bounded.apply(ambiguous));
    QCOMPARE(bounded.rowCount(), 20000);
    auto excessive = std::make_shared<ui::FileDetails>();
    excessive->target = target;
    excessive->items.resize(ui::FileModel::MaximumRows + 1);
    QVERIFY(!bounded.apply(excessive));
    bounded.select(std::nullopt);
    QVERIFY(!bounded.apply(files));
  }
  void readOnlyFileDetails() {
    const auto profile = qEnvironmentVariable("BIGLYBTC_TEST_PROFILE");
    if (profile.isEmpty()) { QSKIP("Read-only generated profile case is run by file_details_case.py"); }
    QVERIFY(profile.startsWith(QStringLiteral("/tmp/biglybtc-")));
    const bool padded = qEnvironmentVariableIntValue("BIGLYBTC_TEST_PADDED") == 1;
    const bool paused = qEnvironmentVariableIntValue("BIGLYBTC_TEST_PAUSED") == 1;
    ui::LibraryWindow window;
    app::SessionBridge bridge;
    wire(window, bridge);
    QSignalSpy snapshots(&bridge, &app::SessionBridge::snapshotReady);
    QSignalSpy details(&bridge, &app::SessionBridge::detailsReady);
    QVERIFY(bridge.open(std::filesystem::path(profile.toStdString()), 0));
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(snapshots.count() == 1 && !bridge.busy(), 60000);
    const auto name = qEnvironmentVariable("BIGLYBTC_TEST_CACHED_NAME");
    QCOMPARE(window.model().data(window.model().index(0, 0), Qt::DisplayRole).toString(), name);
    if (paused) { QCOMPARE(window.model().data(window.model().index(0, 1), Qt::DisplayRole).toString(), QStringLiteral("Paused")); }
    QVERIFY(!window.continueAction().isEnabled());
    QVERIFY(window.filesAction().isEnabled());
    window.filesAction().trigger();
    QTRY_VERIFY_WITH_TIMEOUT(details.count() == 1 && !bridge.busy(), 60000);
    QCOMPARE(window.filesModel().rowCount(), padded ? 5 : 3);
    QCOMPARE(window.filesTable().model()->rowCount(), 3);
    QCOMPARE(window.model().data(window.model().index(0, 0), Qt::DisplayRole).toString(), name);
    QCOMPARE(window.filesModel().data(window.filesModel().index(0, 2), Qt::UserRole).toInt(), -3);
    const auto second = padded ? 2 : 1;
    QCOMPARE(window.filesModel().data(window.filesModel().index(second, 0), Qt::DisplayRole).toString(), QString::fromUtf8("nested/02-שب.bin"));
    QCOMPARE(window.filesModel().data(window.filesModel().index(second, 2), Qt::UserRole).toInt(), 5);
    QCOMPARE(window.filesModel().data(window.filesModel().index(0, 1), Qt::UserRole).toLongLong(), 20000);
    QCOMPARE(window.filesModel().data(window.filesModel().index(second, 1), Qt::UserRole).toLongLong(), 111072);
    if (padded) {
      QCOMPARE(window.filesModel().data(window.filesModel().index(1, 1), Qt::UserRole).toLongLong(), 12768);
      QCOMPARE(window.filesModel().data(window.filesModel().index(3, 1), Qt::UserRole).toLongLong(), 3616);
    }
    QCOMPARE(window.filesModel().data(window.filesModel().index(padded ? 4 : 2, 1), Qt::UserRole).toLongLong(), 0);
    window.paddingAction().setChecked(true);
    QCOMPARE(window.filesTable().model()->rowCount(), padded ? 5 : 3);
    QVERIFY(bridge.refresh());
    QTRY_VERIFY_WITH_TIMEOUT(snapshots.count() == 2 && details.count() == 2 && !bridge.busy(), 60000);
    QCOMPARE(window.filesModel().rowCount(), padded ? 5 : 3);
    const auto capture = qEnvironmentVariable("BIGLYBTC_TEST_CAPTURE");
    if (!capture.isEmpty()) { QVERIFY(window.grab().save(capture)); }
    qInfo("READ_ONLY_ORIGINAL_FILES_VERIFIED");
  }
  void realProfile() {
    const auto profile = qEnvironmentVariable("BIGLYBTC_TEST_PROFILE");
    if (profile.isEmpty()) { QSKIP("Generated profile integration is run by desktop_interop.py"); }
    QVERIFY(profile.startsWith(QStringLiteral("/tmp/biglybtc-")));
    const auto port = qEnvironmentVariableIntValue("BIGLYBTC_TEST_PEER_PORT");
    QVERIFY(port > 0 && port <= 65535);
    const auto mode = qEnvironmentVariable("BIGLYBTC_TEST_ACTION", QStringLiteral("complete"));
    const bool multifile = qEnvironmentVariableIntValue("BIGLYBTC_TEST_MULTIFILE") == 1;
    const auto expectedName = multifile ? QStringLiteral("multi-fixture") : QStringLiteral("generated.bin");
    const auto expectedSize = multifile ? qEnvironmentVariable("BIGLYBTC_TEST_BYTES").toLongLong() : qint64{131072};
    const auto config = read(profile + QStringLiteral("/biglybt.config"));
    QVERIFY(!config.isEmpty());
    ui::LibraryWindow window;
    app::SessionBridge bridge;
    wire(window, bridge);
    QSignalSpy snapshots(&bridge, &app::SessionBridge::snapshotReady);
    QSignalSpy messages(&bridge, &app::SessionBridge::messageReady);
    QVERIFY(bridge.open(std::filesystem::path(profile.toStdString()), static_cast<unsigned short>(port)));
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(snapshots.count() == 1 && !bridge.busy(), 60000);
    QCOMPARE(window.model().rowCount(), 1);
    QCOMPARE(window.model().data(window.model().index(0, 0), Qt::DisplayRole).toString(), expectedName);
    QCOMPARE(window.model().data(window.model().index(0, 2), Qt::UserRole).toLongLong(), expectedSize);
    auto conflict = storage::ProfileLease::acquire(std::filesystem::path(profile.toStdString()));
    QVERIFY(!conflict);
    QCOMPARE(conflict.error().code, storage::ErrorCode::Busy);
    const auto stale = window.model().targetAt(0).value();
    QVERIFY(bridge.refresh());
    QTRY_VERIFY_WITH_TIMEOUT(snapshots.count() == 2 && !bridge.busy(), 60000);
    QVERIFY(bridge.continueDownload(stale));
    QTRY_VERIFY_WITH_TIMEOUT(snapshots.count() == 3 && !bridge.busy(), 60000);
    QVERIFY(!messages.isEmpty());
    QVERIFY(messages.last().at(0).toString().contains(QStringLiteral("stale")));
    if (mode == QStringLiteral("complete")) {
      QVERIFY(window.continueAction().isEnabled());
      window.continueAction().trigger();
      QTRY_VERIFY_WITH_TIMEOUT(snapshots.count() >= 4 && !bridge.busy(), 120000);
      QCOMPARE(window.model().data(window.model().index(0, 3), Qt::UserRole).toDouble(), 100.0);
      const auto parent = QFileInfo(profile).absolutePath();
      if (multifile) {
        const QDir expected(qEnvironmentVariable("BIGLYBTC_TEST_EXPECTED"));
        QDirIterator iterator(expected.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
        int count = 0;
        while (iterator.hasNext()) {
          const auto file = iterator.next();
          const auto actual = parent + QStringLiteral("/payload/multi-fixture/") + expected.relativeFilePath(file);
          QVERIFY(QFile::exists(actual));
          QCOMPARE(read(actual), read(file));
          QVERIFY(!QFile::exists(actual + QStringLiteral(".az!")));
          ++count;
        }
        QCOMPARE(count, 3);
        QVERIFY(!QFile::exists(parent + QStringLiteral("/payload/multi-fixture/.pad")));
      } else {
        const auto expected = read(qEnvironmentVariable("BIGLYBTC_TEST_EXPECTED"));
        QVERIFY(!expected.isEmpty());
        QCOMPARE(read(parent + QStringLiteral("/payload/generated.bin")), expected);
        QVERIFY(!QFile::exists(parent + QStringLiteral("/payload/generated.bin.az!")));
      }
    } else {
      QVERIFY(mode == QStringLiteral("cancel") || mode == QStringLiteral("close"));
      QVERIFY(bridge.continueDownload(window.model().targetAt(0).value()));
      if (mode == QStringLiteral("close")) { QVERIFY(!window.close()); }
      else { bridge.cancel(); }
      QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(), 60000);
      if (mode == QStringLiteral("close")) { QTRY_VERIFY(!window.isVisible()); }
    }
    QCOMPARE(read(profile + QStringLiteral("/biglybt.config")), config);
    const auto capture = qEnvironmentVariable("BIGLYBTC_TEST_CAPTURE");
    if (!capture.isEmpty()) {
      QVERIFY(capture.startsWith(QStringLiteral("/tmp/biglybtc-")));
      QVERIFY(window.grab().save(capture));
    }
    qInfo("DESKTOP_PROFILE_VERIFIED");
  }
};
QTEST_MAIN(DesktopTests)
#include "desktop_tests.moc"
