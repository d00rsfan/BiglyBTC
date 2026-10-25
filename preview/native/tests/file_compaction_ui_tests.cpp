#include "library_window.hpp"
#include <QAction>
#include <QApplication>
#include <QAbstractButton>
#include <QItemSelectionModel>
#include <QMessageBox>
#include <QPointer>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTabWidget>
#include <QTimer>
#include <QtTest>
#include <initializer_list>
#include <memory>

using namespace biglybtc;
namespace {
std::shared_ptr<ui::LibraryPresentation> library(quint64 generation = 1) {
  auto result = std::make_shared<ui::LibraryPresentation>(); result->revision = generation;
  result->items.push_back({.key=QStringLiteral("generated"),.hash=QString(40,QLatin1Char('A')),
      .name=QStringLiteral("owned bundle"),.state=QStringLiteral("Stopped"),.bytes=100,.completed=0});
  return result;
}
std::shared_ptr<ui::FileDetails> files(ui::Target target, bool allowed = true) {
  auto result = std::make_shared<ui::FileDetails>(); result->target = target;
  result->canCompact = allowed; // Synthetic UI authorization, not an engine proof.
  result->items = {{7,QStringLiteral("zeta.bin"),40,0,false,false,allowed},
      {2,QStringLiteral("alpha.bin"),50,0,false,false,allowed},
      {9,QStringLiteral(".pad/3"),10,0,true,true,false}};
  return result;
}
ui::Target prepare(ui::LibraryWindow& window) {
  window.setPresentation(library()); const auto target = window.model().targetAt(0).value();
  window.setDetails({target,QStringLiteral("owned bundle"),100,files(target)});
  window.show(); window.detailTabs().setCurrentIndex(1); QCoreApplication::processEvents(); return target;
}
bool select(ui::LibraryWindow& window, std::initializer_list<int> ids) {
  auto& table = window.filesTable(); auto* proxy = qobject_cast<QSortFilterProxyModel*>(table.model());
  if (!proxy) { return false; } table.selectionModel()->clearSelection();
  for (int id : ids) {
    const auto at = proxy->mapFromSource(window.filesModel().index(window.filesModel().rowFor(id),0));
    if (!at.isValid()) { return false; }
    table.selectionModel()->select(at,QItemSelectionModel::Select|QItemSelectionModel::Rows);
    table.selectionModel()->setCurrentIndex(at,QItemSelectionModel::NoUpdate);
  }
  QCoreApplication::processEvents(); return table.selectionModel()->selectedRows().size()==static_cast<qsizetype>(ids.size());
}
QAction* action(ui::LibraryWindow& window) { return window.findChild<QAction*>(QStringLiteral("files-reclaim-space")); }
bool force(QAction* item) { return item && QMetaObject::invokeMethod(item,"triggered",Qt::DirectConnection,Q_ARG(bool,false)); }
void answer(QPointer<ui::LibraryWindow> window, QMessageBox::StandardButton choice) {
  QTimer::singleShot(0,window,[window,choice] {
    if (!window) { return; }
    if (auto* box=window->findChild<QMessageBox*>(QStringLiteral("files-reclaim-confirmation"))) {
      if (auto* button=box->button(choice)) { button->click(); }
    }
  });
}
}
class FileCompactionUiTests final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));
    QApplication::setQuitOnLastWindowClosed(false);
  }
  void explicitConfirmationRoutesOneOriginalIndexWithoutChangingSkip() {
    ui::LibraryWindow window; const auto target=prepare(window); QVERIFY(select(window,{2}));
    auto* item=action(window); QVERIFY(item); QTRY_VERIFY(item->isEnabled());
    QSignalSpy reclaim(&window,&ui::LibraryWindow::fileCompactionRequested);
    QSignalSpy ordinarySkip(&window,&ui::LibraryWindow::filePolicyRequested);
    answer(&window,QMessageBox::Cancel); item->trigger(); QCOMPARE(reclaim.count(),0);
    answer(&window,QMessageBox::Yes); item->trigger(); QCOMPARE(reclaim.count(),1);
    QVERIFY(qvariant_cast<ui::Target>(reclaim[0][0]) == target); QCOMPARE(reclaim[0][1].toInt(),2);
    QCOMPARE(ordinarySkip.count(),0); QCOMPARE(window.filesModel().presentation()->items[1].skipped,false);
  }
  void unavailableMultiplePaddingBusyAndStaleFramesRejectWithoutDialog() {
    ui::LibraryWindow window; const auto target=prepare(window); auto* item=action(window); QVERIFY(item);
    QSignalSpy reclaim(&window,&ui::LibraryWindow::fileCompactionRequested);
    QVERIFY(select(window,{2,7})); QTRY_VERIFY(!item->isEnabled()); QVERIFY(force(item));
    QVERIFY(select(window,{2})); QTRY_VERIFY(item->isEnabled());
    window.setBusy(true); QVERIFY(force(item)); window.setBusy(false); QCoreApplication::processEvents();
    window.setDetails({target,QStringLiteral("unavailable"),100,files(target,false)});
    QVERIFY(select(window,{2})); QTRY_VERIFY(!item->isEnabled()); QVERIFY(force(item));
    window.paddingAction().setChecked(true); QVERIFY(select(window,{9})); QVERIFY(force(item));
    window.setPresentation(library(2)); QVERIFY(force(item));
    QCOMPARE(reclaim.count(),0); QVERIFY(!window.findChild<QMessageBox*>(QStringLiteral("files-reclaim-confirmation")));
  }
  void confirmationRejectsSameTargetReplacementAndNewGeneration() {
    for (bool replaceProfile : {false,true}) {
      ui::LibraryWindow window; const auto target=prepare(window); QVERIFY(select(window,{7}));
      auto* item=action(window); QVERIFY(item); QTRY_VERIFY(item->isEnabled());
      QSignalSpy reclaim(&window,&ui::LibraryWindow::fileCompactionRequested);
      QTimer::singleShot(0,&window,[&] {
        auto* box=window.findChild<QMessageBox*>(QStringLiteral("files-reclaim-confirmation")); QVERIFY(box);
        if (replaceProfile) { window.setPresentation(library(2)); }
        else { window.setDetails({target,QStringLiteral("same-target replacement"),100,files(target)}); }
        box->button(QMessageBox::Yes)->click();
      });
      item->trigger(); QCOMPARE(reclaim.count(),0);
    }
  }
  void confirmationAndDeliveryMayDeleteOwnerAndNestedIntentIsSuppressed() {
    {
      auto owner=std::make_unique<ui::LibraryWindow>(); QPointer<ui::LibraryWindow> window=owner.get();
      prepare(*window); QVERIFY(select(*window,{7})); auto* item=action(*window); QVERIFY(item); QTRY_VERIFY(item->isEnabled());
      QTimer::singleShot(0,window,[&owner] { owner.reset(); }); item->trigger(); QVERIFY(window.isNull());
    }
    auto owner=std::make_unique<ui::LibraryWindow>(); QPointer<ui::LibraryWindow> window=owner.get();
    prepare(*window); QVERIFY(select(*window,{7})); auto* item=action(*window); QVERIFY(item); QTRY_VERIFY(item->isEnabled());
    int calls=0; connect(window,&ui::LibraryWindow::fileCompactionRequested,this,[&](ui::Target,int index) {
      ++calls; QCOMPARE(index,7); QVERIFY(force(item)); owner.reset();
    });
    answer(window,QMessageBox::Yes); item->trigger(); QVERIFY(window.isNull()); QCOMPARE(calls,1);
  }
};
QTEST_MAIN(FileCompactionUiTests)
#include "file_compaction_ui_tests.moc"
