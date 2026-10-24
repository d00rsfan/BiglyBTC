#include "scheduler_dialog.hpp"

#include <QCheckBox>
#include <QEvent>
#include <QLabel>
#include <QHeaderView>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTableWidget>
#include <QtTest>
#include <functional>
#include <limits>

using namespace biglybtc::ui;
namespace {
SchedulerPresentation packet(SchedulerRequest request, quint64 revision=1,
    bool enabled=false, int upload=17, int poll=10000) {
  auto values=std::make_shared<SchedulerValues>();
  values->preferences={enabled,false,false,poll};
  values->configuration.defaultUpload=upload;
  values->configuration.defaultDownload=31;
  values->configuration.defaultsEnforce=true;
  schedule::Schedule item;
  item.enabled=true;item.days.fill(true);item.start=60;item.end=120;
  item.upload=42;item.download=77;item.category=std::string("owned π");
  values->configuration.schedules.push_back(std::move(item));
  values->hasSavedSchedules=true;
  return std::make_shared<const SchedulerPacket>(SchedulerPacket{request.session,request.request,revision,std::move(values),{}});
}
SchedulerSavePresentation saved(const SchedulerSubmission& request, bool complete,
    bool xmlSaved, SchedulerPresentation snapshot, QString failure={}) {
  return std::make_shared<const SchedulerSavePacket>(SchedulerSavePacket{
      request.session,request.request,request.revision,complete,xmlSaved,std::move(snapshot),std::move(failure)});
}
class EnabledHook final:public QObject {
public:
  std::function<void()> callback;
protected:
  bool eventFilter(QObject* watched,QEvent* event) override {
    const QPointer<QObject> alive(watched);
    if(event->type()==QEvent::EnabledChange&&callback){auto once=std::move(callback);once();}
    return !alive;
  }
};
}

