#include "library_window.hpp"
#include "saved_service_pages.hpp"
#include "saved_services_presentation.hpp"
#include "biglybtc/saved_services.hpp"

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QPointer>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QtTest>

#include <memory>

// Window routing tests use immutable synthetic DTOs. They neither read saved
// files nor activate hosting, sharing, network, or original Java services.
namespace {
using namespace biglybtc;
std::shared_ptr<ui::LibraryPresentation> library(quint64 revision = 1) {
  auto value = std::make_shared<ui::LibraryPresentation>();
  value->revision = revision;
  for (int row = 0; row < 2; ++row) {
    const auto hash = QStringLiteral("%1").arg(row + 1, 40, 16, QChar('0')).toUpper();
    value->items.push_back({.key = hash, .hash = hash,
        .name = row == 0 ? QStringLiteral("Alpha.bin") : QStringLiteral("Complete.bin"),
        .state = row == 0 ? QStringLiteral("Queued") : QStringLiteral("Stopped"),
        .bytes = 100, .completed = row == 0 ? 25 : 100, .received = 100, .sent = 0, .canContinue = row == 0});
  }
  return value;
}
std::shared_ptr<const core::SavedHosting> hosting(char marker = 'A') {
  auto value = std::make_shared<core::SavedHosting>();
  value->listPresent = true;
  core::SavedHostingRecord row;
  row.hash = std::string(20, marker);
  row.persistent = true;
  row.passive = false;
  row.disposition = core::SavedHostingDisposition::PersistentHost;
  row.rawStatus = 2;
  row.status = 2;
  value->records.push_back(std::move(row));
  return value;
}
std::shared_ptr<const core::SavedShares> sharing(QString marker = QStringLiteral("/inert/Alpha")) {
  auto value = std::make_shared<core::SavedShares>();
  value->listPresent = true;
  core::SavedShareRecord row;
  row.rawType = 1;
  row.type = 1;
  row.kind = core::SavedShareKind::File;
  row.file = core::SavedText{marker.toStdString()};
  row.persistent = false;
  value->records.push_back(std::move(row));
  return value;
}
ui::SavedServicesPresentation ready(quint64 request, char marker = 'A') {
  auto value = std::make_shared<ui::SavedServicesPacket>();
  value->request = request;
  value->hosting = {ui::SavedPageState::Ready, ui::SavedPageSource::Primary, hosting(marker), {}};
  value->sharing = {ui::SavedPageState::Ready, ui::SavedPageSource::Saving, sharing(), {}};
  return value;
}
QAction* action(ui::LibraryWindow& window, const QString& id) { return window.findChild<QAction*>(id); }
QWidget* torrents(ui::LibraryWindow& window) {
  return window.findChild<QWidget*>(QStringLiteral("classic-my-torrents-page"));
}
ui::SavedHostingPage* hostingPage(ui::LibraryWindow& window) {
  return window.hostingPage();
}
ui::SavedSharesPage* sharingPage(ui::LibraryWindow& window) {
  return window.sharesPage();
}
bool choose(ui::LibraryWindow& window, int sourceRow = 0) {
  auto& table = sourceRow == 0 ? window.incompleteTable() : window.completeTable();
  auto* proxy = qobject_cast<QSortFilterProxyModel*>(table.model());
  if (!proxy) { return false; }
  const auto index = proxy->mapFromSource(window.model().index(sourceRow, 0));
  if (!index.isValid()) { return false; }
  table.setFocus();
  QCoreApplication::processEvents();
  table.selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  QCoreApplication::processEvents();
  return table.selectionModel()->isSelected(index);
}
void files(ui::LibraryWindow& window) {
  const auto target = window.model().targetAt(0);
  if (!target) { return; }
  auto value = std::make_shared<ui::FileDetails>();
  value->target = *target;
  value->items.push_back({0, QStringLiteral("payload.bin"), 100, 0, false, false});
  window.setDetails({*target, QStringLiteral("Alpha.bin"), 100, value});
}
quint64 requestAt(const QSignalSpy& spy, int row) {
  return spy.at(row).at(0).toULongLong();
}
// The provider models only the Window/worker signal order, using local DTOs.
// It provides no Bridge, filesystem, service, or actual-worker coverage.
void bindSyntheticOwner(ui::LibraryWindow& window) {
  QObject::connect(&window, &ui::LibraryWindow::savedServicesRequested, &window,
      [&window](quint64 request) {
        window.setBusy(true);
        QTimer::singleShot(0, &window, [&window, request] {
          window.setSavedServices(ready(request));
          window.setBusy(false);
        });
      });
}
bool closeTab(ui::LibraryWindow& window, QWidget* page) {
  const auto index = window.mainTabs().indexOf(page);
  return index >= 0 && QMetaObject::invokeMethod(&window.mainTabs(), "tabCloseRequested",
      Qt::DirectConnection, Q_ARG(int, index));
}
} // namespace

class SavedServicesNavigationTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
  }

  void realShortcutsAcrossFocusedWidgets_data() {
    QTest::addColumn<QString>("origin");
    QTest::addColumn<int>("destination");
    QTest::newRow("filter-to-tracker") << QStringLiteral("filter") << 2;
    QTest::newRow("files-to-shares") << QStringLiteral("files") << 3;
    QTest::newRow("tracker-to-shares") << QStringLiteral("hosting") << 3;
    QTest::newRow("shares-to-tracker") << QStringLiteral("sharing") << 2;
  }
  void realShortcutsAcrossFocusedWidgets() {
    QFETCH(QString, origin);
    QFETCH(int, destination);
    ui::LibraryWindow window;
    bindSyntheticOwner(window);
    window.setPresentation(library());
    window.show();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());
    QVERIFY(choose(window));
    window.filesAction().trigger();
    files(window);
    window.filter().setText(QStringLiteral(".BIN"));
    const auto target = window.model().targetAt(0);
    QVERIFY(target);
    QVERIFY(window.filesModel().target() == target);
    auto* mainPage = torrents(window);
    QVERIFY(mainPage);
    QWidget* focus = &window.filter();
    if (origin == QStringLiteral("files")) { focus = &window.filesTable(); }
    else if (origin == QStringLiteral("hosting")) {
      auto* open = action(window, QStringLiteral("classic-my-tracker"));
      QVERIFY(open);
      open->trigger();
      QTRY_VERIFY(hostingPage(window) && hostingPage(window)->state() == ui::SavedPageState::Ready);
      focus = &hostingPage(window)->table();
    } else if (origin == QStringLiteral("sharing")) {
      auto* open = action(window, QStringLiteral("classic-my-shares"));
      QVERIFY(open);
      open->trigger();
      QTRY_VERIFY(sharingPage(window) && sharingPage(window)->state() == ui::SavedPageState::Ready);
      focus = &sharingPage(window)->table();
    }
    focus->setFocus();
    QTRY_VERIFY(focus->hasFocus());
    QSignalSpy resets(&window.model(), &QAbstractItemModel::modelReset);
    QSignalSpy details(&window, &ui::LibraryWindow::detailsRequested);
    QSignalSpy transfers(&window, &ui::LibraryWindow::continueRequested);
    QSignalSpy refreshes(&window, &ui::LibraryWindow::refreshRequested);
    auto* go = action(window, destination == 2 ? QStringLiteral("classic-my-tracker") : QStringLiteral("classic-my-shares"));
    QVERIFY(go);
    QVERIFY(go->isEnabled());
    QCOMPARE(go->shortcutContext(), Qt::WindowShortcut);
    QSignalSpy triggered(go, &QAction::triggered);
    QTest::keyClick(focus, destination == 2 ? Qt::Key_2 : Qt::Key_3, Qt::ControlModifier);
    QTRY_COMPARE(triggered.count(), 1);
    ui::SavedServicePage* page = destination == 2 ? static_cast<ui::SavedServicePage*>(hostingPage(window))
        : static_cast<ui::SavedServicePage*>(sharingPage(window));
    QVERIFY(page);
    QTRY_COMPARE(page->state(), ui::SavedPageState::Ready);
    QCOMPARE(window.mainTabs().currentWidget(), page);
    QTRY_VERIFY(page->table().hasFocus());
    QTest::keyClick(&page->table(), Qt::Key_1, Qt::ControlModifier);
    QTRY_COMPARE(window.mainTabs().currentWidget(), mainPage);
    QTRY_VERIFY(window.incompleteTable().hasFocus());
    QCOMPARE(window.filter().text(), QStringLiteral(".BIN"));
    QVERIFY(window.filesModel().target() == target);
    QCOMPARE(window.filesModel().rowCount(), 1);
    QCOMPARE(window.detailTabs().currentIndex(), 1);
    QCOMPARE(window.model().rowCount(), 2);
    auto* proxy = qobject_cast<QSortFilterProxyModel*>(window.incompleteTable().model());
    QVERIFY(proxy);
    QCOMPARE(window.incompleteTable().selectionModel()->selectedRows().size(), 1);
    QCOMPARE(proxy->mapToSource(window.incompleteTable().currentIndex()).row(), 0);
    QCOMPARE(resets.count(), 0);
    QCOMPARE(details.count(), 0);
    QCOMPARE(transfers.count(), 0);
    QCOMPARE(refreshes.count(), 0);
  }

  void reactivationUsesSinglePagesWithoutReloading() {
    ui::LibraryWindow window;
    bindSyntheticOwner(window);
    window.setPresentation(library());
    QSignalSpy requests(&window, &ui::LibraryWindow::savedServicesRequested);
    auto* hostAction = action(window, QStringLiteral("classic-my-tracker"));
    auto* shareAction = action(window, QStringLiteral("classic-my-shares"));
    auto* torrentAction = action(window, QStringLiteral("classic-my-torrents"));
    QVERIFY(hostAction && shareAction && torrentAction);
    hostAction->trigger();
    QTRY_VERIFY(hostingPage(window) && hostingPage(window)->state() == ui::SavedPageState::Ready);
    shareAction->trigger();
    QTRY_VERIFY(sharingPage(window) && sharingPage(window)->state() == ui::SavedPageState::Ready && !window.cancelAction().isEnabled());
    auto* hostPage = hostingPage(window);
    auto* sharePage = sharingPage(window);
    auto* mainPage = torrents(window);
    QVERIFY(mainPage);
    QCOMPARE(window.mainTabs().count(), 3);
    const auto delivered = requests.count();
    window.mainTabs().tabBar()->moveTab(window.mainTabs().indexOf(hostPage), 0);
    window.mainTabs().setTabText(window.mainTabs().indexOf(hostPage), QStringLiteral("Relabelled saved hosts"));
    for (int turn = 0; turn < 6; ++turn) {
      hostAction->trigger();
      QCOMPARE(window.mainTabs().currentWidget(), hostPage);
      shareAction->trigger();
      QCOMPARE(window.mainTabs().currentWidget(), sharePage);
      torrentAction->trigger();
      QCOMPARE(window.mainTabs().currentWidget(), mainPage);
      QCOMPARE(window.mainTabs().count(), 3);
    }
    QCoreApplication::processEvents();
    QCOMPARE(requests.count(), delivered);
    QCOMPARE(hostingPage(window), hostPage);
    QCOMPARE(sharingPage(window), sharePage);
    QCOMPARE(window.findChildren<QWidget*>(QStringLiteral("saved-hosting-page")).size(), 1);
    QCOMPARE(window.findChildren<QWidget*>(QStringLiteral("saved-shares-page")).size(), 1);
    QVERIFY(closeTab(window, mainPage));
    QCOMPARE(window.mainTabs().indexOf(mainPage) >= 0, true);
    QCOMPARE(window.mainTabs().count(), 3);
    for (auto side : {QTabBar::LeftSide, QTabBar::RightSide}) {
      QVERIFY(!window.mainTabs().tabBar()->tabButton(window.mainTabs().indexOf(mainPage), side));
    }
  }

  void auxiliaryPageCannotActOnHiddenTorrent_data() {
    QTest::addColumn<bool>("useShares");
    QTest::newRow("tracker") << false;
    QTest::newRow("shares") << true;
  }
  void auxiliaryPageCannotActOnHiddenTorrent() {
    QFETCH(bool, useShares);
    ui::LibraryWindow window;
    bindSyntheticOwner(window);
    window.setPresentation(library());
    window.show();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());
    QVERIFY(choose(window));
    window.filesAction().trigger();
    files(window);
    QVERIFY(window.continueAction().isEnabled());
    const auto target = window.model().targetAt(0);
    QVERIFY(target);
    const auto selected = window.incompleteTable().selectionModel()->selection();
    auto* open = action(window, useShares ? QStringLiteral("classic-my-shares") : QStringLiteral("classic-my-tracker"));
    QVERIFY(open);
    open->trigger();
    QTRY_VERIFY(useShares ? sharingPage(window) && sharingPage(window)->state() == ui::SavedPageState::Ready
        : hostingPage(window) && hostingPage(window)->state() == ui::SavedPageState::Ready);
    ui::SavedServicePage* page = useShares ? static_cast<ui::SavedServicePage*>(sharingPage(window))
        : static_cast<ui::SavedServicePage*>(hostingPage(window));
    QVERIFY(page);
    page->table().setFocus();
    QTRY_VERIFY(page->table().hasFocus());
    QVERIFY(!window.continueAction().isEnabled());
    QVERIFY(!window.filesAction().isEnabled());
    QSignalSpy transfers(&window, &ui::LibraryWindow::continueRequested);
    QSignalSpy details(&window, &ui::LibraryWindow::detailsRequested);
    QTest::keyClick(&page->table(), Qt::Key_R, Qt::ControlModifier);
    QTest::keyClick(&page->table(), Qt::Key_I, Qt::ControlModifier);
    window.continueAction().trigger();
    window.filesAction().trigger();
    // Programmatic hidden selection changes must not initiate lazy details.
    QVERIFY(choose(window, 1));
    QCoreApplication::processEvents();
    QCOMPARE(window.mainTabs().currentWidget(), page);
    QCOMPARE(transfers.count(), 0);
    QCOMPARE(details.count(), 0);
    QVERIFY(!window.continueAction().isEnabled());
    QVERIFY(!window.filesAction().isEnabled());
    page->table().setFocus();
    QTRY_VERIFY(page->table().hasFocus());
    QTest::keyClick(&page->table(), Qt::Key_F, Qt::ControlModifier);
    QTRY_COMPARE(window.mainTabs().currentWidget(), torrents(window));
    QTRY_VERIFY(window.filter().hasFocus());
    QCOMPARE(transfers.count(), 0);
    // Returning to the visible Files view may legitimately request the newly
    // selected row; the zero-dispatch assertions above cover the hidden view.
    // The selection was deliberately changed above; it remains valid for the
    // torrent model when the view becomes visible, never for an auxiliary view.
    QVERIFY(window.filesAction().isEnabled());
    QVERIFY(window.model().accepts(*target));
    QVERIFY(!selected.isEmpty());
  }

  void closeReopenRejectsStaleGeneration() {
    ui::LibraryWindow window;
    window.setPresentation(library());
    QSignalSpy requests(&window, &ui::LibraryWindow::savedServicesRequested);
    auto* open = action(window, QStringLiteral("classic-my-tracker"));
    QVERIFY(open);
    open->trigger();
    QTRY_COMPARE(requests.count(), 1);
    const auto oldRequest = requestAt(requests, 0);
    QVERIFY(oldRequest != 0);
    auto* oldPage = hostingPage(window);
    QVERIFY(oldPage);
    QPointer oldGuard(oldPage);
    window.setBusy(true);
    QVERIFY(closeTab(window, oldPage));
    QVERIFY(!hostingPage(window));
    QCOMPARE(window.mainTabs().count(), 1);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(oldGuard.isNull());
    open->trigger();
    auto* current = hostingPage(window);
    QVERIFY(current);
    QVERIFY(current->generation() > oldRequest);
    QCOMPARE(current->state(), ui::SavedPageState::Loading);
    QCoreApplication::processEvents();
    QCOMPARE(requests.count(), 1);
    window.setSavedServices(ready(oldRequest, 'A'));
    QCOMPARE(current->state(), ui::SavedPageState::Loading);
    QCOMPARE(current->model().rowCount(), 0);
    window.setBusy(false);
    QTRY_COMPARE(requests.count(), 2);
    const auto newRequest = requestAt(requests, 1);
    QCOMPARE(newRequest, current->generation());
    QVERIFY(newRequest > oldRequest);
    window.setBusy(true);
    window.setSavedServices(ready(newRequest, 'B'));
    window.setBusy(false);
    QCOMPARE(current->state(), ui::SavedPageState::Ready);
    QCOMPARE(current->model().rowCount(), 1);
    QCOMPARE(current->model().data(current->model().index(0, 0)).toString(), QString::fromLatin1(QByteArray(20, 'B').toHex().toUpper()));
    window.setSavedServices(ready(oldRequest, 'C'));
    QCOMPARE(current->model().data(current->model().index(0, 0)).toString(), QString::fromLatin1(QByteArray(20, 'B').toHex().toUpper()));
    QCOMPARE(window.mainTabs().count(), 2);
    QCOMPARE(window.findChildren<QWidget*>(QStringLiteral("saved-hosting-page")).size(), 1);
  }

  void busyNavigationAndRefreshKeepOnlyLatestPending() {
    ui::LibraryWindow window;
    window.setPresentation(library());
    window.show();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());
    QSignalSpy requests(&window, &ui::LibraryWindow::savedServicesRequested);
    QSignalSpy refreshes(&window, &ui::LibraryWindow::refreshRequested);
    auto* hostAction = action(window, QStringLiteral("classic-my-tracker"));
    auto* shareAction = action(window, QStringLiteral("classic-my-shares"));
    auto* refresh = action(window, QStringLiteral("classic-refresh"));
    QVERIFY(hostAction && shareAction && refresh);
    window.setBusy(true);
    hostAction->trigger();
    QVERIFY(hostingPage(window));
    const auto first = hostingPage(window)->generation();
    shareAction->trigger();
    auto* shares = sharingPage(window);
    QVERIFY(shares);
    QVERIFY(shares->generation() > first);
    QVERIFY(refresh->isEnabled());
    shares->table().setFocus();
    QTRY_VERIFY(shares->table().hasFocus());
    auto latest = shares->generation();
    for (int turn = 0; turn < 3; ++turn) {
      QTest::keyClick(&shares->table(), Qt::Key_F5);
      QVERIFY(shares->generation() > latest);
      latest = shares->generation();
      QCOMPARE(hostingPage(window)->generation(), latest);
    }
    QCoreApplication::processEvents();
    QCOMPARE(requests.count(), 0);
    QCOMPARE(refreshes.count(), 0);
    window.setBusy(false);
    QTRY_COMPARE(requests.count(), 1);
    QCOMPARE(requestAt(requests, 0), latest);
    window.setBusy(true);
    window.setSavedServices(ready(latest));
    window.setBusy(false);
    QCoreApplication::processEvents();
    QCOMPARE(requests.count(), 1);
    QCOMPARE(hostingPage(window)->state(), ui::SavedPageState::Ready);
    QCOMPARE(shares->state(), ui::SavedPageState::Ready);
  }

  void noResultBusyReleaseFailsOnlyActiveAndRetainsNewerPending() {
    ui::LibraryWindow window;
    window.setPresentation(library());
    QSignalSpy requests(&window, &ui::LibraryWindow::savedServicesRequested);
    auto* hostAction = action(window, QStringLiteral("classic-my-tracker"));
    auto* shareAction = action(window, QStringLiteral("classic-my-shares"));
    QVERIFY(hostAction && shareAction);
    hostAction->trigger();
    QTRY_COMPARE(requests.count(), 1);
    const auto active = requestAt(requests, 0);
    window.setBusy(true);
    shareAction->trigger();
    auto* hostPage = hostingPage(window);
    auto* sharePage = sharingPage(window);
    QVERIFY(hostPage && sharePage);
    const auto latest = sharePage->generation();
    QVERIFY(latest > active);
    QCOMPARE(hostPage->generation(), latest);
    window.setBusy(false); // Model worker completion/cancellation without a packet.
    QCOMPARE(hostPage->state(), ui::SavedPageState::Loading);
    QCOMPARE(sharePage->state(), ui::SavedPageState::Loading);
    QTRY_COMPARE(requests.count(), 2);
    QCOMPARE(requestAt(requests, 1), latest);
    window.setBusy(true);
    window.setSavedServices(ready(active, 'A'));
    QCOMPARE(hostPage->state(), ui::SavedPageState::Loading);
    QCOMPARE(sharePage->state(), ui::SavedPageState::Loading);
    window.setSavedServices(ready(latest, 'B'));
    window.setBusy(false);
    QCOMPARE(hostPage->state(), ui::SavedPageState::Ready);
    QCOMPARE(sharePage->state(), ui::SavedPageState::Ready);
    QCoreApplication::processEvents();
    QCOMPARE(requests.count(), 2);
  }

  void failedActiveRefreshRecoversWithoutAcceptingOldPacket() {
    ui::LibraryWindow window;
    window.setPresentation(library());
    QSignalSpy requests(&window, &ui::LibraryWindow::savedServicesRequested);
    auto* open = action(window, QStringLiteral("classic-my-tracker"));
    auto* refresh = action(window, QStringLiteral("classic-refresh"));
    QVERIFY(open && refresh);
    open->trigger();
    QTRY_COMPARE(requests.count(), 1);
    const auto failed = requestAt(requests, 0);
    QSignalSpy cancellations(&window, &ui::LibraryWindow::cancelRequested);
    window.setBusy(true);
    QVERIFY(window.cancelAction().isEnabled());
    window.cancelAction().trigger();
    QCOMPARE(cancellations.count(), 1);
    window.setBusy(false);
    auto* page = hostingPage(window);
    QVERIFY(page);
    QCOMPARE(page->state(), ui::SavedPageState::Failed);
    QVERIFY(!page->statusText().isEmpty());
    window.setSavedServices(ready(failed));
    QCOMPARE(page->state(), ui::SavedPageState::Failed);
    refresh->trigger();
    QTRY_COMPARE(requests.count(), 2);
    const auto next = requestAt(requests, 1);
    QVERIFY(next > failed);
    QCOMPARE(page->state(), ui::SavedPageState::Loading);
    window.setBusy(true);
    window.setSavedServices(ready(next));
    window.setBusy(false);
    QCOMPARE(page->state(), ui::SavedPageState::Ready);
  }

  void refreshRoutesToCurrentViewOnly() {
    ui::LibraryWindow window;
    bindSyntheticOwner(window);
    window.setPresentation(library());
    window.show();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());
    QSignalSpy refreshes(&window, &ui::LibraryWindow::refreshRequested);
    QSignalSpy requests(&window, &ui::LibraryWindow::savedServicesRequested);
    window.filter().setFocus();
    QTRY_VERIFY(window.filter().hasFocus());
    QTest::keyClick(&window.filter(), Qt::Key_F5);
    QCOMPARE(refreshes.count(), 1);
    QCOMPARE(requests.count(), 0);
    auto* open = action(window, QStringLiteral("classic-my-tracker"));
    QVERIFY(open);
    open->trigger();
    QTRY_VERIFY(hostingPage(window) && hostingPage(window)->state() == ui::SavedPageState::Ready && !window.cancelAction().isEnabled());
    auto* page = hostingPage(window);
    const auto old = page->generation();
    const auto count = requests.count();
    page->table().setFocus();
    QTRY_VERIFY(page->table().hasFocus());
    QTest::keyClick(&page->table(), Qt::Key_F5);
    QTRY_COMPARE(requests.count(), count + 1);
    QTRY_COMPARE(page->state(), ui::SavedPageState::Ready);
    QVERIFY(page->generation() > old);
    QCOMPARE(refreshes.count(), 1);
    auto* mainAction = action(window, QStringLiteral("classic-my-torrents"));
    QVERIFY(mainAction);
    mainAction->trigger();
    window.filter().setFocus();
    QTRY_VERIFY(window.filter().hasFocus());
    QTest::keyClick(&window.filter(), Qt::Key_F5);
    QCOMPARE(refreshes.count(), 2);
    QCOMPARE(requests.count(), count + 1);
  }

  void independentPacketStatesAndSources_data() {
    QTest::addColumn<int>("kind");
    QTest::newRow("primary-and-saving") << 0;
    QTest::newRow("backup-and-missing") << 1;
    QTest::newRow("missing-and-failed") << 2;
    QTest::newRow("failed-and-primary") << 3;
  }
  void independentPacketStatesAndSources() {
    QFETCH(int, kind);
    ui::LibraryWindow window;
    window.setPresentation(library());
    window.setBusy(true);
    auto* hostAction = action(window, QStringLiteral("classic-my-tracker"));
    auto* shareAction = action(window, QStringLiteral("classic-my-shares"));
    QVERIFY(hostAction && shareAction);
    QSignalSpy requests(&window, &ui::LibraryWindow::savedServicesRequested);
    hostAction->trigger();
    shareAction->trigger();
    auto* hosts = hostingPage(window);
    auto* shares = sharingPage(window);
    QVERIFY(hosts && shares);
    const auto generation = shares->generation();
    QCOMPARE(hosts->generation(), generation);
    window.setBusy(false);
    QTRY_COMPARE(requests.count(), 1);
    QCOMPARE(requestAt(requests, 0), generation);
    window.setBusy(true);
    auto value = std::make_shared<ui::SavedServicesPacket>(*ready(generation));
    if (kind == 1) {
      value->hosting.source = ui::SavedPageSource::Backup;
      value->sharing = {ui::SavedPageState::Missing, {}, {}, {}};
    } else if (kind == 2) {
      value->hosting = {ui::SavedPageState::Missing, {}, {}, {}};
      value->sharing = {ui::SavedPageState::Failed, {}, {}, QStringLiteral("Synthetic saved-file read failure")};
    } else if (kind == 3) {
      value->hosting = {ui::SavedPageState::Failed, {}, {}, QStringLiteral("Synthetic saved-file read failure")};
      value->sharing.source = ui::SavedPageSource::Primary;
    }
    window.setSavedServices(value);
    window.setBusy(false);
    QCOMPARE(hosts->state(), value->hosting.state);
    QCOMPARE(shares->state(), value->sharing.state);
    QCOMPARE(hosts->model().rowCount(), value->hosting.state == ui::SavedPageState::Ready ? 1 : 0);
    QCOMPARE(shares->model().rowCount(), value->sharing.state == ui::SavedPageState::Ready ? 1 : 0);
    if (kind == 0) {
      QVERIFY(hosts->provenanceText().contains(QStringLiteral("Primary")));
      QVERIFY(shares->provenanceText().contains(QStringLiteral("Saving")));
    } else if (kind == 1) {
      QVERIFY(hosts->provenanceText().contains(QStringLiteral("Backup")));
      QCOMPARE(shares->provenanceText(), QStringLiteral("Source: —"));
    }
    QCOMPARE(window.model().rowCount(), 2);
    QCOMPARE(window.mainTabs().count(), 3);
  }

  void unrelatedAndDuplicatePacketsCannotReplaceCurrentRows() {
    ui::LibraryWindow window;
    window.setPresentation(library());
    QSignalSpy requests(&window, &ui::LibraryWindow::savedServicesRequested);
    auto* open = action(window, QStringLiteral("classic-my-tracker"));
    QVERIFY(open);
    open->trigger();
    QTRY_COMPARE(requests.count(), 1);
    const auto active = requestAt(requests, 0);
    auto* page = hostingPage(window);
    QVERIFY(page);
    window.setBusy(true);
    window.setSavedServices({});
    window.setSavedServices(ready(active + 1, 'C'));
    QCOMPARE(page->state(), ui::SavedPageState::Loading);
    QCOMPARE(page->model().rowCount(), 0);
    window.setSavedServices(ready(active, 'B'));
    QCOMPARE(page->state(), ui::SavedPageState::Ready);
    const auto saved = page->model().data(page->model().index(0, 0));
    window.setSavedServices(ready(active, 'C'));
    window.setBusy(false);
    QCOMPARE(page->model().data(page->model().index(0, 0)), saved);
    QCoreApplication::processEvents();
    QCOMPARE(requests.count(), 1);
  }
};

QTEST_MAIN(SavedServicesNavigationTests)
#include "saved_services_navigation_tests.moc"
