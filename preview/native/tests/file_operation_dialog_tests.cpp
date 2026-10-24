#include "file_operation_dialog.hpp"
#include <QCheckBox>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QtTest>
#include <functional>
#include <unistd.h>
using namespace biglybtc::ui;
namespace {
Target target(){return {QString(40,QChar('a')),1};}
std::shared_ptr<FileOperationPlanPacket> planFor(const FileOperationRequest& request) {
  auto plan=std::make_shared<FileOperationPlanPacket>();plan->request=request;plan->approvalScopeDigest=QString(64,QChar('b'));
  plan->sourceRoot=QStringLiteral("/owned/source");plan->targetRoot=request.destinationParent;
  plan->paths={{QStringLiteral("/owned/source/<data>"),request.destinationParent.isEmpty()?QString{}:request.destinationParent+QStringLiteral("/data"),4096,true},
      {QStringLiteral("/owned/source/missing"),{},0,false},{QStringLiteral("/owned/source/empty"),{},0,true}};
  plan->presentFiles=2;plan->missingFiles=1;plan->totalBytes=4096;return plan;
}
bool capture(QWidget& widget) {
  const auto root=qgetenv("BIGLYBTC_FILEOPS_CAPTURE_DIRECTORY");if(root.isEmpty()){return true;}
  const QFileInfo info(QString::fromLocal8Bit(root));if(!info.isAbsolute()||!info.isDir()||info.isSymLink()||info.ownerId()!=static_cast<uint>(::geteuid())){return false;}
  const auto destination=QDir(info.canonicalFilePath()).filePath(QStringLiteral("file-operation-preview.png"));if(QFileInfo(destination).isSymLink()){return false;}
  return widget.grab().save(destination,"PNG");
}
class EnabledHook final:public QObject {
public:std::function<void()> callback;
protected:bool eventFilter(QObject* watched,QEvent* event)override {
  const QPointer<QObject> alive(watched);if(event->type()==QEvent::EnabledChange&&callback){auto once=std::move(callback);once();}return !alive;
}
};
}
class FileOperationDialogTests final:public QObject {
 Q_OBJECT
private slots:
 void initTestCase(){qRegisterMetaType<FileOperationRequest>();qRegisterMetaType<FileOperationSubmission>();}
 void exactOwnedPlanAndExplicitDeleteConfirmation() {
  FileOperationDialog dialog;QSignalSpy reads(&dialog,&FileOperationDialog::planRequested),writes(&dialog,&FileOperationDialog::executeRequested);
  QVERIFY(dialog.beginOperation(1,7,target(),FileOperation::RemoveDeleteData));QCOMPARE(reads.count(),0);
  dialog.show();QTRY_COMPARE(reads.count(),1);const auto request=reads[0][0].value<FileOperationRequest>();
  auto packet=planFor(request);QVERIFY(dialog.setPlan(packet));
  auto* table=dialog.findChild<QTableView*>("file-operation-paths");QVERIFY(table);QCOMPARE(table->model()->rowCount(),3);
  QCOMPARE(table->model()->index(0,0).data().toString(),QStringLiteral("/owned/source/<data>"));
  QCOMPARE(table->model()->index(1,2).data().toString(),QStringLiteral("—"));QVERIFY(!table->model()->index(1,2).data(Qt::UserRole).isValid());
  QCOMPARE(table->model()->index(2,2).data(Qt::UserRole).toULongLong(),quint64(0));
  auto* execute=dialog.findChild<QPushButton*>("file-operation-execute");QVERIFY(!execute->isEnabled());dialog.accept();QCOMPARE(writes.count(),0);
  packet->paths[0].source=QStringLiteral("/mutated/alias");packet->approvalScopeDigest=QString(64,QChar('c'));
  auto* confirmation=dialog.findChild<QCheckBox*>("file-operation-confirm-delete");confirmation->setChecked(true);QVERIFY(execute->isEnabled());
  QVERIFY(capture(dialog));execute->click();QCOMPARE(writes.count(),1);const auto submitted=writes[0][0].value<FileOperationSubmission>();
  QVERIFY(submitted.deleteConfirmed);QCOMPARE(submitted.plan->approvalScopeDigest,QString(64,QChar('b')));QCOMPARE(submitted.plan->paths[0].source,QStringLiteral("/owned/source/<data>"));QVERIFY(dialog.pending());
 }
 void allOperationsKeepTargetIdentityAndDestinationEditsInvalidateApproval() {
  for(auto operation:{FileOperation::RemoveKeepData,FileOperation::Relocate,FileOperation::Relink}) {
   FileOperationDialog dialog;QSignalSpy reads(&dialog,&FileOperationDialog::planRequested),writes(&dialog,&FileOperationDialog::executeRequested);
   QVERIFY(dialog.beginOperation(9,3,target(),operation,QStringLiteral("/owned/target")));dialog.show();QTRY_COMPARE(reads.count(),1);
   auto request=reads[0][0].value<FileOperationRequest>();QCOMPARE(request.operation,operation);QCOMPARE(request.target,target());QVERIFY(dialog.setPlan(planFor(request)));
   if(operation!=FileOperation::RemoveKeepData) {
    dialog.findChild<QLineEdit*>("file-operation-destination")->setText(QStringLiteral("/owned/new"));QVERIFY(!dialog.plan());
    dialog.accept();QCOMPARE(writes.count(),0);dialog.findChild<QPushButton*>("file-operation-review")->click();QCOMPARE(reads.count(),2);
    QVERIFY(!dialog.setPlan(planFor(request)));request=reads[1][0].value<FileOperationRequest>();QCOMPARE(request.destinationParent,QStringLiteral("/owned/new"));QVERIFY(dialog.setPlan(planFor(request)));
   }
   dialog.accept();QCOMPARE(writes.count(),1);const auto submission=writes[0][0].value<FileOperationSubmission>();QVERIFY(!submission.deleteConfirmed);QCOMPARE(submission.plan->request,request);
  }
 }
 void busyFirstShowAndStaleOrMalformedPlansCannotAuthorize() {
  FileOperationDialog dialog;QSignalSpy reads(&dialog,&FileOperationDialog::planRequested),writes(&dialog,&FileOperationDialog::executeRequested);
  QVERIFY(!dialog.beginOperation(0,1,target(),FileOperation::Relink));QVERIFY(dialog.beginOperation(4,8,target(),FileOperation::Relink,QStringLiteral("/owned/new")));
  dialog.setBusy(true);dialog.show();QTest::qWait(1);QCOMPARE(reads.count(),0);dialog.setBusy(false);QTRY_COMPARE(reads.count(),1);
  auto request=reads[0][0].value<FileOperationRequest>();auto old=planFor(request);old->request.revision++;QVERIFY(!dialog.setPlan(old));QVERIFY(dialog.pending());
  auto malformed=planFor(request);malformed->totalBytes++;QVERIFY(!dialog.setPlan(malformed));QVERIFY(!dialog.plan());dialog.accept();QCOMPARE(writes.count(),0);
  dialog.findChild<QPushButton*>("file-operation-review")->click();QCOMPARE(reads.count(),2);request=reads[1][0].value<FileOperationRequest>();
  auto failed=planFor(request);failed->failure=QStringLiteral("<stale inode>");QVERIFY(!dialog.setPlan(failed));
  auto* status=dialog.findChild<QLabel*>("file-operation-status");QCOMPARE(status->textFormat(),Qt::PlainText);QCOMPARE(status->text(),failed->failure);
 }
 void cancelWaitsForActualOutcomeAndProgressIsOrdered() {
  FileOperationDialog dialog;QSignalSpy reads(&dialog,&FileOperationDialog::planRequested),cancels(&dialog,&FileOperationDialog::cancelRequested);
  QVERIFY(dialog.beginOperation(1,1,target(),FileOperation::RemoveKeepData));dialog.show();QTRY_COMPARE(reads.count(),1);
  const auto request=reads[0][0].value<FileOperationRequest>();QVERIFY(dialog.setPlan(planFor(request)));dialog.accept();QVERIFY(dialog.pending());
  auto progress=std::make_shared<FileOperationProgressPacket>(FileOperationProgressPacket{request,1,1,2,QStringLiteral("<data>")});QVERIFY(dialog.setProgress(progress));QVERIFY(!dialog.setProgress(progress));
  auto regressed=std::make_shared<FileOperationProgressPacket>(*progress);regressed->sequence=2;regressed->completedFiles=0;QVERIFY(!dialog.setProgress(regressed));
  dialog.reject();QCOMPARE(cancels.count(),1);dialog.reject();QCOMPARE(cancels.count(),1);QVERIFY(dialog.isVisible());QVERIFY(dialog.pending());
  auto result=std::make_shared<FileOperationResultPacket>(FileOperationResultPacket{request,true,false,{}});QVERIFY(dialog.setResult(result));QVERIFY(!dialog.pending());QVERIFY(!dialog.isVisible());QCOMPARE(dialog.result(),int(QDialog::Accepted));
 }
 void reentrantPlanAndExecuteReceiversMayDeleteOwner() {
  QPointer<FileOperationDialog> dialog=new FileOperationDialog;connect(dialog,&FileOperationDialog::planRequested,this,[&](FileOperationRequest){delete dialog.data();});
  QVERIFY(dialog->beginOperation(1,1,target(),FileOperation::RemoveKeepData));dialog->show();QTRY_VERIFY(dialog.isNull());
  dialog=new FileOperationDialog;QSignalSpy reads(dialog,&FileOperationDialog::planRequested);QVERIFY(dialog->beginOperation(1,1,target(),FileOperation::RemoveKeepData));dialog->show();QTRY_COMPARE(reads.count(),1);QVERIFY(dialog->setPlan(planFor(reads[0][0].value<FileOperationRequest>())));
  connect(dialog,&FileOperationDialog::executeRequested,this,[&](FileOperationSubmission){delete dialog.data();});dialog->accept();QVERIFY(dialog.isNull());
 }
 void enabledCallbackCanRebindAndInvalidateOuterPlan() {
  FileOperationDialog dialog;QSignalSpy reads(&dialog,&FileOperationDialog::planRequested);QVERIFY(dialog.beginOperation(1,1,target(),FileOperation::RemoveKeepData));dialog.show();QTRY_COMPARE(reads.count(),1);
  auto* execute=dialog.findChild<QPushButton*>("file-operation-execute");EnabledHook hook;hook.callback=[&]{QVERIFY(dialog.beginOperation(2,9,target(),FileOperation::RemoveKeepData));};execute->installEventFilter(&hook);
  QVERIFY(!dialog.setPlan(planFor(reads[0][0].value<FileOperationRequest>())));QTRY_COMPARE(reads.count(),2);QVERIFY(dialog.pending());QVERIFY(!dialog.plan());
  const auto current=reads[1][0].value<FileOperationRequest>();QCOMPARE(current.session,quint64(2));QVERIFY(dialog.setPlan(planFor(current)));
 }
 void profileInvalidationDropsPendingReplies() {
  FileOperationDialog dialog;QSignalSpy reads(&dialog,&FileOperationDialog::planRequested);QVERIFY(dialog.beginOperation(2,1,target(),FileOperation::RemoveKeepData));dialog.show();QTRY_COMPARE(reads.count(),1);
  const auto old=reads[0][0].value<FileOperationRequest>();dialog.invalidateSession();QVERIFY(!dialog.pending());QVERIFY(!dialog.isVisible());QVERIFY(!dialog.setPlan(planFor(old)));QVERIFY(!dialog.beginOperation(1,1,target(),FileOperation::RemoveKeepData));
 }
 void reopenedSameSessionNeverReusesOldRequest() {
  FileOperationRequest old;
  {FileOperationDialog first;QSignalSpy reads(&first,&FileOperationDialog::planRequested);QVERIFY(first.beginOperation(1,1,target(),FileOperation::RemoveKeepData));first.show();QTRY_COMPARE(reads.count(),1);old=reads[0][0].value<FileOperationRequest>();first.reject();}
  FileOperationDialog next;QSignalSpy reads(&next,&FileOperationDialog::planRequested);QVERIFY(next.beginOperation(1,1,target(),FileOperation::RemoveKeepData));next.show();QTRY_COMPARE(reads.count(),1);
  const auto current=reads[0][0].value<FileOperationRequest>();QVERIFY(current.request>old.request);QVERIFY(!next.setPlan(planFor(old)));QVERIFY(next.pending());QVERIFY(next.setPlan(planFor(current)));
 }
};
QTEST_MAIN(FileOperationDialogTests)
#include "file_operation_dialog_tests.moc"
