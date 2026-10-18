#include "library_window.hpp"

#include <QAbstractItemModelTester>
#include <QAction>
#include <QApplication>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTabBar>
#include <QTabWidget>
#include <QtTest>

#include <algorithm>

namespace {
using namespace biglybtc;

std::shared_ptr<ui::LibraryPresentation> sample(quint64 revision = 1) {
  auto value = std::make_shared<ui::LibraryPresentation>();
  value->revision = revision;
  const QStringList names{QStringLiteral("Alpha.bin"), QStringLiteral("Beta.bin"), QStringLiteral("Complete.bin")};
  for (int row = 0; row < names.size(); ++row) {
    const auto hash = QStringLiteral("%1").arg(row + 1, 40, 16, QChar('0')).toUpper();
    value->items.push_back({.key = hash, .hash = hash, .name = names[row],
        .state = row == 2 ? QStringLiteral("Stopped") : QStringLiteral("Queued"),
        .bytes = 100, .completed = row == 2 ? 100 : 25, .received = 100, .sent = 0, .canContinue = row != 2});
  }
  return value;
}

bool choose(ui::LibraryWindow& window, QTableView& table, int sourceRow) {
  auto* proxy = qobject_cast<QSortFilterProxyModel*>(table.model());
  if (!proxy) { return false; }
  const auto index = proxy->mapFromSource(window.model().index(sourceRow, 0));
  if (!index.isValid()) { return false; }
  table.setFocus();
  QCoreApplication::processEvents();
  table.selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  QCoreApplication::processEvents();
  return table.currentIndex() == index && table.selectionModel()->isSelected(index);
}

std::optional<ui::Target> selected(ui::LibraryWindow& window) {
  auto& table = window.table();
  auto* proxy = qobject_cast<QSortFilterProxyModel*>(table.model());
  if (!proxy || table.selectionModel()->selectedRows().size() != 1 || !table.currentIndex().isValid()) { return std::nullopt; }
  return window.model().targetAt(proxy->mapToSource(table.currentIndex()).row());
}

void supplyFiles(ui::LibraryWindow& window, const ui::Target& target, const QString& name) {
  auto files = std::make_shared<ui::FileDetails>();
  files->target = target;
  files->items.push_back({0, QStringLiteral("payload.bin"), 100, 0, false, false});
  window.setDetails({target, name, 100, files});
}

QAction* navigation(ui::LibraryWindow& window) {
  return window.findChild<QAction*>(QStringLiteral("classic-my-torrents"));
}

QWidget* torrentsPage(ui::LibraryWindow& window) {
  return window.findChild<QWidget*>(QStringLiteral("classic-my-torrents-page"));
}
} // namespace

class NavigationTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
  }

  void viewMenuRegistersOneWindowShortcut() {
    ui::LibraryWindow window;
    auto* action = navigation(window);
    auto* page = torrentsPage(window);
    QVERIFY(action);
    QVERIFY(page);
    QCOMPARE(window.findChildren<QAction*>(QStringLiteral("classic-my-torrents")).size(), 1);
    QCOMPARE(window.findChildren<QWidget*>(QStringLiteral("classic-my-torrents-page")).size(), 1);
    QVERIFY(action->isEnabled());
    QCOMPARE(action->shortcut(), QKeySequence(QStringLiteral("Ctrl+1")));
    QCOMPARE(action->shortcutContext(), Qt::WindowShortcut);
    QCOMPARE(action->text().remove(QChar('&')), QStringLiteral("My Torrents"));
    QMenu* view = nullptr;
    for (auto* top : window.menuBar()->actions()) {
      if (top->text().remove(QChar('&')) == QStringLiteral("View")) { view = top->menu(); }
    }
    QVERIFY(view);
    QVERIFY(!view->actions().isEmpty());
    QCOMPARE(view->actions().first(), action);
    QCOMPARE(window.mainTabs().currentWidget(), page);
    QCOMPARE(window.mainTabs().tabText(window.mainTabs().indexOf(page)), QStringLiteral("My Torrents"));
    QVERIFY(!window.mainTabs().tabsClosable());
    for (int number = 2; number <= 3; ++number) {
      auto* unavailable = window.findChild<QAction*>(number == 2 ? QStringLiteral("classic-my-tracker") : QStringLiteral("classic-my-shares"));
      QVERIFY(unavailable);
      QVERIFY(!unavailable->isEnabled());
      QCOMPARE(unavailable->shortcut(), QKeySequence(QStringLiteral("Ctrl+%1").arg(number)));
    }
  }

  void realShortcutFromChildFocus_data() {
    QTest::addColumn<QString>("origin");
    QTest::newRow("filter") << QStringLiteral("filter");
    QTest::newRow("files") << QStringLiteral("files");
    QTest::newRow("incomplete-table") << QStringLiteral("incomplete");
    QTest::newRow("complete-table") << QStringLiteral("complete");
  }

  void realShortcutFromChildFocus() {
    QFETCH(QString, origin);
    ui::LibraryWindow window;
    window.setPresentation(sample());
    window.show();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());
    auto* action = navigation(window);
    auto* page = torrentsPage(window);
    QVERIFY(action);
    QVERIFY(page);
    auto& pane = origin == QStringLiteral("complete") ? window.completeTable() : window.incompleteTable();
    const int row = origin == QStringLiteral("complete") ? 2 : 1;
    QVERIFY(choose(window, pane, row));
    const auto target = window.model().targetAt(row).value();
    window.filesAction().trigger();
    supplyFiles(window, target, row == 2 ? QStringLiteral("Complete.bin") : QStringLiteral("Beta.bin"));
    window.filter().setText(QStringLiteral(".BIN"));
    QWidget* focus = &pane;
    if (origin == QStringLiteral("filter")) { focus = &window.filter(); }
    else if (origin == QStringLiteral("files")) { focus = &window.filesTable(); }
    focus->setFocus();
    QTRY_VERIFY(focus->hasFocus());
    QSignalSpy triggered(action, &QAction::triggered);
    QSignalSpy resets(&window.model(), &QAbstractItemModel::modelReset);
    QSignalSpy refreshes(&window, &ui::LibraryWindow::refreshRequested);
    QSignalSpy continuations(&window, &ui::LibraryWindow::continueRequested);
    QSignalSpy cancellations(&window, &ui::LibraryWindow::cancelRequested);
    QSignalSpy requests(&window, &ui::LibraryWindow::detailsRequested);
    auto* source = &window.model();
    auto* incomplete = window.incompleteTable().model();
    auto* complete = window.completeTable().model();
    const auto count = window.mainTabs().count();
    QTest::keyClick(focus, Qt::Key_1, Qt::ControlModifier);
    QTRY_COMPARE(triggered.count(), 1);
    QCOMPARE(window.mainTabs().currentWidget(), page);
    QTRY_VERIFY(pane.hasFocus());
    QVERIFY(selected(window) == target);
    auto* alternate = new QLineEdit(&window.mainTabs());
    window.mainTabs().addTab(alternate, QStringLiteral("Test secondary page"));
    window.mainTabs().setCurrentWidget(alternate);
    alternate->setFocus();
    QTRY_VERIFY(alternate->hasFocus());
    QTest::keyClick(alternate, Qt::Key_1, Qt::ControlModifier);
    QTRY_COMPARE(triggered.count(), 2);
    QCOMPARE(window.mainTabs().currentWidget(), page);
    QTRY_VERIFY(pane.hasFocus());
    QCOMPARE(window.mainTabs().count(), count + 1);
    QCOMPARE(&window.model(), source);
    QCOMPARE(window.incompleteTable().model(), incomplete);
    QCOMPARE(window.completeTable().model(), complete);
    QCOMPARE(resets.count(), 0);
    QCOMPARE(window.model().rowCount(), 3);
    QCOMPARE(window.incompleteTable().model()->rowCount(), 2);
    QCOMPARE(window.completeTable().model()->rowCount(), 1);
    QCOMPARE(window.filter().text(), QStringLiteral(".BIN"));
    QCOMPARE(window.detailTabs().currentIndex(), 1);
    QCOMPARE(window.filesModel().rowCount(), 1);
    QVERIFY(window.filesModel().target() == target);
    QVERIFY(selected(window) == target);
    QCOMPARE(refreshes.count(), 0);
    QCOMPARE(continuations.count(), 0);
    QCOMPARE(cancellations.count(), 0);
    QCOMPARE(requests.count(), 0);
  }

  void repeatedMenuActivationUsesWidgetIdentity() {
    ui::LibraryWindow window;
    window.setPresentation(sample());
    window.show();
    QVERIFY(choose(window, window.incompleteTable(), 1));
    const auto target = window.model().targetAt(1).value();
    window.filesAction().trigger();
    supplyFiles(window, target, QStringLiteral("Beta.bin"));
    auto* page = torrentsPage(window);
    auto* action = navigation(window);
    QVERIFY(page);
    QVERIFY(action);
    auto* alternate = new QLineEdit(&window.mainTabs());
    window.mainTabs().addTab(alternate, QStringLiteral("Test secondary page"));
    window.mainTabs().tabBar()->moveTab(window.mainTabs().indexOf(page), 1);
    window.mainTabs().setTabText(window.mainTabs().indexOf(page), QStringLiteral("Relabelled torrent page"));
    QSignalSpy resets(&window.model(), &QAbstractItemModel::modelReset);
    QSignalSpy requests(&window, &ui::LibraryWindow::detailsRequested);
    for (int turn = 0; turn < 8; ++turn) {
      window.mainTabs().setCurrentWidget(alternate);
      action->trigger();
      QCOMPARE(window.mainTabs().currentWidget(), page);
      QCOMPARE(window.mainTabs().count(), 2);
      QCOMPARE(window.findChildren<QWidget*>(QStringLiteral("classic-my-torrents-page")).size(), 1);
      QVERIFY(selected(window) == target);
      QVERIFY(window.filesModel().target() == target);
      QCOMPARE(window.filesModel().rowCount(), 1);
      QCOMPARE(window.detailTabs().currentIndex(), 1);
    }
    QCOMPARE(resets.count(), 0);
    QCOMPARE(requests.count(), 0);
  }

  void navigationPreservesFilterAndMultipleSelection() {
    ui::LibraryWindow window;
    window.setPresentation(sample());
    window.show();
    QVERIFY(choose(window, window.incompleteTable(), 0));
    window.filter().setText(QStringLiteral(".bin"));
    auto& table = window.incompleteTable();
    auto* proxy = qobject_cast<QSortFilterProxyModel*>(table.model());
    QVERIFY(proxy);
    const auto second = proxy->mapFromSource(window.model().index(1, 0));
    QVERIFY(second.isValid());
    table.selectionModel()->select(second, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    QCOMPARE(table.selectionModel()->selectedRows().size(), 2);
    const auto current = table.currentIndex();
    const auto selection = table.selectionModel()->selection();
    QVERIFY(!window.continueAction().isEnabled());
    QVERIFY(!window.filesAction().isEnabled());
    QSignalSpy requests(&window, &ui::LibraryWindow::detailsRequested);
    QSignalSpy continuations(&window, &ui::LibraryWindow::continueRequested);
    auto* alternate = new QLineEdit(&window.mainTabs());
    window.mainTabs().addTab(alternate, QStringLiteral("Test secondary page"));
    window.mainTabs().setCurrentWidget(alternate);
    auto* action = navigation(window);
    QVERIFY(action);
    action->trigger();
    QCOMPARE(window.mainTabs().currentWidget(), torrentsPage(window));
    QCOMPARE(window.filter().text(), QStringLiteral(".bin"));
    QCOMPARE(table.currentIndex(), current);
    QVERIFY(table.selectionModel()->selection() == selection);
    QCOMPARE(table.selectionModel()->selectedRows().size(), 2);
    QVERIFY(!window.continueAction().isEnabled());
    QVERIFY(!window.filesAction().isEnabled());
    QCOMPARE(requests.count(), 0);
    QCOMPARE(continuations.count(), 0);
  }

  void navigationDoesNotReselectAFilteredTarget() {
    ui::LibraryWindow window;
    window.setPresentation(sample());
    window.show();
    QVERIFY(choose(window, window.incompleteTable(), 0));
    window.filter().setText(QStringLiteral("no matching torrent"));
    QCOMPARE(window.incompleteTable().model()->rowCount(), 0);
    QCOMPARE(window.completeTable().model()->rowCount(), 0);
    QVERIFY(!selected(window).has_value());
    auto* alternate = new QLineEdit(&window.mainTabs());
    window.mainTabs().addTab(alternate, QStringLiteral("Test secondary page"));
    window.mainTabs().setCurrentWidget(alternate);
    auto* action = navigation(window);
    QVERIFY(action);
    QSignalSpy resets(&window.model(), &QAbstractItemModel::modelReset);
    QSignalSpy requests(&window, &ui::LibraryWindow::detailsRequested);
    QSignalSpy continuations(&window, &ui::LibraryWindow::continueRequested);
    action->trigger();
    QCOMPARE(window.mainTabs().currentWidget(), torrentsPage(window));
    QCOMPARE(window.filter().text(), QStringLiteral("no matching torrent"));
    QCOMPARE(window.model().rowCount(), 3);
    QCOMPARE(window.incompleteTable().model()->rowCount(), 0);
    QCOMPARE(window.completeTable().model()->rowCount(), 0);
    QVERIFY(!selected(window).has_value());
    QVERIFY(!window.filesModel().target().has_value());
    QVERIFY(!window.continueAction().isEnabled());
    QVERIFY(!window.filesAction().isEnabled());
    QCOMPARE(resets.count(), 0);
    QCOMPARE(requests.count(), 0);
    QCOMPARE(continuations.count(), 0);
  }

  void navigationUsesRefreshedTargetWithoutResettingModel() {
    ui::LibraryWindow window;
    QAbstractItemModelTester tester(&window.model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    window.setPresentation(sample());
    window.show();
    QVERIFY(choose(window, window.incompleteTable(), 1));
    const auto old = window.model().targetAt(1).value();
    auto next = sample(2);
    std::reverse(next->items.begin(), next->items.end());
    window.setPresentation(next);
    const auto target = selected(window);
    QVERIFY(target.has_value());
    QCOMPARE(target->hash, old.hash);
    QCOMPARE(target->generation, quint64{2});
    QVERIFY(!window.model().accepts(old));
    auto* alternate = new QLineEdit(&window.mainTabs());
    window.mainTabs().addTab(alternate, QStringLiteral("Test secondary page"));
    window.mainTabs().setCurrentWidget(alternate);
    auto* action = navigation(window);
    QVERIFY(action);
    QSignalSpy resets(&window.model(), &QAbstractItemModel::modelReset);
    QSignalSpy continuations(&window, &ui::LibraryWindow::continueRequested);
    action->trigger();
    QVERIFY(selected(window) == target);
    QCOMPARE(resets.count(), 0);
    QCOMPARE(continuations.count(), 0);
    window.setDetails({old, QStringLiteral("stale-name"), 100});
    QCOMPARE(window.model().data(window.model().index(window.model().rowFor(target->hash), 0), Qt::DisplayRole).toString(), QStringLiteral("Beta.bin"));
    QVERIFY(window.continueAction().isEnabled());
    window.continueAction().trigger();
    QCOMPARE(continuations.count(), 1);
    QVERIFY(qvariant_cast<ui::Target>(continuations.first().at(0)) == *target);
  }

  void offlineNavigationDoesNotDispatchWork_data() {
    QTest::addColumn<bool>("busy");
    QTest::newRow("idle") << false;
    QTest::newRow("busy") << true;
  }

  void offlineNavigationDoesNotDispatchWork() {
    QFETCH(bool, busy);
    ui::LibraryWindow window;
    auto value = sample();
    for (auto& item : value->items) { item.canContinue = false; }
    window.setOfflinePreview(true);
    window.setPresentation(value);
    window.show();
    QVERIFY(choose(window, window.incompleteTable(), 0));
    window.setBusy(busy);
    auto* action = navigation(window);
    QVERIFY(action);
    QVERIFY(action->isEnabled());
    QSignalSpy refreshes(&window, &ui::LibraryWindow::refreshRequested);
    QSignalSpy continuations(&window, &ui::LibraryWindow::continueRequested);
    QSignalSpy cancellations(&window, &ui::LibraryWindow::cancelRequested);
    QSignalSpy requests(&window, &ui::LibraryWindow::detailsRequested);
    auto* alternate = new QLineEdit(&window.mainTabs());
    window.mainTabs().addTab(alternate, QStringLiteral("Test secondary page"));
    window.mainTabs().setCurrentWidget(alternate);
    action->trigger();
    QCOMPARE(window.mainTabs().currentWidget(), torrentsPage(window));
    QVERIFY(!window.continueAction().isEnabled());
    QCOMPARE(window.cancelAction().isEnabled(), busy);
    const QStringList unavailable{QStringLiteral("classic-my-tracker"), QStringLiteral("classic-my-shares"),
        QStringLiteral("classic-open"), QStringLiteral("classic-search"), QStringLiteral("classic-start-stop"), QStringLiteral("classic-remove")};
    for (const auto& name : unavailable) {
      auto* unsupported = window.findChild<QAction*>(name);
      QVERIFY2(unsupported, qPrintable(name));
      QVERIFY2(!unsupported->isEnabled(), qPrintable(name));
    }
    QCOMPARE(refreshes.count(), 0);
    QCOMPARE(continuations.count(), 0);
    QCOMPARE(cancellations.count(), 0);
    QCOMPARE(requests.count(), 0);
    window.setBusy(false);
  }
};

QTEST_MAIN(NavigationTests)
#include "navigation_tests.moc"
