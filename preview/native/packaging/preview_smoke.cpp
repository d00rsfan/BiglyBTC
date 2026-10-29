#include "library_window.hpp"
#include "wayland_console.hpp"
#include "session_bridge.hpp"
#include "statistics_page.hpp"
#include "saved_speed_manager_view.hpp"
#include "saved_speed_map_widget.hpp"
#include "saved_history_plot.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QComboBox>
#include <QDir>
#include <QDateEdit>
#include <QDateTime>
#include <QScrollArea>
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
#include <memory>

namespace {
using namespace biglybtc;

int consoleForwarded=0;
QtMsgType consoleType=QtDebugMsg;
QString consoleText;
const char* consoleCategory=nullptr;
void consoleSpy(QtMsgType type,const QMessageLogContext& context,const QString& message){
  ++consoleForwarded;consoleType=type;consoleText=message;consoleCategory=context.category;
}

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
  QObject::connect(&bridge, &app::SessionBridge::liveSpeedManagerReady, &window, &ui::LibraryWindow::setLiveSpeedManager);
  QObject::connect(&bridge, &app::SessionBridge::historyReady, &window, &ui::LibraryWindow::setHistory);
  QObject::connect(&bridge, &app::SessionBridge::historyWeekReady, &window, &ui::LibraryWindow::setHistoryWeek);
  QObject::connect(&bridge, &app::SessionBridge::historyMonthReady, &window, &ui::LibraryWindow::setHistoryMonth);
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
  QObject::connect(&window, &ui::LibraryWindow::historyRequested, &bridge, &app::SessionBridge::history);
  QObject::connect(&window, &ui::LibraryWindow::historyWeekRequested, &bridge, &app::SessionBridge::historyWeek);
  QObject::connect(&window, &ui::LibraryWindow::historyMonthRequested, &bridge, &app::SessionBridge::historyMonth);
  QObject::connect(&window, &ui::LibraryWindow::columnLayoutsRequested, &bridge, &app::SessionBridge::columnLayouts);
  QObject::connect(&window, &ui::LibraryWindow::columnLayoutSaveRequested, &bridge, &app::SessionBridge::saveColumnLayout);
  QObject::connect(&window, &ui::LibraryWindow::cancelRequested, &bridge, &app::SessionBridge::cancel);
}
} // namespace

