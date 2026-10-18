#include "library_window.hpp"
#include "session_bridge.hpp"
#include "statistics_page.hpp"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QMap>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QItemSelectionModel>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTabWidget>
#include <QtTest>

#include <array>

namespace {
using namespace biglybtc;

QByteArray bytes(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

void wire(ui::LibraryWindow& window, app::SessionBridge& bridge) {
  QObject::connect(&bridge, &app::SessionBridge::snapshotReady, &window, &ui::LibraryWindow::setPresentation);
  QObject::connect(&bridge, &app::SessionBridge::progressReady, &window, &ui::LibraryWindow::setTelemetry);
  QObject::connect(&bridge, &app::SessionBridge::detailsReady, &window, &ui::LibraryWindow::setDetails);
  QObject::connect(&bridge, &app::SessionBridge::savedServicesReady, &window, &ui::LibraryWindow::setSavedServices);
  QObject::connect(&bridge, &app::SessionBridge::statisticsReady, &window, &ui::LibraryWindow::setStatistics);
  QObject::connect(&bridge, &app::SessionBridge::engineStatisticsReady, &window, &ui::LibraryWindow::setEngineStatistics);
  QObject::connect(&bridge, &app::SessionBridge::columnLayoutSessionReady, &window, &ui::LibraryWindow::beginColumnLayoutSession);
  QObject::connect(&bridge, &app::SessionBridge::columnLayoutsReady, &window, &ui::LibraryWindow::setColumnLayouts);
  QObject::connect(&bridge, &app::SessionBridge::columnLayoutSaved, &window, &ui::LibraryWindow::setColumnLayoutSaved);
  QObject::connect(&bridge, &app::SessionBridge::busyChanged, &window, &ui::LibraryWindow::setBusy);
  QObject::connect(&bridge, &app::SessionBridge::messageReady, &window, &ui::LibraryWindow::setMessage);
  QObject::connect(&window, &ui::LibraryWindow::refreshRequested, &bridge, &app::SessionBridge::refresh);
  QObject::connect(&window, &ui::LibraryWindow::continueRequested, &bridge, &app::SessionBridge::continueDownload);
  QObject::connect(&window, &ui::LibraryWindow::detailsRequested, &bridge, &app::SessionBridge::details);
  QObject::connect(&window, &ui::LibraryWindow::savedServicesRequested, &bridge, &app::SessionBridge::savedServices);
  QObject::connect(&window, &ui::LibraryWindow::statisticsRequested, &bridge, &app::SessionBridge::statistics);
  QObject::connect(&window, &ui::LibraryWindow::columnLayoutsRequested, &bridge, &app::SessionBridge::columnLayouts);
  QObject::connect(&window, &ui::LibraryWindow::columnLayoutSaveRequested, &bridge, &app::SessionBridge::saveColumnLayout);
  QObject::connect(&window, &ui::LibraryWindow::cancelRequested, &bridge, &app::SessionBridge::cancel);
}
} // namespace

class PreviewSmoke final : public QObject {
  Q_OBJECT
private slots:
  void generatedSampleLibraryAndFiles() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
    const auto profile = qEnvironmentVariable("BIGLYBTC_PREVIEW_PROFILE");
    QVERIFY2(!profile.isEmpty(), "Prepare the sample with preview.py and set BIGLYBTC_PREVIEW_PROFILE");
    const QFileInfo profileInfo(profile);
    const QDir root(profileInfo.absolutePath());
    QVERIFY(profileInfo.isDir());
    QCOMPARE(profileInfo.fileName(), QStringLiteral("profile"));
    QCOMPARE(profileInfo.canonicalFilePath(), profile);
    QCOMPARE(QFileInfo(root.absolutePath()).absolutePath(), QStringLiteral("/tmp"));
    QVERIFY(root.dirName().startsWith(QStringLiteral("biglybtc-preview-")));