class SchedulerDialogTests final:public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    qRegisterMetaType<SchedulerRequest>();qRegisterMetaType<SchedulerSubmission>();
    qRegisterMetaType<SchedulerPresentation>();qRegisterMetaType<SchedulerSavePresentation>();
  }
  void lazyLoadAndFullPositiveJavaPollRange() {
    SchedulerDialog dialog;QSignalSpy reads(&dialog,&SchedulerDialog::loadRequested);
    QVERIFY(dialog.beginSession(7));QCOMPARE(reads.count(),0);
    dialog.show();QTRY_COMPARE(reads.count(),1);
    const auto request=reads.at(0).at(0).value<SchedulerRequest>();
    QVERIFY(request.session==7&&request.request>0);
    QVERIFY(dialog.setSnapshot(packet(request,1,false,17,std::numeric_limits<int>::max())));
    auto* poll=dialog.findChild<QSpinBox*>("scheduler-poll");QVERIFY(poll);
    QCOMPARE(poll->value(),std::numeric_limits<int>::max());
    auto* table=dialog.findChild<QTableWidget*>("scheduler-schedules");QVERIFY(table);
    QCOMPARE(table->rowCount(),1);QCOMPARE(table->columnCount(),11);
    QCOMPARE(table->verticalHeader()->count(),1);
    QVERIFY(!dialog.pending());QVERIFY(!dialog.dirty());QCOMPARE(reads.count(),1);
  }
  void staleSessionsRequestsAndMalformedSnapshotCannotReplaceDraft() {
    SchedulerDialog dialog;QSignalSpy reads(&dialog,&SchedulerDialog::loadRequested);
    QVERIFY(dialog.beginSession(3));dialog.show();QTRY_COMPARE(reads.count(),1);
    const auto first=reads.at(0).at(0).value<SchedulerRequest>();
    QVERIFY(!dialog.setSnapshot(packet({3,first.request+1},9,true,999)));
    QVERIFY(dialog.pending());QVERIFY(dialog.setSnapshot(packet(first,4)));
    auto* upload=dialog.findChild<QSpinBox*>("scheduler-default-upload");QVERIFY(upload);
    upload->setValue(23);QVERIFY(dialog.dirty());
    QVERIFY(!dialog.setSnapshot(packet(first,99,true,999)));QCOMPARE(upload->value(),23);
    QVERIFY(!dialog.beginSession(0));QVERIFY(!dialog.beginSession(3));QVERIFY(!dialog.beginSession(2));
    QVERIFY(dialog.beginSession(4));QTRY_COMPARE(reads.count(),2);
    const auto next=reads.at(1).at(0).value<SchedulerRequest>();
    QVERIFY(!dialog.setSnapshot(packet(first,100,true,999)));QVERIFY(dialog.pending());
    auto malformed=std::make_shared<SchedulerPacket>(*packet(next));malformed->revision=0;
    QVERIFY(!dialog.setSnapshot(malformed));QVERIFY(!dialog.pending());
    auto* reload=dialog.findChild<QPushButton*>("scheduler-reload");QVERIFY(reload);QVERIFY(reload->isEnabled());
    reload->click();QTRY_COMPARE(reads.count(),3);
    QVERIFY(dialog.setSnapshot(packet(reads.at(2).at(0).value<SchedulerRequest>(),5,false,47)));
    QCOMPARE(upload->value(),47);QVERIFY(!dialog.dirty());
  }
  void renderBlocksEditSignalsButKeepsModelAndHeaderNotifications() {
    SchedulerDialog dialog;QSignalSpy reads(&dialog,&SchedulerDialog::loadRequested);
    QVERIFY(dialog.beginSession(1));dialog.show();QTRY_COMPARE(reads.count(),1);
    auto* enabled=dialog.findChild<QCheckBox*>("scheduler-enabled");QVERIFY(enabled);
    QSignalSpy toggles(enabled,&QCheckBox::toggled);
    auto* table=dialog.findChild<QTableWidget*>("scheduler-schedules");QVERIFY(table);
    QSignalSpy rows(table->model(),&QAbstractItemModel::rowsInserted);
    QVERIFY(dialog.setSnapshot(packet(reads.at(0).at(0).value<SchedulerRequest>(),1,true)));
    QVERIFY(enabled->isChecked());QCOMPARE(toggles.count(),0);QVERIFY(rows.count()>0);
    QCOMPARE(table->verticalHeader()->count(),1);QVERIFY(!dialog.dirty());
    enabled->setChecked(false);QCOMPARE(toggles.count(),1);QVERIFY(dialog.dirty());
  }
  void partialXmlSaveRebasesRowsAndRetainsOnlyUncommittedPreferences() {
    SchedulerDialog dialog;QSignalSpy reads(&dialog,&SchedulerDialog::loadRequested);
    QSignalSpy writes(&dialog,&SchedulerDialog::saveRequested);
    QVERIFY(dialog.beginSession(1));dialog.show();QTRY_COMPARE(reads.count(),1);
    QVERIFY(dialog.setSnapshot(packet(reads.at(0).at(0).value<SchedulerRequest>(),10)));
    auto* enabled=dialog.findChild<QCheckBox*>("scheduler-enabled");QVERIFY(enabled);enabled->setChecked(true);
    auto* upload=dialog.findChild<QSpinBox*>("scheduler-default-upload");QVERIFY(upload);upload->setValue(91);
    auto* apply=dialog.findChild<QPushButton*>("scheduler-apply");QVERIFY(apply);apply->click();
    QCOMPARE(writes.count(),1);const auto first=writes.at(0).at(0).value<SchedulerSubmission>();
    QVERIFY(first.session==1&&first.request>0&&first.revision==10);QCOMPARE(first.defaultUpload,91);
    QVERIFY(first.preferences.enabled);QVERIFY(dialog.pending());
    QVERIFY(dialog.setSaveResult(saved(first,false,true,packet({1,first.request},11,false,91),QStringLiteral("Settings save failed"))));
    QVERIFY(!dialog.pending());QVERIFY(dialog.dirty());QVERIFY(enabled->isChecked());QCOMPARE(upload->value(),91);
    apply->click();QCOMPARE(writes.count(),2);const auto second=writes.at(1).at(0).value<SchedulerSubmission>();
    QCOMPARE(second.revision,quint64(11));QVERIFY(second.request>first.request);QVERIFY(second.preferences.enabled);
    QVERIFY(!dialog.setSaveResult(saved(first,true,true,packet({1,first.request},12,true,91))));QVERIFY(dialog.pending());
    QVERIFY(dialog.setSaveResult(saved(second,true,true,packet({1,second.request},12,true,91))));
    QVERIFY(!dialog.pending());QVERIFY(!dialog.dirty());QVERIFY(enabled->isChecked());
    QCOMPARE(dialog.findChild<QLabel*>("scheduler-status")->text(),QStringLiteral("Schedules and settings saved"));
  }
  void failedSaveRetainsWholeDraftAndCanRetry() {
    SchedulerDialog dialog;QSignalSpy reads(&dialog,&SchedulerDialog::loadRequested);
    QSignalSpy writes(&dialog,&SchedulerDialog::saveRequested);
    QVERIFY(dialog.beginSession(1));dialog.show();QTRY_COMPARE(reads.count(),1);
    QVERIFY(dialog.setSnapshot(packet(reads.at(0).at(0).value<SchedulerRequest>(),1)));
    auto* upload=dialog.findChild<QSpinBox*>("scheduler-default-upload");QVERIFY(upload);upload->setValue(123);
    auto* apply=dialog.findChild<QPushButton*>("scheduler-apply");QVERIFY(apply);apply->click();
    QCOMPARE(writes.count(),1);const auto request=writes.at(0).at(0).value<SchedulerSubmission>();
    QVERIFY(dialog.setSaveResult(saved(request,false,false,{},QStringLiteral("<owned CAS conflict>"))));
    QVERIFY(dialog.dirty());QVERIFY(!dialog.pending());QCOMPARE(upload->value(),123);
    auto* status=dialog.findChild<QLabel*>("scheduler-status");QVERIFY(status);
    QCOMPARE(status->textFormat(),Qt::PlainText);QCOMPARE(status->text(),QStringLiteral("<owned CAS conflict>"));
    apply->click();QCOMPARE(writes.count(),2);QVERIFY(dialog.pending());
    const auto retry=writes.at(1).at(0).value<SchedulerSubmission>();QCOMPARE(retry.revision,request.revision);
  }
  void directLoadAndSaveReceiversMayDeleteDialog() {
    QPointer<SchedulerDialog> loading=new SchedulerDialog;
    connect(loading,&SchedulerDialog::loadRequested,this,[&](SchedulerRequest){delete loading.data();});
    QVERIFY(loading->beginSession(1));loading->show();QTRY_VERIFY(loading.isNull());
    QPointer<SchedulerDialog> saving=new SchedulerDialog;QSignalSpy reads(saving,&SchedulerDialog::loadRequested);
    QVERIFY(saving->beginSession(1));saving->show();QTRY_COMPARE(reads.count(),1);
    QVERIFY(saving->setSnapshot(packet(reads.at(0).at(0).value<SchedulerRequest>())));
    saving->findChild<QSpinBox*>("scheduler-default-upload")->setValue(41);
    connect(saving,&SchedulerDialog::saveRequested,this,[&](SchedulerSubmission){delete saving.data();});
    auto* apply=saving->findChild<QPushButton*>("scheduler-apply");QVERIFY(apply);apply->click();
    QVERIFY(saving.isNull());
  }
  void enabledChangeDuringRenderMayDeleteOwnerWithoutRestoringDeadBlockers() {
    QPointer<SchedulerDialog> dialog=new SchedulerDialog;QSignalSpy reads(dialog,&SchedulerDialog::loadRequested);
    QVERIFY(dialog->beginSession(1));dialog->show();QTRY_COMPARE(reads.count(),1);
    auto* table=dialog->findChild<QTableWidget*>("scheduler-schedules");QVERIFY(table);QVERIFY(!table->isEnabled());
    EnabledHook hook;hook.callback=[&]{delete dialog.data();};table->installEventFilter(&hook);
    static_cast<void>(dialog->setSnapshot(packet(reads.at(0).at(0).value<SchedulerRequest>())));
    QVERIFY(dialog.isNull());
  }
  void enabledChangeDuringRenderCanBindNewSessionAndOldRenderStops() {
    SchedulerDialog dialog;QSignalSpy reads(&dialog,&SchedulerDialog::loadRequested);
    QVERIFY(dialog.beginSession(1));dialog.show();QTRY_COMPARE(reads.count(),1);
    auto* table=dialog.findChild<QTableWidget*>("scheduler-schedules");QVERIFY(table);
    EnabledHook hook;hook.callback=[&]{QVERIFY(dialog.beginSession(2));};table->installEventFilter(&hook);
    const auto old=reads.at(0).at(0).value<SchedulerRequest>();
    QVERIFY(!dialog.setSnapshot(packet(old,8,true,100)));
    QTRY_COMPARE(reads.count(),2);QVERIFY(dialog.pending());QVERIFY(!dialog.dirty());
    QVERIFY(!dialog.setSnapshot(packet(old,9,true,200)));
    const auto fresh=reads.at(1).at(0).value<SchedulerRequest>();QCOMPARE(fresh.session,quint64(2));
    QVERIFY(dialog.setSnapshot(packet(fresh,1,false,37)));
    QCOMPARE(dialog.findChild<QSpinBox*>("scheduler-default-upload")->value(),37);
    QVERIFY(!dialog.findChild<QCheckBox*>("scheduler-enabled")->isChecked());QVERIFY(!dialog.dirty());
  }
  void deletedRowControlRefusesSaveAndKeepsDirtyDraft() {
    SchedulerDialog dialog;QSignalSpy reads(&dialog,&SchedulerDialog::loadRequested);
    QSignalSpy writes(&dialog,&SchedulerDialog::saveRequested);
    QVERIFY(dialog.beginSession(1));dialog.show();QTRY_COMPARE(reads.count(),1);
    QVERIFY(dialog.setSnapshot(packet(reads.at(0).at(0).value<SchedulerRequest>())));
    dialog.findChild<QSpinBox*>("scheduler-default-upload")->setValue(18);
    auto* table=dialog.findChild<QTableWidget*>("scheduler-schedules");QVERIFY(table);
    delete table->cellWidget(0,4);
    dialog.findChild<QPushButton*>("scheduler-apply")->click();
    QCOMPARE(writes.count(),0);QVERIFY(!dialog.pending());QVERIFY(dialog.dirty());
    QVERIFY(dialog.findChild<QLabel*>("scheduler-status")->text().contains(QStringLiteral("unavailable")));
  }
};

QTEST_MAIN(SchedulerDialogTests)
#include "scheduler_dialog_tests.moc"
