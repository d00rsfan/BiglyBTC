#include "torrent_creation_dialog.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest>
#include <functional>
#include <unistd.h>
using namespace biglybtc::ui;
namespace {
void fill(TorrentCreationDialog& dialog) {
 dialog.findChild<QLineEdit*>("torrent-creation-source")->setText(QStringLiteral("/owned/source"));
 dialog.findChild<QLineEdit*>("torrent-creation-output")->setText(QStringLiteral("/owned/result.torrent"));
}
TorrentCreationResultPresentation result(const TorrentCreationRequest& request,bool completed=false,bool cancelled=false,QString failure={}) {
 return std::make_shared<const TorrentCreationResultPacket>(TorrentCreationResultPacket{request.session,request.request,request.revision,completed,cancelled,request.options.output,completed?QString(40,QChar('d')):QString{},std::move(failure),completed});
}
bool capture(QWidget& widget) {
 const auto root=qgetenv("BIGLYBTC_FILEOPS_CAPTURE_DIRECTORY");if(root.isEmpty()){return true;}
 const QFileInfo info(QString::fromLocal8Bit(root));if(!info.isAbsolute()||!info.isDir()||info.isSymLink()||info.ownerId()!=static_cast<uint>(::geteuid())){return false;}
 const auto destination=QDir(info.canonicalFilePath()).filePath(QStringLiteral("torrent-creation-preview.png"));if(QFileInfo(destination).isSymLink()){return false;}return widget.grab().save(destination,"PNG");
}
class EnabledHook final:public QObject {
public:std::function<void()> callback;
protected:bool eventFilter(QObject* watched,QEvent* event)override {
 const QPointer<QObject> alive(watched);if(event->type()==QEvent::EnabledChange&&callback){auto once=std::move(callback);once();}return !alive;
}
};
}
class TorrentCreationDialogTests final:public QObject {
 Q_OBJECT
private slots:
 void initTestCase(){qRegisterMetaType<TorrentCreationRequest>();}
 void explicitV1OptionsAndOriginalPieceChoicesArePreserved() {
  TorrentCreationDialog dialog;QSignalSpy requests(&dialog,&TorrentCreationDialog::createRequested);
  QVERIFY(dialog.beginSession(8,4));dialog.show();fill(dialog);QCOMPARE(requests.count(),0);
  auto* piece=dialog.findChild<QComboBox*>("torrent-creation-piece-size");QVERIFY(piece);QCOMPARE(piece->count(),21);QCOMPARE(piece->currentData().toULongLong(),quint64(0));
  QCOMPARE(piece->itemData(2).toULongLong(),quint64(49152));QCOMPARE(piece->itemData(20).toULongLong(),quint64(134217728));
  auto* import=dialog.findChild<QCheckBox*>("torrent-creation-import");QVERIFY(!import->isChecked());
  dialog.findChild<QComboBox*>("torrent-creation-source-kind")->setCurrentIndex(1);piece->setCurrentIndex(2);
  dialog.findChild<QPlainTextEdit*>("torrent-creation-trackers")->setPlainText(QStringLiteral("https://tracker.invalid/announce\nudp://127.0.0.1:1234/announce\n\nhttp://other.invalid/a,b"));
  dialog.findChild<QCheckBox*>("torrent-creation-private")->setChecked(true);dialog.findChild<QPlainTextEdit*>("torrent-creation-comment")->setPlainText(QStringLiteral("<owned π>"));
  QVERIFY(capture(dialog));dialog.accept();QCOMPARE(requests.count(),1);const auto request=requests[0][0].value<TorrentCreationRequest>();
  QCOMPARE(request.session,quint64(8));QCOMPARE(request.revision,quint64(4));QVERIFY(request.request>0);QCOMPARE(request.options.sourceKind,TorrentCreationSource::Directory);
  QCOMPARE(request.options.pieceSize,quint64(49152));QVERIFY(request.options.privateTorrent);QVERIFY(!request.options.importAfterCreation);
  QCOMPARE(request.options.trackerTiers.size(),std::size_t(2));QCOMPARE(request.options.trackerTiers[0].size(),2);QCOMPARE(request.options.trackerTiers[1][0],QStringLiteral("http://other.invalid/a,b"));QCOMPARE(request.options.comment,QStringLiteral("<owned π>"));QVERIFY(dialog.pending());
 }
 void invalidInputsAndBusyStateNeverDispatch() {
  TorrentCreationDialog dialog;QSignalSpy requests(&dialog,&TorrentCreationDialog::createRequested);QVERIFY(!dialog.beginSession(0,1));QVERIFY(dialog.beginSession(1,1));dialog.show();
  dialog.accept();QCOMPARE(requests.count(),0);fill(dialog);dialog.setBusy(true);dialog.accept();QCOMPARE(requests.count(),0);dialog.setBusy(false);
  auto* trackers=dialog.findChild<QPlainTextEdit*>("torrent-creation-trackers");trackers->setPlainText(QStringLiteral("file:///owned/no-network"));dialog.accept();QCOMPARE(requests.count(),0);
  trackers->setPlainText(QStringLiteral("https://invalid.example/a b"));dialog.accept();QCOMPARE(requests.count(),0);trackers->clear();
  auto* comment=dialog.findChild<QPlainTextEdit*>("torrent-creation-comment");comment->setPlainText(QString(6000,QChar(0x20ac)));dialog.accept();QCOMPARE(requests.count(),0);comment->clear();
  dialog.findChild<QCheckBox*>("torrent-creation-import")->setChecked(true);dialog.accept();QCOMPARE(requests.count(),1);QVERIFY(requests[0][0].value<TorrentCreationRequest>().options.importAfterCreation);
 }
 void progressRejectsStaleSequenceRevisionRegressionsAndOverflows() {
  TorrentCreationDialog dialog;QSignalSpy requests(&dialog,&TorrentCreationDialog::createRequested);QVERIFY(dialog.beginSession(1,9));fill(dialog);dialog.accept();const auto request=requests[0][0].value<TorrentCreationRequest>();
  auto progress=std::make_shared<TorrentCreationProgressPacket>(TorrentCreationProgressPacket{request.session,request.request,request.revision,1,0,0});QVERIFY(dialog.setProgress(progress));QVERIFY(!dialog.setProgress(progress));
  progress=std::make_shared<TorrentCreationProgressPacket>(TorrentCreationProgressPacket{request.session,request.request,request.revision,2,50,100});QVERIFY(dialog.setProgress(progress));
  auto invalid=std::make_shared<TorrentCreationProgressPacket>(*progress);invalid->sequence=3;invalid->revision++;QVERIFY(!dialog.setProgress(invalid));
  invalid->revision=request.revision;invalid->completedBytes=49;QVERIFY(!dialog.setProgress(invalid));invalid->completedBytes=50;invalid->totalBytes=101;QVERIFY(!dialog.setProgress(invalid));
  invalid->totalBytes=quint64{1}<<63;QVERIFY(!dialog.setProgress(invalid));
  QCOMPARE(dialog.findChild<QLabel*>("torrent-creation-status")->text(),QStringLiteral("50 of 100 source bytes processed"));
 }
 void failedCreateRetainsDraftAndRetryUsesFreshIdentity() {
  TorrentCreationDialog dialog;QSignalSpy requests(&dialog,&TorrentCreationDialog::createRequested);QVERIFY(dialog.beginSession(1,3));fill(dialog);dialog.accept();const auto first=requests[0][0].value<TorrentCreationRequest>();
  QVERIFY(dialog.setResult(result(first,false,false,QStringLiteral("<output already exists>"))));QVERIFY(!dialog.pending());
  auto* status=dialog.findChild<QLabel*>("torrent-creation-status");QCOMPARE(status->textFormat(),Qt::PlainText);QCOMPARE(status->text(),QStringLiteral("<output already exists>"));
  QCOMPARE(dialog.findChild<QLineEdit*>("torrent-creation-output")->text(),first.options.output);dialog.accept();QCOMPARE(requests.count(),2);
  const auto second=requests[1][0].value<TorrentCreationRequest>();QVERIFY(second.request>first.request);QVERIFY(!dialog.setResult(result(first,true)));QVERIFY(dialog.pending());QVERIFY(dialog.setResult(result(second,true)));QCOMPARE(dialog.result(),int(QDialog::Accepted));
 }
 void cancellationWaitsAndPublishedOutcomeWinsWithoutAutomaticRetry() {
  TorrentCreationDialog dialog;QSignalSpy requests(&dialog,&TorrentCreationDialog::createRequested),cancels(&dialog,&TorrentCreationDialog::cancelRequested);
  QVERIFY(dialog.beginSession(1,1));dialog.show();fill(dialog);dialog.accept();auto request=requests[0][0].value<TorrentCreationRequest>();
  dialog.reject();dialog.reject();QCOMPARE(cancels.count(),1);QVERIFY(dialog.isVisible());QVERIFY(dialog.pending());QVERIFY(dialog.setResult(result(request,true)));QCOMPARE(dialog.result(),int(QDialog::Accepted));
  TorrentCreationDialog failed;QSignalSpy writes(&failed,&TorrentCreationDialog::createRequested);QVERIFY(failed.beginSession(2,1));fill(failed);failed.accept();request=writes[0][0].value<TorrentCreationRequest>();
  auto published=std::make_shared<TorrentCreationResultPacket>(*result(request,false,false,QStringLiteral("Directory sync failed")));published->outputPublished=true;QVERIFY(failed.setResult(published));
  auto* create=failed.findChild<QPushButton*>("torrent-creation-create");QVERIFY(!create->isEnabled());failed.accept();QCOMPARE(writes.count(),1);
  failed.findChild<QLineEdit*>("torrent-creation-output")->setText(QStringLiteral("/owned/other.torrent"));QVERIFY(create->isEnabled());failed.accept();QCOMPARE(writes.count(),2);
 }
 void newProfileRejectsOldResultsAndInvalidationCloses() {
  TorrentCreationDialog dialog;QSignalSpy requests(&dialog,&TorrentCreationDialog::createRequested);QVERIFY(dialog.beginSession(1,2));fill(dialog);dialog.accept();const auto old=requests[0][0].value<TorrentCreationRequest>();
  QVERIFY(dialog.beginSession(2,7));QVERIFY(!dialog.pending());QVERIFY(!dialog.setResult(result(old,true)));QVERIFY(!dialog.beginSession(1,2));QVERIFY(!dialog.beginSession(2,7));
  dialog.show();dialog.invalidateSession();QVERIFY(!dialog.isVisible());dialog.accept();QCOMPARE(requests.count(),1);
 }
 void directRequestAndCancelCallbacksMayDeleteOwner() {
  QPointer<TorrentCreationDialog> dialog=new TorrentCreationDialog;QVERIFY(dialog->beginSession(1,1));fill(*dialog);
  connect(dialog,&TorrentCreationDialog::createRequested,this,[&](TorrentCreationRequest){delete dialog.data();});dialog->accept();QVERIFY(dialog.isNull());
  dialog=new TorrentCreationDialog;QVERIFY(dialog->beginSession(1,1));fill(*dialog);dialog->accept();
  connect(dialog,&TorrentCreationDialog::cancelRequested,this,[&](TorrentCreationRequest){delete dialog.data();});dialog->reject();QVERIFY(dialog.isNull());
 }
 void enabledChangeDeletionAbortsDispatch() {
  QPointer<TorrentCreationDialog> dialog=new TorrentCreationDialog;QSignalSpy requests(dialog,&TorrentCreationDialog::createRequested);QVERIFY(dialog->beginSession(1,1));fill(*dialog);
  auto* source=dialog->findChild<QLineEdit*>("torrent-creation-source");EnabledHook hook;hook.callback=[&]{delete dialog.data();};source->installEventFilter(&hook);dialog->accept();QVERIFY(dialog.isNull());QCOMPARE(requests.count(),0);
 }
 void reopenedSameSessionRejectsPreviousDialogResult() {
  TorrentCreationRequest old;
  {TorrentCreationDialog first;QSignalSpy requests(&first,&TorrentCreationDialog::createRequested);QVERIFY(first.beginSession(1,1));fill(first);first.accept();old=requests[0][0].value<TorrentCreationRequest>();first.invalidateSession();}
  TorrentCreationDialog next;QSignalSpy requests(&next,&TorrentCreationDialog::createRequested);QVERIFY(next.beginSession(1,1));fill(next);next.accept();const auto current=requests[0][0].value<TorrentCreationRequest>();
  QVERIFY(current.request>old.request);QVERIFY(!next.setResult(result(old,true)));QVERIFY(next.pending());QVERIFY(next.setResult(result(current,true)));
 }
};
QTEST_MAIN(TorrentCreationDialogTests)
#include "torrent_creation_dialog_tests.moc"