    QMap<QString, QByteArray> original;
    original.insert(profile + QStringLiteral("/downloads.config"), {});
    original.insert(profile + QStringLiteral("/tracker.config"), {});
    original.insert(profile + QStringLiteral("/sharing.config"), {});
    original.insert(profile + QStringLiteral("/tables.config"), {});
    original.insert(profile + QStringLiteral("/BiglyBT.statistics"), {});
    original.insert(profile + QStringLiteral("/active/cache.dat"), {});
    const QDir active(profile + QStringLiteral("/active"));
    const auto states = active.entryList({QStringLiteral("*.dat")}, QDir::Files);
    QCOMPARE(states.size(), 5); // Four torrent states and the original metadata cache.
    for (const auto& name : states) { original.insert(active.filePath(name), {}); }
    for (auto it = original.begin(); it != original.end(); ++it) {
      it.value() = bytes(it.key());
      QVERIFY2(!it.value().isEmpty(), qPrintable(it.key()));
    }
    const QDir payload(root.filePath(QStringLiteral("payload")));
    QVERIFY(payload.exists());
    QVERIFY(payload.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());

    const std::array<QString, 4> names{QStringLiteral("Welcome to BiglyBTC.txt"),
        QString::fromUtf8("Sample music — ש العربية"), QStringLiteral("Example documents"),
        QStringLiteral("Paused sample.bin")};
    const std::array<QString, 4> expectedStates{QStringLiteral("Complete"), QStringLiteral("Stopped"),
        QStringLiteral("Queued"), QStringLiteral("Paused")};
    constexpr std::array<int, 4> fileCounts{1, 2, 2, 1};

    ui::LibraryWindow window;
    window.setOfflinePreview(true);
    app::SessionBridge bridge;
    wire(window, bridge);
    QSignalSpy snapshots(&bridge, &app::SessionBridge::snapshotReady);
    QSignalSpy details(&bridge, &app::SessionBridge::detailsReady);
    QSignalSpy layouts(&bridge, &app::SessionBridge::columnLayoutsReady);
    QVERIFY(bridge.open(std::filesystem::path(profile.toStdString()), 0));
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(snapshots.count() == 1 && layouts.count() == 1 && !bridge.busy(), 30000);
    QCOMPARE(window.incompleteTable().columnWidth(1), 124);
    QCOMPARE(window.completeTable().columnWidth(1), 132);
    QCOMPARE(window.incompleteTable().horizontalHeader()->sectionResizeMode(0), QHeaderView::Stretch);
    QCOMPARE(window.model().rowCount(), 4);
    QCOMPARE(window.mainTabs().tabText(0), QStringLiteral("My Torrents"));
    auto* myTorrents = window.findChild<QAction*>(QStringLiteral("classic-my-torrents"));
    QVERIFY(myTorrents && myTorrents->isEnabled());
    QCOMPARE(myTorrents->shortcut(), QKeySequence(QStringLiteral("Ctrl+1")));
    QVERIFY(!window.continueAction().isEnabled());
    QCOMPARE(window.incompleteTable().model()->rowCount(), 3);
    QCOMPARE(window.completeTable().model()->rowCount(), 1);
    QCOMPARE(window.model().columnCount(), 13);
    QCOMPARE(window.model().headerData(9, Qt::Horizontal, Qt::DisplayRole).toString(), QStringLiteral("Saved queue position"));
    QVERIFY(!window.incompleteTable().isColumnHidden(9));
    QVERIFY(!window.completeTable().isColumnHidden(9));
    for (int column = 10; column < 13; ++column) {
      QVERIFY(window.incompleteTable().isColumnHidden(column));
      QVERIFY(window.completeTable().isColumnHidden(column));
    }
    const std::array<qint64, 3> selectedSizes{32768, 32768, 0};
    const std::array<qint64, 3> selectedRemaining{0, 16384, 0};
    const std::array<int, 3> selectedDone{1000, 500, 1000};
    for (int row = 0; row < 4; ++row) {
      if (row == 3) {
        for (int column = 10; column < 13; ++column) {
          QVERIFY(!window.model().data(window.model().index(row, column), Qt::UserRole).isValid());
          QCOMPARE(window.model().data(window.model().index(row, column), Qt::DisplayRole).toString(), QString::fromUtf8("—"));
        }
      } else {
        const auto at = static_cast<std::size_t>(row);
        QCOMPARE(window.model().data(window.model().index(row, 10), Qt::UserRole).toLongLong(), selectedSizes[at]);
        QCOMPARE(window.model().data(window.model().index(row, 11), Qt::UserRole).toLongLong(), selectedRemaining[at]);
        QCOMPARE(window.model().data(window.model().index(row, 12), Qt::UserRole).toInt(), selectedDone[at]);
      }
    }
    // Zero selected bytes/100% saved selected Done does not reclassify the row.
    QCOMPARE(window.model().completionAt(2), std::optional<bool>{false});

