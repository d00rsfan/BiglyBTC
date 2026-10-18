#include "library_window.hpp"

#include <QAbstractItemModelTester>
#include <QAction>
#include <QApplication>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QTableView>
#include <QTabWidget>
#include <QToolBar>
#include <QTimer>
#include <QtTest>

#include <algorithm>
#include <limits>

namespace {
using namespace biglybtc;

std::shared_ptr<ui::LibraryPresentation> sample(quint64 revision = 1) {
  auto result = std::make_shared<ui::LibraryPresentation>();
  result->revision = revision;
  const QStringList names{QStringLiteral("Alpha[1]*.bin"), QStringLiteral("beta.bin"),
      QStringLiteral("Gamma.bin"), QStringLiteral("Delta complete.bin")};
  const QStringList states{QStringLiteral("Stopped"), QStringLiteral("Paused"),
      QStringLiteral("Downloading"), QStringLiteral("Stopped")};
  const qint64 completed[]{25, 10, 80, 100};
  for (int i = 0; i < 4; ++i) {
    const auto hash = QStringLiteral("%1").arg(i + 1, 40, 16, QChar('0')).toUpper();
    result->items.push_back({.key = hash, .hash = hash, .name = names[i], .state = states[i],
        .bytes = 100, .completed = completed[i], .received = completed[i], .sent = 0,
        .canContinue = i == 0 || i == 2});
  }
  return result;
}

QSortFilterProxyModel* proxy(QTableView& table) {
  return qobject_cast<QSortFilterProxyModel*>(table.model());
}

bool choose(ui::LibraryWindow& window, QTableView& table, int sourceRow) {
  auto* model = proxy(table);
  if (!model) { return false; }
  const auto index = model->mapFromSource(window.model().index(sourceRow, 0));
  if (!index.isValid()) { return false; }
  table.setFocus();
  QCoreApplication::processEvents();
  table.selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  QCoreApplication::processEvents();
  return table.currentIndex() == index && table.selectionModel()->isSelected(index);
}

std::optional<ui::Target> current(ui::LibraryWindow& window) {
  auto& table = window.table();
  auto* model = proxy(table);
  return model && table.currentIndex().isValid()
      ? window.model().targetAt(model->mapToSource(table.currentIndex()).row()) : std::nullopt;
}

ui::Details details(const ui::Target& target, const QString& name = QStringLiteral("selected.bin")) {
  auto files = std::make_shared<ui::FileDetails>();
  files->target = target;
  files->items.push_back({0, name, 100, 0, false, false});
  return {target, name, 100, files};
}

QString plain(QString text) { return text.remove(QChar('&')); }
} // namespace

class ClassicTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
  }

  void splitStructureAndSourceColumns() {
    ui::LibraryWindow window;
    QAbstractItemModelTester source(&window.model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    window.setPresentation(sample());
    auto& incomplete = window.incompleteTable();
    auto& complete = window.completeTable();
    QVERIFY(proxy(incomplete));
    QVERIFY(proxy(complete));
    QAbstractItemModelTester incompleteTester(incomplete.model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    QAbstractItemModelTester completeTester(complete.model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    QCOMPARE(proxy(incomplete)->sourceModel(), &window.model());
    QCOMPARE(proxy(complete)->sourceModel(), &window.model());
    QCOMPARE(incomplete.model()->rowCount(), 3);
    QCOMPARE(complete.model()->rowCount(), 1);
    QCOMPARE(window.model().rowCount(), 4);
    QCOMPARE(incomplete.selectionMode(), QAbstractItemView::ExtendedSelection);
    QCOMPARE(complete.selectionMode(), QAbstractItemView::ExtendedSelection);
    QCOMPARE(incomplete.selectionBehavior(), QAbstractItemView::SelectRows);
    QCOMPARE(complete.selectionBehavior(), QAbstractItemView::SelectRows);
    QCOMPARE(window.splitter().orientation(), Qt::Vertical);
    QCOMPARE(window.splitter().count(), 2);
    QCOMPARE(window.mainTabs().tabText(0), QStringLiteral("My Torrents"));
    QCOMPARE(window.detailTabs().tabText(0), QStringLiteral("General"));
    QCOMPARE(window.detailTabs().tabText(1), QStringLiteral("Files"));
    QCOMPARE(window.detailTabs().currentIndex(), 0);
    const QStringList headers{QStringLiteral("Name"), QStringLiteral("State"), QStringLiteral("Size (bytes)"),
        QStringLiteral("Progress"), QStringLiteral("Down (B/s)"), QStringLiteral("Up (B/s)"),
        QStringLiteral("Received (bytes)"), QStringLiteral("Sent (bytes)"), QStringLiteral("Hash"),
        QStringLiteral("Saved queue position"), QStringLiteral("Saved selected size (bytes)"),
        QStringLiteral("Saved selected remaining (bytes)"), QStringLiteral("Saved selected done")};
    QCOMPARE(window.model().columnCount(), headers.size());
    for (int column = 0; column < headers.size(); ++column) {
      QCOMPARE(window.model().headerData(column, Qt::Horizontal, Qt::DisplayRole).toString(), headers[column]);
    }
    for (int row = 0; row < 4; ++row) {
      const auto index = window.model().index(row, 0);
      QCOMPARE(proxy(incomplete)->mapFromSource(index).isValid(), row != 3);
      QCOMPARE(proxy(complete)->mapFromSource(index).isValid(), row == 3);
    }
  }

  void completionUsesKnownBytes_data() {
    QTest::addColumn<bool>("knownSize");
    QTest::addColumn<qint64>("size");
    QTest::addColumn<bool>("knownCompleted");
    QTest::addColumn<qint64>("completed");
    QTest::addColumn<QString>("state");
    QTest::addColumn<bool>("complete");
    QTest::newRow("zero-complete") << true << qint64{0} << true << qint64{0} << QStringLiteral("Stopped") << true;
    QTest::newRow("zero-unknown-progress") << true << qint64{0} << false << qint64{0} << QStringLiteral("Complete") << false;
    QTest::newRow("unknown-size") << false << qint64{0} << true << qint64{100} << QStringLiteral("Complete") << false;
    QTest::newRow("unknown-both") << false << qint64{0} << false << qint64{0} << QStringLiteral("Seeding") << false;
    QTest::newRow("unknown-progress") << true << qint64{100} << false << qint64{0} << QStringLiteral("Complete") << false;
    QTest::newRow("paused-partial") << true << qint64{100} << true << qint64{25} << QStringLiteral("Paused") << false;
    QTest::newRow("paused-complete") << true << qint64{100} << true << qint64{100} << QStringLiteral("Paused") << true;
    QTest::newRow("stopped-complete") << true << qint64{100} << true << qint64{100} << QStringLiteral("Stopped") << true;
    QTest::newRow("misleading-state") << true << qint64{100} << true << qint64{25} << QStringLiteral("Complete") << false;
    QTest::newRow("overshoot") << true << qint64{100} << true << qint64{101} << QStringLiteral("Downloading") << true;
    QTest::newRow("maximum-exact") << true << std::numeric_limits<qint64>::max() << true
        << std::numeric_limits<qint64>::max() << QStringLiteral("Stopped") << true;
  }

  void completionUsesKnownBytes() {
    QFETCH(bool, knownSize);
    QFETCH(qint64, size);
    QFETCH(bool, knownCompleted);
    QFETCH(qint64, completed);
    QFETCH(QString, state);
    QFETCH(bool, complete);
    ui::LibraryWindow window;
    auto value = sample();
    value->items.resize(1);
    value->items[0].bytes = knownSize ? std::optional<qint64>{size} : std::nullopt;
    value->items[0].completed = knownCompleted ? std::optional<qint64>{completed} : std::nullopt;
    value->items[0].state = state;
    window.setPresentation(value);
    QCOMPARE(window.model().rowCount(), 1);
    QCOMPARE(window.completeTable().model()->rowCount(), complete ? 1 : 0);
    QCOMPARE(window.incompleteTable().model()->rowCount(), complete ? 0 : 1);
  }

  void activePaneDetailsAndLateReply() {
    ui::LibraryWindow window;
    window.setPresentation(sample());
    window.show();
    QVERIFY(choose(window, window.incompleteTable(), 0));
    const auto first = window.model().targetAt(0).value();
    QSignalSpy requests(&window, &ui::LibraryWindow::detailsRequested);
    window.filesAction().trigger();
    QCOMPARE(window.detailTabs().currentIndex(), 1);
    QVERIFY(!requests.isEmpty());
    QVERIFY(qvariant_cast<ui::Target>(requests.last().at(0)) == first);
    window.setDetails(details(first, QStringLiteral("first.bin")));
    QCOMPARE(window.filesModel().rowCount(), 1);
    QVERIFY(choose(window, window.completeTable(), 3));
    const auto second = window.model().targetAt(3).value();
    QCOMPARE(&window.table(), &window.completeTable());
    QVERIFY(current(window) == second);
    QCOMPARE(window.incompleteTable().selectionModel()->selectedRows().size(), 0);
    QCOMPARE(window.filesModel().rowCount(), 0);
    QVERIFY(qvariant_cast<ui::Target>(requests.last().at(0)) == second);
    window.setDetails(details(first, QStringLiteral("late.bin")));
    QCOMPARE(window.filesModel().rowCount(), 0);
    window.setDetails(details(second, QStringLiteral("second.bin")));
    QCOMPARE(window.filesModel().rowCount(), 1);
    QCOMPARE(window.filesModel().data(window.filesModel().index(0, 0), Qt::DisplayRole).toString(), QStringLiteral("second.bin"));
    QVERIFY(choose(window, window.incompleteTable(), 2));
    const auto third = window.model().targetAt(2).value();
    QVERIFY(current(window) == third);
    QCOMPARE(window.completeTable().selectionModel()->selectedRows().size(), 0);
    QSignalSpy continuations(&window, &ui::LibraryWindow::continueRequested);
    QVERIFY(window.continueAction().isEnabled());
    window.continueAction().trigger();
    QCOMPARE(continuations.count(), 1);
    QVERIFY(qvariant_cast<ui::Target>(continuations.at(0).at(0)) == third);
  }

  void extendedSelectionDisablesSingleTargetActions() {
    ui::LibraryWindow window;
    window.setPresentation(sample());
    window.show();
    auto& table = window.incompleteTable();
    QVERIFY(choose(window, table, 0));
    const auto first = window.model().targetAt(0).value();
    window.filesAction().trigger();
    window.setDetails(details(first));
    QCOMPARE(window.filesModel().rowCount(), 1);
    const auto other = proxy(table)->mapFromSource(window.model().index(2, 0));
    table.selectionModel()->select(other, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    QCOMPARE(table.selectionModel()->selectedRows().size(), 2);
    QVERIFY(!window.continueAction().isEnabled());
    QVERIFY(!window.filesAction().isEnabled());
    QCOMPARE(window.filesModel().rowCount(), 0);
    QVERIFY(!window.filesModel().target().has_value());
    QSignalSpy continuations(&window, &ui::LibraryWindow::continueRequested);
    QSignalSpy requests(&window, &ui::LibraryWindow::detailsRequested);
    window.continueAction().trigger();
    window.filesAction().trigger();
    QCOMPARE(continuations.count(), 0);
    QCOMPARE(requests.count(), 0);
    window.setDetails(details(first, QStringLiteral("late-multiple.bin")));
    QCOMPARE(window.filesModel().rowCount(), 0);
    QVERIFY(choose(window, table, 2));
    QVERIFY(window.continueAction().isEnabled());
    QVERIFY(window.filesAction().isEnabled());
    table.selectionModel()->clearSelection();
    QVERIFY(table.currentIndex().isValid());
    QVERIFY(!window.continueAction().isEnabled());
    QVERIFY(!window.filesAction().isEnabled());
  }

  void literalCaseInsensitiveNameFilter() {
    ui::LibraryWindow window;
    window.setPresentation(sample());
    window.show();
    QVERIFY(choose(window, window.incompleteTable(), 0));
    const auto first = window.model().targetAt(0).value();
    window.filesAction().trigger();
    window.setDetails(details(first, QStringLiteral("Alpha[1]*.bin")));
    window.filter().setText(QStringLiteral("ALPHA[1]*"));
    QCOMPARE(window.incompleteTable().model()->rowCount(), 1);
    QCOMPARE(window.completeTable().model()->rowCount(), 0);
    QVERIFY(proxy(window.incompleteTable())->mapFromSource(window.model().index(0, 0)).isValid());
    window.filter().setText(QStringLiteral("beta"));
    QCOMPARE(window.incompleteTable().model()->rowCount(), 1);
    QCOMPARE(window.filesModel().rowCount(), 0);
    QVERIFY(!window.filesAction().isEnabled());
    QVERIFY(!window.continueAction().isEnabled());
    QSignalSpy requests(&window, &ui::LibraryWindow::detailsRequested);
    QSignalSpy continuations(&window, &ui::LibraryWindow::continueRequested);
    window.setDetails(details(first, QStringLiteral("Alpha[1]*.bin")));
    window.filesAction().trigger();
    window.continueAction().trigger();
    QCOMPARE(window.filesModel().rowCount(), 0);
    QCOMPARE(requests.count(), 0);
    QCOMPARE(continuations.count(), 0);
    window.filter().setText(QStringLiteral("Paused"));
    QCOMPARE(window.incompleteTable().model()->rowCount(), 0);
    QCOMPARE(window.completeTable().model()->rowCount(), 0);
    window.filter().clear();
    QCOMPARE(window.incompleteTable().model()->rowCount(), 3);
    QCOMPARE(window.completeTable().model()->rowCount(), 1);
    QVERIFY(!window.filesAction().isEnabled());
  }

  void revisionRefreshPreservesHashAndRejectsLateResults() {
    ui::LibraryWindow window;
    window.setPresentation(sample());
    window.show();
    QVERIFY(choose(window, window.incompleteTable(), 2));
    const auto old = window.model().targetAt(2).value();
    window.filesAction().trigger();
    window.setDetails(details(old, QStringLiteral("Gamma.bin")));
    auto next = sample(2);
    std::reverse(next->items.begin(), next->items.end());
    window.setPresentation(next);
    const auto selected = current(window);
    QVERIFY(selected.has_value());
    QCOMPARE(selected->hash, old.hash);
    QCOMPARE(selected->generation, quint64{2});
    QCOMPARE(window.table().selectionModel()->selectedRows().size(), 1);
    QCOMPARE(window.filesModel().rowCount(), 0);
    window.setDetails(details(old, QStringLiteral("old-revision.bin")));
    QCOMPARE(window.filesModel().rowCount(), 0);
    window.setTelemetry({old, 100, 0, 0, QStringLiteral("stale-complete")});
    QCOMPARE(window.incompleteTable().model()->rowCount(), 3);
    QCOMPARE(window.completeTable().model()->rowCount(), 1);
    window.setDetails(details(*selected, QStringLiteral("current.bin")));
    QCOMPARE(window.filesModel().rowCount(), 1);
    QCOMPARE(window.filesModel().data(window.filesModel().index(0, 0), Qt::DisplayRole).toString(), QStringLiteral("current.bin"));
  }

  void telemetryMovesSelectedTorrentBetweenPanes() {
    ui::LibraryWindow window;
    window.setPresentation(sample());
    window.show();
    QVERIFY(choose(window, window.incompleteTable(), 2));
    const auto target = window.model().targetAt(2).value();
    QSignalSpy resets(&window.model(), &QAbstractItemModel::modelReset);
    window.setTelemetry({target, 100, 0, 10, QStringLiteral("Seeding")});
    QCOMPARE(resets.count(), 0);
    QCOMPARE(window.incompleteTable().model()->rowCount(), 2);
    QCOMPARE(window.completeTable().model()->rowCount(), 2);
    QCOMPARE(&window.table(), &window.completeTable());
    QVERIFY(current(window) == target);
    QCOMPARE(window.completeTable().selectionModel()->selectedRows().size(), 1);
    QCOMPARE(window.incompleteTable().selectionModel()->selectedRows().size(), 0);
    window.setTelemetry({target, 99, 5, 0, QStringLiteral("Checking")});
    QCOMPARE(window.incompleteTable().model()->rowCount(), 3);
    QCOMPARE(window.completeTable().model()->rowCount(), 1);
    QCOMPARE(&window.table(), &window.incompleteTable());
    QVERIFY(current(window) == target);
  }

  void revisionMovesMembershipWithoutLosingIdentity() {
    ui::LibraryWindow window;
    window.setPresentation(sample());
    window.show();
    QVERIFY(choose(window, window.incompleteTable(), 0));
    const auto old = window.model().targetAt(0).value();
    auto next = sample(2);
    next->items[0].completed = 100;
    next->items[0].state = QStringLiteral("Paused");
    window.setPresentation(next);
    QCOMPARE(&window.table(), &window.completeTable());
    const auto selected = current(window);
    QVERIFY(selected.has_value());
    QCOMPARE(selected->hash, old.hash);
    QCOMPARE(selected->generation, quint64{2});
    QCOMPARE(window.incompleteTable().model()->rowCount(), 2);
    QCOMPARE(window.completeTable().model()->rowCount(), 2);
  }

  void busySelectionRequestsCurrentTargetAfterCompletion() {
    ui::LibraryWindow window;
    window.setPresentation(sample());
    window.show();
    QVERIFY(choose(window, window.incompleteTable(), 0));
    const auto old = window.model().targetAt(0).value();
    window.filesAction().trigger();
    window.setBusy(true);
    QSignalSpy requests(&window, &ui::LibraryWindow::detailsRequested);
    QVERIFY(choose(window, window.completeTable(), 3));
    const auto now = window.model().targetAt(3).value();
    QCOMPARE(requests.count(), 0);
    QVERIFY(!window.filesAction().isEnabled());
    window.setDetails(details(old, QStringLiteral("old-worker-result.bin")));
    QCOMPARE(window.filesModel().rowCount(), 0);
    window.setBusy(false);
    QTRY_COMPARE(requests.count(), 1);
    QVERIFY(qvariant_cast<ui::Target>(requests.first().at(0)) == now);
    window.setDetails(details(now, QStringLiteral("current-worker-result.bin")));
    QCOMPARE(window.filesModel().rowCount(), 1);
    QVERIFY(window.filesModel().target() == now);
  }

  void blankContextClearsOldTargetAndRejectsLateDetails() {
    ui::LibraryWindow window;
    window.setPresentation(sample());
    window.show();
    auto& table = window.incompleteTable();
    QVERIFY(choose(window, table, 0));
    const auto old = window.model().targetAt(0).value();
    window.filesAction().trigger();
    window.setDetails(details(old, QStringLiteral("selected-before-blank.bin")));
    QCOMPARE(window.filesModel().rowCount(), 1);
    QVERIFY(window.continueAction().isEnabled());
    const QPoint blank(10, table.viewport()->height() - 4);
    QVERIFY(table.viewport()->rect().contains(blank));
    QVERIFY(!table.indexAt(blank).isValid());
    bool inspected = false;
    bool noTarget = false;
    bool disabled = false;
    QSignalSpy continuations(&window, &ui::LibraryWindow::continueRequested);
    QSignalSpy requests(&window, &ui::LibraryWindow::detailsRequested);
    QTimer::singleShot(0, &window, [&] {
      auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
      if (!menu) { return; }
      inspected = true;
      noTarget = table.selectionModel()->selectedRows().isEmpty() && !table.currentIndex().isValid();
      disabled = !window.continueAction().isEnabled() && !window.filesAction().isEnabled();
      window.continueAction().trigger();
      window.filesAction().trigger();
      menu->close();
    });
    QVERIFY(QMetaObject::invokeMethod(&table, "customContextMenuRequested", Qt::DirectConnection, Q_ARG(QPoint, blank)));
    QVERIFY(inspected);
    QVERIFY(noTarget);
    QVERIFY(disabled);
    QCOMPARE(continuations.count(), 0);
    QCOMPARE(requests.count(), 0);
    QCOMPARE(window.filesModel().rowCount(), 0);
    QVERIFY(!window.filesModel().target().has_value());
    window.setDetails(details(old, QStringLiteral("late-after-blank.bin")));
    QCOMPARE(window.filesModel().rowCount(), 0);
    QVERIFY(!window.filesAction().isEnabled());
    QVERIFY(!window.continueAction().isEnabled());
    QCOMPARE(table.selectionModel()->selectedRows().size(), 0);
  }

  void generalUnknownSelectionRequestsMetadataAfterBusy() {
    ui::LibraryWindow window;
    auto value = sample();
    value->items[2].bytes.reset();
    value->items[2].completed.reset();
    window.setPresentation(value);
    window.show();
    QVERIFY(choose(window, window.incompleteTable(), 0));
    QCOMPARE(window.detailTabs().currentIndex(), 0);
    QSignalSpy requests(&window, &ui::LibraryWindow::detailsRequested);
    window.setBusy(true);
    QVERIFY(choose(window, window.incompleteTable(), 2));
    const auto target = window.model().targetAt(2).value();
    QVERIFY(!window.model().hasDetails(target));
    QVERIFY(!window.model().data(window.model().index(2, 2), Qt::UserRole).isValid());
    QCOMPARE(window.detailTabs().currentIndex(), 0);
    QCOMPARE(requests.count(), 0);
    window.setBusy(false);
    QTRY_COMPARE(requests.count(), 1);
    QVERIFY(qvariant_cast<ui::Target>(requests.first().at(0)) == target);
    QCOMPARE(window.detailTabs().currentIndex(), 0);
    window.setDetails(details(target, QStringLiteral("resolved-after-busy.bin")));
    QVERIFY(window.model().hasDetails(target));
    QCOMPARE(window.model().data(window.model().index(2, 2), Qt::UserRole).toLongLong(), qint64{100});
    window.setBusy(false);
    QCoreApplication::processEvents();
    QCOMPARE(requests.count(), 1);
  }

  void contextUsesSharedActionsAndRejectsHiddenTarget() {
    ui::LibraryWindow window;
    window.setPresentation(sample());
    window.show();
    auto& table = window.incompleteTable();
    QVERIFY(choose(window, table, 0));
    const auto point = table.visualRect(table.currentIndex()).center();
    QVERIFY(table.viewport()->rect().contains(point));
    bool inspected = false;
    bool shared = false;
    bool disabled = false;
    QSignalSpy continuations(&window, &ui::LibraryWindow::continueRequested);
    QSignalSpy requests(&window, &ui::LibraryWindow::detailsRequested);
    QTimer::singleShot(0, &window, [&] {
      auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
      if (!menu) { return; }
      inspected = true;
      shared = menu->actions().contains(&window.continueAction()) && menu->actions().contains(&window.filesAction());
      window.filter().setText(QStringLiteral("beta"));
      disabled = !window.continueAction().isEnabled() && !window.filesAction().isEnabled();
      window.continueAction().trigger();
      window.filesAction().trigger();
      menu->close();
    });
    QVERIFY(QMetaObject::invokeMethod(&table, "customContextMenuRequested", Qt::DirectConnection, Q_ARG(QPoint, point)));
    QVERIFY(inspected);
    QVERIFY(shared);
    QVERIFY(disabled);
    QCOMPARE(continuations.count(), 0);
    QCOMPARE(requests.count(), 0);
  }

  void classicMenusAndOfflineActions() {
    ui::LibraryWindow window;
    window.setPresentation(sample());
    window.show();
    const QStringList expected{QStringLiteral("File"), QStringLiteral("Transfers"), QStringLiteral("Torrent"),
        QStringLiteral("View"), QStringLiteral("Tools"), QStringLiteral("Plugins"),
        QStringLiteral("Community"), QStringLiteral("Help")};
    QStringList menus;
    for (auto* action : window.menuBar()->actions()) { menus.push_back(plain(action->text())); }
    QCOMPARE(menus, expected);
    const QStringList offline{QStringLiteral("classic-open"), QStringLiteral("classic-search"),
        QStringLiteral("classic-launch"), QStringLiteral("classic-top"), QStringLiteral("classic-up"),
        QStringLiteral("classic-down"), QStringLiteral("classic-bottom"), QStringLiteral("classic-start-stop"),
        QStringLiteral("classic-remove")};
    for (const auto& name : offline) {
      auto* action = window.findChild<QAction*>(name);
      QVERIFY2(action, qPrintable(name));
      QVERIFY2(!action->isEnabled(), qPrintable(name));
      QVERIFY2(!action->toolTip().isEmpty(), qPrintable(name));
    }
    const auto menuContains = [&](QAction* wanted) {
      for (auto* menu : window.findChildren<QMenu*>()) {
        if (menu->actions().contains(wanted)) { return true; }
      }
      return false;
    };
    const auto toolbarContains = [&](QAction* wanted) {
      for (auto* toolbar : window.findChildren<QToolBar*>()) {
        if (toolbar->actions().contains(wanted)) { return true; }
      }
      return false;
    };
    QVERIFY(menuContains(&window.continueAction()));
    QVERIFY(menuContains(&window.cancelAction()));
    QVERIFY(menuContains(&window.filesAction()));
    QVERIFY(toolbarContains(&window.continueAction()));
    QVERIFY(toolbarContains(&window.cancelAction()));
    QVERIFY(toolbarContains(&window.filesAction()));
    QVERIFY(choose(window, window.incompleteTable(), 0));
    QVERIFY(window.continueAction().isEnabled());
    window.setBusy(true);
    QVERIFY(!window.continueAction().isEnabled());
    QVERIFY(!window.filesAction().isEnabled());
    QVERIFY(window.cancelAction().isEnabled());
    window.setBusy(false);
    QVERIFY(window.continueAction().isEnabled());
    QVERIFY(!window.cancelAction().isEnabled());
    for (const auto& name : offline) { QVERIFY(!window.findChild<QAction*>(name)->isEnabled()); }
  }
};

QTEST_MAIN(ClassicTests)
#include "classic_tests.moc"
