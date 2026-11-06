// SPDX-License-Identifier: GPL-2.0-or-later
#include "session_bridge.hpp"
#include "library_window.hpp"
#include "category_editor.hpp"
#include "category_store.hpp"
#include <future>
#include "owned_engine_fixture.hpp"
#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <array>
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
  QObject::connect(&bridge,&Bridge::recoveryCloseOffered,&window,&Window::offerRecoveryClose);
  QObject::connect(&window,&Window::sessionCloseWithoutCheckpointRequested,&bridge,[owner=QPointer<Bridge>(&bridge),view](quint64 session){if(owner&&view&&!owner->closeSessionWithoutCheckpoint(session)){view->retrySessionClose();}});
  QObject::connect(&bridge,&Bridge::sessionClosedWithoutCheckpoint,&window,[view](quint64,bool,bool){if(view){view->completeSessionClose();}});
}
struct GuardedProfileFile {
  QByteArray bytes;std::array<long long,8> stamp;
  bool operator==(const GuardedProfileFile&)const=default;
};
std::map<QString,GuardedProfileFile> profileImage(const QString& root){
  std::map<QString,GuardedProfileFile> out;
  QDirIterator entries(root,QDir::Files|QDir::Hidden|QDir::System,QDirIterator::Subdirectories);
  while(entries.hasNext()){
    const auto path=entries.next();struct stat s{};test::require(::lstat(path.toStdString().c_str(),&s)==0&&S_ISREG(s.st_mode),"Generated profile regular file");
    out.emplace(QDir(root).relativeFilePath(path),GuardedProfileFile{test::read(path),{static_cast<long long>(s.st_dev),static_cast<long long>(s.st_ino),static_cast<long long>(s.st_mode),s.st_size,s.st_mtim.tv_sec,s.st_mtim.tv_nsec,s.st_ctim.tv_sec,s.st_ctim.tv_nsec}});
  }
  return out;
}
thread_local std::stop_source* recoveryStop=nullptr;
thread_local bool recoveryUnacknowledged=false;
void pauseCategoryDeletion(storage::CategoryDeletionStage stage,std::size_t){
  if(recoveryUnacknowledged&&stage==storage::CategoryDeletionStage::MemberWritten){
    throw std::runtime_error("Generated unacknowledged writer prefix");
  }
  if(!recoveryUnacknowledged&&stage==storage::CategoryDeletionStage::JournalSynced){recoveryStop->request_stop();}
}
void prepareCategoryRecovery(test::OwnedEngineFixture& fixture,const QString& kind){
  // Real generated store/lease on its construction worker. No serialized
  // journal synthesis, GUI grant, network service or authority test hook.
  auto work=std::async(std::launch::async,[&]{
    auto take=[]<class T>(std::expected<T,storage::Error> value)->T{
      if(!value){throw std::runtime_error(value.error().message);}return std::move(*value);
    };
    auto lease=take(storage::ProfileLease::acquire(fixture.native()));
    auto store=take(storage::CategoryStore::open(lease));
    auto defs=take(store->create(take(store->snapshot()),"Recover me")).snapshot;
    const auto target=take(store->readTarget(defs,*fixture.hash));
    take(store->assign(defs,target,std::string("Recover me")));
    std::stop_source stop;recoveryStop=&stop;recoveryUnacknowledged=kind==QStringLiteral("unacknowledged");
    const auto reset=qScopeGuard([]{recoveryStop=nullptr;recoveryUnacknowledged=false;});
    auto result=store->remove(defs,"Recover me",stop.get_token(),storage::CategoryStore::Deadline::max(),pauseCategoryDeletion);
    test::require(!result&&result.error().code==(recoveryUnacknowledged?storage::ErrorCode::Conflict:storage::ErrorCode::Cancelled),"Real deletion prefix was not retained");
    test::require(bool(store->close()),"Close generated category writer");
  });
  work.get();
  if(kind==QStringLiteral("external")){auto bytes=test::read(fixture.stateFile);test::require(QFile::rename(fixture.stateFile,fixture.stateFile+".foreign"),"Replace generated physical preimage");test::write(fixture.stateFile,bytes);} // Same bytes are not the same inode.
  if(kind==QStringLiteral("legacy")){test::write(fixture.profile+"/.BiglyBTC.category-delete","legacy preserved review");}
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
  void actualRetainedPublicRecoveryCloseRequiresRefusalAndPreservesProfile(){
    test::OwnedEngineFixture fixture(false,true);stopped(fixture);QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);
    app::SessionBridge bridge;ui::LibraryWindow window;wire(bridge,window);window.show();
    QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),traffic(&bridge,&app::SessionBridge::progressReady),failed(&bridge,&app::SessionBridge::sessionCloseFailed),offered(&bridge,&app::SessionBridge::recoveryCloseOffered),normalClosed(&bridge,&app::SessionBridge::sessionClosed),drained(&bridge,&app::SessionBridge::sessionClosedWithoutCheckpoint),intent(&window,&ui::LibraryWindow::sessionCloseWithoutCheckpointRequested);
    QVERIFY(bridge.open(fixture.native(),fixture.port()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!rows.empty()&&!sessions.empty(),30000);
    const auto session=sessions.last()[0].toULongLong();QVERIFY(!bridge.closeSessionWithoutCheckpoint(session+1));
    // Same-session scalar is insufficient: no ordinary refusal has occurred.
    QVERIFY(bridge.closeSessionWithoutCheckpoint(session));QTRY_VERIFY(!bridge.busy()&&failed.count()==1);QVERIFY(drained.empty()&&normalClosed.empty());
    QVERIFY(bridge.downloadAction({target(rows)},core::DownloadAction::Start));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000);
    const auto progressed=[&]{for(const auto& row:traffic){if(qvariant_cast<ui::Telemetry>(row[0]).completed>0){return true;}}return false;};
    QTRY_VERIFY_WITH_TIMEOUT(progressed()&&qvariant_cast<ui::Presentation>(rows.last()[0])->items.front().providerAdmission,30000);
    const auto live=qvariant_cast<ui::Presentation>(rows.last()[0]);QVERIFY(live->items.front().providerAdmission);
    // Fence the existing serial worker with an actual read. A preceding idle
    // accounting turn must finish before this read starts; injecting the external
    // candidate here cannot race a prior already-authorized profile save.
    struct stat inventoryInfo{};QVERIFY(::stat((fixture.profile+"/downloads.config").toStdString().c_str(),&inventoryInfo)==0);
    watchedDevice=inventoryInfo.st_dev;watchedInode=inventoryInfo.st_ino;readEntered=false;holdRead=true;watchRead=true;
    const auto releaseRead=qScopeGuard([]{watchRead=false;stopRead();});
    QVERIFY(bridge.refresh());QTRY_VERIFY_WITH_TIMEOUT(readEntered.load(),10000);
    // Real external source-review candidate, deliberately legacy/ambiguous;
    // never a fabricated safe acknowledgement or a new source permission.
    test::write(fixture.profile+"/.BiglyBTC.category-delete","preserve external recovery evidence");
    stopRead();watchRead=false;QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000);
    const auto before=profileImage(fixture.profile);const auto bytesBefore=test::read(fixture.stateFile);
    bool declined=false,confirmed=false;
    // The actual backend refusal is async. Drive the modal only when it exists,
    // without guessing its timing or accepting an unrelated dialog.
    QTimer answer;answer.setInterval(10);QObject::connect(&answer,&QTimer::timeout,&window,[&]{if(auto* question=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());question&&question->windowTitle()==QStringLiteral("Close without saving progress")){if(auto* button=question->button(QMessageBox::No)){declined=true;button->click();}}});answer.start();
    window.close();QTRY_VERIFY_WITH_TIMEOUT(failed.count()==2&&offered.count()==1&&declined&&!bridge.busy(),10000);answer.stop();
    QVERIFY(drained.empty()&&normalClosed.empty()&&intent.empty());QVERIFY(profileImage(fixture.profile)==before);QVERIFY(window.isVisible());
    QObject::disconnect(&answer,nullptr,&window,nullptr);QObject::connect(&answer,&QTimer::timeout,&window,[&]{if(auto* question=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());question&&question->windowTitle()==QStringLiteral("Close without saving progress")){if(auto* button=question->button(QMessageBox::Yes)){confirmed=true;button->click();}}});answer.start();
    const auto stopAnswers=qScopeGuard([&]{answer.stop();});window.close();
    QTRY_VERIFY_WITH_TIMEOUT(drained.count()==1&&!bridge.busy(),15000);QVERIFY(confirmed);QCOMPARE(intent.count(),1);QCOMPARE(intent.last()[0].toULongLong(),session);QVERIFY(normalClosed.empty());
    QVERIFY(drained.last()[1].toBool()&&drained.last()[2].toBool());QVERIFY(profileImage(fixture.profile)==before);QCOMPARE(test::read(fixture.stateFile),bytesBefore);
    QVERIFY(!bridge.closeSessionWithoutCheckpoint(session));
    {auto lease=storage::ProfileLease::acquire(fixture.native());QVERIFY(lease);} // Actual SDK/actors/disk and same-OFD lease ended.
    // Reopen preserves the unresolved evidence and uses recovery-only startup;
    // omission never forges a successful checkpoint or automatically replays it.
    app::SessionBridge reopened;QSignalSpy fresh(&reopened,&app::SessionBridge::snapshotReady),ended(&reopened,&app::SessionBridge::sessionClosed);
    QVERIFY(reopened.openNormal(fixture.native()));QTRY_VERIFY(!reopened.busy()&&!fresh.empty());
    QVERIFY(profileImage(fixture.profile)==before);const auto view=qvariant_cast<ui::Presentation>(fresh.last()[0]);QCOMPARE(view->items.front().providerAdmission,quint64{0});
    QVERIFY(reopened.closeSession());QTRY_COMPARE_WITH_TIMEOUT(ended.count(),1,15000);QVERIFY(ended.last()[1].toBool()&&ended.last()[2].toBool());
  }
  void interruptedDeletionRequiresExplicitCurrentWorkerResume_data(){
    QTest::addColumn<QString>("kind");
    for(const auto* kind:{"acknowledged","unacknowledged","external","legacy"}){QTest::newRow(kind)<<QString::fromLatin1(kind);}
  }
  void interruptedDeletionRequiresExplicitCurrentWorkerResume(){
    QFETCH(QString,kind);test::OwnedEngineFixture fixture(false); // Existing generated fixture only.
    const auto path=fixture.profile+"/categories.config",journal=fixture.profile+"/BiglyBTC.category-delete.config";
    prepareCategoryRecovery(fixture,kind);
    const auto definitionsBefore=test::read(path),stateBefore=test::read(fixture.stateFile),journalBefore=test::read(journal);
    const auto inventory=test::read(fixture.profile+"/downloads.config"),payload=test::read(fixture.payload+'/'+fixture.files.front().relative);
    app::SessionBridge bridge;ui::LibraryWindow window;wire(bridge,window);window.show();
    QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),finished(&bridge,&app::SessionBridge::categoryCommandFinished),closed(&bridge,&app::SessionBridge::sessionClosed),intents(&window,&ui::LibraryWindow::categoryCommandRequested),traffic(&bridge,&app::SessionBridge::progressReady);
    // Fixture row is queued75, not stopped70. Pending journal must prevent
    // normal automatic owner startup BEFORE any resilient writer can run.
    QVERIFY(bridge.openNormal(fixture.native()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!rows.empty()&&!sessions.empty(),10000);
    auto* table=window.findChild<QTableView*>(QStringLiteral("incomplete-torrents"));auto* model=window.findChild<ui::LibraryModel*>();QVERIFY(table&&model);QTRY_COMPARE(table->model()->rowCount(),1);QTRY_COMPARE(model->revision(),target(rows).generation);table->setCurrentIndex(table->model()->index(0,0));table->selectRow(0);
    auto shown=qvariant_cast<ui::Presentation>(rows.last()[0]);QCOMPARE(shown->items.front().providerAdmission,quint64{0});QCOMPARE(shown->items.front().providerGeneration,quint64{0});QVERIFY(traffic.empty());
    QCOMPARE(test::read(path),definitionsBefore);QCOMPARE(test::read(fixture.stateFile),stateBefore);QCOMPARE(test::read(journal),journalBefore);QCOMPARE(test::read(fixture.profile+"/downloads.config"),inventory);
    auto* manage=window.findChild<QAction*>(QStringLiteral("classic-categories"));QVERIFY(manage&&manage->isEnabled());manage->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished.count()==1,10000);QCOMPARE(latest(finished)->status,ui::CategoryCommandStatus::Loaded);QVERIFY(latest(finished)->recoveryRequired&&!latest(finished)->writable);
    QPointer<ui::CategoryManagerDialog> dialog=window.findChild<ui::CategoryManagerDialog*>();QVERIFY(dialog);
    auto* resume=dialog->findChild<QPushButton*>(QStringLiteral("category-resume-delete"));auto* reload=dialog->findChild<QPushButton*>(QStringLiteral("category-reload"));QVERIFY(resume&&reload);
    const bool safe=kind==QStringLiteral("acknowledged");
    QCOMPARE(latest(finished)->recovery.state,safe?ui::CategoryRecoveryInfo::State::Resumable:ui::CategoryRecoveryInfo::State::Review);QCOMPARE(resume->isEnabled(),safe);
    if(safe){
      QCOMPARE(latest(finished)->recovery.name,QStringLiteral("Recover me"));QCOMPARE(latest(finished)->recovery.acknowledgedMembers,quint64{0});QCOMPARE(latest(finished)->recovery.totalMembers,quint64{1});
      const auto stale=*latest(finished);reload->click();QTRY_VERIFY(!bridge.busy()&&finished.count()==2);
      ui::CategoryCommand forged;forged.session=stale.session;forged.request=100;forged.generation=stale.generation;forged.kind=ui::CategoryCommandKind::ResumeRemove;forged.name=QStringLiteral("Recover me");
      QVERIFY(bridge.categoryCommand(forged));QTRY_VERIFY(!bridge.busy()&&finished.count()==3);QCOMPARE(latest(finished)->status,ui::CategoryCommandStatus::Rejected);QCOMPARE(test::read(journal),journalBefore);
      // Use a fresh Window session below because its request IDs are real and
      // independent of the deliberately forged worker sequence just consumed.
      QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed.last()[1].toBool()&&closed.last()[2].toBool());
      QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
      QVERIFY(bridge.openNormal(fixture.native()));QTRY_VERIFY(!bridge.busy()&&sessions.count()==2);QTRY_COMPARE(model->revision(),target(rows).generation);QTRY_COMPARE(table->model()->rowCount(),1);table->setCurrentIndex(table->model()->index(0,0));table->selectRow(0);manage->trigger();QTRY_VERIFY(!bridge.busy()&&finished.count()==4);
      dialog=window.findChild<ui::CategoryManagerDialog*>();QVERIFY(dialog);resume=dialog->findChild<QPushButton*>(QStringLiteral("category-resume-delete"));QVERIFY(resume&&resume->isEnabled());
      bool declined=false;QTimer::singleShot(0,&window,[&]{if(auto* question=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){if(auto* button=question->button(QMessageBox::No)){declined=true;button->click();}}});
      QTimer::singleShot(1500,&window,[]{if(auto* question=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){question->reject();}});
      resume->click();QVERIFY(declined);QCOMPARE(finished.count(),4);QCOMPARE(test::read(journal),journalBefore);
      bool confirmed=false;QTimer::singleShot(0,&window,[&]{if(auto* question=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){if(auto* button=question->button(QMessageBox::Yes)){confirmed=true;button->click();}}});
      QTimer::singleShot(1500,&window,[]{if(auto* question=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){question->reject();}});
      const auto oldIntents=intents.count();resume->click();QVERIFY(confirmed);QCOMPARE(intents.count(),oldIntents+1);QCOMPARE(qvariant_cast<ui::CategoryCommand>(intents.last()[0]).kind,ui::CategoryCommandKind::ResumeRemove);QVERIFY(dialog&&!resume->isEnabled());
      QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished.count()==5,10000);QCOMPARE(latest(finished)->status,ui::CategoryCommandStatus::Saved);QVERIFY(latest(finished)->refreshFailure.isEmpty());QCOMPARE(latest(finished)->recovery.state,ui::CategoryRecoveryInfo::State::None);QVERIFY(latest(finished)->restartRequired);QVERIFY(!QFileInfo::exists(journal));
      auto state=core::Document::parse(test::read(fixture.stateFile).toStdString());QVERIFY(state&&state->find("attributes")&&!state->find("attributes")->find("category"));
      QVERIFY(rawPreserved(stateBefore,test::read(fixture.stateFile),{"info","resume","opaque.fixture"}));
      shown=qvariant_cast<ui::Presentation>(rows.last()[0]);QCOMPARE(shown->items.front().providerAdmission,quint64{0});QVERIFY(traffic.empty()); // Successful resume does not implicitly start queued row.
    }else{
      const auto packet=latest(finished);ui::CategoryCommand forged;forged.session=packet->session;forged.request=100;forged.generation=packet->generation;forged.kind=ui::CategoryCommandKind::ResumeRemove;forged.name=QStringLiteral("Recover me");
      QVERIFY(bridge.categoryCommand(forged));QTRY_VERIFY(!bridge.busy()&&finished.count()==2);QCOMPARE(latest(finished)->status,ui::CategoryCommandStatus::Rejected);
      QCOMPARE(test::read(path),definitionsBefore);QCOMPARE(test::read(fixture.stateFile),stateBefore);QCOMPARE(test::read(journal),journalBefore);
      if(kind==QStringLiteral("legacy")){QCOMPARE(test::read(fixture.profile+"/.BiglyBTC.category-delete"),QByteArray("legacy preserved review"));}
    }
    QCOMPARE(test::read(fixture.profile+"/downloads.config"),inventory);QCOMPARE(test::read(fixture.payload+'/'+fixture.files.front().relative),payload);
    const int expected=closed.count()+1;window.close();QTRY_COMPARE_WITH_TIMEOUT(closed.count(),expected,15000);QVERIFY(closed.last()[1].toBool()&&closed.last()[2].toBool());
    {auto lease=storage::ProfileLease::acquire(fixture.native());QVERIFY(lease);} // Owning summaries retain no OFD.
  }
  void pendingResumeCancelledBeforePublicationAndJoinedClosePreservesPrefix(){
    test::OwnedEngineFixture fixture(false);prepareCategoryRecovery(fixture,QStringLiteral("acknowledged"));
    const auto path=fixture.profile+"/categories.config",journal=fixture.profile+"/BiglyBTC.category-delete.config";
    const auto before=test::read(journal),state=test::read(fixture.stateFile),defs=test::read(path);
    app::SessionBridge bridge;QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),finished(&bridge,&app::SessionBridge::categoryCommandFinished),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(fixture.native(),0));QTRY_VERIFY(!bridge.busy()&&!sessions.empty());
    ui::CategoryCommand command;command.session=sessions.last()[0].toULongLong();command.request=1;QVERIFY(bridge.categoryCommand(command));QTRY_VERIFY(!bridge.busy()&&finished.count()==1);QCOMPARE(latest(finished)->recovery.state,ui::CategoryRecoveryInfo::State::Resumable);
    struct stat info{};QVERIFY(::stat(path.toStdString().c_str(),&info)==0);watchedDevice=info.st_dev;watchedInode=info.st_ino;readEntered=false;holdRead=true;watchRead=true;
    const auto unblock=qScopeGuard([]{watchRead=false;stopRead();});
    command.kind=ui::CategoryCommandKind::ResumeRemove;command.name=QStringLiteral("Recover me");command.generation=latest(finished)->generation;command.request=2;
    QVERIFY(bridge.categoryCommand(command));QTRY_VERIFY_WITH_TIMEOUT(readEntered.load(),10000);bridge.cancel();stopRead();watchRead=false;
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&finished.count()==2,10000);QCOMPARE(latest(finished)->status,ui::CategoryCommandStatus::Cancelled);
    QCOMPARE(test::read(journal),before);QCOMPARE(test::read(fixture.stateFile),state);QCOMPARE(test::read(path),defs);
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.count(),1,15000);QVERIFY(closed.last()[1].toBool()&&closed.last()[2].toBool());
    command.request=3;QVERIFY(!bridge.categoryCommand(command));QCOMPARE(test::read(journal),before);
  }
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
    // A retained real Public owner can checkpoint a guarded state. Refuse
    // before creating ANY journal rather than promising active-owner recovery.
    const auto retainedState=test::read(fixture.stateFile),retainedDefs=test::read(path);
    request.kind=ui::CategoryCommandKind::Remove;request.request=5;request.generation=latest(finished)->generation;request.target=target(rows);
    QVERIFY(bridge.categoryCommand(request));QTRY_VERIFY(!bridge.busy()&&finished.count()==5);QCOMPARE(latest(finished)->status,ui::CategoryCommandStatus::Rejected);
    QVERIFY(!QFileInfo::exists(fixture.profile+"/BiglyBTC.category-delete.config"));QCOMPARE(test::read(fixture.stateFile),retainedState);QCOMPARE(test::read(path),retainedDefs);
    QCOMPARE(presentation()->items.front().providerAdmission,admission);
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