    for (int row = 0; row < 4; ++row) {
      const auto at = static_cast<std::size_t>(row);
      QCOMPARE(window.model().data(window.model().index(row, 9), Qt::UserRole).toLongLong(), qint64{row + 1});
      const qint64 total = (row + 1) * qint64{32768};
      QCOMPARE(window.model().data(window.model().index(row, 0), Qt::DisplayRole).toString(), names[at]);
      QCOMPARE(window.model().data(window.model().index(row, 1), Qt::DisplayRole).toString(), expectedStates[at]);
      QCOMPARE(window.model().data(window.model().index(row, 2), Qt::UserRole).toLongLong(), total);
      const auto target = window.model().targetAt(row);
      QVERIFY(target.has_value());
      QVERIFY(!window.model().canContinue(*target));
      auto& pane = row == 0 ? window.completeTable() : window.incompleteTable();
      auto* proxy = qobject_cast<QSortFilterProxyModel*>(pane.model());
      QVERIFY(proxy);
      const auto visible = proxy->mapFromSource(window.model().index(row, 0));
      QVERIFY(visible.isValid());
      pane.setFocus();
      QCoreApplication::processEvents();
      pane.selectionModel()->setCurrentIndex(visible, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
      if (row == 0) {
        QVERIFY(window.filesAction().isEnabled());
        window.filesAction().trigger();
      }
      QTRY_VERIFY_WITH_TIMEOUT(details.count() == row + 1 && !bridge.busy(), 30000);
      QVERIFY(window.filesModel().target() == target);
      QCOMPARE(window.filesModel().rowCount(), fileCounts[at]);
      QCOMPARE(window.filesTable().model()->rowCount(), fileCounts[at]);
      QVERIFY(!window.continueAction().isEnabled());
      qint64 sum = 0;
      for (int file = 0; file < fileCounts[at]; ++file) {
        const auto expectedPath = fileCounts[at] == 1 ? names[at]
            : file == 0 ? QStringLiteral("Read me.txt") : QString::fromUtf8("Examples/Unicode — ש.txt");
        QCOMPARE(window.filesModel().data(window.filesModel().index(file, 0), Qt::DisplayRole).toString(), expectedPath);
        const auto size = window.filesModel().data(window.filesModel().index(file, 1), Qt::UserRole).toLongLong();
        QCOMPARE(size, total / fileCounts[at]);
        sum += size;
        QCOMPARE(window.filesModel().originalIndex(file).value_or(-1), file);
        const bool wanted = row != 2 && !(row == 1 && file == 1);
        QCOMPARE(window.filesModel().data(window.filesModel().index(file, 3), Qt::UserRole).toBool(), wanted);
      }
      QCOMPARE(sum, total);
    }

    const auto selected = window.filesModel().target();
    QVERIFY(selected.has_value());
    QVERIFY(bridge.refresh());
    QTRY_VERIFY_WITH_TIMEOUT(snapshots.count() == 2 && details.count() == 5 && !bridge.busy(), 30000);
    QCOMPARE(window.model().rowCount(), 4);
    QCOMPARE(window.filesModel().target()->hash, selected->hash);
    // Ordinary refresh leaves session choices alone; explicit Reload restores
    // the saved headers without changing the current selected/detail target.
    auto* reloadLayout = window.findChild<QAction*>(QStringLiteral("classic-reload-column-layout"));
    QVERIFY(reloadLayout && reloadLayout->isEnabled());
    const auto beforeReloadTarget = window.filesModel().target();
    window.incompleteTable().setColumnWidth(1, 211);
    reloadLayout->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(layouts.count() == 2 && !bridge.busy(), 30000);
    QCOMPARE(window.incompleteTable().columnWidth(1), 124);
    QCOMPARE(window.completeTable().columnWidth(1), 132);
    QVERIFY(window.filesModel().target() == beforeReloadTarget);
    QCOMPARE(snapshots.count(), 2);
    QCOMPARE(details.count(), 5);
    QVERIFY(window.filesModel().target()->generation > selected->generation);
    QVERIFY(!window.continueAction().isEnabled());
    const auto capture = qEnvironmentVariable("BIGLYBTC_PREVIEW_CAPTURE");
    if (!capture.isEmpty()) {
      const QFileInfo image(capture);
      QCOMPARE(image.absoluteDir().canonicalPath(), root.canonicalPath());
      QVERIFY(!image.exists());
      QVERIFY(window.grab().save(capture));
      const auto chooserTarget = window.filesModel().target();
      QVERIFY(chooserTarget);
      // Exercise the actual chooser and retain a second view of the new saved
      // columns, then restore the usual preview defaults before navigation.
      for (auto* table : {&window.incompleteTable(), &window.completeTable()}) {
        const auto prefix = table == &window.completeTable() ? QStringLiteral("classic-columns-complete")
                                                            : QStringLiteral("classic-columns-incomplete");
        for (int column = 2; column < 13; ++column) {
          auto* action = table->findChild<QAction*>(prefix + QStringLiteral("-column-%1").arg(column));
          QVERIFY(action);
          const bool wanted = column >= 10;
          if (action->isChecked() != wanted) { action->trigger(); }
          QCOMPARE(table->isColumnHidden(column), !wanted);
        }
      }
      QCoreApplication::processEvents();
      const auto selectedCapture = root.filePath(QStringLiteral("saved-selected-preview.png"));
      QVERIFY(!QFileInfo::exists(selectedCapture));
      QVERIFY(window.grab().save(selectedCapture));
      for (auto* table : {&window.incompleteTable(), &window.completeTable()}) {
        const auto name = table == &window.completeTable() ? QStringLiteral("classic-columns-complete-reset")
                                                          : QStringLiteral("classic-columns-incomplete-reset");
        auto* reset = table->findChild<QAction*>(name);
        QVERIFY(reset);
        reset->trigger();
        for (int column = 10; column < 13; ++column) { QVERIFY(table->isColumnHidden(column)); }
      }
      QVERIFY(window.filesModel().target() == chooserTarget);
    }
    QSignalSpy saved(&bridge, &app::SessionBridge::savedServicesReady);
    const auto detailCount = details.count();
    const auto libraryCount = snapshots.count();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());
    window.filesTable().setFocus();
    QTRY_VERIFY(window.filesTable().hasFocus());
    QTest::keyClick(&window.filesTable(), Qt::Key_2, Qt::ControlModifier);
    QTRY_VERIFY_WITH_TIMEOUT(window.hostingPage() && window.hostingPage()->state() == ui::SavedPageState::Ready
        && !bridge.busy() && saved.count() == 1, 30000);
    auto* hosting = window.hostingPage();
    QCOMPARE(window.mainTabs().currentWidget(), hosting);
    QCOMPARE(hosting->model().rowCount(), 2);
    QCOMPARE(hosting->model().data(hosting->model().index(0, 1)).toString(), QStringLiteral("Persistent host"));
    QCOMPARE(hosting->model().data(hosting->model().index(0, 2)).toString(), QStringLiteral("Stopped"));
    QCOMPARE(hosting->model().data(hosting->model().index(1, 1)).toString(), QStringLiteral("Statistics backup"));
    QCOMPARE(hosting->provenanceText(), QStringLiteral("Source: Primary"));
    QVERIFY(!window.filesAction().isEnabled());
    QVERIFY(!window.continueAction().isEnabled());
    if (!capture.isEmpty()) {
      const auto path = root.filePath(QStringLiteral("my-tracker-preview.png"));
      QVERIFY(!QFileInfo::exists(path));
      QVERIFY(window.grab().save(path));
    }
    QTest::keyClick(&hosting->table(), Qt::Key_3, Qt::ControlModifier);
    QTRY_VERIFY_WITH_TIMEOUT(window.sharesPage() && window.sharesPage()->state() == ui::SavedPageState::Ready
        && hosting->state() == ui::SavedPageState::Ready && !bridge.busy() && saved.count() == 2, 30000);
    auto* shares = window.sharesPage();
    QCOMPARE(window.mainTabs().currentWidget(), shares);
    QCOMPARE(window.mainTabs().count(), 3);
    QCOMPARE(shares->model().rowCount(), 2);
    QCOMPARE(shares->model().data(shares->model().index(0, 0)).toString(), payload.filePath(QStringLiteral("Synthetic shared file.txt")));
    QCOMPARE(shares->model().data(shares->model().index(0, 1)).toString(), QStringLiteral("File"));
    QCOMPARE(shares->model().data(shares->model().index(0, 2)).toString(), QStringLiteral("Preview"));
    QCOMPARE(shares->model().data(shares->model().index(0, 3), Qt::UserRole).toBool(), true);
    QCOMPARE(shares->model().data(shares->model().index(1, 1)).toString(), QStringLiteral("Directory contents"));
    QCOMPARE(shares->model().data(shares->model().index(1, 4), Qt::UserRole).toBool(), true);
    QCOMPARE(shares->model().data(shares->model().index(1, 5)).toString(), QStringLiteral("101,102"));
    QCOMPARE(shares->provenanceText(), QStringLiteral("Source: Primary"));
    if (!capture.isEmpty()) {
      const auto path = root.filePath(QStringLiteral("my-shares-preview.png"));
      QVERIFY(!QFileInfo::exists(path));
      QVERIFY(window.grab().save(path));
    }
    shares->table().setFocus();
    QTRY_VERIFY(shares->table().hasFocus());
    QTest::keyClick(&shares->table(), Qt::Key_I, Qt::ControlModifier);
    QTest::keyClick(&shares->table(), Qt::Key_R, Qt::ControlModifier);
    QTest::keyClick(&shares->table(), Qt::Key_F5);
    QTRY_VERIFY_WITH_TIMEOUT(saved.count() == 3 && shares->state() == ui::SavedPageState::Ready
        && hosting->state() == ui::SavedPageState::Ready && !bridge.busy(), 30000);
    QCOMPARE(details.count(), detailCount);
    QCOMPARE(snapshots.count(), libraryCount);
    QCOMPARE(window.model().rowCount(), 4);
    QTest::keyClick(&shares->table(), Qt::Key_1, Qt::ControlModifier);
    QTRY_COMPARE(window.mainTabs().currentWidget(), window.findChild<QWidget*>(QStringLiteral("classic-my-torrents-page")));
    QVERIFY(window.filesAction().isEnabled());
    QVERIFY(!window.continueAction().isEnabled());
    QVERIFY(window.filesModel().target()->hash == selected->hash);
    auto* mainPage = window.mainTabs().currentWidget();
    const auto savedCount = saved.count();
    QTest::keyClick(&window.table(), Qt::Key_W, Qt::ControlModifier);
    QTRY_COMPARE(window.mainTabs().indexOf(mainPage), -1);
    QCOMPARE(window.mainTabs().count(), 2);
    QVERIFY(!window.filesAction().isEnabled());
    QTest::keyClick(window.mainTabs().currentWidget(), Qt::Key_1, Qt::ControlModifier);
    QTRY_COMPARE(window.mainTabs().currentWidget(), mainPage);
    QVERIFY(window.filesModel().target()->hash == selected->hash);
    auto* closeTab = window.findChild<QAction*>(QStringLiteral("classic-close-tab"));
    QVERIFY(closeTab);
    for (int remaining = 3; remaining > 0; --remaining) {
      QVERIFY(closeTab->isEnabled());
      closeTab->trigger();
      QCOMPARE(window.mainTabs().count(), remaining - 1);
    }
    QVERIFY(!closeTab->isEnabled());
    QVERIFY(!window.filesAction().isEnabled());
    QVERIFY(!window.continueAction().isEnabled());
    myTorrents->trigger();
    QCOMPARE(window.mainTabs().currentWidget(), mainPage);
    QCOMPARE(window.mainTabs().count(), 1);
    QCOMPARE(window.model().rowCount(), 4);
    QCOMPARE(snapshots.count(), libraryCount);
    QCOMPARE(details.count(), detailCount);
    QCOMPARE(saved.count(), savedCount);
    QVERIFY(!bridge.busy());
    QVERIFY(window.close());
    QVERIFY(!window.isVisible());
    for (auto it = original.cbegin(); it != original.cend(); ++it) { QCOMPARE(bytes(it.key()), it.value()); }
    QVERIFY(payload.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
  }
  void generatedColumnPreferencesSaveAndRestart() {
    const auto profile = qEnvironmentVariable("BIGLYBTC_PREVIEW_PROFILE");
    const QFileInfo profileInfo(profile);
    const QDir root(profileInfo.absolutePath());
    QVERIFY(profileInfo.isDir()); QCOMPARE(profileInfo.fileName(), QStringLiteral("profile"));
    QCOMPARE(profileInfo.canonicalFilePath(), profile);
    QCOMPARE(QFileInfo(root.absolutePath()).absolutePath(), QStringLiteral("/tmp"));
    QVERIFY(root.dirName().startsWith(QStringLiteral("biglybtc-preview-")));
    const auto path = profile + QStringLiteral("/tables.config");
    const auto original = bytes(path); QVERIFY(!original.isEmpty());
    {
      ui::LibraryWindow window; app::SessionBridge bridge; wire(window, bridge);
      window.setOfflinePreview(true); window.resize(1280, 800); window.show();
      QSignalSpy layouts(&bridge, &app::SessionBridge::columnLayoutsReady);
      QSignalSpy writes(&bridge, &app::SessionBridge::columnLayoutSaved);
      QVERIFY(bridge.open(profile.toStdString(), 0));
      QTRY_VERIFY_WITH_TIMEOUT(layouts.count() == 1 && !bridge.busy(), 30000);
      auto* save = window.findChild<QAction*>(QStringLiteral("classic-save-column-layout"));
      QVERIFY(save && save->isEnabled());
      window.incompleteTable().setColumnWidth(1, 279);
      window.incompleteTable().setColumnHidden(12, false);
      window.completeTable().setColumnWidth(7, 267);
      save->trigger();
      QTRY_VERIFY_WITH_TIMEOUT(writes.count() == 1 && !bridge.busy(), 30000);
      const auto result = qvariant_cast<ui::ColumnLayoutSavePresentation>(writes.at(0).at(0));
      QVERIFY(result && result->saved);
      QCOMPARE(bytes(path + QStringLiteral(".bak")), original);
      QVERIFY(window.close());
    }
    const auto saved = bytes(path); QVERIFY(saved != original);
    {
      ui::LibraryWindow window; app::SessionBridge bridge; wire(window, bridge);
      window.setOfflinePreview(true); window.resize(1280, 800); window.show();
      QSignalSpy layouts(&bridge, &app::SessionBridge::columnLayoutsReady);
      QVERIFY(bridge.open(profile.toStdString(), 0));
      QTRY_VERIFY_WITH_TIMEOUT(layouts.count() == 1 && !bridge.busy(), 30000);
      QCOMPARE(window.incompleteTable().columnWidth(1), 279);
      QVERIFY(!window.incompleteTable().isColumnHidden(12));
      QCOMPARE(window.completeTable().columnWidth(7), 267);
      QCOMPARE(window.model().rowCount(), 4); QCOMPARE(bytes(path), saved);
      QVERIFY(window.close());
    }
    QVERIFY(QDir(root.filePath(QStringLiteral("payload"))).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
  }
  void explicitFirstSaveCreatesOnlyAfterMissingReadAndRestarts() {
    const auto sample = qEnvironmentVariable("BIGLYBTC_PREVIEW_PROFILE");
    const QDir root(QFileInfo(sample).absolutePath());
    QCOMPARE(QFileInfo(root.absolutePath()).absolutePath(), QStringLiteral("/tmp"));
    QVERIFY(root.dirName().startsWith(QStringLiteral("biglybtc-preview-")));
    const auto path = root.filePath(QStringLiteral("first-save-profile"));
    QVERIFY(!QFileInfo::exists(path)); QVERIFY(root.mkdir(QStringLiteral("first-save-profile")));
    QFile downloads(path + QStringLiteral("/downloads.config")); QVERIFY(downloads.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    QCOMPARE(downloads.write("d9:downloadslee"), qint64{15}); downloads.close();
    const auto tables = path + QStringLiteral("/tables.config"); QByteArray first;
    {
      ui::LibraryWindow window; app::SessionBridge bridge; wire(window, bridge); window.show();
      QSignalSpy reads(&bridge, &app::SessionBridge::columnLayoutsReady), writes(&bridge, &app::SessionBridge::columnLayoutSaved);
      QVERIFY(bridge.open(path.toStdString(), 0)); QTRY_VERIFY_WITH_TIMEOUT(reads.count() == 1 && !bridge.busy(), 30000);
      QCOMPARE(qvariant_cast<ui::ColumnLayoutsPresentation>(reads.at(0).at(0))->state, ui::ColumnLayoutReadState::Missing);
      QVERIFY(!QFileInfo::exists(tables)); QVERIFY(!QFileInfo::exists(tables + QStringLiteral(".bak")));
      auto* save = window.findChild<QAction*>(QStringLiteral("classic-save-column-layout")); QVERIFY(save && save->isEnabled());
      window.incompleteTable().setColumnWidth(1, 312); window.completeTable().setColumnWidth(7, 245);
      save->trigger(); QTRY_VERIFY_WITH_TIMEOUT(writes.count() == 1 && !bridge.busy(), 30000);
      QVERIFY(qvariant_cast<ui::ColumnLayoutSavePresentation>(writes.at(0).at(0))->saved);
      first = bytes(tables); QVERIFY(!first.isEmpty());
      QVERIFY(!QFileInfo::exists(tables + QStringLiteral(".bak"))); QVERIFY(!QFileInfo::exists(tables + QStringLiteral(".saving")));
      QVERIFY(window.close());
    }
    {
      ui::LibraryWindow window; app::SessionBridge bridge; wire(window, bridge); window.show();
      QSignalSpy reads(&bridge, &app::SessionBridge::columnLayoutsReady);
      QVERIFY(bridge.open(path.toStdString(), 0)); QTRY_VERIFY_WITH_TIMEOUT(reads.count() == 1 && !bridge.busy(), 30000);
      QCOMPARE(window.incompleteTable().columnWidth(1), 312); QCOMPARE(window.completeTable().columnWidth(7), 245);
      QCOMPARE(window.model().rowCount(), 0); QCOMPARE(bytes(tables), first); QVERIFY(window.close());
    }
  }

  void statisticsShortcutSavedTotalsAndCloseReopen() {
    const auto profile = qEnvironmentVariable("BIGLYBTC_PREVIEW_PROFILE");
    const QDir root(QFileInfo(profile).absolutePath());
    QCOMPARE(QFileInfo(root.absolutePath()).absolutePath(), QStringLiteral("/tmp"));
    QVERIFY(root.dirName().startsWith(QStringLiteral("biglybtc-preview-")));
    const auto path = profile + QStringLiteral("/BiglyBT.statistics");
    const auto saved = bytes(path); QVERIFY(!saved.isEmpty());
    const auto tables = bytes(profile + QStringLiteral("/tables.config"));
    ui::LibraryWindow window; app::SessionBridge bridge; wire(window, bridge);
    window.setOfflinePreview(true); window.resize(1280, 800); window.show(); window.activateWindow();
    QSignalSpy snapshots(&bridge, &app::SessionBridge::snapshotReady), layouts(&bridge, &app::SessionBridge::columnLayoutsReady);
    QSignalSpy reads(&bridge, &app::SessionBridge::statisticsReady), details(&bridge, &app::SessionBridge::detailsReady);
    QSignalSpy cancels(&window, &ui::LibraryWindow::cancelRequested);
    QVERIFY(bridge.open(profile.toStdString(), 0));
    QTRY_VERIFY_WITH_TIMEOUT(layouts.count() == 1 && !bridge.busy(), 30000);
    window.filter().setText(QStringLiteral("Paused"));
    auto& table = window.incompleteTable();
    QCOMPARE(table.model()->rowCount(), 1);
    table.setFocus(); table.selectionModel()->setCurrentIndex(table.model()->index(0, 0),
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QTRY_VERIFY_WITH_TIMEOUT(window.filesModel().target() && !bridge.busy(), 30000);
    const auto target = window.filesModel().target();
    const auto detailCount = details.count();
    table.setColumnWidth(1, 247);
    window.filter().setFocus(); QTRY_VERIFY(window.filter().hasFocus());
    QTest::keyClick(&window.filter(), Qt::Key_5, Qt::ControlModifier);
    QTRY_VERIFY_WITH_TIMEOUT(window.statisticsPage() && window.statisticsPage()->readState() == ui::StatisticsReadState::Ready
        && reads.count() == 1 && !bridge.busy(), 30000);
    auto* page = window.statisticsPage();
    QCOMPARE(window.mainTabs().currentWidget(), page); QCOMPARE(page->tabs().currentIndex(), 0);
    QCOMPARE(page->tabs().tabText(0), QStringLiteral("Activity"));
    QCOMPARE(page->sampleCount(), std::size_t{0});
    auto* down = page->findChild<QLabel*>(QStringLiteral("statistics-payload-download-rate"));
    auto* up = page->findChild<QLabel*>(QStringLiteral("statistics-payload-upload-rate"));
    QVERIFY(down && up); QCOMPARE(down->text(), QStringLiteral("Unavailable")); QCOMPARE(up->text(), QStringLiteral("Unavailable"));
    auto* total = page->findChild<QLabel*>(QStringLiteral("statistics-saved-downloaded"));
    auto* since = page->findChild<QLabel*>(QStringLiteral("statistics-since-mark-downloaded"));
    auto* source = page->findChild<QLabel*>(QStringLiteral("statistics-source"));
    QVERIFY(total && since && source);
    QCOMPARE(total->text(), QStringLiteral("5368709120 bytes")); QCOMPARE(total->toolTip(), QStringLiteral("5368709120 bytes"));
    QCOMPARE(since->text(), QStringLiteral("4294967296 bytes"));
    QVERIFY(source->text().contains(QStringLiteral("BiglyBT.statistics")));
    QVERIFY(source->text().contains(QStringLiteral("primary")));
    const auto capture = qEnvironmentVariable("BIGLYBTC_PREVIEW_CAPTURE");
    if (!capture.isEmpty()) {
      const auto activity = root.filePath(QStringLiteral("statistics-activity-preview.png"));
      QVERIFY(!QFileInfo::exists(activity)); QCoreApplication::processEvents(); QVERIFY(window.grab().save(activity));
    }
    page->tabs().setCurrentIndex(1); QCoreApplication::processEvents();
    auto* transferTabs = page->findChild<QTabWidget*>(QStringLiteral("statistics-transfer-tabs"));
    QVERIFY(transferTabs); QCOMPARE(transferTabs->currentIndex(), 0);
    if (!capture.isEmpty()) {
      const auto transfers = root.filePath(QStringLiteral("statistics-transfers-preview.png"));
      QVERIFY(!QFileInfo::exists(transfers)); QVERIFY(window.grab().save(transfers));
    }
    QTest::keyClick(&page->tabs(), Qt::Key_1, Qt::ControlModifier);
    QCOMPARE(window.mainTabs().currentWidget()->objectName(), QStringLiteral("classic-my-torrents-page"));
    QCOMPARE(window.filter().text(), QStringLiteral("Paused")); QCOMPARE(table.columnWidth(1), 247);
    QVERIFY(window.filesModel().target() == target);
    QTest::keyClick(&window.filter(), Qt::Key_5, Qt::ControlModifier);
    QCOMPARE(window.statisticsPage(), page); QCOMPARE(page->tabs().currentIndex(), 1); QCOMPARE(reads.count(), 1);
    QPointer<ui::StatisticsPage> old(page);
    QTest::keyClick(&page->tabs(), Qt::Key_W, Qt::ControlModifier); QVERIFY(!window.statisticsPage());
    QCOMPARE(cancels.count(), 0);
    QTest::keyClick(&window.filter(), Qt::Key_5, Qt::ControlModifier);
    QTRY_VERIFY_WITH_TIMEOUT(window.statisticsPage() && window.statisticsPage()->readState() == ui::StatisticsReadState::Ready
        && reads.count() == 2 && !bridge.busy(), 30000);
    QCOMPARE(window.statisticsPage()->tabs().currentIndex(), 0); QTRY_VERIFY(old.isNull());
    QCOMPARE(window.statisticsPage()->sampleCount(), std::size_t{0});
    QCOMPARE(snapshots.count(), 1); QCOMPARE(details.count(), detailCount); QCOMPARE(bytes(path), saved);
    QCOMPARE(bytes(profile + QStringLiteral("/tables.config")), tables);
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".bak"))); QVERIFY(!QFileInfo::exists(path + QStringLiteral(".saving")));
    QVERIFY(QDir(root.filePath(QStringLiteral("payload"))).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
    QVERIFY(window.close());
  }

};

QTEST_MAIN(PreviewSmoke)
#include "preview_smoke.moc"
