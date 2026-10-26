#include "library_window.hpp"

#include <QAction>
#include <QApplication>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QtTest>

#include <array>
#include <initializer_list>

namespace {
using namespace biglybtc;
using Action = core::DownloadAction;

std::shared_ptr<ui::LibraryPresentation> sample(quint64 revision, std::array<quint16, 4> masks) {
  auto value = std::make_shared<ui::LibraryPresentation>();
  value->revision = revision;
  const QStringList names{QStringLiteral("Alpha.bin"), QStringLiteral("Beta.bin"),
      QStringLiteral("Gamma.bin"), QStringLiteral("Delta complete.bin")};
  for (int row = 0; row < 4; ++row) {
    const auto hash = QStringLiteral("%1").arg(row + 1, 40, 16, QLatin1Char('0')).toUpper();
    value->items.push_back({.key = hash, .hash = hash, .name = names[row],
        .state = QStringLiteral("Downloading"), .bytes = 100, .completed = row == 3 ? 100 : 20,
        .received = 20, .sent = 0, .canContinue = true,
        .actionMask = masks[static_cast<std::size_t>(row)]});
  }
  return value;
}

bool selectRows(ui::LibraryWindow& window, std::initializer_list<int> sourceRows, bool focus = true) {
  auto& table = window.incompleteTable();
  auto* proxy = qobject_cast<QSortFilterProxyModel*>(table.model());
  if (!proxy || sourceRows.size() == 0) { return false; }
  if (focus) { table.setFocus(); QCoreApplication::processEvents(); }
  table.selectionModel()->clearSelection();
  bool first = true;
  for (const int row : sourceRows) {
    const auto index = proxy->mapFromSource(window.model().index(row, 0));
    if (!index.isValid()) { return false; }
    table.selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    if (first) {
      table.selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
      first = false;
    }
  }
  QCoreApplication::processEvents();
  return table.selectionModel()->selectedRows().size() == static_cast<qsizetype>(sourceRows.size());
}

QAction* action(ui::LibraryWindow& window, const char* name) {
  return window.findChild<QAction*>(QString::fromLatin1(name));
}

bool forceTrigger(QAction* value) {
  return value && QMetaObject::invokeMethod(value, "triggered", Qt::DirectConnection, Q_ARG(bool, false));
}

ui::DownloadTargets targets(ui::LibraryWindow& window, std::initializer_list<int> rows) {
  ui::DownloadTargets result;
  for (const int row : rows) {
    if (const auto target = window.model().targetAt(row)) { result.push_back(*target); }
  }
  return result;
}
} // namespace

class LibraryActionUiTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
    qRegisterMetaType<ui::DownloadTargets>();
    qRegisterMetaType<Action>();
  }

  void hybridCompanionUsesFreshSingleTargetAndRemainsPassive() {
    ui::LibraryWindow window;window.setPresentation(sample(91,{0,0,0,0}));window.beginColumnLayoutSession(901);window.show();
    QVERIFY(selectRows(window,{0}));auto* useV2=action(window,"classic-hybrid-v2");QVERIFY(useV2);
    QSignalSpy requests(&window,&ui::LibraryWindow::hybridCompanionRequested);QTRY_VERIFY(useV2->isEnabled());
    const auto capture=qEnvironmentVariable("BIGLYBTC_HYBRID_MENU_CAPTURE");
    if(!capture.isEmpty()) {
      auto* menu=qobject_cast<QMenu*>(useV2->parent());QVERIFY(menu);
      menu->show();QCoreApplication::processEvents();QVERIFY(menu->grab().save(capture));menu->hide();
    }
    useV2->trigger();QCOMPARE(requests.size(),1);QCOMPARE(qvariant_cast<ui::Target>(requests.first()[0]),targets(window,{0}).front());QVERIFY(requests.first()[1].toBool());
    window.setBusy(true);QVERIFY(forceTrigger(useV2));QCOMPARE(requests.size(),1);
    window.setBusy(false);QVERIFY(selectRows(window,{0,1}));QVERIFY(forceTrigger(useV2));QCOMPARE(requests.size(),1);
    QVERIFY(selectRows(window,{1}));window.setPresentation(sample(92,{0,0,0,0}));
    useV2->trigger();QCOMPARE(requests.size(),2);QCOMPARE(qvariant_cast<ui::Target>(requests.last()[0]).generation,quint64{92});
  }
  void hybridCompanionDirectReceiverMayDeleteWindow() {
    QPointer<ui::LibraryWindow> window=new ui::LibraryWindow;
    window->setPresentation(sample(93,{0,0,0,0}));window->beginColumnLayoutSession(903);window->show();QVERIFY(selectRows(*window,{0}));
    auto* useV1=action(*window,"classic-hybrid-v1");QVERIFY(useV1);QTRY_VERIFY(useV1->isEnabled());
    unsigned calls=0;connect(window,&ui::LibraryWindow::hybridCompanionRequested,this,[&](ui::Target target,bool v2){
      ++calls;QCOMPARE(target.generation,quint64{93});QVERIFY(!v2);delete window.data();
    });
    useV1->trigger();QVERIFY(window.isNull());QCOMPARE(calls,1U);
  }
  void eachCommandRoutesWholeSelection_data() {
    QTest::addColumn<int>("ordinal");
    QTest::addColumn<QByteArray>("id");
    const std::array<const char*, 10> ids{"classic-start", "classic-stop", "classic-pause", "classic-resume",
        "classic-queue", "classic-force-start", "classic-top", "classic-up", "classic-down", "classic-bottom"};
    for (std::size_t i = 0; i < ids.size(); ++i) {
      QTest::newRow(ids[i]) << static_cast<int>(i) << QByteArray(ids[i]);
    }
  }
  void eachCommandRoutesWholeSelection() {
    QFETCH(int, ordinal);
    QFETCH(QByteArray, id);
    const auto command = static_cast<Action>(ordinal);
    const auto mask = ui::downloadActionBit(command);
    ui::LibraryWindow window;
    window.setPresentation(sample(17, {mask, mask, 0, 0}));
    window.show();
    QVERIFY(selectRows(window, {1, 0}));
    auto* item = action(window, id.constData());
    QVERIFY(item);
    QTRY_VERIFY(item->isEnabled());
    auto* menu = window.findChild<QMenu*>(QStringLiteral("classic-torrent-menu"));
    QVERIFY(menu);
    QVERIFY(menu->actions().contains(item));
    QSignalSpy requests(&window, &ui::LibraryWindow::downloadActionRequested);
    const auto expected = targets(window, {0, 1});
    item->trigger();
    QCOMPARE(requests.count(), 1);
    QVERIFY(qvariant_cast<ui::DownloadTargets>(requests.first().at(0)) == expected);
    QCOMPARE(qvariant_cast<Action>(requests.first().at(1)), command);
    QCOMPARE(expected.size(), std::size_t{2});
    for (const auto& target : expected) { QCOMPARE(target.generation, quint64{17}); }
  }

  void unsupportedMemberNeverDispatchesPartialSelection() {
    ui::LibraryWindow window;
    const auto start = ui::downloadActionBit(Action::Start);
    window.setPresentation(sample(1, {start, 0, start, 0}));
    window.show();
    QVERIFY(selectRows(window, {0, 1}));
    auto* item = action(window, "classic-start");
    QVERIFY(item);
    QTRY_VERIFY(!item->isEnabled());
    QSignalSpy requests(&window, &ui::LibraryWindow::downloadActionRequested);
    QVERIFY(forceTrigger(item));
    QVERIFY(forceTrigger(action(window, "classic-start-stop")));
    QCOMPARE(requests.count(), 0);
    QVERIFY(selectRows(window, {0, 2}));
    QTRY_VERIFY(item->isEnabled());
    item->trigger();
    QCOMPARE(requests.count(), 1);
    QVERIFY(qvariant_cast<ui::DownloadTargets>(requests.first().at(0)) == targets(window, {0, 2}));
  }

  void transfersAllIncludesFilteredAndCompleteRows() {
    ui::LibraryWindow window;
    const auto start = ui::downloadActionBit(Action::Start);
    const auto stop = ui::downloadActionBit(Action::Stop);
    window.setPresentation(sample(9, {start, stop, start, start}));
    window.show();
    window.filter().setText(QStringLiteral("Alpha"));
    QCoreApplication::processEvents();
    QCOMPARE(window.incompleteTable().model()->rowCount(), 1);
    QCOMPARE(window.completeTable().model()->rowCount(), 0);
    auto* startAll = action(window, "classic-start-all");
    auto* stopAll = action(window, "classic-stop-all");
    QVERIFY(startAll && stopAll);
    auto* menu = window.findChild<QMenu*>(QStringLiteral("classic-transfers-menu"));
    QVERIFY(menu && menu->actions().contains(startAll) && menu->actions().contains(stopAll));
    QTRY_VERIFY(startAll->isEnabled() && stopAll->isEnabled());
    QSignalSpy requests(&window, &ui::LibraryWindow::downloadActionRequested);
    startAll->trigger();
    stopAll->trigger();
    QCOMPARE(requests.count(), 2);
    QVERIFY(qvariant_cast<ui::DownloadTargets>(requests.at(0).at(0)) == targets(window, {0, 2, 3}));
    QVERIFY(qvariant_cast<ui::DownloadTargets>(requests.at(1).at(0)) == targets(window, {1}));
    QCOMPARE(qvariant_cast<Action>(requests.at(0).at(1)), Action::Start);
    QCOMPARE(qvariant_cast<Action>(requests.at(1).at(1)), Action::Stop);
  }

  void toolbarAndContextReuseTheCommandObjects() {
    ui::LibraryWindow window;
    const auto start = ui::downloadActionBit(Action::Start);
    window.setPresentation(sample(1, {start, start, 0, 0}));
    window.show();
    QVERIFY(selectRows(window, {0, 1}));
    auto* alias = action(window, "classic-start-stop");
    auto* startItem = action(window, "classic-start");
    QVERIFY(alias && startItem);
    QTRY_VERIFY(alias->isEnabled());
    auto* toolbar = window.findChild<QToolBar*>(QStringLiteral("classic-toolbar"));
    QVERIFY(toolbar && toolbar->actions().contains(alias));
    auto* button = qobject_cast<QToolButton*>(toolbar->widgetForAction(alias));
    QVERIFY(button);
    QSignalSpy requests(&window, &ui::LibraryWindow::downloadActionRequested);
    QTest::mouseClick(button, Qt::LeftButton);
    QCOMPARE(requests.count(), 1);
    bool shared = false;
    QTimer::singleShot(0, &window, [&] {
      auto* menu = window.findChild<QMenu*>(QStringLiteral("classic-torrent-context"));
      if (!menu) { return; }
      QCoreApplication::processEvents();
      shared = menu->actions().contains(alias) && menu->actions().contains(startItem)
          && startItem->isEnabled();
      if (shared) { startItem->trigger(); }
      menu->close();
    });
    auto& table = window.incompleteTable();
    const auto point = table.visualRect(table.currentIndex()).center();
    QVERIFY(QMetaObject::invokeMethod(&table, "customContextMenuRequested", Qt::DirectConnection, Q_ARG(QPoint, point)));
    QVERIFY(shared);
    QCOMPARE(requests.count(), 2);
    QVERIFY(qvariant_cast<ui::DownloadTargets>(requests.at(1).at(0)) == targets(window, {0, 1}));
  }

  void openMenuDoesNotRetarget_data() {
    QTest::addColumn<bool>("newRevision");
    QTest::newRow("new-library-revision") << true;
    QTest::newRow("new-selection") << false;
  }
  void openMenuDoesNotRetarget() {
    QFETCH(bool, newRevision);
    ui::LibraryWindow window;
    const auto start = ui::downloadActionBit(Action::Start);
    window.setPresentation(sample(1, {start, start, start, start}));
    window.show();
    QVERIFY(selectRows(window, {0, 1}));
    QSignalSpy requests(&window, &ui::LibraryWindow::downloadActionRequested);
    bool visited = false;
    bool invoked = false;
    QTimer::singleShot(0, &window, [&] {
      auto* menu = window.findChild<QMenu*>(QStringLiteral("classic-torrent-context"));
      if (!menu) { return; }
      visited = true;
      if (newRevision) { window.setPresentation(sample(2, {start, start, start, start})); }
      else { selectRows(window, {2}, false); }
      QCoreApplication::processEvents();
      invoked = forceTrigger(action(window, "classic-start"));
      menu->close();
    });
    auto& table = window.incompleteTable();
    const auto point = table.visualRect(table.currentIndex()).center();
    QVERIFY(QMetaObject::invokeMethod(&table, "customContextMenuRequested", Qt::DirectConnection, Q_ARG(QPoint, point)));
    QVERIFY(visited && invoked);
    QCOMPARE(requests.count(), 0);
  }

  void explicitPermissionsBusyAndOfflineAreIndependentOfSavedNames() {
    ui::LibraryWindow window;
    window.setPresentation(sample(1, {}));
    window.show();
    QVERIFY(selectRows(window, {0}));
    auto* start = action(window, "classic-start");
    QVERIFY(start);
    QTRY_VERIFY(!start->isEnabled());
    QVERIFY(window.continueAction().isEnabled());
    QSignalSpy requests(&window, &ui::LibraryWindow::downloadActionRequested);
    QSignalSpy oldContinue(&window, &ui::LibraryWindow::continueRequested);
    QVERIFY(forceTrigger(start));
    QCOMPARE(requests.count(), 0);
    const auto mask = ui::downloadActionBit(Action::Start);
    window.setPresentation(sample(2, {mask, 0, 0, 0}));
    QTRY_VERIFY(start->isEnabled());
    window.setBusy(true);
    QVERIFY(forceTrigger(start));
    QCOMPARE(requests.count(), 0);
    window.setBusy(false);
    QCoreApplication::processEvents();
    window.setOfflinePreview(true);
    QVERIFY(forceTrigger(start));
    QCOMPARE(requests.count(), 0);
    // The old isolated Continue action keeps its existing contract.
    window.continueAction().trigger();
    QCOMPARE(oldContinue.count(), 1);
    window.setOfflinePreview(false);
    QCoreApplication::processEvents();
    QTRY_VERIFY(start->isEnabled());
    start->trigger();
    QCOMPARE(requests.count(), 1);
  }

  void invalidCapabilitiesAndAmbiguousTargetsFailClosed() {
    ui::LibraryWindow window;
    const auto start = ui::downloadActionBit(Action::Start);
    window.setPresentation(sample(1, {start, 0, 0, 0}));
    window.show();
    QVERIFY(selectRows(window, {0}));
    auto invalid = sample(2, {quint16{1} << 15, 0, 0, 0});
    window.setPresentation(invalid);
    QCOMPARE(window.model().revision(), quint64{1});
    auto ambiguous = sample(3, {start, start, start, 0});
    ambiguous->items[1].hash = ambiguous->items[0].hash;
    window.setPresentation(ambiguous);
    QVERIFY(selectRows(window, {0, 2}));
    QCoreApplication::processEvents();
    auto* startItem = action(window, "classic-start");
    auto* all = action(window, "classic-start-all");
    QVERIFY(startItem && all);
    QVERIFY(!startItem->isEnabled());
    QVERIFY(!all->isEnabled());
    QSignalSpy requests(&window, &ui::LibraryWindow::downloadActionRequested);
    QVERIFY(forceTrigger(startItem));
    QVERIFY(forceTrigger(all));
    QCOMPARE(requests.count(), 0);
    QCOMPARE(ui::downloadActionBit(static_cast<Action>(255)), quint16{0});
    QCOMPARE(window.model().permittedActions({QStringLiteral("missing"), 3}), quint16{0});
    QCOMPARE(window.model().permittedActionsAt(-1), quint16{0});
  }

  void actionPropertyCallbacksCannotResumeOldState() {
    ui::LibraryWindow window;
    auto* start = action(window, "classic-start");
    auto* stop = action(window, "classic-stop");
    QVERIFY(start && stop);
    bool rebound = false;
    connect(start, &QAction::changed, &window, [&] {
      if (!rebound && start->isEnabled()) {
        rebound = true;
        const auto mask = ui::downloadActionBit(Action::Stop);
        window.setPresentation(sample(2, {mask, 0, 0, 0}));
      }
    });
    const auto mask = ui::downloadActionBit(Action::Start);
    window.setPresentation(sample(1, {mask, 0, 0, 0}));
    QTRY_VERIFY(rebound && !start->isEnabled() && stop->isEnabled());
    QSignalSpy requests(&window, &ui::LibraryWindow::downloadActionRequested);
    QVERIFY(forceTrigger(start));
    stop->trigger();
    QCOMPARE(requests.count(), 1);
    QCOMPARE(qvariant_cast<ui::DownloadTargets>(requests.first().at(0)).front().generation, quint64{2});

    QPointer<ui::LibraryWindow> doomed = new ui::LibraryWindow;
    auto* doomedStart = action(*doomed, "classic-start");
    QVERIFY(doomedStart);
    connect(doomedStart, &QAction::changed, doomed, [doomed, doomedStart] {
      if (doomed && doomedStart->isEnabled()) { delete doomed.data(); }
    });
    doomed->setPresentation(sample(1, {mask, 0, 0, 0}));
    QTRY_VERIFY(doomed.isNull());
  }

  void dispatchReentryCannotSubmitTwiceOrMutateCapturedTargets() {
    ui::LibraryWindow window;
    const auto mask = static_cast<quint16>(ui::downloadActionBit(Action::Start) | ui::downloadActionBit(Action::Queue));
    window.setPresentation(sample(1, {mask, 0, 0, 0}));
    window.show();
    QVERIFY(selectRows(window, {0}));
    auto* start = action(window, "classic-start");
    auto* queue = action(window, "classic-queue");
    QVERIFY(start && queue);
    QTRY_VERIFY(start->isEnabled() && queue->isEnabled());
    int calls = 0;
    ui::DownloadTargets first;
    connect(&window, &ui::LibraryWindow::downloadActionRequested, &window,
        [&](ui::DownloadTargets submitted, Action) {
      ++calls;
      if (calls != 1) { return; }
      first = std::move(submitted);
      queue->trigger();
      window.setPresentation(sample(2, {mask, 0, 0, 0}));
    });
    start->trigger();
    QCOMPARE(calls, 1);
    QCOMPARE(first.size(), std::size_t{1});
    QCOMPARE(first.front().generation, quint64{1});
    QCoreApplication::processEvents();
    QTRY_VERIFY(queue->isEnabled());
    queue->trigger();
    QCOMPARE(calls, 2);
    QCOMPARE(first.front().generation, quint64{1});
  }
  void dispatchAllowsOwnerDeletionWithoutASecondPublication() {
    QPointer<ui::LibraryWindow> window = new ui::LibraryWindow;
    const auto start = ui::downloadActionBit(Action::Start);
    window->setPresentation(sample(1, {start, 0, 0, 0}));
    auto* item = action(*window, "classic-start");
    QVERIFY(item);
    QTRY_VERIFY(item->isEnabled());
    int calls = 0;
    ui::DownloadTargets submitted;
    connect(window.data(), &ui::LibraryWindow::downloadActionRequested, this,
        [&](ui::DownloadTargets value, Action command) {
      ++calls;
      submitted = std::move(value);
      QCOMPARE(command, Action::Start);
      delete window.data();
    });
    item->trigger();
    QVERIFY(window.isNull());
    QCOMPARE(calls, 1);
    QCOMPARE(submitted.size(), std::size_t{1});
    QCOMPARE(submitted.front().generation, quint64{1});
    QCoreApplication::processEvents();
    QCOMPARE(calls, 1);
  }
};
QTEST_MAIN(LibraryActionUiTests)
#include "library_action_ui_tests.moc"
