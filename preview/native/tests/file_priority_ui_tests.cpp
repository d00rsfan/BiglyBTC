#include "library_window.hpp"

#include <QAction>
#include <QApplication>
#include <QItemSelectionModel>
#include <QMenu>
#include <QPointer>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTabWidget>
#include <QTimer>
#include <QtTest>

#include <initializer_list>
#include <memory>
#include <utility>

namespace {
using namespace biglybtc;
std::shared_ptr<ui::LibraryPresentation> library(quint64 revision = 1) {
  auto result = std::make_shared<ui::LibraryPresentation>();
  result->revision = revision;
  result->items.push_back({.key = QStringLiteral("generated"), .hash = QString(40, QLatin1Char('A')),
      .name = QStringLiteral("generated bundle"), .state = QStringLiteral("Stopped"), .bytes = 100, .completed = 0});
  return result;
}
std::shared_ptr<ui::FileDetails> files(const ui::Target& target, bool canEdit = true) {
  auto result = std::make_shared<ui::FileDetails>();
  result->target = target;
  // Deliberately unsorted paths and non-contiguous original IDs.
  result->items = {{7, QStringLiteral("zeta.bin"), 40, 0, false, false},
      {2, QStringLiteral("alpha.bin"), 50, 1, false, false},
      {9, QStringLiteral(".pad/3"), 10, 0, true, true}};
  result->canEdit = canEdit;
  return result;
}
ui::Target prepare(ui::LibraryWindow& window, bool canEdit = true) {
  window.setPresentation(library());
  const auto target = window.model().targetAt(0).value();
  window.setDetails({target, QStringLiteral("generated bundle"), 100, files(target, canEdit)});
  window.show();
  window.detailTabs().setCurrentIndex(1);
  QCoreApplication::processEvents();
  return target;
}
bool selectFiles(ui::LibraryWindow& window, std::initializer_list<int> originals) {
  auto& table = window.filesTable();
  auto* proxy = qobject_cast<QSortFilterProxyModel*>(table.model());
  if (!proxy || originals.size() == 0) { return false; }
  table.selectionModel()->clearSelection();
  bool first = true;
  for (const int original : originals) {
    const auto index = proxy->mapFromSource(window.filesModel().index(window.filesModel().rowFor(original), 0));
    if (!index.isValid()) { return false; }
    table.selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    if (first) { table.selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate); first = false; }
  }
  QCoreApplication::processEvents();
  return table.selectionModel()->selectedRows().size() == static_cast<qsizetype>(originals.size());
}
QAction* action(ui::LibraryWindow& window, const char* id) { return window.findChild<QAction*>(QString::fromLatin1(id)); }
bool forced(QAction* item) {
  return item && QMetaObject::invokeMethod(item, "triggered", Qt::DirectConnection, Q_ARG(bool, false));
}
} // namespace

class FilePriorityUiTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
    qRegisterMetaType<ui::FileIndices>();
  }
  void selectedOriginalIndicesRoute_data() {
    QTest::addColumn<QByteArray>("id"); QTest::addColumn<int>("priority"); QTest::addColumn<bool>("skipped");
    QTest::newRow("normal") << QByteArray("files-priority-normal") << 0 << false;
    QTest::newRow("high") << QByteArray("files-priority-high") << 1 << false;
    QTest::newRow("low") << QByteArray("files-priority-low") << -1 << false;
    QTest::newRow("skip") << QByteArray("files-priority-skip") << 0 << true;
  }
  void selectedOriginalIndicesRoute() {
    QFETCH(QByteArray, id); QFETCH(int, priority); QFETCH(bool, skipped);
    ui::LibraryWindow window;
    const auto target = prepare(window);
    QCOMPARE(window.filesTable().selectionMode(), QAbstractItemView::ExtendedSelection);
    QVERIFY(selectFiles(window, {7, 2}));
    auto* item = action(window, id.constData()); QVERIFY(item);
    QTRY_VERIFY(item->isEnabled());
    QSignalSpy submitted(&window, &ui::LibraryWindow::filePolicyRequested);
    bool shared = false;
    QTimer::singleShot(0, &window, [&] {
      auto* menu = window.findChild<QMenu*>(QStringLiteral("files-priority-context"));
      if (!menu) { return; }
      QCoreApplication::processEvents();
      shared = menu->actions().contains(item) && item->isEnabled();
      if (shared) { item->trigger(); }
      menu->close();
    });
    auto& table = window.filesTable();
    const auto point = table.visualRect(table.currentIndex()).center();
    QVERIFY(QMetaObject::invokeMethod(&table, "customContextMenuRequested", Qt::DirectConnection, Q_ARG(QPoint, point)));
    QVERIFY(shared);
    QCOMPARE(submitted.count(), 1);
    QVERIFY(qvariant_cast<ui::Target>(submitted.first().at(0)) == target);
    QVERIFY(qvariant_cast<ui::FileIndices>(submitted.first().at(1)) == ui::FileIndices({2, 7}));
    QCOMPARE(submitted.first().at(2).toInt(), priority);
    QCOMPARE(submitted.first().at(3).toBool(), skipped);
    QCOMPARE(window.filesModel().rowCount(), 3); // Command intent never edits the local facts.
    QCOMPARE(window.filesModel().data(window.filesModel().index(0, 2), Qt::UserRole).toInt(), 0);
  }
  void explicitReadOnlyAndAnySelectedPaddingRejectWholeSubmission() {
    ui::LibraryWindow window;
    const auto target = prepare(window, false);
    QVERIFY(selectFiles(window, {2}));
    auto* high = action(window, "files-priority-high"); QVERIFY(high);
    QTRY_VERIFY(!high->isEnabled());
    QSignalSpy submitted(&window, &ui::LibraryWindow::filePolicyRequested);
    QVERIFY(forced(high)); QCOMPARE(submitted.count(), 0);
    window.setDetails({target, QStringLiteral("generated bundle"), 100, files(target)});
    QVERIFY(selectFiles(window, {2}));
    QTRY_VERIFY(high->isEnabled());
    window.paddingAction().setChecked(true);
    QVERIFY(selectFiles(window, {2, 9}));
    QVERIFY(!high->isEnabled());
    QVERIFY(forced(high)); QCOMPARE(submitted.count(), 0);
    QVERIFY(!window.filesModel().editableAt(window.filesModel().rowFor(9)));
    QVERIFY(!window.filesModel().editableAt(-1));
  }
  void busyOfflineAndNewLibraryGenerationInvalidateOldDetails() {
    ui::LibraryWindow window;
    const auto old = prepare(window);
    QVERIFY(selectFiles(window, {7}));
    auto* normal = action(window, "files-priority-normal"); QVERIFY(normal);
    QTRY_VERIFY(normal->isEnabled());
    QSignalSpy submitted(&window, &ui::LibraryWindow::filePolicyRequested);
    window.setBusy(true); QVERIFY(forced(normal)); QCOMPARE(submitted.count(), 0);
    window.setBusy(false); QCoreApplication::processEvents();
    window.setOfflinePreview(true); QVERIFY(forced(normal)); QCOMPARE(submitted.count(), 0);
    window.setOfflinePreview(false); QCoreApplication::processEvents();
    window.setPresentation(library(2));
    QVERIFY(forced(normal)); QCOMPARE(submitted.count(), 0);
    window.setDetails({old, QStringLiteral("stale"), 100, files(old)});
    QVERIFY(!window.filesModel().presentation());
    const auto current = window.model().targetAt(0).value();
    window.setDetails({current, QStringLiteral("fresh"), 100, files(current)});
    QVERIFY(selectFiles(window, {7}));
    QTRY_VERIFY(normal->isEnabled());
    normal->trigger(); QCOMPARE(submitted.count(), 1);
    QCOMPARE(qvariant_cast<ui::Target>(submitted.first().at(0)).generation, quint64{2});
  }
  void contextMenuRejectsReplacedSameTargetFrame() {
    ui::LibraryWindow window;
    const auto target = prepare(window);
    QVERIFY(selectFiles(window, {2, 7}));
    QSignalSpy submitted(&window, &ui::LibraryWindow::filePolicyRequested);
    bool shared = false;
    QTimer::singleShot(0, &window, [&] {
      auto* menu = window.findChild<QMenu*>(QStringLiteral("files-priority-context"));
      if (!menu) { return; }
      auto* high = action(window, "files-priority-high");
      shared = high && menu->actions().contains(high);
      window.setDetails({target, QStringLiteral("new same-target details"), 100, files(target)});
      QCoreApplication::processEvents();
      forced(high);
      menu->close();
    });
    auto& table = window.filesTable();
    const auto point = table.visualRect(table.currentIndex()).center();
    QVERIFY(QMetaObject::invokeMethod(&table, "customContextMenuRequested", Qt::DirectConnection, Q_ARG(QPoint, point)));
    QVERIFY(shared); QCOMPARE(submitted.count(), 0);
  }
  void fileActionChangedMayRebindOrDeleteOwner() {
    ui::LibraryWindow window;
    const auto target = prepare(window);
    auto* high = action(window, "files-priority-high"); QVERIFY(high);
    window.filesTable().selectionModel()->clearSelection();
    QCoreApplication::processEvents();
    QTRY_VERIFY(!high->isEnabled());
    bool rebound = false;
    connect(high, &QAction::changed, &window, [&] {
      if (!rebound && high->isEnabled()) {
        rebound = true;
        window.setDetails({target, QStringLiteral("readonly replacement"), 100, files(target, false)});
      }
    });
    QVERIFY(selectFiles(window, {7}));
    QTRY_VERIFY(rebound && !high->isEnabled());
    QSignalSpy submitted(&window, &ui::LibraryWindow::filePolicyRequested);
    QVERIFY(forced(high)); QCOMPARE(submitted.count(), 0);
    auto doomedOwner = std::make_unique<ui::LibraryWindow>();
    QPointer<ui::LibraryWindow> doomed = doomedOwner.get();
    prepare(*doomed);
    auto* doomedHigh = action(*doomed, "files-priority-high"); QVERIFY(doomedHigh);
    doomed->filesTable().selectionModel()->clearSelection();
    QCoreApplication::processEvents();
    QTRY_VERIFY(!doomedHigh->isEnabled());
    connect(doomedHigh, &QAction::changed, doomed, [doomed, doomedHigh, &doomedOwner] {
      if (doomed && doomedHigh->isEnabled()) { doomedOwner.reset(); }
    });
    // Selection itself publishes no QAction changes; the queued entry is the
    // only place that may invoke this deletion callback.
    auto& table = doomed->filesTable();
    auto* proxy = qobject_cast<QSortFilterProxyModel*>(table.model()); QVERIFY(proxy);
    const auto index = proxy->mapFromSource(doomed->filesModel().index(doomed->filesModel().rowFor(7), 0));
    QVERIFY(index.isValid());
    table.selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QTRY_VERIFY(doomed.isNull());
  }
  void fileDispatchBlocksNestedCommandsAndAllowsDeletion() {
    auto owner = std::make_unique<ui::LibraryWindow>();
    QPointer<ui::LibraryWindow> window = owner.get();
    prepare(*window);
    QVERIFY(selectFiles(*window, {2, 7}));
    auto* high = action(*window, "files-priority-high");
    auto* low = action(*window, "files-priority-low"); QVERIFY(high && low);
    QTRY_VERIFY(high->isEnabled() && low->isEnabled());
    int calls = 0;
    ui::FileIndices actual;
    int actualPriority = 0; bool actualSkipped = true;
    connect(window.data(), &ui::LibraryWindow::filePolicyRequested, this,
        [&](ui::Target, ui::FileIndices indices, int priority, bool skipped) {
      ++calls; actual = std::move(indices); actualPriority = priority; actualSkipped = skipped;
      low->trigger();
      owner.reset();
    });
    high->trigger();
    QVERIFY(window.isNull()); QCOMPARE(calls, 1);
    QVERIFY(actual == ui::FileIndices({2, 7}));
    QCOMPARE(actualPriority, 1); QVERIFY(!actualSkipped);
  }
};
QTEST_MAIN(FilePriorityUiTests)
#include "file_priority_ui_tests.moc"
