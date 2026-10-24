#include "scheduler_host_fixture.hpp"
#include "library_window.hpp"
#include "scheduler_dialog.hpp"
#include <QAction>
#include <QApplication>
#include <QPushButton>
#include <QSpinBox>
#include <QtTest>

using namespace biglybtc;
namespace sh=test::scheduler_host;
namespace {
struct Fixture{
  QTemporaryDir root{QStringLiteral("/tmp/biglybtc-scheduler-host-XXXXXX")};QString profile=root.path()+QStringLiteral("/profile");
  Fixture(){sh::require(root.isValid()&&QDir().mkdir(profile),"Create generated scheduler Host profile");sh::write(profile+"/downloads.config","d9:downloadslee");sh::write(profile+"/biglybt.config","d7:unknownli7e3:rawee");sh::original(profile);}
};
}
class SchedulerHostTests final:public QObject{
  Q_OBJECT
private slots:
  void initTestCase(){QApplication::setQuitOnLastWindowClosed(false);}
  void realModuleSparseSaveAndPartialConfigCasRemainExplicit(){
    Fixture fixture;sh::ApprovedModule module;app::SessionBridge bridge(nullptr,{},{},module.factory());
    QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),reads(&bridge,&app::SessionBridge::schedulerReady),writes(&bridge,&app::SessionBridge::schedulerSaved),closed(&bridge,&app::SessionBridge::sessionClosed);
    const auto config=sh::read(fixture.profile+"/biglybt.config"),xml=sh::read(sh::schedulerFile(fixture.profile));
    QVERIFY(bridge.open(fixture.profile.toStdString(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty(),10000);const auto session=sessions.first()[0].toULongLong();
    QVERIFY(bridge.loadScheduler({session,1}));QTRY_COMPARE_WITH_TIMEOUT(reads.size(),1,10000);QTRY_VERIFY(!bridge.busy());
    auto packet=sh::last(reads);QVERIFY2(packet&&packet->snapshot,qPrintable(packet?packet->failure:QStringLiteral("No packet")));QVERIFY(packet->revision);QVERIFY(!packet->snapshot->preferences.enabled);
    QCOMPARE(sh::read(fixture.profile+"/biglybt.config"),config);QCOMPARE(sh::read(sh::schedulerFile(fixture.profile)),xml);
    auto change=sh::submission(packet,2);change.schedules[0].value.upload=23;
    QVERIFY(bridge.saveScheduler(change));QTRY_COMPARE_WITH_TIMEOUT(writes.size(),1,10000);QTRY_VERIFY(!bridge.busy());
    auto result=sh::saved(writes);QVERIFY2(result->saved,qPrintable(result->failure));QVERIFY(result->schedulesSaved);QVERIFY(result->snapshot&&result->snapshot->revision>packet->revision);
    QCOMPARE(result->snapshot->snapshot->configuration.schedules[0].upload,23);QVERIFY(sh::read(sh::schedulerFile(fixture.profile)).contains("<!--untouched-->"));QVERIFY(sh::read(sh::schedulerFile(fixture.profile)).contains("<future value=\"preserved\"/>"));QCOMPARE(sh::read(fixture.profile+"/biglybt.config"),config);
    const auto after=sh::read(sh::schedulerFile(fixture.profile));QVERIFY(bridge.saveScheduler(change));QTRY_COMPARE(writes.size(),2);QTRY_VERIFY(!bridge.busy());QVERIFY(!sh::saved(writes)->saved);QCOMPARE(sh::read(sh::schedulerFile(fixture.profile)),after);
    auto partial=sh::submission(result->snapshot,3);partial.schedules[0].value.download=29;
    auto external=sh::document(config);QVERIFY(external.replaceValue("external","i88e"));const auto edited=QByteArray::fromStdString(external.serialize());sh::write(fixture.profile+"/biglybt.config",edited);
    QVERIFY(bridge.saveScheduler(partial));QTRY_COMPARE_WITH_TIMEOUT(writes.size(),3,10000);QTRY_VERIFY(!bridge.busy());
    result=sh::saved(writes);QVERIFY(!result->saved);QVERIFY(result->schedulesSaved);QVERIFY(!result->failure.isEmpty());QVERIFY(result->snapshot);QCOMPARE(result->snapshot->snapshot->configuration.schedules[0].download,29);QCOMPARE(sh::read(fixture.profile+"/biglybt.config"),edited);
    QVERIFY(!bridge.loadScheduler({session+1,4}));QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,10000);QVERIFY(closed.first()[1].toBool());QVERIFY(closed.first()[2].toBool());
  }
  void toolsDialogUsesActualWorkerAndFinalRevision(){
    Fixture fixture;sh::ApprovedModule module;app::SessionBridge bridge(nullptr,{},{},module.factory());ui::LibraryWindow window;
    connect(&bridge,&app::SessionBridge::snapshotReady,&window,&ui::LibraryWindow::setPresentation);connect(&bridge,&app::SessionBridge::columnLayoutSessionReady,&window,&ui::LibraryWindow::beginColumnLayoutSession);connect(&bridge,&app::SessionBridge::busyChanged,&window,&ui::LibraryWindow::setBusy);
    connect(&bridge,&app::SessionBridge::schedulerReady,&window,&ui::LibraryWindow::setScheduler);connect(&bridge,&app::SessionBridge::schedulerSaved,&window,&ui::LibraryWindow::setSchedulerSaved);
    connect(&window,&ui::LibraryWindow::schedulerRequested,&bridge,[&](ui::SchedulerRequest r){if(!bridge.loadScheduler(r)){window.rejectSchedulerRequest(r);}});connect(&window,&ui::LibraryWindow::schedulerSaveRequested,&bridge,[&](ui::SchedulerSubmission r){if(!bridge.saveScheduler(r)){window.rejectSchedulerSave(r);}});
    QSignalSpy writes(&bridge,&app::SessionBridge::schedulerSaved);QVERIFY(bridge.open(fixture.profile.toStdString(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000);
    window.show();auto* action=window.findChild<QAction*>("classic-speed-scheduler");QVERIFY(action);action->trigger();auto* dialog=window.findChild<ui::SchedulerDialog*>();QVERIFY(dialog);QTRY_VERIFY_WITH_TIMEOUT(!dialog->pending(),10000);
    auto* upload=dialog->findChild<QSpinBox*>("scheduler-default-upload");auto* apply=dialog->findChild<QPushButton*>("scheduler-apply");QVERIFY(upload);QVERIFY(apply);QTRY_VERIFY_WITH_TIMEOUT(upload->isEnabled(),10000);upload->setValue(31);QVERIFY(dialog->dirty());apply->click();
    QTRY_COMPARE_WITH_TIMEOUT(writes.size(),1,10000);QVERIFY2(sh::saved(writes)->saved,qPrintable(sh::saved(writes)->failure));QTRY_VERIFY(!dialog->dirty()&&!dialog->pending());QCOMPARE(upload->value(),31);QVERIFY(window.close());
  }
};
QTEST_MAIN(SchedulerHostTests)
#include "scheduler_host_tests.moc"
