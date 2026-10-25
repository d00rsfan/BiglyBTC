#include "library_window.hpp"
#include "local_rating_dialog.hpp"
#include "session_bridge.hpp"
#include "../adapters/local_rating.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <stdexcept>

using namespace biglybtc;
namespace {
QByteArray b(const QByteArray& value){return QByteArray::number(value.size())+':'+value;}
QByteArray dictionary(std::initializer_list<std::pair<QByteArray,QByteArray>> rows){
  std::vector<std::pair<QByteArray,QByteArray>> sorted(rows);std::sort(sorted.begin(),sorted.end());QByteArray out("d");
  for(const auto& [key,value]:sorted){out+=b(key)+value;}return out+'e';
}
void write(const QString& path,const QByteArray& value){
  QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate)||file.write(value)!=value.size()){throw std::runtime_error("owned rating fixture write");}
}
QByteArray read(const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly)){throw std::runtime_error("owned rating fixture read");}return file.readAll();}
struct Profile{
  QTemporaryDir root{QStringLiteral("/tmp/biglybtc-rating-host-XXXXXX")};QString profile,state,payload,hash;
  QByteArray info,resume;
  Profile(){
    if(!root.isValid()){throw std::runtime_error("owned rating fixture root");}
    profile=root.path()+"/profile";payload=root.path()+"/payload";
    if(!QDir().mkpath(profile+"/active")||!QDir().mkpath(payload)){throw std::runtime_error("owned rating fixture dirs");}
    const QByteArray bytes("abc");write(payload+"/fixture.bin",bytes);
    info=dictionary({{"length","i3e"},{"name",b("fixture.bin")},{"piece length","i16384e"},{"pieces",b(QCryptographicHash::hash(bytes,QCryptographicHash::Sha1))}});
    const auto identity=QCryptographicHash::hash(info,QCryptographicHash::Sha1);hash=QString::fromLatin1(identity.toHex().toUpper());state=profile+"/active/"+hash+".dat";
    resume=dictionary({{"data",dictionary({{"blocks","de"},{"resume data",b(QByteArray(1,'\1'))},{"valid","i1e"}})}});
    write(state,dictionary({{"attributes",dictionary({{"Plugin.azrating.comment",b("original")},{"Plugin.azrating.rating",b("2")},{"future",b("keep")},{"storetypes","l1:Le"}})},{"info",info},{"resume",resume}}));
    const auto row=dictionary({{"completedbytes","i0e"},{"downloaded","i17e"},{"file_priorities","li-1ee"},{"persistent","i1e"},{"position","i1e"},
      {"save_dir",b(payload.toUtf8())},{"save_file",b("fixture.bin")},{"state","i70e"},{"torrent",b(state.toUtf8())},{"torrent_hash",b(identity)},{"uploaded","i9e"}});
    write(profile+"/downloads.config",dictionary({{"downloads","l"+row+"e"}}));write(profile+"/tables.config","de");write(profile+"/biglybt.config","de");
  }
};
void connectRating(app::SessionBridge& bridge,ui::LibraryWindow& window){
  QObject::connect(&bridge,&app::SessionBridge::snapshotReady,&window,&ui::LibraryWindow::setPresentation);
  QObject::connect(&bridge,&app::SessionBridge::columnLayoutSessionReady,&window,&ui::LibraryWindow::beginColumnLayoutSession);
  QObject::connect(&bridge,&app::SessionBridge::busyChanged,&window,&ui::LibraryWindow::setBusy);
  QObject::connect(&bridge,&app::SessionBridge::localRatingReady,&window,&ui::LibraryWindow::setLocalRating);
  QObject::connect(&bridge,&app::SessionBridge::localRatingSaved,&window,&ui::LibraryWindow::setLocalRatingSaved);
  QObject::connect(&window,&ui::LibraryWindow::localRatingRequested,&bridge,
    [owner=QPointer<app::SessionBridge>(&bridge),view=QPointer<ui::LibraryWindow>(&window)](ui::LocalRatingRequest value){
      if(owner&&view&&!owner->loadLocalRating(value)&&view){view->rejectLocalRatingRequest(value);}
    });
  QObject::connect(&window,&ui::LibraryWindow::localRatingSaveRequested,&bridge,
    [owner=QPointer<app::SessionBridge>(&bridge),view=QPointer<ui::LibraryWindow>(&window)](ui::LocalRatingSubmission value){
      if(owner&&view&&!owner->saveLocalRating(value)&&view){view->rejectLocalRatingSave(value);}
    });
}
ui::Presentation rows(quint64 revision,int count){
  auto result=std::make_shared<ui::LibraryPresentation>();result->revision=revision;
  for(int n=0;n<count;++n){ui::LibraryItem item;item.hash=QString(40,QChar::fromLatin1(static_cast<char>('a'+n)));item.key=item.hash;item.name="owned";item.state="Stopped";item.bytes=3;item.completed=0;result->items.push_back(std::move(item));}return result;
}
std::shared_ptr<ui::LocalRatingPacket> synthetic(const ui::LocalRatingRequest& request){
  auto values=std::make_shared<ui::LocalRatingValues>();values->score=2;values->comment="synthetic saved";values->attributesRevision=QByteArray(32,'t');
  auto packet=std::make_shared<ui::LocalRatingPacket>();packet->identity=request;packet->snapshot=std::move(values);return packet;
}
class Hook final:public QObject{
public:std::function<void()> action;
  bool eventFilter(QObject*,QEvent* event) override{if(event->type()==QEvent::ToolTipChange&&action){auto callback=std::move(action);callback();return true;}return false;}
};
ui::LocalRatingDialog* visibleDialog(ui::LibraryWindow& window){
  for(auto* dialog:window.findChildren<ui::LocalRatingDialog*>()){if(dialog->isVisible()){return dialog;}}return nullptr;
}
}
class LocalRatingWiringTests final:public QObject{
  Q_OBJECT
private slots:
  void initTestCase(){QApplication::setQuitOnLastWindowClosed(false);qRegisterMetaType<ui::LocalRatingRequest>();qRegisterMetaType<ui::LocalRatingSubmission>();}
  void singleRatingActionPreservesBulkFileActions(){
    ui::LibraryWindow window;window.setOfflinePreview(false);window.beginColumnLayoutSession(4);window.setPresentation(rows(7,2));window.show();
    auto* rating=window.findChild<QAction*>("classic-my-rating");auto* remove=window.findChild<QAction*>("classic-remove");QVERIFY(rating&&remove);
    window.table().selectAll();QTRY_VERIFY(remove->isEnabled());QVERIFY(!rating->isEnabled());
    window.table().clearSelection();window.table().selectRow(0);QTRY_VERIFY(rating->isEnabled());
    QSignalSpy requests(&window,&ui::LibraryWindow::localRatingRequested);rating->trigger();QTRY_COMPARE(requests.size(),1);
    const auto request=requests[0][0].value<ui::LocalRatingRequest>();QCOMPARE(request.session,quint64{4});QCOMPARE(request.target.generation,quint64{7});
    auto* dialog=window.findChild<ui::LocalRatingDialog*>();QVERIFY(dialog);window.setLocalRating(synthetic(request));
    auto* comment=dialog->findChild<QPlainTextEdit*>("local-rating-comment");QVERIFY(comment);QCOMPARE(comment->toPlainText(),QStringLiteral("synthetic saved"));
    dialog->reject();
  }
  void actualWindowBridgeSaveUsesSoleLeaseAndNoEngineOrDdb(){
    Profile fixture;app::SessionBridge bridge;ui::LibraryWindow window;connectRating(bridge,window);
    QSignalSpy reads(&bridge,&app::SessionBridge::localRatingReady),saves(&bridge,&app::SessionBridge::localRatingSaved),native(&bridge,&app::SessionBridge::nativeSessionReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    const auto before=read(fixture.state),payload=read(fixture.payload+"/fixture.bin");
    QVERIFY(bridge.open(fixture.profile.toStdString(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&window.model().rowCount()==1,10000);
    window.setOfflinePreview(false);window.show();window.table().selectRow(0);
    auto* action=window.findChild<QAction*>("classic-my-rating");QVERIFY(action);QTRY_VERIFY(action->isEnabled());action->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(reads.size(),1,10000);QTRY_VERIFY(!bridge.busy());
    auto readPacket=reads[0][0].value<ui::LocalRatingPresentation>();QVERIFY2(readPacket->snapshot,qPrintable(readPacket->failure));
    QCOMPARE(read(fixture.state),before);QCOMPARE(native.size(),0);
    QPointer<ui::LocalRatingDialog> dialog=window.findChild<ui::LocalRatingDialog*>();QVERIFY(dialog);
    auto* score=dialog->findChild<QComboBox*>("local-rating-score");auto* comment=dialog->findChild<QPlainTextEdit*>("local-rating-comment");auto* save=dialog->findChild<QPushButton*>("local-rating-save");QVERIFY(score&&comment&&save);
    score->setCurrentIndex(5);comment->setPlainText(QStringLiteral("owned actual save"));QTRY_VERIFY(save->isEnabled());save->click();
    QTRY_COMPARE_WITH_TIMEOUT(saves.size(),1,10000);QTRY_VERIFY(!bridge.busy());QVERIFY2(saves[0][0].value<ui::LocalRatingSavePresentation>()->saved,qPrintable(saves[0][0].value<ui::LocalRatingSavePresentation>()->failure));
    QTRY_VERIFY(dialog.isNull());QCOMPARE(read(fixture.payload+"/fixture.bin"),payload);QCOMPARE(native.size(),0);
    auto doc=core::Document::parse(read(fixture.state).toStdString(),storage::compatibleConfigLimits());QVERIFY(doc);
    QCOMPARE(doc->originalBytes().substr(doc->find("resume")->begin,doc->find("resume")->end-doc->find("resume")->begin),fixture.resume.toStdString());
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,10000);QVERIFY(closed[0][1].toBool());QVERIFY(closed[0][2].toBool());
    auto lease=storage::ProfileLease::acquire(fixture.profile.toStdString());QVERIFY(lease);auto hash=core::TorrentHash::parse(QByteArray::fromHex(fixture.hash.toLatin1()).toStdString());QVERIFY(hash);
    auto local=storage::readLocalRating(*lease,*hash);QVERIFY(local);QCOMPARE(local->local.value.score,5);QCOMPARE(*local->local.value.comment,std::u16string(u"owned actual save"));
  }
  void actualBridgeRejectsStaleRevisionForgedTokenAndWrongSession(){
    Profile fixture;app::SessionBridge bridge;QSignalSpy snapshots(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),reads(&bridge,&app::SessionBridge::localRatingReady),saves(&bridge,&app::SessionBridge::localRatingSaved);
    QVERIFY(bridge.open(fixture.profile.toStdString(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!snapshots.empty()&&!sessions.empty(),10000);
    const auto initial=snapshots.back()[0].value<ui::Presentation>();const auto session=sessions.back()[0].toULongLong();
    ui::LocalRatingRequest request{session,1,initial->revision,{fixture.hash,initial->revision}};
    QVERIFY(bridge.loadLocalRating(request));QTRY_COMPARE_WITH_TIMEOUT(reads.size(),1,10000);QTRY_VERIFY(!bridge.busy());
    const auto value=reads.back()[0].value<ui::LocalRatingPresentation>();QVERIFY(value->snapshot);const auto before=read(fixture.state);
    QVERIFY(bridge.refresh());QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&snapshots.back()[0].value<ui::Presentation>()->revision>request.revision,10000);
    QVERIFY(bridge.saveLocalRating({request,value->snapshot->attributesRevision,4,QStringLiteral("stale")}));QTRY_COMPARE_WITH_TIMEOUT(saves.size(),1,10000);QTRY_VERIFY(!bridge.busy());
    QVERIFY(!saves.back()[0].value<ui::LocalRatingSavePresentation>()->saved);QCOMPARE(read(fixture.state),before);
    const auto current=snapshots.back()[0].value<ui::Presentation>();request={session,2,current->revision,{fixture.hash,current->revision}};
    QVERIFY(bridge.loadLocalRating(request));QTRY_COMPARE_WITH_TIMEOUT(reads.size(),2,10000);QTRY_VERIFY(!bridge.busy());
    QVERIFY(bridge.saveLocalRating({request,QByteArray(32,'x'),4,QStringLiteral("forged")}));QTRY_COMPARE_WITH_TIMEOUT(saves.size(),2,10000);QTRY_VERIFY(!bridge.busy());
    QVERIFY(!saves.back()[0].value<ui::LocalRatingSavePresentation>()->saved);QCOMPARE(read(fixture.state),before);
    ++request.session;QVERIFY(!bridge.loadLocalRating(request));QVERIFY(!bridge.busy());QCOMPARE(read(fixture.state),before);
  }
  void profileChangeBusyAndDirectChildCallbacksFenceOldRoutes(){
    ui::LibraryWindow window;window.setOfflinePreview(false);window.beginColumnLayoutSession(1);window.setPresentation(rows(1,1));window.show();window.table().selectRow(0);
    QSignalSpy reads(&window,&ui::LibraryWindow::localRatingRequested),writes(&window,&ui::LibraryWindow::localRatingSaveRequested);
    auto* action=window.findChild<QAction*>("classic-my-rating");QVERIFY(action);QTRY_VERIFY(action->isEnabled());action->trigger();QTRY_COMPARE(reads.size(),1);
    const auto old=reads[0][0].value<ui::LocalRatingRequest>();window.beginColumnLayoutSession(2);window.setPresentation(rows(2,1));window.setLocalRating(synthetic(old));QCOMPARE(writes.size(),0);
    window.table().selectRow(0);QTRY_VERIFY(action->isEnabled());action->trigger();QTRY_COMPARE(reads.size(),2);const auto fresh=reads[1][0].value<ui::LocalRatingRequest>();QCOMPARE(fresh.session,quint64{2});window.setLocalRating(synthetic(fresh));
    auto* dialog=visibleDialog(window);QVERIFY(dialog);auto* comment=dialog->findChild<QPlainTextEdit*>("local-rating-comment");auto* save=dialog->findChild<QPushButton*>("local-rating-save");QVERIFY(comment&&save);
    comment->setPlainText(QStringLiteral("draft"));QTRY_VERIFY(save->isEnabled());window.setBusy(true);save->click();QCOMPARE(writes.size(),0);QVERIFY(!save->isEnabled());window.setBusy(false);
    QPointer<ui::LibraryWindow> deleted=new ui::LibraryWindow;deleted->setOfflinePreview(false);deleted->beginColumnLayoutSession(3);deleted->setPresentation(rows(3,1));deleted->show();deleted->table().selectRow(0);
    QSignalSpy deleteRead(deleted,&ui::LibraryWindow::localRatingRequested);auto* deleteAction=deleted->findChild<QAction*>("classic-my-rating");QVERIFY(deleteAction);QTRY_VERIFY(deleteAction->isEnabled());deleteAction->trigger();QTRY_COMPARE(deleteRead.size(),1);
    auto* target=visibleDialog(*deleted)->findChild<QLabel*>("local-rating-status");QVERIFY(target);Hook hook;hook.action=[&]{delete deleted.data();};target->installEventFilter(&hook);
    deleted->setLocalRating(synthetic(deleteRead[0][0].value<ui::LocalRatingRequest>()));QVERIFY(deleted.isNull());
  }
};
QTEST_MAIN(LocalRatingWiringTests)
#include "local_rating_wiring_tests.moc"
