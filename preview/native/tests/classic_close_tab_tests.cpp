#include "library_window.hpp"
#include "saved_service_pages.hpp"
#include "saved_services_presentation.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QtTest>

#include <memory>

// Actual Window/Qt event routing with synthetic immutable presentations only.
// No files, Bridge worker, original Java services or network are activated.
namespace {
using namespace biglybtc;
std::shared_ptr<ui::LibraryPresentation> sample() {
  auto value = std::make_shared<ui::LibraryPresentation>();
  value->revision = 1;
  const QStringList names{QStringLiteral("Alpha.bin"), QStringLiteral("Beta.bin"), QStringLiteral("Complete.bin")};
  for (int row = 0; row < names.size(); ++row) {
    const auto hash = QStringLiteral("%1").arg(row + 1, 40, 16, QChar('0')).toUpper();
    value->items.push_back({.key = hash, .hash = hash, .name = names[row],
        .state = row == 2 ? QStringLiteral("Stopped") : QStringLiteral("Queued"),
        .bytes = 100, .completed = row == 2 ? 100 : 25, .received = 100, .sent = 0,
        .canContinue = row != 2, .savedPosition = row});
  }
  return value;
}
QAction* action(ui::LibraryWindow& window, const QString& id) { return window.findChild<QAction*>(id); }
QWidget* torrents(ui::LibraryWindow& window) { return window.findChild<QWidget*>(QStringLiteral("classic-my-torrents-page")); }
QMenu* fileMenu(ui::LibraryWindow& window) {
  for (auto* top : window.menuBar()->actions()) {
    if (top->text().remove(QChar('&')) == QStringLiteral("File")) { return top->menu(); }
  }
  return nullptr;
}
QAbstractButton* closeButton(ui::LibraryWindow& window, QWidget* page) {
  const int index = window.mainTabs().indexOf(page);
  if (index < 0) { return nullptr; }
  for (auto side : {QTabBar::LeftSide, QTabBar::RightSide}) {
    if (auto* button = qobject_cast<QAbstractButton*>(window.mainTabs().tabBar()->tabButton(index, side))) { return button; }
  }
  return nullptr;
}
bool choose(ui::LibraryWindow& window, int row = 1) {
  auto& table = window.incompleteTable();
  auto* proxy = qobject_cast<QSortFilterProxyModel*>(table.model());
  if (!proxy) { return false; }
  const auto index = proxy->mapFromSource(window.model().index(row, 0));
  if (!index.isValid()) { return false; }
  table.setFocus();
  table.selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  return table.selectionModel()->isSelected(index);
}
std::optional<ui::Target> selected(ui::LibraryWindow& window) {
  auto& table = window.table();
  auto* proxy = qobject_cast<QSortFilterProxyModel*>(table.model());
  return proxy && table.currentIndex().isValid()
      ? window.model().targetAt(proxy->mapToSource(table.currentIndex()).row()) : std::nullopt;
}
void supplyFiles(ui::LibraryWindow& window, const ui::Target& target) {
  auto value = std::make_shared<ui::FileDetails>();
  value->target = target;
  value->items = {{0, QStringLiteral("first.bin"), 20, 0, false, false},
      {1, QStringLiteral("second.bin"), 80, 0, false, false}};
  window.setDetails({target, QStringLiteral("Beta.bin"), 100, value});
}
ui::SavedServicesPresentation missing(quint64 request) {
  auto value = std::make_shared<ui::SavedServicesPacket>();
  value->request = request;
  value->hosting.state = ui::SavedPageState::Missing;
  value->sharing.state = ui::SavedPageState::Missing;
  return value;
}
// Manual-delivery cases accept synchronously, like the real Bridge, while the
// test retains control of packet delivery and the final busy release.
void bindHeldSyntheticOwner(ui::LibraryWindow& window) {
  QObject::connect(&window, &ui::LibraryWindow::savedServicesRequested, &window,
      [&window](quint64) { window.setBusy(true); }, Qt::DirectConnection);
}
void bindSyntheticOwner(ui::LibraryWindow& window) {
  QObject::connect(&window, &ui::LibraryWindow::savedServicesRequested, &window, [&window](quint64 request) {
    window.setBusy(true);
    QTimer::singleShot(0, &window, [&window, request] {
      window.setSavedServices(missing(request));
      window.setBusy(false);
    });
  });
}
quint64 requestAt(const QSignalSpy& spy, int index) { return spy.at(index).at(0).toULongLong(); }
} // namespace

class ClassicCloseTabTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
  }

  void realCtrlWClosesCurrentViewFromChildFocus_data() {
    QTest::addColumn<QString>("origin");
    QTest::newRow("torrent-filter") << QStringLiteral("filter");
    QTest::newRow("torrent-files") << QStringLiteral("files");
    QTest::newRow("tracker-table") << QStringLiteral("hosting");
    QTest::newRow("shares-table") << QStringLiteral("sharing");
  }
  void realCtrlWClosesCurrentViewFromChildFocus() {
    QFETCH(QString, origin);
    ui::LibraryWindow window;
    bindSyntheticOwner(window);
    window.setPresentation(sample());
    window.show();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());
    QVERIFY(choose(window));
    const auto target = window.model().targetAt(1);
    QVERIFY(target);
    QWidget* focus = &window.filter();
    if (origin == QStringLiteral("files")) {
      window.filesAction().trigger();
      supplyFiles(window, *target);
      focus = &window.filesTable();
    } else if (origin == QStringLiteral("hosting")) {
      auto* open = action(window, QStringLiteral("classic-my-tracker"));
      QVERIFY(open);
      open->trigger();
      QTRY_VERIFY(window.hostingPage() && window.hostingPage()->state() == ui::SavedPageState::Missing);
      focus = &window.hostingPage()->table();
    } else if (origin == QStringLiteral("sharing")) {
      auto* open = action(window, QStringLiteral("classic-my-shares"));
      QVERIFY(open);
      open->trigger();
      QTRY_VERIFY(window.sharesPage() && window.sharesPage()->state() == ui::SavedPageState::Missing);
      focus = &window.sharesPage()->table();
    }
    focus->setFocus();
    QTRY_VERIFY(focus->hasFocus());
    auto* close = action(window, QStringLiteral("classic-close-tab"));
    QVERIFY(close);
    QVERIFY(close->isEnabled());
    QCOMPARE(close->shortcut(), QKeySequence(QStringLiteral("Ctrl+W")));
    QCOMPARE(close->shortcutContext(), Qt::WindowShortcut);
    QPointer<QWidget> oldPage(window.mainTabs().currentWidget());
    QVERIFY(oldPage);
    const bool main = oldPage == torrents(window);
    const int count = window.mainTabs().count();
    QSignalSpy triggered(close, &QAction::triggered);
    QSignalSpy cancels(&window, &ui::LibraryWindow::cancelRequested);
    QSignalSpy refreshes(&window, &ui::LibraryWindow::refreshRequested);
    QSignalSpy requests(&window, &ui::LibraryWindow::savedServicesRequested);
    QTest::keyClick(focus, Qt::Key_W, Qt::ControlModifier);
    QTRY_COMPARE(triggered.count(), 1);
    QCOMPARE(window.mainTabs().count(), count - 1);
    QVERIFY(window.isVisible());
    if (main) {
      QVERIFY(oldPage);
      QCOMPARE(window.mainTabs().indexOf(oldPage.data()), -1);
    } else {
      if (origin == QStringLiteral("hosting")) { QVERIFY(!window.hostingPage()); }
      else { QVERIFY(!window.sharesPage()); }
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      QVERIFY(oldPage.isNull());
    }
    QCoreApplication::processEvents();
    QCOMPARE(cancels.count(), 0);
    QCOMPARE(refreshes.count(), 0);
    QCOMPARE(requests.count(), 0);
  }

  void fileMenuActionAndTabButtonCloseKnownViews_data() {
    QTest::addColumn<QString>("route");
    QTest::addColumn<QString>("view");
    QTest::newRow("file-menu-torrents") << QStringLiteral("menu") << QStringLiteral("main");
    QTest::newRow("tab-button-torrents") << QStringLiteral("button") << QStringLiteral("main");
    QTest::newRow("tab-button-tracker") << QStringLiteral("button") << QStringLiteral("hosting");
    QTest::newRow("action-shares") << QStringLiteral("action") << QStringLiteral("sharing");
  }
  void fileMenuActionAndTabButtonCloseKnownViews() {
    QFETCH(QString, route);
    QFETCH(QString, view);
    ui::LibraryWindow window;
    bindSyntheticOwner(window);
    window.setPresentation(sample());
    window.show();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());
    if (view != QStringLiteral("main")) {
      auto* open = action(window, view == QStringLiteral("hosting") ? QStringLiteral("classic-my-tracker") : QStringLiteral("classic-my-shares"));
      QVERIFY(open);
      open->trigger();
      QTRY_VERIFY(view == QStringLiteral("hosting") ? window.hostingPage() && window.hostingPage()->state() == ui::SavedPageState::Missing
          : window.sharesPage() && window.sharesPage()->state() == ui::SavedPageState::Missing);
    }
    auto* close = action(window, QStringLiteral("classic-close-tab"));
    auto* file = fileMenu(window);
    QVERIFY(close && file);
    QVERIFY(file->actions().contains(close));
    QCOMPARE(close->text().remove(QChar('&')), QStringLiteral("Close Tab"));
    QVERIFY(close->isEnabled());
    QPointer<QWidget> oldPage(window.mainTabs().currentWidget());
    QVERIFY(oldPage);
    const auto count = window.mainTabs().count();
    QSignalSpy cancels(&window, &ui::LibraryWindow::cancelRequested);
    QSignalSpy closeRequests(&window.mainTabs(), &QTabWidget::tabCloseRequested);
    if (route == QStringLiteral("menu")) {
      file->popup(window.mapToGlobal(QPoint(20, 60)));
      QTRY_VERIFY(file->isVisible());
      const auto rect = file->actionGeometry(close);
      QVERIFY(rect.isValid());
      QTest::mouseClick(file, Qt::LeftButton, Qt::NoModifier, rect.center());
    } else if (route == QStringLiteral("button")) {
      auto* button = closeButton(window, oldPage.data());
      QVERIFY(button);
      QVERIFY(button->isVisible());
      QTest::mouseClick(button, Qt::LeftButton);
      QCOMPARE(closeRequests.count(), 1);
    } else { close->trigger(); }
    QTRY_COMPARE(window.mainTabs().count(), count - 1);
    QVERIFY(window.isVisible());
    QCOMPARE(cancels.count(), 0);
    if (view == QStringLiteral("main")) { QVERIFY(oldPage); }
    else {
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      QVERIFY(oldPage.isNull());
    }
  }

  void mainReopensSameWidgetAndPreservesViewState() {
    ui::LibraryWindow window;
    window.setPresentation(sample());
    window.show();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());
    window.filter().setText(QStringLiteral(".BIN"));
    QVERIFY(choose(window));
    const auto target = window.model().targetAt(1);
    QVERIFY(target);
    window.filesAction().trigger();
    supplyFiles(window, *target);
    window.filesTable().setCurrentIndex(window.filesTable().model()->index(1, 0));
    auto* mainPage = torrents(window);
    auto* close = action(window, QStringLiteral("classic-close-tab"));
    QVERIFY(mainPage && close);
    QPointer<QWidget> guard(mainPage);
    auto* model = &window.model();
    auto* tableModel = window.incompleteTable().model();
    auto* fileModel = &window.filesModel();
    const auto selection = window.incompleteTable().selectionModel()->selection();
    const auto fileIndex = window.filesTable().currentIndex();
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    QSignalSpy details(&window, &ui::LibraryWindow::detailsRequested);
    QSignalSpy refreshes(&window, &ui::LibraryWindow::refreshRequested);
    QSignalSpy requests(&window, &ui::LibraryWindow::savedServicesRequested);
    QSignalSpy transfers(&window, &ui::LibraryWindow::continueRequested);
    QSignalSpy cancels(&window, &ui::LibraryWindow::cancelRequested);
    for (int turn = 0; turn < 4; ++turn) {
      close->trigger();
      QCOMPARE(window.mainTabs().count(), 0);
      QCOMPARE(window.mainTabs().currentWidget(), nullptr);
      QVERIFY(guard);
      QVERIFY(!close->isEnabled());
      QCoreApplication::processEvents();
      window.setFocus();
      QTest::keyClick(&window, turn % 2 == 0 ? Qt::Key_1 : Qt::Key_F, Qt::ControlModifier);
      QTRY_COMPARE(window.mainTabs().currentWidget(), mainPage);
      QCOMPARE(window.mainTabs().count(), 1);
      QVERIFY(close->isEnabled());
      QCOMPARE(torrents(window), mainPage);
      QCOMPARE(&window.model(), model);
      QCOMPARE(window.incompleteTable().model(), tableModel);
      QCOMPARE(&window.filesModel(), fileModel);
      QCOMPARE(window.filter().text(), QStringLiteral(".BIN"));
      QVERIFY(window.incompleteTable().selectionModel()->selection() == selection);
      QVERIFY(selected(window) == target);
      QVERIFY(window.filesModel().target() == target);
      QCOMPARE(window.filesModel().rowCount(), 2);
      QCOMPARE(window.filesTable().currentIndex(), fileIndex);
      QCOMPARE(window.detailTabs().currentIndex(), 1);
      if (turn % 2 == 0) { QTRY_VERIFY(window.incompleteTable().hasFocus()); }
      else { QTRY_VERIFY(window.filter().hasFocus()); }
    }
    QCOMPARE(resets.count(), 0);
    QCOMPARE(details.count(), 0);
    QCOMPARE(refreshes.count(), 0);
    QCOMPARE(requests.count(), 0);
    QCOMPARE(transfers.count(), 0);
    QCOMPARE(cancels.count(), 0);
    QCOMPARE(window.findChildren<QWidget*>(QStringLiteral("classic-my-torrents-page")).size(), 1);
  }

  void zeroTabsKeepsWindowOpenAndCanRestoreMain_data() {
    QTest::addColumn<bool>("find");
    QTest::newRow("ctrl-one") << false;
    QTest::newRow("ctrl-find") << true;
  }
  void zeroTabsKeepsWindowOpenAndCanRestoreMain() {
    QFETCH(bool, find);
    ui::LibraryWindow window;
    window.setPresentation(sample());
    window.show();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());
    auto* mainPage = torrents(window);
    auto* close = action(window, QStringLiteral("classic-close-tab"));
    auto* refresh = action(window, QStringLiteral("classic-refresh"));
    QVERIFY(mainPage && close && refresh);
    QSignalSpy cancels(&window, &ui::LibraryWindow::cancelRequested);
    QSignalSpy refreshes(&window, &ui::LibraryWindow::refreshRequested);
    QSignalSpy transfers(&window, &ui::LibraryWindow::continueRequested);
    QSignalSpy details(&window, &ui::LibraryWindow::detailsRequested);
    close->trigger();
    QCOMPARE(window.mainTabs().count(), 0);
    QVERIFY(window.isVisible());
    QVERIFY(!close->isEnabled());
    QVERIFY(!refresh->isEnabled());
    QVERIFY(!window.continueAction().isEnabled());
    QVERIFY(!window.filesAction().isEnabled());
    window.setFocus();
    QTest::keyClick(&window, Qt::Key_W, Qt::ControlModifier);
    QTest::keyClick(&window, Qt::Key_F5);
    QTest::keyClick(&window, Qt::Key_R, Qt::ControlModifier);
    QTest::keyClick(&window, Qt::Key_I, Qt::ControlModifier);
    close->trigger();
    refresh->trigger();
    window.continueAction().trigger();
    window.filesAction().trigger();
    QCoreApplication::processEvents();
    QCOMPARE(window.mainTabs().count(), 0);
    QVERIFY(window.isVisible());
    QCOMPARE(cancels.count(), 0);
    QCOMPARE(refreshes.count(), 0);
    QCOMPARE(transfers.count(), 0);
    QCOMPARE(details.count(), 0);
    QTest::keyClick(&window, find ? Qt::Key_F : Qt::Key_1, Qt::ControlModifier);
    QTRY_COMPARE(window.mainTabs().currentWidget(), mainPage);
    QCOMPARE(window.mainTabs().count(), 1);
    QVERIFY(close->isEnabled());
    if (find) { QTRY_VERIFY(window.filter().hasFocus()); }
    else { QTRY_VERIFY(window.incompleteTable().hasFocus()); }
  }

  void closingBusyMainDoesNotCancelOrSetWindowClosing() {
    ui::LibraryWindow window;
    window.setPresentation(sample());
    window.show();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());
    QVERIFY(choose(window));
    auto* mainPage = torrents(window);
    auto* close = action(window, QStringLiteral("classic-close-tab"));
    QVERIFY(mainPage && close);
    QSignalSpy cancels(&window, &ui::LibraryWindow::cancelRequested);
    QSignalSpy details(&window, &ui::LibraryWindow::detailsRequested);
    QSignalSpy refreshes(&window, &ui::LibraryWindow::refreshRequested);
    window.setBusy(true); // Window contract only; no real worker is installed.
    QVERIFY(close->isEnabled());
    window.filter().setFocus();
    QTRY_VERIFY(window.filter().hasFocus());
    QTest::keyClick(&window.filter(), Qt::Key_W, Qt::ControlModifier);
    QCOMPARE(window.mainTabs().count(), 0);
    QCOMPARE(cancels.count(), 0);
    QVERIFY(window.isVisible());
    window.setFocus();
    QTest::keyClick(&window, Qt::Key_1, Qt::ControlModifier);
    QTRY_COMPARE(window.mainTabs().currentWidget(), mainPage);
    QVERIFY(close->isEnabled());
    QVERIFY(!window.continueAction().isEnabled());
    window.setBusy(false);
    QCoreApplication::processEvents();
    QVERIFY(window.isVisible());
    QVERIFY(close->isEnabled());
    QVERIFY(window.continueAction().isEnabled());
    QCOMPARE(cancels.count(), 0);
    QCOMPARE(details.count(), 0);
    QCOMPARE(refreshes.count(), 0);
  }

  void busyAuxiliaryCloseReopenRejectsOldPacketAndKeepsLatestPending() {
    ui::LibraryWindow window;
    bindHeldSyntheticOwner(window);
    window.setPresentation(sample());
    window.show();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());
    auto* open = action(window, QStringLiteral("classic-my-tracker"));
    auto* close = action(window, QStringLiteral("classic-close-tab"));
    QVERIFY(open && close);
    QSignalSpy requests(&window, &ui::LibraryWindow::savedServicesRequested);
    QSignalSpy cancels(&window, &ui::LibraryWindow::cancelRequested);
    QSignalSpy refreshes(&window, &ui::LibraryWindow::refreshRequested);
    QSignalSpy resets(&window.model(), &QAbstractItemModel::modelReset);
    open->trigger();
    QTRY_COMPARE(requests.count(), 1);
    QVERIFY(window.cancelAction().isEnabled()); // Accepted before the signal returned.
    const auto oldRequest = requestAt(requests, 0);
    auto* oldPage = window.hostingPage();
    QVERIFY(oldPage);
    QPointer<ui::SavedHostingPage> oldGuard(oldPage);
    window.setBusy(true);
    oldPage->table().setFocus();
    QTRY_VERIFY(oldPage->table().hasFocus());
    QTest::keyClick(&oldPage->table(), Qt::Key_W, Qt::ControlModifier);
    QVERIFY(!window.hostingPage());
    QCOMPARE(window.mainTabs().count(), 1);
    QCOMPARE(cancels.count(), 0);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(oldGuard.isNull());
    open->trigger();
    auto* fresh = window.hostingPage();
    QVERIFY(fresh);
    const auto latest = fresh->generation();
    QVERIFY(latest > oldRequest);
    QCoreApplication::processEvents();
    QCOMPARE(requests.count(), 1);
    window.setSavedServices(missing(oldRequest));
    QCOMPARE(fresh->state(), ui::SavedPageState::Loading);
    window.setBusy(false);
    QTRY_COMPARE(requests.count(), 2);
    QVERIFY(window.cancelAction().isEnabled()); // Accepted before the signal returned.
    QCOMPARE(requestAt(requests, 1), latest);
    window.setBusy(true);
    window.setSavedServices(missing(latest));
    window.setBusy(false);
    QCOMPARE(fresh->state(), ui::SavedPageState::Missing);
    QCOMPARE(window.mainTabs().currentWidget(), fresh);
    QCOMPARE(window.model().rowCount(), 3);
    QCOMPARE(resets.count(), 0);
    QCOMPARE(cancels.count(), 0);
    QCOMPARE(refreshes.count(), 0);
  }

  void unknownViewAndWindowClosingDisableCloseTab() {
    ui::LibraryWindow window;
    window.setPresentation(sample());
    window.show();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());
    auto* close = action(window, QStringLiteral("classic-close-tab"));
    auto* mainAction = action(window, QStringLiteral("classic-my-torrents"));
    auto* sharesAction = action(window, QStringLiteral("classic-my-shares"));
    QVERIFY(close && mainAction && sharesAction);
    auto* unknown = new QLineEdit(&window.mainTabs());
    window.mainTabs().addTab(unknown, QStringLiteral("Synthetic unknown view"));
    window.mainTabs().setCurrentWidget(unknown);
    unknown->setFocus();
    QTRY_VERIFY(unknown->hasFocus());
    QVERIFY(!close->isEnabled());
    QSignalSpy cancels(&window, &ui::LibraryWindow::cancelRequested);
    QTest::keyClick(unknown, Qt::Key_W, Qt::ControlModifier);
    close->trigger();
    const auto index = window.mainTabs().indexOf(unknown);
    QVERIFY(QMetaObject::invokeMethod(&window.mainTabs(), "tabCloseRequested", Qt::DirectConnection, Q_ARG(int, index)));
    QCOMPARE(window.mainTabs().currentWidget(), unknown);
    QCOMPARE(window.mainTabs().count(), 2);
    QVERIFY(window.isVisible());
    QCOMPARE(cancels.count(), 0);
    mainAction->trigger();
    QVERIFY(close->isEnabled());
    window.setBusy(true);
    QVERIFY(!window.close());
    QCOMPARE(cancels.count(), 1);
    QVERIFY(!close->isEnabled());
    close->trigger();
    sharesAction->trigger();
    QCOMPARE(window.mainTabs().count(), 2);
    QVERIFY(!window.sharesPage());
    window.setBusy(false);
    QTRY_VERIFY(!window.isVisible());
    QCOMPARE(cancels.count(), 1);
  }
};

QTEST_MAIN(ClassicCloseTabTests)
#include "classic_close_tab_tests.moc"