class PreviewSmoke final : public QObject {
  Q_OBJECT
private slots:
  void regularTrackerTooltipKeepsActualRetainedCountsSeparateFromMainlineEstimate(){
    ui::LibraryModel model;auto snapshot=std::make_shared<ui::LibraryPresentation>();snapshot->revision=1;
    ui::LibraryItem row;row.key="owned-tracker";row.hash=QString(40,'1');row.name="Owned queued download";row.state="Queued";
    row.trackerScrape=ui::TrackerScrapeValues{5,6,1700000000000,false};snapshot->items.push_back(row);QVERIFY(model.apply(snapshot));
    auto tooltip=model.data(model.index(0,1),Qt::ToolTipRole).toString();QVERIFY(tooltip.contains("5 seeds, 6 peers"));QVERIFY(tooltip.contains("Retained tracker counts"));QVERIFY(!tooltip.contains("Mainline"));
    auto both=std::make_shared<ui::LibraryPresentation>(*snapshot);both->revision=2;both->items[0].mainlineEstimate=ui::MainlineEstimateValues{2,3,1700000001000};QVERIFY(model.apply(both));
    tooltip=model.data(model.index(0,1),Qt::ToolTipRole).toString();QVERIFY(tooltip.contains("5 seeds, 6 peers"));QVERIFY(tooltip.contains("Mainline estimate: 2 seeds, 3 peers"));
    auto failed=std::make_shared<ui::LibraryPresentation>(*snapshot);failed->revision=3;failed->items[0].trackerScrape=ui::TrackerScrapeValues{-1,-1,1700000002000,true};QVERIFY(model.apply(failed));
    tooltip=model.data(model.index(0,1),Qt::ToolTipRole).toString();QVERIFY(tooltip.contains("Tracker scrape failed"));QVERIFY(!tooltip.contains("-1 seeds"));
    auto retained=std::make_shared<ui::LibraryPresentation>(*failed);retained->revision=4;retained->items[0].trackerScrape=ui::TrackerScrapeValues{5,6,1700000003000,true};QVERIFY(model.apply(retained));
    tooltip=model.data(model.index(0,1),Qt::ToolTipRole).toString();QVERIFY(tooltip.contains("Tracker scrape failed"));QVERIFY(tooltip.contains("Retained tracker counts: 5 seeds, 6 peers"));
    auto invalid=std::make_shared<ui::LibraryPresentation>(*retained);invalid->revision=5;invalid->items[0].trackerScrape->seeds=-2;QVERIFY(!model.apply(invalid));
  }
  void exactWaylandNoFocusWarningKeepsOtherDiagnosticsAndForwardHandler(){
    const QMessageLogContext wayland(__FILE__,__LINE__,Q_FUNC_INFO,"qt.qpa.wayland.textinput");
    const QMessageLogContext other(__FILE__,__LINE__,Q_FUNC_INFO,"another.category");
    const QString warning=QStringLiteral("virtual void QtWaylandClient::QWaylandTextInputv3::disableSurface(wl_surface*) Trying to disable 0x62b29d842ea0 but 0x0 is focused");
    consoleForwarded=0;
    app::detail::forwardWaylandConsole(QtWarningMsg,wayland,warning,consoleSpy);
    QCOMPARE(consoleForwarded,0);
    for(const auto type:{QtDebugMsg,QtInfoMsg,QtCriticalMsg,QtFatalMsg}){
      app::detail::forwardWaylandConsole(type,wayland,warning,consoleSpy);
      QCOMPARE(consoleType,type);QCOMPARE(consoleText,warning);QCOMPARE(consoleCategory,wayland.category);
    }
    QCOMPARE(consoleForwarded,4);
    app::detail::forwardWaylandConsole(QtWarningMsg,other,warning,consoleSpy);
    QCOMPARE(consoleForwarded,5);QCOMPARE(consoleCategory,other.category);
    const std::array messages={QString(warning).replace("but 0x0","but 0x1234"),
      QString(warning).replace("is focused","is enabled"),QString(warning).replace("disableSurface","enableSurface"),
      QString(warning).replace("0x62b29d842ea0","0x0"),QStringLiteral("custom: ")+warning,warning+QLatin1Char('\n')};
    for(const auto& message:messages){
      const auto before=consoleForwarded;app::detail::forwardWaylandConsole(QtWarningMsg,wayland,message,consoleSpy);
      QCOMPARE(consoleForwarded,before+1);QCOMPARE(consoleText,message);
    }
    const auto before=consoleForwarded;
    app::detail::forwardWaylandConsole(QtWarningMsg,wayland,QString(warning).replace("62b29d842ea0","ABCD"),consoleSpy);
    QCOMPARE(consoleForwarded,before);
  }
  void explicitQtConsoleControlsDisableNativeFiltering(){
    const std::array names={"BIGLYBTC_WAYLAND_DIAGNOSTICS","QT_FATAL_WARNINGS","QT_MESSAGE_PATTERN","QT_LOGGING_RULES","QT_LOGGING_CONF"};
    struct Restore {
      const std::array<const char*,5>& names;
      std::array<bool,5> present{};std::array<QByteArray,5> values;
      explicit Restore(const std::array<const char*,5>& keys):names(keys){
        for(std::size_t i=0;i<names.size();++i){present[i]=qEnvironmentVariableIsSet(names[i]);values[i]=qgetenv(names[i]);qunsetenv(names[i]);}
      }
      ~Restore(){for(std::size_t i=0;i<names.size();++i){if(present[i]){qputenv(names[i],values[i]);}else{qunsetenv(names[i]);}}}
    } restore(names);
    QCOMPARE(app::detail::waylandConsoleFilterAllowed(),std::string_view(qVersion())=="6.12.0");
    for(std::size_t i=1;i<names.size();++i){qputenv(names[i],QByteArray{});QVERIFY(!app::detail::waylandConsoleFilterAllowed());qunsetenv(names[i]);}
    qputenv(names[0],"1");QVERIFY(!app::detail::waylandConsoleFilterAllowed());
  }
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
    // Exercise the actual held-profile saved map, including private runtime painting.
    const auto readsBeforeCapacity = reads.count();
    transferTabs->setCurrentIndex(3);
    auto* capacity = page->findChild<ui::SavedSpeedManagerView*>(QStringLiteral("saved-speed-manager-view"));
    auto* savedMap = page->findChild<ui::SavedSpeedMapWidget*>(QStringLiteral("saved-speed-map"));
    QVERIFY(capacity && savedMap);
    QTRY_VERIFY_WITH_TIMEOUT(capacity->presentation() && capacity->presentation()->state == ui::SavedSpeedManagerState::Ready
        && savedMap->map() && reads.count() == readsBeforeCapacity + 1 && !bridge.busy(), 30000);
    QCOMPARE(savedMap->map()->records.size(), std::size_t{8});
    QVERIFY(savedMap->map()->source->originalBytes() == capacity->presentation()->history->source->originalBytes());
    const auto capacityReads = reads.count();
    auto* scroll = capacity->findChild<QScrollArea*>(QStringLiteral("capacity-scroll"));
    QVERIFY(scroll); scroll->ensureWidgetVisible(savedMap);
    if (!capture.isEmpty()) {
      const auto image = root.filePath(QStringLiteral("statistics-capacity-preview.png"));
      QVERIFY(!QFileInfo::exists(image)); QCoreApplication::processEvents(); QVERIFY(window.grab().save(image));
    }
    transferTabs->setCurrentIndex(0);
    const QStringList families{QStringLiteral("Cache"), QStringLiteral("Tracker"),
        QStringLiteral("Counters"), QStringLiteral("Tags"), QStringLiteral("Country"),
        QStringLiteral("DHT"), QStringLiteral("DDB Operations"), QStringLiteral("Vivaldi")};
    QCOMPARE(page->tabs().count(), 10);
    for (int index = 2; index < 10; ++index) {
      page->tabs().setCurrentIndex(index); QCoreApplication::processEvents();
      QCOMPARE(page->tabs().tabText(index), families[index - 2]);
      if (!capture.isEmpty()) {
        const auto image = root.filePath(QStringLiteral("statistics-section-%1-preview.png").arg(index));
        QVERIFY(!QFileInfo::exists(image)); QVERIFY(window.grab().save(image));
      }
    }
    page->tabs().setCurrentIndex(1); QCoreApplication::processEvents();
    QTest::keyClick(&page->tabs(), Qt::Key_1, Qt::ControlModifier);
    QCOMPARE(window.mainTabs().currentWidget()->objectName(), QStringLiteral("classic-my-torrents-page"));
    QCOMPARE(window.filter().text(), QStringLiteral("Paused")); QCOMPARE(table.columnWidth(1), 247);
    QVERIFY(window.filesModel().target() == target);
    QTest::keyClick(&window.filter(), Qt::Key_5, Qt::ControlModifier);
    QCOMPARE(window.statisticsPage(), page); QCOMPARE(page->tabs().currentIndex(), 1); QCOMPARE(reads.count(), capacityReads);
    const auto expectedNavigation = page->navigation(); QVERIFY(expectedNavigation); QCOMPARE(expectedNavigation->section, 1);
    QPointer<ui::StatisticsPage> old(page);
    QTest::keyClick(&page->tabs(), Qt::Key_W, Qt::ControlModifier); QVERIFY(!window.statisticsPage());
    QCOMPARE(cancels.count(), 0);
    QTest::keyClick(&window.filter(), Qt::Key_5, Qt::ControlModifier);
    QTRY_VERIFY_WITH_TIMEOUT(window.statisticsPage() && window.statisticsPage()->readState() == ui::StatisticsReadState::Ready
        && reads.count() == capacityReads + 1 && !bridge.busy(), 30000);
    QCOMPARE(window.statisticsPage()->tabs().currentIndex(), expectedNavigation->section); QTRY_VERIFY(old.isNull());
    QCOMPARE(window.statisticsPage()->navigation(), expectedNavigation);
    QCOMPARE(window.statisticsPage()->sampleCount(), std::size_t{0});
    QCOMPARE(snapshots.count(), 1); QCOMPARE(details.count(), detailCount); QCOMPARE(bytes(path), saved);
    QCOMPARE(bytes(profile + QStringLiteral("/tables.config")), tables);
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".bak"))); QVERIFY(!QFileInfo::exists(path + QStringLiteral(".saving")));
    QVERIFY(QDir(root.filePath(QStringLiteral("payload"))).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
    QVERIFY(window.close());
  }

  void generatedSavedHistoryDayAndMissingDayRemainReadOnly() {
    const auto profile = qEnvironmentVariable("BIGLYBTC_PREVIEW_PROFILE");
    const QDir root(QFileInfo(profile).absolutePath());
    QCOMPARE(QFileInfo(root.absolutePath()).absolutePath(), QStringLiteral("/tmp"));
    QVERIFY(root.dirName().startsWith(QStringLiteral("biglybtc-preview-")));
    const auto file = profile + QStringLiteral("/stats/2024/01/02.dat");
    const auto before = bytes(file); QVERIFY(!before.isEmpty());
    const auto saved = bytes(profile + QStringLiteral("/BiglyBT.statistics"));
    ui::LibraryWindow window; app::SessionBridge bridge; wire(window, bridge);
    window.setOfflinePreview(true); window.resize(1280, 900); window.show(); window.activateWindow();
    QSignalSpy layouts(&bridge, &app::SessionBridge::columnLayoutsReady), reads(&bridge, &app::SessionBridge::historyReady);
    QSignalSpy totals(&bridge, &app::SessionBridge::statisticsReady), snapshots(&bridge, &app::SessionBridge::snapshotReady);
    QVERIFY(bridge.open(profile.toStdString(), 0)); QTRY_VERIFY_WITH_TIMEOUT(layouts.count() == 1 && !bridge.busy(), 30000);
    auto* statistics = window.findChild<QAction*>(QStringLiteral("classic-statistics")); QVERIFY(statistics); statistics->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(totals.count() == 1 && !bridge.busy(), 30000); QCOMPARE(reads.count(), 0);
    auto* page = window.statisticsPage(); QVERIFY(page);
    auto* editor = page->findChild<QDateEdit*>(QStringLiteral("saved-history-date")); QVERIFY(editor); editor->setDate(QDate(2024, 1, 2));
    page->tabs().setCurrentIndex(1); auto* sections = page->findChild<QTabWidget*>(QStringLiteral("statistics-transfer-tabs")); QVERIFY(sections); sections->setCurrentIndex(1);
    QTRY_VERIFY_WITH_TIMEOUT(page->historyPresentation() && !bridge.busy(), 30000);
    const auto frame = page->historyPresentation(); QCOMPARE(frame->state, ui::HistoryReadState::Ready); QVERIFY(frame->snapshot);
    QCOMPARE(frame->snapshot->observations, std::size_t{9});
    QCOMPARE((*frame->snapshot->totals)[3], std::int64_t{17301504});
    auto* table = page->findChild<QTableView*>(QStringLiteral("saved-history-hours")); QVERIFY(table);
    QCOMPARE(table->model()->rowCount(), 24); QCOMPARE(table->model()->columnCount(), 7);
    QCOMPARE(table->model()->data(table->model()->index(2, 4)).toString(), QStringLiteral("0 bytes"));
    QCOMPARE(table->model()->data(table->model()->index(3, 4)).toString(), QStringLiteral("—"));
    auto* plot = page->findChild<ui::SavedHistoryPlot*>(QStringLiteral("saved-history-chart")); QVERIFY(plot); QVERIFY(plot->isVisible());
    const auto plotted = plot->presentation(); QVERIFY(plotted); QCOMPARE(plotted->mode(), ui::SavedHistoryPlotMode::Day);
    QCOMPARE(plotted->buckets().size(), std::size_t{24});
    for (std::size_t i = 0; i < 24; ++i) {
      const auto& bucket = plotted->buckets()[i]; const auto& hour = frame->snapshot->hours[i];
      QVERIFY(bucket.date == frame->date); QVERIFY(bucket.hour == i);
      QCOMPARE(bucket.downloaded.has_value(), hour.has_value()); QCOMPARE(bucket.uploaded.has_value(), hour.has_value());
      if (hour) { QCOMPARE(*bucket.downloaded, (*hour)[3]); QCOMPARE(*bucket.uploaded, (*hour)[1]); }
    }
    QVERIFY(plotted->buckets()[2].downloaded == std::int64_t{0}); QVERIFY(!plotted->buckets()[3].downloaded);
    const auto capture = qEnvironmentVariable("BIGLYBTC_PREVIEW_CAPTURE");
    if (!capture.isEmpty()) {
      const auto path = root.filePath(QStringLiteral("statistics-history-preview.png")); QVERIFY(!QFileInfo::exists(path)); QVERIFY(window.grab().save(path));
    }
    editor->setDate(QDate(2024, 1, 4));
    QTRY_VERIFY_WITH_TIMEOUT(page->historyPresentation() && page->historyPresentation()->date == (core::HistoryDate{2024, 1, 4}) && !bridge.busy(), 30000);
    QCOMPARE(page->historyPresentation()->state, ui::HistoryReadState::Missing); QVERIFY(!page->historyPresentation()->snapshot);
    QVERIFY(plot->presentation());
    for (const auto& bucket : plot->presentation()->buckets()) { QVERIFY(!bucket.downloaded); QVERIFY(!bucket.uploaded); }
    QVERIFY(plot->presentation() != plotted); QVERIFY(plotted->buckets()[2].downloaded == std::int64_t{0});
    if (!capture.isEmpty()) {
      const auto path = root.filePath(QStringLiteral("statistics-history-missing-preview.png")); QVERIFY(!QFileInfo::exists(path)); QVERIFY(window.grab().save(path));
    }
    QCOMPARE(bytes(file), before); QCOMPARE(bytes(profile + QStringLiteral("/BiglyBT.statistics")), saved);
    QVERIFY(!QFileInfo::exists(file + QStringLiteral(".bak"))); QVERIFY(!QFileInfo::exists(file + QStringLiteral(".saving")));
    QCOMPARE(totals.count(), 1); QCOMPARE(snapshots.count(), 1);
  }
  void generatedSevenDayOverviewAndMissingWeekRemainReadOnly() {
    const auto profile = qEnvironmentVariable("BIGLYBTC_PREVIEW_PROFILE");
    const QDir root(QFileInfo(profile).absolutePath());
    QCOMPARE(QFileInfo(root.absolutePath()).absolutePath(), QStringLiteral("/tmp"));
    QVERIFY(root.dirName().startsWith(QStringLiteral("biglybtc-preview-")));
    const auto ending = QDate(2024, 1, 2); QMap<QString, QByteArray> files;
    for (int offset = 0; offset < 7; ++offset) {
      const auto path = profile + ending.addDays(-offset).toString(QStringLiteral("'/stats/'yyyy/MM/dd'.dat'"));
      if (QFileInfo::exists(path)) { files.insert(path, bytes(path)); }
    }
    QCOMPARE(files.size(), 6); const auto saved = bytes(profile + QStringLiteral("/BiglyBT.statistics"));
    ui::LibraryWindow window; app::SessionBridge bridge; wire(window, bridge);
    window.setOfflinePreview(true); window.resize(1280, 900); window.show(); window.activateWindow();
    QSignalSpy layouts(&bridge, &app::SessionBridge::columnLayoutsReady), reads(&bridge, &app::SessionBridge::historyWeekReady);
    QSignalSpy days(&bridge, &app::SessionBridge::historyReady), totals(&bridge, &app::SessionBridge::statisticsReady), snapshots(&bridge, &app::SessionBridge::snapshotReady);
    QVERIFY(bridge.open(profile.toStdString(), 0)); QTRY_VERIFY_WITH_TIMEOUT(layouts.count() == 1 && !bridge.busy(), 30000);
    auto* statistics = window.findChild<QAction*>(QStringLiteral("classic-statistics")); QVERIFY(statistics); statistics->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(totals.count() == 1 && !bridge.busy(), 30000); QCOMPARE(reads.count(), 0);
    auto* page = window.statisticsPage(); QVERIFY(page);
    auto* editor = page->findChild<QDateEdit*>(QStringLiteral("saved-history-week-date")); QVERIFY(editor); editor->setDate(ending);
    auto* modes = page->findChild<QTabWidget*>(QStringLiteral("statistics-history-tabs")); QVERIFY(modes); modes->setCurrentIndex(1);
    page->tabs().setCurrentIndex(1); auto* transfers = page->findChild<QTabWidget*>(QStringLiteral("statistics-transfer-tabs")); QVERIFY(transfers); transfers->setCurrentIndex(1);
    QTRY_VERIFY_WITH_TIMEOUT(page->historyWeekPresentation() && !bridge.busy(), 30000);
    const auto packet = page->historyWeekPresentation(); QCOMPARE(packet->state, ui::HistoryWeekReadState::Ready); QVERIFY(packet->snapshot);
    QCOMPARE(packet->snapshot->days.front().date, (core::HistoryDate{2023, 12, 27})); QCOMPARE(packet->snapshot->days.back().date, (core::HistoryDate{2024, 1, 2}));
    QVERIFY(!packet->snapshot->days[2].snapshot); QVERIFY(packet->snapshot->days[4].snapshot); QVERIFY(!packet->snapshot->days[4].snapshot->totals);
    QCOMPARE((*packet->snapshot->days[3].snapshot->totals)[3], std::int64_t{-2048});
    QCOMPARE((*packet->snapshot->days[5].snapshot->totals)[3], std::int64_t{0});
    QCOMPARE((*packet->snapshot->days[6].snapshot->totals)[3], std::int64_t{17301504});
    auto* table = page->findChild<QTableView*>(QStringLiteral("saved-history-week-days")); QVERIFY(table);
    QCOMPARE(table->model()->rowCount(), 7); QCOMPARE(table->model()->columnCount(), 8);
    QCOMPARE(table->model()->data(table->model()->index(3, 5)).toString(), QStringLiteral("-2048 bytes"));
    QCOMPARE(table->model()->data(table->model()->index(5, 5)).toString(), QStringLiteral("0 bytes"));
    QVERIFY(!table->model()->data(table->model()->index(2, 5), Qt::UserRole).isValid());
    auto* plot = page->findChild<ui::SavedHistoryPlot*>(QStringLiteral("saved-history-week-chart")); QVERIFY(plot); QVERIFY(plot->isVisible());
    const auto plotted = plot->presentation(); QVERIFY(plotted); QCOMPARE(plotted->mode(), ui::SavedHistoryPlotMode::Week);
    QCOMPARE(plotted->buckets().size(), std::size_t{7});
    for (std::size_t i = 0; i < 7; ++i) {
      const auto& bucket = plotted->buckets()[i]; const auto& day = packet->snapshot->days[i];
      QVERIFY(bucket.date == day.date); QVERIFY(!bucket.hour);
      const auto known = day.snapshot && day.snapshot->totals;
      QCOMPARE(bucket.downloaded.has_value(), known); QCOMPARE(bucket.uploaded.has_value(), known);
      if (known) { QCOMPARE(*bucket.downloaded, (*day.snapshot->totals)[3]); QCOMPARE(*bucket.uploaded, (*day.snapshot->totals)[1]); }
    }
    QVERIFY(plotted->buckets()[3].downloaded == std::int64_t{-2048}); QVERIFY(plotted->buckets()[5].downloaded == std::int64_t{0});
    const auto capture = qEnvironmentVariable("BIGLYBTC_PREVIEW_CAPTURE");
    auto* resolution = page->findChild<QComboBox*>(QStringLiteral("saved-history-week-resolution")); QVERIFY(resolution);
    QCOMPARE(resolution->currentIndex(), 0); QCOMPARE(reads.count(), 1);
    resolution->setCurrentIndex(1);
    QTRY_COMPARE_WITH_TIMEOUT(plot->presentation()->mode(), ui::SavedHistoryPlotMode::WeekHourly, 1000);
    const auto hourly = plot->presentation(); QVERIFY(hourly); QCOMPARE(hourly->mode(), ui::SavedHistoryPlotMode::WeekHourly);
    QCOMPARE(hourly->buckets().size(), std::size_t{168});
    for (std::size_t i = 0; i < 168; ++i) {
      const auto& bucket = hourly->buckets()[i]; const auto& day = packet->snapshot->days[i / 24];
      QVERIFY(bucket.date == day.date); QVERIFY(bucket.hour == i % 24);
      const auto hour = day.snapshot ? day.snapshot->hours[i % 24] : std::nullopt;
      QCOMPARE(bucket.downloaded.has_value(), hour.has_value()); QCOMPARE(bucket.uploaded.has_value(), hour.has_value());
      if (hour) { QCOMPARE(*bucket.downloaded, (*hour)[3]); QCOMPARE(*bucket.uploaded, (*hour)[1]); }
    }
    QVERIFY(page->historyWeekPresentation() == packet); QCOMPARE(reads.count(), 1);
    QCOMPARE(table->model()->rowCount(), 7); QCOMPARE(table->model()->columnCount(), 8);
    if (!capture.isEmpty()) {
      const auto path = root.filePath(QStringLiteral("statistics-history-week-hourly-preview.png")); QVERIFY(!QFileInfo::exists(path)); QVERIFY(window.grab().save(path));
    }
    resolution->setCurrentIndex(0); QTRY_COMPARE_WITH_TIMEOUT(plot->presentation()->mode(), ui::SavedHistoryPlotMode::Week, 1000);
    QCOMPARE(plotted->buckets().size(), std::size_t{7}); QVERIFY(plotted->buckets()[3].downloaded == std::int64_t{-2048});
    QVERIFY(page->historyWeekPresentation() == packet); QCOMPARE(reads.count(), 1);

    if (!capture.isEmpty()) {
      const auto path = root.filePath(QStringLiteral("statistics-history-week-preview.png")); QVERIFY(!QFileInfo::exists(path)); QVERIFY(window.grab().save(path));
    }
    editor->setDate(QDate(2024, 2, 1));
    QTRY_VERIFY_WITH_TIMEOUT(page->historyWeekPresentation() && page->historyWeekPresentation()->endingDate == (core::HistoryDate{2024, 2, 1}) && !bridge.busy(), 30000);
    QCOMPARE(page->historyWeekPresentation()->state, ui::HistoryWeekReadState::Ready);
    for (const auto& row : page->historyWeekPresentation()->snapshot->days) { QVERIFY(!row.snapshot); }
    QVERIFY(plot->presentation());
    for (const auto& bucket : plot->presentation()->buckets()) { QVERIFY(!bucket.downloaded); QVERIFY(!bucket.uploaded); }
    QVERIFY(plot->presentation() != plotted); QVERIFY(plotted->buckets()[3].downloaded == std::int64_t{-2048});
    resolution->setCurrentIndex(1);
    QTRY_COMPARE_WITH_TIMEOUT(plot->presentation()->mode(), ui::SavedHistoryPlotMode::WeekHourly, 1000);
    QVERIFY(plot->presentation()); QCOMPARE(plot->presentation()->mode(), ui::SavedHistoryPlotMode::WeekHourly);
    QCOMPARE(plot->presentation()->buckets().size(), std::size_t{168});
    for (const auto& bucket : plot->presentation()->buckets()) { QVERIFY(!bucket.downloaded); QVERIFY(!bucket.uploaded); }
    QCOMPARE(reads.count(), 2); QCOMPARE(table->model()->rowCount(), 7); QCOMPARE(table->model()->columnCount(), 8);
    if (!capture.isEmpty()) {
      const auto path = root.filePath(QStringLiteral("statistics-history-week-hourly-missing-preview.png")); QVERIFY(!QFileInfo::exists(path)); QVERIFY(window.grab().save(path));
    }
    resolution->setCurrentIndex(0); QTRY_COMPARE_WITH_TIMEOUT(plot->presentation()->mode(), ui::SavedHistoryPlotMode::Week, 1000);
    if (!capture.isEmpty()) {
      const auto path = root.filePath(QStringLiteral("statistics-history-week-missing-preview.png")); QVERIFY(!QFileInfo::exists(path)); QVERIFY(window.grab().save(path));
    }
    for (auto it = files.cbegin(); it != files.cend(); ++it) { QCOMPARE(bytes(it.key()), it.value()); }
    QVERIFY(!QFileInfo::exists(profile + QStringLiteral("/stats/2023/12/29.dat")));
    QCOMPARE(bytes(profile + QStringLiteral("/BiglyBT.statistics")), saved); QCOMPARE(reads.count(), 2); QCOMPARE(days.count(), 0); QCOMPARE(totals.count(), 1); QCOMPARE(snapshots.count(), 1);
    QVERIFY(QDir(root.filePath(QStringLiteral("payload"))).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
  }


  void statisticsNavigationSurvivesCloseAndNewSessionClearsMemory() {
    const auto profile = qEnvironmentVariable("BIGLYBTC_PREVIEW_PROFILE");
    const QDir root(QFileInfo(profile).absolutePath());
    QCOMPARE(QFileInfo(root.absolutePath()).absolutePath(), QStringLiteral("/tmp"));
    QVERIFY(root.dirName().startsWith(QStringLiteral("biglybtc-preview-")));
    QMap<QString, QByteArray> original;
    for (const auto& name : {QStringLiteral("/downloads.config"), QStringLiteral("/tables.config"), QStringLiteral("/BiglyBT.statistics"), QStringLiteral("/stats/2024/01/02.dat")}) {
      original.insert(profile + name, bytes(profile + name)); QVERIFY(!original.value(profile + name).isEmpty());
    }
    ui::LibraryWindow window; auto bridge = std::make_unique<app::SessionBridge>(); wire(window, *bridge);
    window.setOfflinePreview(true); window.resize(1280, 900); window.show(); window.activateWindow();
    QSignalSpy columns(bridge.get(), &app::SessionBridge::columnLayoutsReady), snapshots(bridge.get(), &app::SessionBridge::snapshotReady);
    QSignalSpy totals(bridge.get(), &app::SessionBridge::statisticsReady), weeks(bridge.get(), &app::SessionBridge::historyWeekReady), days(bridge.get(), &app::SessionBridge::historyReady);
    QVERIFY(bridge->open(profile.toStdString(), 0)); QTRY_VERIFY_WITH_TIMEOUT(columns.count() == 1 && !bridge->busy(), 30000);
    auto* statistics = window.findChild<QAction*>(QStringLiteral("classic-statistics"));
    auto* close = window.findChild<QAction*>(QStringLiteral("classic-close-tab")); QVERIFY(statistics); QVERIFY(close);
    statistics->trigger(); QTRY_VERIFY_WITH_TIMEOUT(totals.count() == 1 && !bridge->busy(), 30000);
    auto* page = window.statisticsPage(); QVERIFY(page);
    auto* daily = page->findChild<QDateEdit*>(QStringLiteral("saved-history-date"));
    auto* ending = page->findChild<QDateEdit*>(QStringLiteral("saved-history-week-date"));
    auto* periods = page->findChild<QTabWidget*>(QStringLiteral("statistics-history-tabs"));
    auto* transfers = page->findChild<QTabWidget*>(QStringLiteral("statistics-transfer-tabs"));
    auto* resolution = page->findChild<QComboBox*>(QStringLiteral("saved-history-week-resolution"));
    QVERIFY(daily); QVERIFY(ending); QVERIFY(periods); QVERIFY(transfers); QVERIFY(resolution);
    daily->setDate(QDate(2024, 1, 1)); ending->setDate(QDate(2024, 1, 2));
    resolution->setCurrentIndex(1); periods->setCurrentIndex(1); transfers->setCurrentIndex(1); page->tabs().setCurrentIndex(1);
    QTRY_VERIFY_WITH_TIMEOUT(weeks.count() == 1 && page->historyWeekPresentation() && !bridge->busy(), 30000);
    const auto remembered = page->navigation(); QVERIFY(remembered); QVERIFY(remembered->hourly);
    QCOMPARE(remembered->dailyDate, (core::HistoryDate{2024, 1, 1})); QCOMPARE(remembered->weekEnding, (core::HistoryDate{2024, 1, 2}));
    QPointer old(page); close->trigger(); QVERIFY(!window.statisticsPage()); QTRY_VERIFY(old.isNull());
    statistics->trigger(); page = window.statisticsPage(); QVERIFY(page); QCOMPARE(page->navigation(), remembered);
    QTRY_VERIFY_WITH_TIMEOUT(totals.count() == 2 && weeks.count() == 2 && page->historyWeekPresentation() && !bridge->busy(), 30000);
    QCOMPARE(page->navigation(), remembered); QCOMPARE(days.count(), 0); QCOMPARE(snapshots.count(), 1);
    auto* chart = page->findChild<ui::SavedHistoryPlot*>(QStringLiteral("saved-history-week-chart")); QVERIFY(chart);
    QTRY_COMPARE(chart->presentation()->mode(), ui::SavedHistoryPlotMode::WeekHourly); QCOMPARE(chart->presentation()->buckets().size(), std::size_t{168});
    QCOMPARE(page->historyWeekPresentation()->endingDate, remembered->weekEnding);
    const auto capture = qEnvironmentVariable("BIGLYBTC_PREVIEW_CAPTURE");
    if (!capture.isEmpty()) {
      const auto path = root.filePath(QStringLiteral("statistics-navigation-reopened-preview.png")); QVERIFY(!QFileInfo::exists(path)); QVERIFY(window.grab().save(path));
    }
    old = page; close->trigger(); QTRY_VERIFY(old.isNull());
    const auto second = root.filePath(QStringLiteral("navigation-new-session")); QVERIFY(!QFileInfo::exists(second)); QVERIFY(QDir().mkpath(second));
    const auto write = [&](const QString& name, const QByteArray& data) {
      QFile file(second + name); return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(data) == data.size() && file.flush();
    };
    QVERIFY(write(QStringLiteral("/downloads.config"), QByteArrayLiteral("d9:downloadslee")));
    QVERIFY(write(QStringLiteral("/tables.config"), QByteArrayLiteral("de")));
    QVERIFY(write(QStringLiteral("/BiglyBT.statistics"), original.value(profile + QStringLiteral("/BiglyBT.statistics"))));
    bridge.reset(); // Each existing worker owns exactly one profile; join it before the next owner.
    app::SessionBridge replacement; wire(window, replacement);
    QSignalSpy nextColumns(&replacement, &app::SessionBridge::columnLayoutsReady), nextSnapshots(&replacement, &app::SessionBridge::snapshotReady);
    QSignalSpy nextTotals(&replacement, &app::SessionBridge::statisticsReady), nextWeeks(&replacement, &app::SessionBridge::historyWeekReady), nextDays(&replacement, &app::SessionBridge::historyReady);
    QVERIFY(replacement.open(second.toStdString(), 0)); QTRY_VERIFY_WITH_TIMEOUT(nextColumns.count() == 1 && !replacement.busy(), 30000);
    statistics->trigger(); page = window.statisticsPage(); QVERIFY(page);
    QTRY_VERIFY_WITH_TIMEOUT(nextTotals.count() == 1 && !replacement.busy(), 30000);
    const auto fresh = page->navigation(); QVERIFY(fresh);
    QCOMPARE(fresh->section, 0); QCOMPARE(fresh->activity, 2); QCOMPARE(fresh->transfers, 0); QCOMPARE(fresh->history, 0); QVERIFY(!fresh->hourly);
    QVERIFY(fresh->dailyDate != remembered->dailyDate); QVERIFY(fresh->weekEnding != remembered->weekEnding);
    QCOMPARE(weeks.count(), 2); QCOMPARE(days.count(), 0); QCOMPARE(snapshots.count(), 1);
    QCOMPARE(nextWeeks.count(), 0); QCOMPARE(nextDays.count(), 0); QCOMPARE(nextSnapshots.count(), 1);
    if (!capture.isEmpty()) {
      const auto path = root.filePath(QStringLiteral("statistics-navigation-new-session-preview.png")); QVERIFY(!QFileInfo::exists(path)); QVERIFY(window.grab().save(path));
    }
    for (auto it = original.cbegin(); it != original.cend(); ++it) { QCOMPARE(bytes(it.key()), it.value()); }
    QVERIFY(QDir(root.filePath(QStringLiteral("payload"))).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
  }

  void historyTrafficUsesFreshSavedFactsWithoutExtraReads() {
    const auto profile = qEnvironmentVariable("BIGLYBTC_PREVIEW_PROFILE");
    const QDir root(QFileInfo(profile).absolutePath());
    const auto original = bytes(profile + QStringLiteral("/stats/2024/01/02.dat")); QVERIFY(!original.isEmpty());
    const auto config = bytes(profile + QStringLiteral("/downloads.config"));
    ui::LibraryWindow window; app::SessionBridge bridge; wire(window, bridge);
    window.setOfflinePreview(true); window.resize(1280, 900); window.show(); window.activateWindow();
    QSignalSpy columns(&bridge, &app::SessionBridge::columnLayoutsReady), totals(&bridge, &app::SessionBridge::statisticsReady);
    QSignalSpy days(&bridge, &app::SessionBridge::historyReady), weeks(&bridge, &app::SessionBridge::historyWeekReady);
    QVERIFY(bridge.open(profile.toStdString(), 0)); QTRY_VERIFY_WITH_TIMEOUT(columns.count() == 1 && !bridge.busy(), 30000);
    auto* statistics = window.findChild<QAction*>(QStringLiteral("classic-statistics"));
    auto* close = window.findChild<QAction*>(QStringLiteral("classic-close-tab")); QVERIFY(statistics); QVERIFY(close);
    statistics->trigger(); QTRY_VERIFY_WITH_TIMEOUT(totals.count() == 1 && !bridge.busy(), 30000);
    auto* page = window.statisticsPage(); QVERIFY(page);
    auto* date = page->findChild<QDateEdit*>(QStringLiteral("saved-history-date"));
    auto* ending = page->findChild<QDateEdit*>(QStringLiteral("saved-history-week-date"));
    auto* periods = page->findChild<QTabWidget*>(QStringLiteral("statistics-history-tabs"));
    auto* transfers = page->findChild<QTabWidget*>(QStringLiteral("statistics-transfer-tabs"));
    auto* dailyTraffic = page->findChild<QComboBox*>(QStringLiteral("saved-history-traffic"));
    auto* weekTraffic = page->findChild<QComboBox*>(QStringLiteral("saved-history-week-traffic"));
    auto* resolution = page->findChild<QComboBox*>(QStringLiteral("saved-history-week-resolution"));
    QVERIFY(date); QVERIFY(ending); QVERIFY(periods); QVERIFY(transfers); QVERIFY(dailyTraffic); QVERIFY(weekTraffic); QVERIFY(resolution);
    QCOMPARE(dailyTraffic->currentIndex(), 0); QCOMPARE(weekTraffic->currentIndex(), 0);
    date->setDate(QDate(2024, 1, 2)); dailyTraffic->setCurrentIndex(1);
    transfers->setCurrentIndex(1); page->tabs().setCurrentIndex(1);
    QTRY_VERIFY_WITH_TIMEOUT(days.count() == 1 && page->historyPresentation() && !bridge.busy(), 30000);
    const auto day = page->historyPresentation(); QVERIFY(day->snapshot);
    auto* plot = page->findChild<ui::SavedHistoryPlot*>(QStringLiteral("saved-history-chart")); QVERIFY(plot);
    QTRY_COMPARE(plot->presentation()->traffic(), ui::SavedHistoryTraffic::Protocol);
    for (std::size_t i = 0; i < 24; ++i) {
      const auto& bucket = plot->presentation()->buckets()[i]; const auto& hour = day->snapshot->hours[i];
      QCOMPARE(bucket.downloaded.has_value(), hour.has_value());
      if (hour) { QCOMPARE(*bucket.downloaded, (*hour)[2]); QCOMPARE(*bucket.uploaded, (*hour)[0]); }
    }
    dailyTraffic->setCurrentIndex(2); QTRY_COMPARE(plot->presentation()->traffic(), ui::SavedHistoryTraffic::Dht);
    dailyTraffic->setCurrentIndex(1); QTRY_COMPARE(plot->presentation()->traffic(), ui::SavedHistoryTraffic::Protocol);
    QCOMPARE(days.count(), 1); QVERIFY(page->historyPresentation() == day);
    const auto capture = qEnvironmentVariable("BIGLYBTC_PREVIEW_CAPTURE");
    if (!capture.isEmpty()) {
      const auto path = root.filePath(QStringLiteral("statistics-history-protocol-preview.png")); QVERIFY(!QFileInfo::exists(path)); QVERIFY(window.grab().save(path));
    }
    ending->setDate(QDate(2024, 1, 2)); weekTraffic->setCurrentIndex(2); resolution->setCurrentIndex(1); periods->setCurrentIndex(1);
    QTRY_VERIFY_WITH_TIMEOUT(weeks.count() == 1 && page->historyWeekPresentation() && !bridge.busy(), 30000);
    const auto week = page->historyWeekPresentation(); QVERIFY(week->snapshot);
    auto* weekPlot = page->findChild<ui::SavedHistoryPlot*>(QStringLiteral("saved-history-week-chart")); QVERIFY(weekPlot);
    QTRY_COMPARE(weekPlot->presentation()->traffic(), ui::SavedHistoryTraffic::Dht);
    QCOMPARE(weekPlot->presentation()->buckets().size(), std::size_t{168});
    for (std::size_t i = 0; i < 168; ++i) {
      const auto& saved = week->snapshot->days[i / 24].snapshot;
      const auto hour = saved ? saved->hours[i % 24] : std::nullopt;
      const auto& bucket = weekPlot->presentation()->buckets()[i];
      QCOMPARE(bucket.downloaded.has_value(), hour.has_value());
      if (hour) { QCOMPARE(*bucket.downloaded, (*hour)[5]); QCOMPARE(*bucket.uploaded, (*hour)[4]); }
    }
    resolution->setCurrentIndex(0); QTRY_COMPARE(weekPlot->presentation()->mode(), ui::SavedHistoryPlotMode::Week);
    resolution->setCurrentIndex(1); QTRY_COMPARE(weekPlot->presentation()->mode(), ui::SavedHistoryPlotMode::WeekHourly);
    QCOMPARE(weeks.count(), 1); QVERIFY(page->historyWeekPresentation() == week);
    const auto remembered = page->navigation(); QVERIFY(remembered);
    QCOMPARE(remembered->dailyTraffic, ui::SavedHistoryTraffic::Protocol); QCOMPARE(remembered->weekTraffic, ui::SavedHistoryTraffic::Dht);
    QPointer old(page); close->trigger(); QTRY_VERIFY(old.isNull());
    statistics->trigger(); page = window.statisticsPage(); QVERIFY(page); QCOMPARE(page->navigation(), remembered);
    QTRY_VERIFY_WITH_TIMEOUT(totals.count() == 2 && weeks.count() == 2 && page->historyWeekPresentation() && !bridge.busy(), 30000);
    weekPlot = page->findChild<ui::SavedHistoryPlot*>(QStringLiteral("saved-history-week-chart")); QVERIFY(weekPlot);
    QTRY_COMPARE(weekPlot->presentation()->traffic(), ui::SavedHistoryTraffic::Dht); QCOMPARE(days.count(), 1);
    if (!capture.isEmpty()) {
      const auto path = root.filePath(QStringLiteral("statistics-history-dht-preview.png")); QVERIFY(!QFileInfo::exists(path)); QVERIFY(window.grab().save(path));
    }
    QCOMPARE(bytes(profile + QStringLiteral("/stats/2024/01/02.dat")), original); QCOMPARE(bytes(profile + QStringLiteral("/downloads.config")), config);
    QVERIFY(QDir(root.filePath(QStringLiteral("payload"))).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
  }

  void savedHistoryKeyboardInspectsRealBucketsWithoutExtraReads() {
    const auto profile = qEnvironmentVariable("BIGLYBTC_PREVIEW_PROFILE");
    const QDir root(QFileInfo(profile).absolutePath());
    QCOMPARE(QFileInfo(root.absolutePath()).absolutePath(), QStringLiteral("/tmp"));
    QVERIFY(root.dirName().startsWith(QStringLiteral("biglybtc-preview-")));
    const QDate ending(2024, 1, 2);
    std::array<QString, 12> paths{
        profile + QStringLiteral("/downloads.config"),
        profile + QStringLiteral("/BiglyBT.statistics"),
        profile + QStringLiteral("/tables.config"),
        profile + QStringLiteral("/tracker.config"),
        profile + QStringLiteral("/sharing.config")};
    for (int offset = 0; offset < 7; ++offset) {
      paths[5 + static_cast<std::size_t>(offset)] = profile
          + ending.addDays(-offset).toString(QStringLiteral("'/stats/'yyyy/MM/dd'.dat'"));
    }
    QMap<QString, bool> present; QMap<QString, QByteArray> original;
    for (const auto& path : paths) {
      const bool exists = QFileInfo::exists(path); present.insert(path, exists);
      if (exists) {
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        original.insert(path, file.readAll()); QCOMPARE(file.error(), QFileDevice::NoError);
      }
    }
    QVERIFY(!original.value(profile + QStringLiteral("/downloads.config")).isEmpty());
    QVERIFY(!original.value(profile + QStringLiteral("/stats/2024/01/02.dat")).isEmpty());
    QVERIFY(QDir(root.filePath(QStringLiteral("payload"))).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());

    ui::LibraryWindow window; app::SessionBridge bridge; wire(window, bridge);
    window.setOfflinePreview(true); window.resize(1280, 900); window.show(); window.activateWindow();
    QSignalSpy layouts(&bridge, &app::SessionBridge::columnLayoutsReady);
    QSignalSpy totals(&bridge, &app::SessionBridge::statisticsReady);
    QSignalSpy days(&bridge, &app::SessionBridge::historyReady);
    QSignalSpy weeks(&bridge, &app::SessionBridge::historyWeekReady);
    QSignalSpy snapshots(&bridge, &app::SessionBridge::snapshotReady);
    QVERIFY(bridge.open(profile.toStdString(), 0));
    QTRY_VERIFY_WITH_TIMEOUT(layouts.count() == 1 && !bridge.busy(), 30000);
    auto* statistics = window.findChild<QAction*>(QStringLiteral("classic-statistics"));
    auto* close = window.findChild<QAction*>(QStringLiteral("classic-close-tab"));
    QVERIFY(statistics); QVERIFY(close); statistics->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(totals.count() == 1 && !bridge.busy(), 30000);
    auto* page = window.statisticsPage(); QVERIFY(page);
    auto* date = page->findChild<QDateEdit*>(QStringLiteral("saved-history-week-date"));
    auto* periods = page->findChild<QTabWidget*>(QStringLiteral("statistics-history-tabs"));
    auto* transfers = page->findChild<QTabWidget*>(QStringLiteral("statistics-transfer-tabs"));
    auto* traffic = page->findChild<QComboBox*>(QStringLiteral("saved-history-week-traffic"));
    auto* resolution = page->findChild<QComboBox*>(QStringLiteral("saved-history-week-resolution"));
    QVERIFY(date); QVERIFY(periods); QVERIFY(transfers); QVERIFY(traffic); QVERIFY(resolution);
    date->setDate(ending); traffic->setCurrentIndex(2); resolution->setCurrentIndex(1);
    periods->setCurrentIndex(1); page->tabs().setCurrentIndex(1); transfers->setCurrentIndex(1);
    QTRY_VERIFY_WITH_TIMEOUT(weeks.count() == 1 && page->historyWeekPresentation() && !bridge.busy(), 30000);
    const auto packet = page->historyWeekPresentation();
    QCOMPARE(packet->state, ui::HistoryWeekReadState::Ready); QVERIFY(packet->snapshot);
    QCOMPARE(packet->endingDate, (core::HistoryDate{2024, 1, 2}));
    auto* plot = page->findChild<ui::SavedHistoryPlot*>(QStringLiteral("saved-history-week-chart"));
    QVERIFY(plot); QVERIFY(plot->isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(plot->presentation()
        && plot->presentation()->mode() == ui::SavedHistoryPlotMode::WeekHourly
        && plot->presentation()->traffic() == ui::SavedHistoryTraffic::Dht, 1000);
    const auto frame = plot->presentation(); QCOMPARE(frame->buckets().size(), std::size_t{168});
    QVERIFY(!plot->selectedBucket()); std::optional<std::size_t> lastKnown;
    for (std::size_t i = 0; i < 168; ++i) {
      const auto& savedDay = packet->snapshot->days[i / 24].snapshot;
      const auto savedHour = savedDay ? savedDay->hours[i % 24] : std::nullopt;
      const auto& bucket = frame->buckets()[i];
      QCOMPARE(bucket.date, packet->snapshot->days[i / 24].date);
      QVERIFY(bucket.hour == i % 24);
      QCOMPARE(bucket.downloaded.has_value(), savedHour.has_value());
      QCOMPARE(bucket.uploaded.has_value(), savedHour.has_value());
      if (savedHour) {
        QCOMPARE(*bucket.downloaded, (*savedHour)[5]); QCOMPARE(*bucket.uploaded, (*savedHour)[4]);
        lastKnown = i;
      }
    }
    QVERIFY(lastKnown); QVERIFY(*lastKnown < std::size_t{167});
    QVERIFY(!frame->buckets().back().downloaded); QVERIFY(!frame->buckets().back().uploaded);
    plot->setFocus(Qt::OtherFocusReason); QTRY_VERIFY_WITH_TIMEOUT(plot->hasFocus(), 1000);
    QTest::keyClick(plot, Qt::Key_End);
    QCOMPARE(plot->selectedBucket(), std::optional<std::size_t>{167});
    QCOMPARE(plot->toolTip(), frame->bucketToolTip(167)); QCOMPARE(plot->accessibleDescription(), frame->bucketToolTip(167));
    QVERIFY(plot->accessibleDescription().contains(QStringLiteral("2024-01-02 23:00 UTC")));
    QVERIFY(plot->accessibleDescription().contains(QStringLiteral("DHT downloaded: Unknown")));
    QVERIFY(plot->accessibleDescription().contains(QStringLiteral("DHT uploaded: Unknown")));
    QTest::keyClick(plot, Qt::Key_End); QCOMPARE(plot->selectedBucket(), std::optional<std::size_t>{167});
    QTest::keyClick(plot, Qt::Key_Home); QCOMPARE(plot->selectedBucket(), std::optional<std::size_t>{0});
    QTest::keyClick(plot, Qt::Key_Left); QCOMPARE(plot->selectedBucket(), std::optional<std::size_t>{0});
    QTest::keyClick(plot, Qt::Key_Right); QCOMPARE(plot->selectedBucket(), std::optional<std::size_t>{1});
    QTest::keyClick(plot, Qt::Key_Home); QCOMPARE(plot->selectedBucket(), std::optional<std::size_t>{0});
    QTest::keyClick(plot, Qt::Key_End); QCOMPARE(plot->selectedBucket(), std::optional<std::size_t>{167});
    // Select an actually observed bucket near End; the supplied fixture's last
    // hour is intentionally unknown, so it must not be treated as saved zero.
    for (std::size_t i = 167; i > *lastKnown; --i) { QTest::keyClick(plot, Qt::Key_Left); }
    QCOMPARE(plot->selectedBucket(), lastKnown);
    const auto exact = frame->bucketToolTip(*lastKnown);
    QCOMPARE(plot->toolTip(), exact); QCOMPARE(plot->accessibleDescription(), exact);
    QVERIFY(exact.contains(frame->bucketLabel(*lastKnown)));
    QVERIFY(exact.contains(QStringLiteral("DHT downloaded: %1 bytes").arg(*frame->buckets()[*lastKnown].downloaded)));
    QVERIFY(exact.contains(QStringLiteral("DHT uploaded: %1 bytes").arg(*frame->buckets()[*lastKnown].uploaded)));
    QVERIFY(plot->presentation() == frame); QVERIFY(page->historyWeekPresentation() == packet);
    QCOMPARE(weeks.count(), 1); QCOMPARE(days.count(), 0); QCOMPARE(totals.count(), 1); QCOMPARE(snapshots.count(), 1);
    const auto capture = qEnvironmentVariable("BIGLYBTC_PREVIEW_CAPTURE");
    if (!capture.isEmpty()) {
      const auto path = root.filePath(QStringLiteral("statistics-history-keyboard-preview.png"));
      QVERIFY(!QFileInfo::exists(path)); QVERIFY(plot->hasFocus()); QVERIFY(window.grab().save(path));
    }

    const auto remembered = page->navigation(); QVERIFY(remembered);
    QCOMPARE(remembered->weekEnding, (core::HistoryDate{2024, 1, 2}));
    QCOMPARE(remembered->weekTraffic, ui::SavedHistoryTraffic::Dht); QVERIFY(remembered->hourly);
    QPointer oldPage(page); QPointer oldPlot(plot); close->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(oldPage.isNull() && oldPlot.isNull(), 1000);
    statistics->trigger(); page = window.statisticsPage(); QVERIFY(page); QCOMPARE(page->navigation(), remembered);
    QTRY_VERIFY_WITH_TIMEOUT(totals.count() == 2 && weeks.count() == 2
        && page->historyWeekPresentation() && !bridge.busy(), 30000);
    plot = page->findChild<ui::SavedHistoryPlot*>(QStringLiteral("saved-history-week-chart")); QVERIFY(plot);
    QTRY_VERIFY_WITH_TIMEOUT(plot->presentation()
        && plot->presentation()->mode() == ui::SavedHistoryPlotMode::WeekHourly
        && plot->presentation()->traffic() == ui::SavedHistoryTraffic::Dht, 1000);
    QVERIFY(plot->presentation() != frame); QVERIFY(!plot->selectedBucket());
    QVERIFY(page->historyWeekPresentation() != packet);
    QCOMPARE(page->historyWeekPresentation()->endingDate, (core::HistoryDate{2024, 1, 2}));
    QCOMPARE(days.count(), 0); QCOMPARE(weeks.count(), 2); QCOMPARE(totals.count(), 2); QCOMPARE(snapshots.count(), 1);
    for (auto it = present.cbegin(); it != present.cend(); ++it) {
      QCOMPARE(QFileInfo::exists(it.key()), it.value());
      if (it.value()) {
        QFile file(it.key()); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original.value(it.key())); QCOMPARE(file.error(), QFileDevice::NoError);
      }
    }
    QVERIFY(QDir(root.filePath(QStringLiteral("payload"))).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
  }

  void savedHistoryCopyPublishesOnlyExplicitRealBucketValues() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    const auto profile = qEnvironmentVariable("BIGLYBTC_PREVIEW_PROFILE");
    const QFileInfo profileInfo(profile);
    const QDir root(profileInfo.absolutePath());
    QVERIFY(profileInfo.isDir()); QCOMPARE(profileInfo.fileName(), QStringLiteral("profile"));
    QCOMPARE(profileInfo.canonicalFilePath(), profile);
    QCOMPARE(QFileInfo(root.absolutePath()).absolutePath(), QStringLiteral("/tmp"));
    QVERIFY(root.dirName().startsWith(QStringLiteral("biglybtc-preview-")));
    const QDate ending(2024, 1, 2);
    std::array<QString, 12> paths{
        profile + QStringLiteral("/downloads.config"),
        profile + QStringLiteral("/BiglyBT.statistics"),
        profile + QStringLiteral("/tables.config"),
        profile + QStringLiteral("/tracker.config"),
        profile + QStringLiteral("/sharing.config")};
    for (int offset = 0; offset < 7; ++offset) {
      paths[5 + static_cast<std::size_t>(offset)] = profile
          + ending.addDays(-offset).toString(QStringLiteral("'/stats/'yyyy/MM/dd'.dat'"));
    }
    QMap<QString, bool> present; QMap<QString, QByteArray> original;
    for (const auto& path : paths) {
      const bool exists = QFileInfo::exists(path); present.insert(path, exists);
      if (exists) {
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        original.insert(path, file.readAll()); QCOMPARE(file.error(), QFileDevice::NoError);
      }
    }
    QVERIFY(!original.value(profile + QStringLiteral("/downloads.config")).isEmpty());
    QVERIFY(!original.value(profile + QStringLiteral("/stats/2024/01/02.dat")).isEmpty());
    QVERIFY(QDir(root.filePath(QStringLiteral("payload"))).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());

    // Restore every original Clipboard MIME format, including on a QtTest return.
    // Other clipboard modes and the real compositor are not touched by this test.
    struct ClipboardRestore final {
      QPointer<QClipboard> clipboard;
      std::unique_ptr<QMimeData> saved;
      explicit ClipboardRestore(QClipboard* value)
          : clipboard(value), saved(std::make_unique<QMimeData>()) {
        if (const auto* mime = clipboard->mimeData(QClipboard::Clipboard)) {
          for (const auto& format : mime->formats()) { saved->setData(format, mime->data(format)); }
        }
      }
      ~ClipboardRestore() {
        if (clipboard) { clipboard->setMimeData(saved.release(), QClipboard::Clipboard); }
      }
      ClipboardRestore(const ClipboardRestore&) = delete;
      ClipboardRestore& operator=(const ClipboardRestore&) = delete;
    };
    auto* clipboard = QGuiApplication::clipboard(); QVERIFY(clipboard);
    ClipboardRestore clipboardRestore(clipboard);
    const auto sentinel = QStringLiteral("BiglyBTC generated preview Copy sentinel");
    clipboard->setText(sentinel, QClipboard::Clipboard);
    qRegisterMetaType<QClipboard::Mode>();
    QSignalSpy clipboardChanges(clipboard, &QClipboard::changed); QVERIFY(clipboardChanges.isValid());

    ui::LibraryWindow window; app::SessionBridge bridge; wire(window, bridge);
    window.setOfflinePreview(true); window.resize(1280, 900); window.show();
    QCoreApplication::processEvents(); window.activateWindow();
    QSignalSpy layouts(&bridge, &app::SessionBridge::columnLayoutsReady);
    QSignalSpy totals(&bridge, &app::SessionBridge::statisticsReady);
    QSignalSpy days(&bridge, &app::SessionBridge::historyReady);
    QSignalSpy weeks(&bridge, &app::SessionBridge::historyWeekReady);
    QSignalSpy snapshots(&bridge, &app::SessionBridge::snapshotReady);
    QVERIFY(bridge.open(profile.toStdString(), 0));
    QTRY_VERIFY_WITH_TIMEOUT(layouts.count() == 1 && !bridge.busy(), 30000);
    auto* statistics = window.findChild<QAction*>(QStringLiteral("classic-statistics"));
    auto* close = window.findChild<QAction*>(QStringLiteral("classic-close-tab"));
    QVERIFY(statistics); QVERIFY(close); statistics->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(totals.count() == 1 && !bridge.busy(), 30000);
    auto* page = window.statisticsPage(); QVERIFY(page);
    auto* date = page->findChild<QDateEdit*>(QStringLiteral("saved-history-week-date"));
    auto* periods = page->findChild<QTabWidget*>(QStringLiteral("statistics-history-tabs"));
    auto* transfers = page->findChild<QTabWidget*>(QStringLiteral("statistics-transfer-tabs"));
    auto* traffic = page->findChild<QComboBox*>(QStringLiteral("saved-history-week-traffic"));
    auto* resolution = page->findChild<QComboBox*>(QStringLiteral("saved-history-week-resolution"));
    QVERIFY(date); QVERIFY(periods); QVERIFY(transfers); QVERIFY(traffic); QVERIFY(resolution);
    date->setDate(ending); traffic->setCurrentIndex(2); resolution->setCurrentIndex(1);
    periods->setCurrentIndex(1); page->tabs().setCurrentIndex(1); transfers->setCurrentIndex(1);
    QTRY_VERIFY_WITH_TIMEOUT(weeks.count() == 1 && page->historyWeekPresentation() && !bridge.busy(), 30000);
    const auto packet = page->historyWeekPresentation();
    QCOMPARE(packet->state, ui::HistoryWeekReadState::Ready); QVERIFY(packet->snapshot);
    QCOMPARE(packet->endingDate, (core::HistoryDate{2024, 1, 2}));
    auto* plot = page->findChild<ui::SavedHistoryPlot*>(QStringLiteral("saved-history-week-chart"));
    QVERIFY(plot); QVERIFY(plot->isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(plot->presentation()
        && plot->presentation()->mode() == ui::SavedHistoryPlotMode::WeekHourly
        && plot->presentation()->traffic() == ui::SavedHistoryTraffic::Dht, 1000);
    const auto frame = plot->presentation(); QCOMPARE(frame->buckets().size(), std::size_t{168});
    std::optional<std::size_t> lastKnown;
    for (std::size_t i = 0; i < 168; ++i) {
      const auto& savedDay = packet->snapshot->days[i / 24].snapshot;
      const auto savedHour = savedDay ? savedDay->hours[i % 24] : std::nullopt;
      const auto& bucket = frame->buckets()[i];
      QCOMPARE(bucket.date, packet->snapshot->days[i / 24].date); QVERIFY(bucket.hour == i % 24);
      QCOMPARE(bucket.downloaded.has_value(), savedHour.has_value());
      QCOMPARE(bucket.uploaded.has_value(), savedHour.has_value());
      if (savedHour) {
        QCOMPARE(*bucket.downloaded, (*savedHour)[5]); QCOMPARE(*bucket.uploaded, (*savedHour)[4]);
        lastKnown = i;
      }
    }
    QVERIFY(lastKnown); QVERIFY(*lastKnown < std::size_t{167});
    auto* copy = plot->findChild<QAction*>(QStringLiteral("saved-history-copy-selection"), Qt::FindDirectChildrenOnly);
    QVERIFY(copy); QCOMPARE(copy->parent(), plot); QCOMPARE(copy->text(), QStringLiteral("Copy selected values"));
    QCOMPARE(copy->shortcut(), QKeySequence(QKeySequence::Copy)); QCOMPARE(copy->shortcutContext(), Qt::WidgetShortcut);
    QCOMPARE(plot->contextMenuPolicy(), Qt::ActionsContextMenu); QVERIFY(plot->actions().contains(copy));
    QVERIFY(!plot->selectedBucket()); QVERIFY(!copy->isEnabled()); copy->trigger();
    QCOMPARE(clipboard->text(QClipboard::Clipboard), sentinel); QCOMPARE(clipboardChanges.count(), 0);

    plot->setFocus(Qt::OtherFocusReason); QTRY_VERIFY_WITH_TIMEOUT(plot->hasFocus(), 1000);
    QTest::keyClick(plot, Qt::Key_End); QCOMPARE(plot->selectedBucket(), std::optional<std::size_t>{167});
    QVERIFY(copy->isEnabled());
    const auto unknown = frame->bucketToolTip(167);
    QCOMPARE(unknown, QStringLiteral("2024-01-02 23:00 UTC\nDHT downloaded: Unknown\nDHT uploaded: Unknown"));
    QCOMPARE(clipboardChanges.count(), 0); QCOMPARE(clipboard->text(QClipboard::Clipboard), sentinel);
    QTest::keyClick(plot, Qt::Key_C, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(clipboard->text(QClipboard::Clipboard), unknown, 1000);
    QVERIFY(!clipboardChanges.isEmpty());
    const auto* copiedMime = clipboard->mimeData(QClipboard::Clipboard);
    QVERIFY(copiedMime); QVERIFY(copiedMime->hasText()); QVERIFY(!copiedMime->hasHtml());

    clipboard->setText(sentinel, QClipboard::Clipboard);
    const auto beforeKnownSelection = clipboardChanges.count();
    for (std::size_t i = 167; i > *lastKnown; --i) { QTest::keyClick(plot, Qt::Key_Left); }
    QCOMPARE(plot->selectedBucket(), lastKnown);
    const auto exact = frame->bucketToolTip(*lastKnown);
    QVERIFY(exact.contains(frame->bucketLabel(*lastKnown)));
    QVERIFY(exact.contains(QStringLiteral("DHT downloaded: %1 bytes").arg(*frame->buckets()[*lastKnown].downloaded)));
    QVERIFY(exact.contains(QStringLiteral("DHT uploaded: %1 bytes").arg(*frame->buckets()[*lastKnown].uploaded)));
    QCOMPARE(plot->accessibleDescription(), exact); QCOMPARE(plot->toolTip(), exact);
    date->setFocus(Qt::OtherFocusReason); QTRY_VERIFY_WITH_TIMEOUT(!plot->hasFocus(), 1000);
    QVERIFY(copy->isEnabled()); QCOMPARE(plot->selectedBucket(), lastKnown);
    QCOMPARE(clipboardChanges.count(), beforeKnownSelection); QCOMPARE(clipboard->text(QClipboard::Clipboard), sentinel);
    // This is the same enabled action exposed by the plot's context menu.
    copy->trigger(); QCOMPARE(clipboard->text(QClipboard::Clipboard), exact);
    QVERIFY(clipboardChanges.count() > beforeKnownSelection);
    const auto afterCopy = clipboardChanges.count();
    traffic->setCurrentIndex(1);
    QTRY_VERIFY_WITH_TIMEOUT(plot->presentation() && plot->presentation()->traffic() == ui::SavedHistoryTraffic::Protocol, 1000);
    QVERIFY(!plot->selectedBucket()); QVERIFY(!copy->isEnabled());
    QCOMPARE(clipboard->text(QClipboard::Clipboard), exact); QCOMPARE(clipboardChanges.count(), afterCopy);
    traffic->setCurrentIndex(2);
    QTRY_VERIFY_WITH_TIMEOUT(plot->presentation() && plot->presentation()->traffic() == ui::SavedHistoryTraffic::Dht, 1000);
    QVERIFY(!plot->selectedBucket()); QVERIFY(!copy->isEnabled());
    plot->setFocus(Qt::OtherFocusReason); QTRY_VERIFY_WITH_TIMEOUT(plot->hasFocus(), 1000);
    QTest::keyClick(plot, Qt::Key_End);
    for (std::size_t i = 167; i > *lastKnown; --i) { QTest::keyClick(plot, Qt::Key_Left); }
    QCOMPARE(plot->selectedBucket(), lastKnown); QCOMPARE(plot->toolTip(), exact); QVERIFY(copy->isEnabled());
    QVERIFY(page->historyWeekPresentation() == packet);
    QCOMPARE(weeks.count(), 1); QCOMPARE(days.count(), 0); QCOMPARE(totals.count(), 1); QCOMPARE(snapshots.count(), 1);
    QCOMPARE(clipboardChanges.count(), afterCopy); QCOMPARE(clipboard->text(QClipboard::Clipboard), exact);
    const auto capture = qEnvironmentVariable("BIGLYBTC_PREVIEW_CAPTURE");
    if (!capture.isEmpty()) {
      const auto path = root.filePath(QStringLiteral("statistics-history-copy-preview.png"));
      QVERIFY(!QFileInfo::exists(path)); QVERIFY(plot->hasFocus()); QVERIFY(window.grab().save(path));
    }

    const auto remembered = page->navigation(); QVERIFY(remembered);
    QPointer oldPage(page); QPointer oldPlot(plot); close->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(oldPage.isNull() && oldPlot.isNull(), 1000);
    statistics->trigger(); page = window.statisticsPage(); QVERIFY(page); QCOMPARE(page->navigation(), remembered);
    QTRY_VERIFY_WITH_TIMEOUT(totals.count() == 2 && weeks.count() == 2
        && page->historyWeekPresentation() && !bridge.busy(), 30000);
    plot = page->findChild<ui::SavedHistoryPlot*>(QStringLiteral("saved-history-week-chart")); QVERIFY(plot);
    QTRY_VERIFY_WITH_TIMEOUT(plot->presentation()
        && plot->presentation()->mode() == ui::SavedHistoryPlotMode::WeekHourly
        && plot->presentation()->traffic() == ui::SavedHistoryTraffic::Dht, 1000);
    copy = plot->findChild<QAction*>(QStringLiteral("saved-history-copy-selection"), Qt::FindDirectChildrenOnly);
    QVERIFY(copy); QVERIFY(!copy->isEnabled()); QVERIFY(!plot->selectedBucket());
    QVERIFY(plot->presentation() != frame); QVERIFY(page->historyWeekPresentation() != packet);
    QCOMPARE(weeks.count(), 2); QCOMPARE(days.count(), 0); QCOMPARE(totals.count(), 2); QCOMPARE(snapshots.count(), 1);
    QCOMPARE(clipboard->text(QClipboard::Clipboard), exact); QCOMPARE(clipboardChanges.count(), afterCopy);
    for (const auto& change : clipboardChanges) {
      QCOMPARE(change.size(), 1); QCOMPARE(qvariant_cast<QClipboard::Mode>(change[0]), QClipboard::Clipboard);
    }
    for (auto it = present.cbegin(); it != present.cend(); ++it) {
      QCOMPARE(QFileInfo::exists(it.key()), it.value());
      if (it.value()) {
        QFile file(it.key()); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original.value(it.key())); QCOMPARE(file.error(), QFileDevice::NoError);
      }
    }
    QVERIFY(QDir(root.filePath(QStringLiteral("payload"))).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
  }

  void generatedThirtyDayHistoryKeepsExactDailyFactsAndFreshReopen() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    const auto profile = qEnvironmentVariable("BIGLYBTC_PREVIEW_PROFILE");
    const QFileInfo profileInfo(profile);
    const QDir root(profileInfo.absolutePath());
    QVERIFY(profileInfo.isDir()); QCOMPARE(profileInfo.fileName(), QStringLiteral("profile"));
    QCOMPARE(profileInfo.canonicalFilePath(), profile);
    QCOMPARE(QFileInfo(root.absolutePath()).absolutePath(), QStringLiteral("/tmp"));
    QVERIFY(root.dirName().startsWith(QStringLiteral("biglybtc-preview-")));
    const QDate ending(2024, 1, 2);
    QMap<QString, bool> present; QMap<QString, QByteArray> original;
    const auto remember = [&](const QString& path) {
      const bool exists = QFileInfo::exists(path); present.insert(path, exists);
      if (exists) { original.insert(path, bytes(path)); }
    };
    for (const auto& leaf : {QStringLiteral("downloads.config"), QStringLiteral("BiglyBT.statistics"),
        QStringLiteral("tables.config"), QStringLiteral("tracker.config"), QStringLiteral("sharing.config")}) {
      remember(profile + QLatin1Char('/') + leaf);
    }
    for (int offset = 0; offset < 30; ++offset) {
      const auto path = profile + ending.addDays(-offset).toString(QStringLiteral("'/stats/'yyyy/MM/dd'.dat'"));
      remember(path); remember(path + QStringLiteral(".bak")); remember(path + QStringLiteral(".saving"));
    }
    const QDir active(profile + QStringLiteral("/active"));
    const auto activeFiles = active.entryList({QStringLiteral("*.dat")}, QDir::Files);
    QCOMPARE(activeFiles.size(), 5);
    for (const auto& leaf : activeFiles) { remember(active.filePath(leaf)); }
    QVERIFY(!original.value(profile + QStringLiteral("/downloads.config")).isEmpty());
    QVERIFY(!original.value(profile + QStringLiteral("/stats/2024/01/02.dat")).isEmpty());
    const auto payload = root.filePath(QStringLiteral("payload"));
    QVERIFY(QDir(payload).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());

    // Generated offscreen Clipboard only; restore all preexisting MIME formats
    // even when a QtTest assertion returns early. No compositor claim is made.
    struct ClipboardRestore final {
      QPointer<QClipboard> clipboard;
      std::unique_ptr<QMimeData> saved;
      explicit ClipboardRestore(QClipboard* value) : clipboard(value), saved(std::make_unique<QMimeData>()) {
        if (const auto* mime = clipboard->mimeData(QClipboard::Clipboard)) {
          for (const auto& format : mime->formats()) { saved->setData(format, mime->data(format)); }
        }
      }
      ~ClipboardRestore() { if (clipboard) { clipboard->setMimeData(saved.release(), QClipboard::Clipboard); } }
      ClipboardRestore(const ClipboardRestore&) = delete;
      ClipboardRestore& operator=(const ClipboardRestore&) = delete;
    };
    auto* clipboard = QGuiApplication::clipboard(); QVERIFY(clipboard);
    ClipboardRestore clipboardRestore(clipboard);
    const auto sentinel = QStringLiteral("BiglyBTC generated 30-day preview Clipboard sentinel");
    clipboard->setText(sentinel, QClipboard::Clipboard);
    qRegisterMetaType<QClipboard::Mode>();
    QSignalSpy clipboardChanges(clipboard, &QClipboard::changed); QVERIFY(clipboardChanges.isValid());

    ui::LibraryWindow window; app::SessionBridge bridge; wire(window, bridge);
    window.setOfflinePreview(true); window.resize(1280, 900); window.show();
    QCoreApplication::processEvents(); window.activateWindow();
    QSignalSpy layouts(&bridge, &app::SessionBridge::columnLayoutsReady);
    QSignalSpy totals(&bridge, &app::SessionBridge::statisticsReady);
    QSignalSpy days(&bridge, &app::SessionBridge::historyReady);
    QSignalSpy weeks(&bridge, &app::SessionBridge::historyWeekReady);
    QSignalSpy months(&bridge, &app::SessionBridge::historyMonthReady);
    QSignalSpy snapshots(&bridge, &app::SessionBridge::snapshotReady);
    QVERIFY(bridge.open(profile.toStdString(), 0));
    QTRY_VERIFY_WITH_TIMEOUT(layouts.count() == 1 && !bridge.busy(), 30000);
    auto* statistics = window.findChild<QAction*>(QStringLiteral("classic-statistics"));
    auto* close = window.findChild<QAction*>(QStringLiteral("classic-close-tab"));
    QVERIFY(statistics); QVERIFY(close); statistics->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(totals.count() == 1 && !bridge.busy(), 30000);
    auto* page = window.statisticsPage(); QVERIFY(page);
    auto* date = page->findChild<QDateEdit*>(QStringLiteral("saved-history-month-date"));
    auto* periods = page->findChild<QTabWidget*>(QStringLiteral("statistics-history-tabs"));
    auto* transfers = page->findChild<QTabWidget*>(QStringLiteral("statistics-transfer-tabs"));
    auto* traffic = page->findChild<QComboBox*>(QStringLiteral("saved-history-month-traffic"));
    QVERIFY(date); QVERIFY(periods); QVERIFY(transfers); QVERIFY(traffic);
    QCOMPARE(periods->count(), 4); QCOMPARE(periods->tabText(2), QStringLiteral("30 days"));
    QCOMPARE(periods->tabText(3), QStringLiteral("Rolling history"));
    QCOMPARE(date->minimumDate(), QDate(1970, 1, 30)); QCOMPARE(traffic->count(), 3);
    date->setDate(ending); periods->setCurrentIndex(2);
    page->tabs().setCurrentIndex(1); transfers->setCurrentIndex(1);
    QTRY_VERIFY_WITH_TIMEOUT(months.count() == 1 && page->historyMonthPresentation() && !bridge.busy(), 30000);
    const auto packet = page->historyMonthPresentation();
    QCOMPARE(packet->state, ui::HistoryMonthReadState::Ready); QVERIFY(packet->snapshot);
    QCOMPARE(packet->endingDate, (core::HistoryDate{2024, 1, 2}));
    QCOMPARE(packet->snapshot->days.front().date, (core::HistoryDate{2023, 12, 4}));
    QCOMPARE(packet->snapshot->days.back().date, (core::HistoryDate{2024, 1, 2}));

    // Independent constants from preview.py's generated records, in the saved
    // protocol-up/payload-up/protocol-down/payload-down/DHT-up/DHT-down order.
    using Counters = std::array<std::int64_t, 6>;
    std::array<std::optional<Counters>, 30> expected{};
    expected[23] = Counters{768, 6144, 384, 12288, 96, 192};
    expected[24] = Counters{640, 5120, 320, 10240, 80, 160};
    expected[26] = Counters{-128, -1024, -64, -2048, -16, -32};
    expected[28] = Counters{0, 0, 0, 0, 0, 0};
    expected[29] = Counters{16896, 1081344, 33792, 17301504, 2112, 4224};
    auto* table = page->findChild<QTableView*>(QStringLiteral("saved-history-month-days")); QVERIFY(table);
    QCOMPARE(table->model()->rowCount(), 30); QCOMPARE(table->model()->columnCount(), 8);
    for (std::size_t i = 0; i < 30; ++i) {
      const auto& row = packet->snapshot->days[i];
      const auto qtDate = ending.addDays(static_cast<int>(i) - 29);
      QCOMPARE(row.date, (core::HistoryDate{qtDate.year(), static_cast<unsigned>(qtDate.month()), static_cast<unsigned>(qtDate.day())}));
      const bool filePresent = i == 23 || i == 24 || i == 26 || i == 27 || i == 28 || i == 29;
      QCOMPARE(row.snapshot.has_value(), filePresent);
      if (row.snapshot) {
        QCOMPARE(row.snapshot->date, row.date);
        QCOMPARE(row.snapshot->totals, expected[i]);
        if (i == 27) { QCOMPARE(row.snapshot->observations, std::size_t{0}); }
      }
      QCOMPARE(table->model()->data(table->model()->index(static_cast<int>(i), 1)).toString(),
          !filePresent ? QStringLiteral("Missing") : i == 27 ? QStringLiteral("No observations") : QStringLiteral("Observed"));
      for (std::size_t counter = 0; counter < 6; ++counter) {
        const auto cell = table->model()->index(static_cast<int>(i), static_cast<int>(counter) + 2);
        const auto value = table->model()->data(cell, Qt::UserRole);
        QCOMPARE(value.isValid(), expected[i].has_value());
        if (expected[i]) {
          QCOMPARE(value.toLongLong(), (*expected[i])[counter]);
          QCOMPARE(table->model()->data(cell).toString(), QStringLiteral("%1 bytes").arg((*expected[i])[counter]));
        } else { QCOMPARE(table->model()->data(cell).toString(), QString::fromUtf8("—")); }
      }
    }
    auto* plot = page->findChild<ui::SavedHistoryPlot*>(QStringLiteral("saved-history-month-chart")); QVERIFY(plot);
    constexpr std::array<ui::SavedHistoryTraffic, 3> kinds{
        ui::SavedHistoryTraffic::Payload, ui::SavedHistoryTraffic::Protocol, ui::SavedHistoryTraffic::Dht};
    constexpr std::array<std::array<std::size_t, 2>, 3> indices{{{3, 1}, {2, 0}, {5, 4}}};
    for (int choice = 0; choice < 3; ++choice) {
      traffic->setCurrentIndex(choice);
      QTRY_VERIFY_WITH_TIMEOUT(plot->presentation() && plot->presentation()->mode() == ui::SavedHistoryPlotMode::Month
          && plot->presentation()->traffic() == kinds[static_cast<std::size_t>(choice)], 1000);
      const auto frame = plot->presentation(); QCOMPARE(frame->buckets().size(), std::size_t{30});
      for (std::size_t i = 0; i < 30; ++i) {
        const auto& bucket = frame->buckets()[i]; QCOMPARE(bucket.date, packet->snapshot->days[i].date); QVERIFY(!bucket.hour);
        QCOMPARE(bucket.downloaded.has_value(), expected[i].has_value()); QCOMPARE(bucket.uploaded.has_value(), expected[i].has_value());
        if (expected[i]) {
          QCOMPARE(*bucket.downloaded, (*expected[i])[indices[static_cast<std::size_t>(choice)][0]]);
          QCOMPARE(*bucket.uploaded, (*expected[i])[indices[static_cast<std::size_t>(choice)][1]]);
        }
      }
      QCOMPARE(months.count(), 1); QCOMPARE(days.count(), 0); QCOMPARE(weeks.count(), 0); QCOMPARE(totals.count(), 1);
      QVERIFY(page->historyMonthPresentation() == packet); QCOMPARE(snapshots.count(), 1);
      QCOMPARE(clipboardChanges.count(), 0); QCOMPARE(clipboard->text(QClipboard::Clipboard), sentinel);
    }
    const auto frame = plot->presentation();
    auto* copy = plot->findChild<QAction*>(QStringLiteral("saved-history-copy-selection"), Qt::FindDirectChildrenOnly);
    QVERIFY(copy); QVERIFY(!copy->isEnabled()); QVERIFY(!plot->selectedBucket());
    QTRY_VERIFY_WITH_TIMEOUT(window.isActiveWindow(), 1000);
    plot->setFocus(Qt::OtherFocusReason); QTRY_VERIFY_WITH_TIMEOUT(plot->hasFocus(), 1000);
    QTest::keyClick(plot, Qt::Key_Home); QCOMPARE(plot->selectedBucket(), std::optional<std::size_t>{0});
    const auto unknown = QStringLiteral("2023-12-04 UTC\nDHT downloaded: Unknown\nDHT uploaded: Unknown");
    QCOMPARE(plot->toolTip(), unknown); QCOMPARE(clipboardChanges.count(), 0);
    QTest::keyClick(plot, Qt::Key_C, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(clipboard->text(QClipboard::Clipboard), unknown, 1000);
    QTest::keyClick(plot, Qt::Key_End); QTest::keyClick(plot, Qt::Key_Left);
    QCOMPARE(plot->selectedBucket(), std::optional<std::size_t>{28});
    const auto zero = QStringLiteral("2024-01-01 UTC\nDHT downloaded: 0 bytes\nDHT uploaded: 0 bytes");
    QCOMPARE(plot->accessibleDescription(), zero); QVERIFY(copy->isEnabled());
    QTest::keyClick(plot, Qt::Key_C, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(clipboard->text(QClipboard::Clipboard), zero, 1000);
    QTest::keyClick(plot, Qt::Key_End); QCOMPARE(plot->selectedBucket(), std::optional<std::size_t>{29});
    const auto exact = QStringLiteral("2024-01-02 UTC\nDHT downloaded: 4224 bytes\nDHT uploaded: 2112 bytes");
    QCOMPARE(plot->toolTip(), exact); QCOMPARE(plot->accessibleDescription(), exact);
    copy->trigger(); QCOMPARE(clipboard->text(QClipboard::Clipboard), exact);
    const auto afterCopy = clipboardChanges.count(); QCOMPARE(afterCopy, 3);
    const auto* copiedMime = clipboard->mimeData(QClipboard::Clipboard);
    QVERIFY(copiedMime); QVERIFY(copiedMime->hasText()); QVERIFY(!copiedMime->hasHtml());
    QVERIFY(page->historyMonthPresentation() == packet); QVERIFY(plot->presentation() == frame);
    QCOMPARE(months.count(), 1); QCOMPARE(days.count(), 0); QCOMPARE(weeks.count(), 0); QCOMPARE(totals.count(), 1);
    const auto capture = qEnvironmentVariable("BIGLYBTC_PREVIEW_CAPTURE");
    if (!capture.isEmpty()) {
      const auto path = root.filePath(QStringLiteral("statistics-history-month-preview.png"));
      table->scrollToBottom(); QCoreApplication::processEvents();
      QVERIFY(!QFileInfo::exists(path)); QVERIFY(window.grab().save(path));
    }

    const auto remembered = page->navigation(); QVERIFY(remembered);
    QCOMPARE(remembered->history, 2); QCOMPARE(remembered->monthEnding, (core::HistoryDate{2024, 1, 2}));
    QCOMPARE(remembered->monthTraffic, ui::SavedHistoryTraffic::Dht);
    QPointer oldPage(page); QPointer oldPlot(plot); close->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(oldPage.isNull() && oldPlot.isNull(), 1000);
    statistics->trigger(); page = window.statisticsPage(); QVERIFY(page); QCOMPARE(page->navigation(), remembered);
    QTRY_VERIFY_WITH_TIMEOUT(totals.count() == 2 && months.count() == 2
        && page->historyMonthPresentation() && !bridge.busy(), 30000);
    QVERIFY(page->historyMonthPresentation() != packet);
    QCOMPARE(page->historyMonthPresentation()->endingDate, (core::HistoryDate{2024, 1, 2}));
    QVERIFY(page->historyMonthPresentation()->snapshot); QVERIFY(page->historyMonthPresentation()->snapshot != packet->snapshot);
    QVERIFY(page->historyMonthPresentation()->snapshot->days[29].snapshot);
    QCOMPARE(page->historyMonthPresentation()->snapshot->days[29].snapshot->totals, expected[29]);
    plot = page->findChild<ui::SavedHistoryPlot*>(QStringLiteral("saved-history-month-chart")); QVERIFY(plot);
    QTRY_VERIFY_WITH_TIMEOUT(plot->presentation() && plot->presentation()->mode() == ui::SavedHistoryPlotMode::Month
        && plot->presentation()->traffic() == ui::SavedHistoryTraffic::Dht, 1000);
    QVERIFY(plot->presentation() != frame); QVERIFY(!plot->selectedBucket());
    copy = plot->findChild<QAction*>(QStringLiteral("saved-history-copy-selection"), Qt::FindDirectChildrenOnly);
    QVERIFY(copy); QVERIFY(!copy->isEnabled());
    QCOMPARE(days.count(), 0); QCOMPARE(weeks.count(), 0); QCOMPARE(totals.count(), 2); QCOMPARE(months.count(), 2); QCOMPARE(snapshots.count(), 1);
    QCOMPARE(clipboardChanges.count(), afterCopy); QCOMPARE(clipboard->text(QClipboard::Clipboard), exact);
    for (const auto& change : clipboardChanges) { QCOMPARE(qvariant_cast<QClipboard::Mode>(change[0]), QClipboard::Clipboard); }
    for (auto it = present.cbegin(); it != present.cend(); ++it) {
      QCOMPARE(QFileInfo::exists(it.key()), it.value());
      if (it.value()) { QCOMPARE(bytes(it.key()), original.value(it.key())); }
    }
    QVERIFY(QDir(payload).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
  }
};
QTEST_MAIN(PreviewSmoke)
#include "preview_smoke.moc"
