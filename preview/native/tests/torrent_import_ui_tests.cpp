#include "library_window.hpp"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolBar>
#include <QtTest>

#include <memory>

namespace {
using namespace biglybtc;
const QString Magnet = QStringLiteral("magnet:?xt=urn:btih:0123456789012345678901234567890123456789&dn=generated");
void emptyProfile(ui::LibraryWindow& window) {
  auto empty = std::make_shared<ui::LibraryPresentation>();
  empty->revision = 1;
  window.setPresentation(empty);
  window.beginColumnLayoutSession(77);
}
} // namespace

class TorrentImportUiTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
  }
  void fileMenuAndToolbarShareCtrlOpenWithoutNeedingRows() {
    ui::LibraryWindow window;
    auto* open = window.findChild<QAction*>(QStringLiteral("classic-open"));
    auto* magnet = window.findChild<QAction*>(QStringLiteral("classic-open-magnet"));
    auto* menu = window.findChild<QMenu*>(QStringLiteral("classic-file-menu"));
    auto* toolbar = window.findChild<QToolBar*>(QStringLiteral("classic-toolbar"));
    QVERIFY(open && magnet && menu && toolbar);
    QVERIFY(menu->actions().contains(open) && menu->actions().contains(magnet));
    QVERIFY(toolbar->actions().contains(open));
    QCOMPARE(open->shortcut(), QKeySequence(QStringLiteral("Ctrl+O")));
    QCOMPARE(open->shortcutContext(), Qt::WindowShortcut);
    QVERIFY(!open->isEnabled() && !magnet->isEnabled());
    QSignalSpy torrents(&window, &ui::LibraryWindow::torrentImportRequested);
    QSignalSpy magnets(&window, &ui::LibraryWindow::magnetImportRequested);
    emptyProfile(window);
    QTRY_VERIFY(open->isEnabled() && magnet->isEnabled());
    QCOMPARE(window.model().rowCount(), 0);
    QCOMPARE(torrents.count(), 0); QCOMPARE(magnets.count(), 0);
    // Cancelled/empty form values never become a worker command.
    QVERIFY(!window.submitTorrentImport({}, {}, false));
    QVERIFY(!window.submitMagnetImport({}, {}));
    QCOMPARE(torrents.count(), 0); QCOMPARE(magnets.count(), 0);
  }
  void submissionsOwnExactUserValues_data() {
    QTest::addColumn<bool>("magnet"); QTest::addColumn<bool>("start");
    QTest::newRow("torrent-start") << false << true;
    QTest::newRow("torrent-stopped") << false << false;
    QTest::newRow("magnet-link") << true << false;
  }
  void submissionsOwnExactUserValues() {
    QFETCH(bool, magnet); QFETCH(bool, start);
    QTemporaryDir owned; QVERIFY(owned.isValid());
    const auto metadata = owned.path() + QStringLiteral("/metadata file.TORRENT");
    const auto destination = owned.path() + QStringLiteral("/download folder");
    ui::LibraryWindow window;
    emptyProfile(window);
    QSignalSpy torrents(&window, &ui::LibraryWindow::torrentImportRequested);
    QSignalSpy magnets(&window, &ui::LibraryWindow::magnetImportRequested);
    if (magnet) {
      QVERIFY(window.submitMagnetImport(Magnet, destination));
      QCOMPARE(magnets.count(), 1); QCOMPARE(torrents.count(), 0);
      QCOMPARE(magnets.first().at(0).toString(), Magnet);
      QCOMPARE(magnets.first().at(1).toString(), destination);
    } else {
      QVERIFY(window.submitTorrentImport(metadata, destination, start));
      QCOMPARE(torrents.count(), 1); QCOMPARE(magnets.count(), 0);
      QCOMPARE(torrents.first().at(0).toString(), metadata);
      QCOMPARE(torrents.first().at(1).toString(), destination);
      QCOMPARE(torrents.first().at(2).toBool(), start);
    }
    // Files need not exist for syntactic UI admission. The Host performs IO and
    // real metadata validation only after receiving this value-owned request.
    QCOMPARE(window.model().rowCount(), 0);
  }
  void malformedOrUnboundedFormValuesNeverDispatch() {
    QTemporaryDir owned; QVERIFY(owned.isValid());
    const auto metadata = owned.path() + QStringLiteral("/fixture.torrent");
    const auto destination = owned.path() + QStringLiteral("/payload");
    ui::LibraryWindow window; emptyProfile(window);
    QSignalSpy torrents(&window, &ui::LibraryWindow::torrentImportRequested);
    QSignalSpy magnets(&window, &ui::LibraryWindow::magnetImportRequested);
    QVERIFY(!window.submitTorrentImport({}, destination, true));
    QVERIFY(!window.submitTorrentImport(QStringLiteral("relative.torrent"), destination, true));
    QVERIFY(!window.submitTorrentImport(owned.path() + QStringLiteral("/wrong.txt"), destination, true));
    QVERIFY(!window.submitTorrentImport(metadata, {}, true));
    QVERIFY(!window.submitTorrentImport(metadata, QStringLiteral("relative"), true));
    QVERIFY(!window.submitTorrentImport(metadata + QChar(u'\0'), destination, true));
    QVERIFY(!window.submitTorrentImport(metadata, destination + QChar(u'\0'), true));
    QVERIFY(!window.submitTorrentImport(QStringLiteral("/") + QString(4096, QLatin1Char('x')) + QStringLiteral(".torrent"), destination, true));
    QVERIFY(!window.submitMagnetImport(QStringLiteral("https://example.invalid/file"), destination));
    QVERIFY(!window.submitMagnetImport(QStringLiteral("magnet:?dn=missing-hash"), destination));
    QVERIFY(!window.submitMagnetImport(QStringLiteral("magnet:?xt="), destination));
    QVERIFY(!window.submitMagnetImport(Magnet + QChar(u'\0'), destination));
    QVERIFY(!window.submitMagnetImport(Magnet + QChar(u'\n'), destination));
    QVERIFY(!window.submitMagnetImport(Magnet + QString(16384, QLatin1Char('a')), destination));
    QVERIFY(!window.submitMagnetImport(Magnet, QStringLiteral("relative")));
    QCOMPARE(torrents.count(), 0); QCOMPARE(magnets.count(), 0);
  }
  void noProfileBusyOfflineAndClosingBlockBothSubmissionPaths() {
    QTemporaryDir owned; QVERIFY(owned.isValid());
    const auto metadata = owned.path() + QStringLiteral("/fixture.torrent");
    const auto destination = owned.path() + QStringLiteral("/payload");
    ui::LibraryWindow window;
    QSignalSpy torrents(&window, &ui::LibraryWindow::torrentImportRequested);
    QSignalSpy magnets(&window, &ui::LibraryWindow::magnetImportRequested);
    QVERIFY(!window.submitTorrentImport(metadata, destination, true));
    QVERIFY(!window.submitMagnetImport(Magnet, destination));
    emptyProfile(window);
    window.setBusy(true);
    QVERIFY(!window.submitTorrentImport(metadata, destination, true));
    QVERIFY(!window.submitMagnetImport(Magnet, destination));
    window.setBusy(false); window.setOfflinePreview(true);
    QVERIFY(!window.submitTorrentImport(metadata, destination, true));
    QVERIFY(!window.submitMagnetImport(Magnet, destination));
    window.setOfflinePreview(false);
    QVERIFY(window.submitTorrentImport(metadata, destination, false));
    QVERIFY(window.submitMagnetImport(Magnet, destination));
    QCOMPARE(torrents.count(), 1); QCOMPARE(magnets.count(), 1);
    window.show(); window.close();
    QVERIFY(!window.submitTorrentImport(metadata, destination, true));
    QVERIFY(!window.submitMagnetImport(Magnet, destination));
    QCOMPARE(torrents.count(), 1); QCOMPARE(magnets.count(), 1);
  }
  void signalReentryIsBlockedAndOwnerMayBeDeleted() {
    QTemporaryDir owned; QVERIFY(owned.isValid());
    const auto metadata = owned.path() + QStringLiteral("/fixture.torrent");
    const auto destination = owned.path() + QStringLiteral("/payload");
    auto owner = std::make_unique<ui::LibraryWindow>();
    QPointer<ui::LibraryWindow> window = owner.get();
    emptyProfile(*window);
    int calls = 0;
    bool nested = true;
    QString actualMetadata, actualDestination;
    connect(window.data(), &ui::LibraryWindow::torrentImportRequested, this,
        [&](QString source, QString target, bool) {
      ++calls; actualMetadata = std::move(source); actualDestination = std::move(target);
      nested = window->submitMagnetImport(Magnet, destination);
      owner.reset();
    });
    const bool submitted = window->submitTorrentImport(metadata, destination, true);
    QVERIFY(submitted && window.isNull());
    QCOMPARE(calls, 1); QVERIFY(!nested);
    QCOMPARE(actualMetadata, metadata); QCOMPARE(actualDestination, destination);
    QCoreApplication::processEvents();
    QCOMPARE(calls, 1);
  }
};
QTEST_MAIN(TorrentImportUiTests)
#include "torrent_import_ui_tests.moc"
