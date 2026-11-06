// SPDX-License-Identifier: GPL-2.0-or-later
#include "session_bridge.hpp"
#include "library_window.hpp"
#include "category_editor.hpp"
#include "owned_engine_fixture.hpp"
#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QFileInfo>
#include <QDir>
#include <QPixmap>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QTimer>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTableView>
#include <QTableWidget>
#include <QtTest>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fcntl.h>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>
namespace {
using namespace biglybtc;
std::atomic_bool watchRead{false},readEntered{false},holdRead{false};
std::atomic<std::uintmax_t> watchedDevice{0},watchedInode{0};
std::mutex readMutex;std::condition_variable readWake;
void stopRead(){holdRead=false;readWake.notify_all();}
void stopped(test::OwnedEngineFixture& fixture){auto bytes=test::read(fixture.profile+"/downloads.config");test::require(bytes.count("5:statei75e")==1,"Generated stopped row");bytes.replace("5:statei75e","5:statei70e");test::write(fixture.profile+"/downloads.config",bytes);}
QByteArray definitions(){libtorrent::entry value(libtorrent::entry::dictionary_type{});value["categories"]=libtorrent::entry::list_type{};value["opaque.root"]=std::string("\0\xffR",3);return QByteArray::fromStdString(test::encoded(value));}
ui::CategoryCommandPresentation latest(const QSignalSpy& spy){return spy.empty()?ui::CategoryCommandPresentation{}:qvariant_cast<ui::CategoryCommandPresentation>(spy.last().at(0));}
ui::Target target(const QSignalSpy& spy){auto value=qvariant_cast<ui::Presentation>(spy.last().at(0));return {value->items.front().hash,value->revision};}
void wire(app::SessionBridge& bridge,ui::LibraryWindow& window){
  using Bridge=app::SessionBridge;using Window=ui::LibraryWindow;
  window.setOfflinePreview(false); // Only editable GUI hint; actual worker owns FS permission.
  QObject::connect(&bridge,&Bridge::categoryCommandFinished,&window,&Window::setCategoryCommandFinished);
  QObject::connect(&window,&Window::categoryCommandRequested,&bridge,[owner=QPointer<Bridge>(&bridge),view=QPointer<Window>(&window)](ui::CategoryCommand command){if(owner&&view&&!owner->categoryCommand(command)&&view){view->rejectCategoryCommand(command);}});
  QObject::connect(&bridge,&Bridge::snapshotReady,&window,&Window::setPresentation);
  QObject::connect(&bridge,&Bridge::columnLayoutSessionReady,&window,&Window::beginColumnLayoutSession);
  QObject::connect(&bridge,&Bridge::busyChanged,&window,&Window::setBusy);
  QObject::connect(&bridge,&Bridge::messageReady,&window,&Window::setMessage);
  QObject::connect(&bridge,&Bridge::detailsReady,&window,&Window::setDetails);
  QObject::connect(&window,&Window::detailsRequested,&bridge,&Bridge::details);
  QObject::connect(&window,&Window::refreshRequested,&bridge,&Bridge::refresh);
  QObject::connect(&window,&Window::cancelRequested,&bridge,&Bridge::cancel);
  window.enableSessionCloseHandshake();const QPointer<Window> view(&window);
  QObject::connect(&window,&Window::sessionCloseRequested,&bridge,[owner=QPointer<Bridge>(&bridge),view]{if(owner&&view&&!owner->closeSession()&&view){view->retrySessionClose();}});
  QObject::connect(&bridge,&Bridge::sessionClosed,&window,[view](quint64,bool,bool){if(view){view->completeSessionClose();}});
  QObject::connect(&bridge,&Bridge::sessionCloseFailed,&window,&Window::retrySessionClose);
}
bool rawPreserved(const QByteArray& before,const QByteArray& after,std::initializer_list<const char*> keys){
  const auto a=core::Document::parse(before.toStdString()),b=core::Document::parse(after.toStdString());if(!a||!b){return false;}
  for(const auto key:keys){const auto* x=a->find(key);const auto* y=b->find(key);if(bool(x)!=bool(y)){return false;}if(x&&a->originalBytes().substr(x->begin,x->end-x->begin)!=b->originalBytes().substr(y->begin,y->end-y->begin)){return false;}}return true;
}
}
extern "C" ssize_t __real_read(int,void*,size_t);
extern "C" ssize_t __wrap_read(int fd,void* buffer,size_t size){
  if(watchRead){struct stat value{};if(::fstat(fd,&value)==0&&static_cast<std::uintmax_t>(value.st_dev)==watchedDevice&&static_cast<std::uintmax_t>(value.st_ino)==watchedInode){
    readEntered=true;std::unique_lock lock(readMutex);readWake.wait_for(lock,std::chrono::seconds(10),[]{return !holdRead.load();});}}
  return __real_read(fd,buffer,size);
}
class CategoryBridgeTests final:public QObject {
  Q_OBJECT
private slots:
  void actualWindowCreateAssignUncategoriseReloadAndJoinedReopen(){
    test::OwnedEngineFixture fixture(false);stopped(fixture);const auto path=fixture.profile+"/categories.config";
    const auto original=definitions();test::write(path,original);
    const auto oldState=test::read(fixture.stateFile),oldInventory=test::read(fixture.profile+"/downloads.config"),oldPayload=test::read(fixture.payload+'/'+fixture.files.front().relative);
    app::SessionBridge bridge;QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),finished(&bridge,&app::SessionBridge::categoryCommandFinished),closed(&bridge,&app::SessionBridge::sessionClosed),detail(&bridge,&app::SessionBridge::detailsReady);
    quint64 firstSession=0;ui::CategoryCommandPresentation retained;
    {
      ui::LibraryWindow window;wire(bridge,window);QSignalSpy intents(&window,&ui::LibraryWindow::categoryCommandRequested);window.show();
      QVERIFY(bridge.open(fixture.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!rows.empty()&&!sessions.empty(),10000);firstSession=sessions.last()[0].toULongLong();
      auto* table=window.findChild<QTableView*>(QStringLiteral("incomplete-torrents"));QVERIFY(table);QTRY_COMPARE(table->model()->rowCount(),1);table->setCurrentIndex(table->model()->index(0,0));table->selectRow(0);
      auto* manage=window.findChild<QAction*>(QStringLiteral("classic-categories"));QVERIFY(manage);QTRY_VERIFY(!bridge.busy()&&manage->isEnabled());manage->trigger();
      QPointer<ui::CategoryManagerDialog> dialog=window.findChild<ui::CategoryManagerDialog*>();QVERIFY(dialog&&dialog->isVisible());
      QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished.count()==1,10000);QCOMPARE(latest(finished)->status,ui::CategoryCommandStatus::Loaded);QVERIFY(latest(finished)->writable);QVERIFY(latest(finished)->assignmentLoaded);
      auto* name=dialog->findChild<QLineEdit*>(QStringLiteral("category-name"));auto* create=dialog->findChild<QPushButton*>(QStringLiteral("category-new"));auto* categories=dialog->findChild<QTableWidget*>(QStringLiteral("category-table"));QVERIFY(name&&create&&categories);QCOMPARE(categories->rowCount(),0);
      name->setText(QStringLiteral("Actual category"));QVERIFY(create->isEnabled());create->click();QVERIFY(bridge.busy());QVERIFY(dialog&&dialog->isVisible());QVERIFY(!create->isEnabled());QCOMPARE(categories->rowCount(),0);
      QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished.count()==2,10000);QCOMPARE(latest(finished)->status,ui::CategoryCommandStatus::Saved);QVERIFY(latest(finished)->refreshFailure.isEmpty());QVERIFY(rawPreserved(original,test::read(path),{"opaque.root"}));
      auto* menu=window.findChild<QMenu*>(QStringLiteral("classic-torrent-category"));QVERIFY(menu);QVERIFY(QMetaObject::invokeMethod(menu,"aboutToShow",Qt::DirectConnection));auto* assign=menu->findChild<QAction*>(QStringLiteral("category-assign-Actual category"));QVERIFY(assign&&assign->isEnabled()&&!assign->isChecked());
      const auto before=target(rows);assign->trigger();QVERIFY(bridge.busy());QVERIFY(!assign->isChecked());const auto requested=qvariant_cast<ui::CategoryCommand>(intents.last()[0]);QCOMPARE(requested.target,before);
      QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished.count()==3,10000);QCOMPARE(latest(finished)->status,ui::CategoryCommandStatus::Saved);QVERIFY(latest(finished)->target);QVERIFY(latest(finished)->target->generation>before.generation);QCOMPARE(*latest(finished)->target,target(rows));QCOMPARE(latest(finished)->assignedName,std::optional<QString>{QStringLiteral("Actual category")});QVERIFY(latest(finished)->assignmentResolved);QVERIFY(!detail.empty());QCOMPARE(qvariant_cast<ui::Details>(detail.last()[0]).target,*latest(finished)->target);
      // Optional evidence from this actual correlated worker/UI flow only.
      // No pre-rendered packet, production CLI flag or existing file overwrite.
      const auto capture=qEnvironmentVariable("BIGLYBTC_CATEGORY_CAPTURE_FILE");
      if(!capture.isEmpty()){
        const QFileInfo output(capture),parent(output.absolutePath());
        QVERIFY(output.isAbsolute()&&output.suffix()==QStringLiteral("png")&&!output.exists());
        QVERIFY(parent.isDir()&&!parent.isSymLink()&&parent.ownerId()==::geteuid());
        QVERIFY(dialog&&dialog->isVisible());dialog->repaint();QApplication::processEvents();QVERIFY(dialog);
        const auto image=dialog->grab();QVERIFY(!image.isNull());
        QFile file(capture);QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::NewOnly));
        QVERIFY(image.save(&file,"PNG"));QVERIFY(file.flush());
      }
      QVERIFY(rawPreserved(oldState,test::read(fixture.stateFile),{"info","resume","opaque.fixture"}));QCOMPARE(test::read(fixture.profile+"/downloads.config"),oldInventory);QCOMPARE(test::read(fixture.payload+'/'+fixture.files.front().relative),oldPayload);
      QVERIFY(QMetaObject::invokeMethod(menu,"aboutToShow",Qt::DirectConnection));auto* clear=menu->findChild<QAction*>(QStringLiteral("category-uncategorise"));QVERIFY(clear&&clear->isEnabled()&&!clear->isChecked());clear->trigger();QVERIFY(!clear->isChecked());
      QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished.count()==4,10000);QCOMPARE(latest(finished)->status,ui::CategoryCommandStatus::Saved);QVERIFY(!latest(finished)->assignedName);QVERIFY(!latest(finished)->assignmentResolved);
      auto* reload=dialog->findChild<QPushButton*>(QStringLiteral("category-reload"));QVERIFY(reload&&reload->isEnabled());const auto saved=test::read(path);reload->click();QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished.count()==5,10000);QCOMPARE(latest(finished)->status,ui::CategoryCommandStatus::Loaded);QCOMPARE(test::read(path),saved);retained=latest(finished);
      window.close();QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed.last()[1].toBool()&&closed.last()[2].toBool());QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(!dialog);
    }
    {auto lease=storage::ProfileLease::acquire(fixture.native());QVERIFY(lease);} // Owning packet holds no lease.
    {
      ui::LibraryWindow window;wire(bridge,window);window.show();QVERIFY(bridge.open(fixture.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.count()==2,10000);QVERIFY(sessions.last()[0].toULongLong()!=firstSession);
      auto* manage=window.findChild<QAction*>(QStringLiteral("classic-categories"));QVERIFY(manage);manage->trigger();QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished.count()==6,10000);QVERIFY(latest(finished)->categories);QVERIFY(std::any_of(latest(finished)->categories->categories.begin(),latest(finished)->categories->categories.end(),[](const auto& row){return row.name=="Actual category";}));
      auto* dialog=window.findChild<ui::CategoryManagerDialog*>();QVERIFY(dialog);const auto count=dialog->findChild<QTableWidget*>(QStringLiteral("category-table"))->rowCount();window.setCategoryCommandFinished(retained);QCOMPARE(dialog->findChild<QTableWidget*>(QStringLiteral("category-table"))->rowCount(),count);
      auto* categories=dialog->findChild<QTableWidget*>(QStringLiteral("category-table"));auto* remove=dialog->findChild<QPushButton*>(QStringLiteral("category-delete"));QVERIFY(categories&&remove);
      int selected=-1;for(int row=0;row<categories->rowCount();++row){if(categories->item(row,0)->text()==QStringLiteral("Actual category")){selected=row;}}QVERIFY(selected>=0);categories->setCurrentCell(selected,0);categories->selectRow(selected);
      if(latest(finished)->deletionSupported){
        QVERIFY(remove->isEnabled());bool answered=false;
        QTimer::singleShot(0,&window,[&]{if(auto* question=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){if(auto* yes=question->button(QMessageBox::Yes)){answered=true;yes->click();}}});
        QTimer::singleShot(2000,&window,[]{if(auto* question=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){question->reject();}});
        remove->click();QVERIFY(answered);QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished.count()==7,10000);QCOMPARE(latest(finished)->status,ui::CategoryCommandStatus::Saved);
        QVERIFY(std::none_of(latest(finished)->categories->categories.begin(),latest(finished)->categories->categories.end(),[](const auto& row){return row.name=="Actual category";}));QVERIFY(rawPreserved(original,test::read(path),{"opaque.root"}));
      }else{QVERIFY(!remove->isEnabled());}
      window.close();QTRY_COMPARE_WITH_TIMEOUT(closed.count(),2,15000);QVERIFY(closed.last()[1].toBool()&&closed.last()[2].toBool());
    }
  }
  void retainedPublicAdmissionSurvivesAssignmentAndCheckpointsTheCategory(){
    test::OwnedEngineFixture fixture(false,true);stopped(fixture);QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);
    const auto path=fixture.profile+"/categories.config";test::write(path,definitions());
    app::SessionBridge bridge;QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),finished(&bridge,&app::SessionBridge::categoryCommandFinished),traffic(&bridge,&app::SessionBridge::progressReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(),fixture.port()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!rows.empty(),30000);
    ui::CategoryCommand request;request.session=sessions.last()[0].toULongLong();request.request=1;QVERIFY(bridge.categoryCommand(request));QTRY_VERIFY(!bridge.busy()&&finished.count()==1);
    request.kind=ui::CategoryCommandKind::Create;request.name=QStringLiteral("Retained");request.request=2;request.generation=latest(finished)->generation;QVERIFY(bridge.categoryCommand(request));QTRY_VERIFY(!bridge.busy()&&finished.count()==2);QCOMPARE(latest(finished)->status,ui::CategoryCommandStatus::Saved);
    QVERIFY(bridge.downloadAction({target(rows)},core::DownloadAction::Start));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000);
    const auto progress=[&]{qint64 completed=0;for(const auto& packet:traffic){const auto value=qvariant_cast<ui::Telemetry>(packet[0]);if(value.target.hash==QString::fromStdString(fixture.hash->hex())){completed=std::max(completed,value.completed);}}return completed;};
    const auto presentation=[&]{return qvariant_cast<ui::Presentation>(rows.last()[0]);};
    QTRY_VERIFY_WITH_TIMEOUT(progress()>0&&presentation()->items.front().providerAdmission,30000);
    const auto admission=presentation()->items.front().providerAdmission;
    request.kind=ui::CategoryCommandKind::Reload;request.request=3;request.target=target(rows);QVERIFY(bridge.categoryCommand(request));QTRY_VERIFY(!bridge.busy()&&finished.count()==3);QCOMPARE(latest(finished)->status,ui::CategoryCommandStatus::Loaded);
    request.kind=ui::CategoryCommandKind::Assign;request.request=4;request.generation=latest(finished)->generation;request.target=target(rows);QVERIFY(bridge.categoryCommand(request));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished.count()==4,10000);
    QCOMPARE(latest(finished)->status,ui::CategoryCommandStatus::Saved);QVERIFY(latest(finished)->refreshFailure.isEmpty());QCOMPARE(presentation()->items.front().providerAdmission,admission);QVERIFY(progress()>0);
    QVERIFY(bridge.downloadAction({target(rows)},core::DownloadAction::Stop));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),15000);
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed.last()[1].toBool()&&closed.last()[2].toBool());
    auto saved=core::Document::parse(test::read(fixture.stateFile).toStdString());QVERIFY(saved);const auto* attrs=saved->find("attributes");QVERIFY(attrs&&attrs->find("category"));QCOMPARE(attrs->find("category")->bytes,std::string("Retained"));
    auto library=core::Document::parse(test::read(fixture.profile+"/downloads.config").toStdString());QVERIFY(library);const auto* downloaded=library->find("downloads")->children.front().find("downloaded");QVERIFY(downloaded&&downloaded->type==core::Node::Type::Integer&&downloaded->integer>0);QCOMPARE(test::read(fixture.payload+'/'+fixture.files.front().relative).size(),test::OwnedEngineFixture::PayloadBytes);
  }
  void actualWorkerRejectsStaleTargetGenerationAndPreservesNoop(){
    test::OwnedEngineFixture fixture(false);stopped(fixture);test::write(fixture.profile+"/categories.config",definitions());app::SessionBridge bridge;
    QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),finished(&bridge,&app::SessionBridge::categoryCommandFinished),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!rows.empty(),10000);
    ui::CategoryCommand request;request.session=sessions.last()[0].toULongLong();request.request=1;request.target=target(rows);QVERIFY(bridge.categoryCommand(request));QTRY_VERIFY(!bridge.busy()&&finished.count()==1);
    request.kind=ui::CategoryCommandKind::Create;request.name=QStringLiteral("One");request.generation=latest(finished)->generation;request.request=2;QVERIFY(bridge.categoryCommand(request));QTRY_VERIFY(!bridge.busy()&&finished.count()==2);QCOMPARE(latest(finished)->status,ui::CategoryCommandStatus::Saved);
    const auto defs=test::read(fixture.profile+"/categories.config"),state=test::read(fixture.stateFile);const auto stale=request;
    request.request=3;request.generation=latest(finished)->generation;request.target=target(rows);QVERIFY(bridge.categoryCommand(request));QTRY_VERIFY(!bridge.busy()&&finished.count()==3);QCOMPARE(latest(finished)->status,ui::CategoryCommandStatus::Unchanged);QCOMPARE(test::read(fixture.profile+"/categories.config"),defs);
    request=stale;request.request=4;QVERIFY(bridge.categoryCommand(request));QTRY_VERIFY(!bridge.busy()&&finished.count()==4);QCOMPARE(latest(finished)->status,ui::CategoryCommandStatus::Rejected);QCOMPARE(test::read(fixture.profile+"/categories.config"),defs);
    request.kind=ui::CategoryCommandKind::Assign;request.request=5;request.generation=latest(finished)->generation;QVERIFY(bridge.categoryCommand(request));QTRY_VERIFY(!bridge.busy()&&finished.count()==5);QCOMPARE(latest(finished)->status,ui::CategoryCommandStatus::Rejected);QCOMPARE(test::read(fixture.stateFile),state);
    request.kind=ui::CategoryCommandKind::Reload;request.request=6;request.target=target(rows);QVERIFY(bridge.categoryCommand(request));QTRY_VERIFY(!bridge.busy()&&finished.count()==6);QVERIFY(latest(finished)->assignmentLoaded);
    auto outside=state;QCOMPARE(outside.count("10:attributesd"),1);outside.replace("10:attributesd","10:attributesd8:category3:One");test::write(fixture.stateFile,outside);
    request.kind=ui::CategoryCommandKind::Assign;request.request=7;request.generation=latest(finished)->generation;request.target=*latest(finished)->target;request.name.reset();
    QVERIFY(bridge.categoryCommand(request));QTRY_VERIFY(!bridge.busy()&&finished.count()==7);QCOMPARE(latest(finished)->status,ui::CategoryCommandStatus::Rejected);QCOMPARE(test::read(fixture.stateFile),outside);
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed.last()[2].toBool());
  }
  void actualCancelledReadAndRecoveryCandidatesNeverReplayAWrite(){
    test::OwnedEngineFixture fixture(false);stopped(fixture);const auto path=fixture.profile+"/categories.config";const auto original=definitions();test::write(path,original);
    app::SessionBridge bridge;QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),finished(&bridge,&app::SessionBridge::categoryCommandFinished),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(),0));QTRY_VERIFY(!bridge.busy()&&!rows.empty());
    struct stat value{};QVERIFY(::stat(path.toStdString().c_str(),&value)==0);watchedDevice=value.st_dev;watchedInode=value.st_ino;readEntered=false;holdRead=true;watchRead=true;
    const auto release=qScopeGuard([]{watchRead=false;stopRead();});
    ui::CategoryCommand request;request.session=sessions.last()[0].toULongLong();request.request=1;QVERIFY(bridge.categoryCommand(request));QTRY_VERIFY_WITH_TIMEOUT(readEntered.load(),10000);bridge.cancel();stopRead();watchRead=false;
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished.count()==1,10000);QCOMPARE(latest(finished)->status,ui::CategoryCommandStatus::Cancelled);QCOMPARE(test::read(path),original);
    test::write(path+".new",QByteArray("de"));request.request=2;QVERIFY(bridge.categoryCommand(request));QTRY_VERIFY(!bridge.busy()&&finished.count()==2);QCOMPARE(latest(finished)->status,ui::CategoryCommandStatus::Loaded);QVERIFY(!latest(finished)->writable);
    request.kind=ui::CategoryCommandKind::Create;request.name=QStringLiteral("Refused");request.generation=latest(finished)->generation;request.request=3;QVERIFY(bridge.categoryCommand(request));QTRY_VERIFY(!bridge.busy()&&finished.count()==3);QCOMPARE(latest(finished)->status,ui::CategoryCommandStatus::Rejected);QCOMPARE(test::read(path),original);QCOMPARE(test::read(path+".new"),QByteArray("de"));
    QVERIFY(QFile::remove(path+".new"));request.kind=ui::CategoryCommandKind::Reload;request.request=4;QVERIFY(bridge.categoryCommand(request));QTRY_VERIFY(!bridge.busy()&&finished.count()==4);QVERIFY(latest(finished)->writable);QCOMPARE(test::read(path),original); // Explicit read, no replay of Refused.
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed.last()[2].toBool());
  }
};
QTEST_MAIN(CategoryBridgeTests)
#include "category_bridge_tests.moc"
