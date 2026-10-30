// Test-only shared-profile, multi-torrent I2P acceptance. No default activation.
// Seed service lives on its caller worker; all leech controls use actual Host/UI.
#include "owned_i2p_service.hpp"
#include "../services/i2p_client/identity.hpp"
#include "../services/i2cp_session/session.hpp"
#include "profile.hpp"
#include "biglybtc/library.hpp"
#include <libtorrent/bencode.hpp>
#include <libtorrent/entry.hpp>
#include "session_bridge.hpp"
#include "library_window.hpp"
#include "statistics_page.hpp"
#include "i2p_statistics_view.hpp"
#include "torrent_intake_dialog.hpp"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QAbstractProxyModel>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPixmap>
#include <QTableView>
#include <QTabWidget>
#include <QTest>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <QTcpSocket>
#include <QNetworkProxy>
#include <QTimer>
#include <QThread>
#include <QMetaObject>
#include <functional>
#include <exception>
#include <openssl/crypto.h>
#include <sys/stat.h>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <array>
#include <algorithm>
#include <optional>
#include <string_view>

namespace i2p=biglybtc::services::i2p;
namespace cp=i2p::i2cp;
namespace lt=libtorrent;
namespace {
constexpr qsizetype PayloadBytes=524288;
constexpr int GlobalDownload=16*1024,GlobalUpload=20*1024;
constexpr int SeedGlobalUpload=40*1024;
void require(bool ok,const char* reason){if(!ok){throw std::runtime_error(reason);}}
i2p::View view(const QByteArray& b){return {reinterpret_cast<const i2p::Byte*>(b.constData()),static_cast<std::size_t>(b.size())};}
QString hex(const i2p::Hash& h){return QByteArray(reinterpret_cast<const char*>(h.data()),static_cast<qsizetype>(h.size())).toHex();}
QByteArray read(const QString& path,qint64 maximum){
  QFile f(path);require(f.open(QIODevice::ReadOnly),"Cannot open generated fixture");
  require(f.size()>=0&&f.size()<=maximum,"Generated fixture exceeds its bound");
  const auto bytes=f.readAll();require(f.error()==QFileDevice::NoError,"Cannot read generated fixture");return bytes;
}
struct SecretBytes {
  QByteArray bytes;
  ~SecretBytes(){OPENSSL_cleanse(bytes.data(),static_cast<std::size_t>(bytes.size()));}
};
void write(const QString& path,const QByteArray& bytes){
  require(QDir().mkpath(QFileInfo(path).absolutePath()),"Cannot create generated parent");
  QSaveFile f(path);require(f.open(QIODevice::WriteOnly),"Cannot open owned output");
  require(f.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner),"Cannot make output private");
  require(f.write(bytes)==bytes.size()&&f.commit(),"Cannot publish owned output");
}
void output(const QString& path,const QJsonObject& object){write(path,QJsonDocument(object).toJson());}
QByteArray encode(const lt::entry& entry){std::string bytes;lt::bencode(std::back_inserter(bytes),entry);return QByteArray::fromStdString(bytes);}
void privateDirectory(const QString& path){require(QDir().mkpath(path),"Cannot create private generated directory");require(::chmod(path.toUtf8().constData(),0700)==0,"Cannot protect generated directory");}
QByteArray expectedPayload(int index){QByteArray result(PayloadBytes,'\0');for(qsizetype n=0;n<result.size();++n){result[n]=static_cast<char>((n*31+17+index*23)%251);}return result;}

namespace app=biglybtc::app;
namespace ui=biglybtc::ui;
using Action=biglybtc::core::DownloadAction;
struct Torrent {
  QString name,path;QByteArray bytes;biglybtc::core::TorrentHash hash;struct stat original{};
  static lt::entry info(const QString& name,const QByteArray& all){
    lt::entry result(lt::entry::dictionary_type{});result["name"]=name.toStdString();result["length"]=PayloadBytes;result["piece length"]=16384;
    std::string pieces;for(qsizetype offset=0;offset<all.size();offset+=16384){pieces+=QCryptographicHash::hash(all.mid(offset,16384),QCryptographicHash::Sha1).toStdString();}result["pieces"]=pieces;return result;
  }
  static biglybtc::core::TorrentHash makeHash(const QString& name,const QByteArray& all){
    const auto bytes=QCryptographicHash::hash(encode(info(name,all)),QCryptographicHash::Sha1);
    auto h=biglybtc::core::TorrentHash::parse(std::string_view(bytes.constData(),static_cast<std::size_t>(bytes.size())));require(h.has_value(),"Generated full torrent hash rejected");return *h;
  }
  Torrent(QString file,QString parent,int index,bool seed):name(std::move(file)),path(parent+'/'+name),bytes(expectedPayload(index)),hash(makeHash(name,bytes)){
    write(path,seed?bytes:QByteArray(PayloadBytes,'\0'));require(::stat(path.toUtf8().constData(),&original)==0,"Cannot stat original payload");
  }
  QString text()const{return QString::fromStdString(hash.hex());}
  bool intact(bool completed)const{
    struct stat now{};return ::stat(path.toUtf8().constData(),&now)==0&&now.st_dev==original.st_dev&&now.st_ino==original.st_ino&&read(path,PayloadBytes)==(completed?bytes:QByteArray(PayloadBytes,'\0'));
  }
};
struct Fixture {
  QString profile,payloadParent;std::vector<Torrent> torrents;i2p::Hash identity{};
  Fixture(const QString& work,const QString& role,bool seed,const QString& tracker):profile(work+'/'+role+"-profile"),payloadParent(work+'/'+role+"-payload"){
    for(const auto& path:{profile,profile+"/active",profile+"/plugins",profile+"/plugins/azneti2phelper",payloadParent}){privateDirectory(path);}
    SecretBytes keys{read(work+'/'+role+".keys",1024)};auto secret=i2p::decodeClientIdentity(view(keys.bytes));
    auto destination=i2p::decodeDestination(view(read(work+'/'+role+".dest",1024)));require(secret&&destination&&secret->destination()->hash==destination->hash,"Actual original key/Destination mismatch");
    identity=destination->hash;write(profile+"/plugins/azneti2phelper/dest_key1.dat",keys.bytes);
    lt::entry inventory(lt::entry::dictionary_type{});inventory["downloads"]=lt::entry::list_type{};
    for(int i=0;i<3;++i){
      torrents.emplace_back(QStringLiteral("sample-%1.bin").arg(i),payloadParent,i,seed);const auto& t=torrents.back();
      const auto state=profile+"/active/"+t.text()+".dat";lt::entry torrent(lt::entry::dictionary_type{});torrent["info"]=Torrent::info(t.name,t.bytes);torrent["announce"]=tracker.toStdString();
      torrent["attributes"]["networks"]=lt::entry::list_type{lt::entry("I2P")};torrent["attributes"]["peersources"]=lt::entry::list_type{lt::entry("Tracker")};torrent["attributes"]["storetypes"]=lt::entry::list_type{lt::entry("L")};
      torrent["attributes"]["opaque.fixture"]=std::string("\0\xffQ",3);write(state,encode(torrent));
      lt::entry row(lt::entry::dictionary_type{});row["torrent_hash"]=QByteArray::fromHex(t.text().toLatin1()).toStdString();row["torrent"]=state.toStdString();row["save_dir"]=payloadParent.toStdString();row["save_file"]=t.name.toStdString();
      row["persistent"]=1;row["state"]=i==2?70:75;row["position"]=i+1;row["completedbytes"]=seed?PayloadBytes:0;row["downloaded"]=0;row["uploaded"]=0;
      row["maxdl"]=(seed?24:(i==0?8:12))*1024;row["maxul"]=(seed?(i==0?24:32):(i==0?10:14))*1024;
      row["file_priorities"]=lt::entry::list_type{lt::entry(1)};inventory["downloads"].list().push_back(row);
    }write(profile+"/downloads.config",encode(inventory));writeSettings(seed,2);write(profile+"/tables.config",QByteArray("de"));
  }
  void writeSettings(bool seed,int downloads)const{
    lt::entry s(lt::entry::dictionary_type{});s["PluginInfo.azneti2phelper.enabled"]=1;s["Plugin.azneti2phelper.enable"]=1;s["Plugin.azneti2phelper.azi2phelper.pure.enabled"]=1;
    s["max downloads"]=downloads;s["max active torrents"]=4;s["min downloads"]=0;s["StartStopManager_iRankType"]=0;
    // Original selectable rank=None avoids inventing unknown swarm/idle facts.
    // Scrapes still carry only real tracker observations; unavailable bits stay visible.
    s["StartStopManager_iFirstPriority_Type"]=1;s["StartStopManager_iFirstPriority_ShareRatio"]=0;
    s["StartStopManager_bFirstPriority_ignore0Peer"]=0;s["StartStopManager_bIgnore0Peers"]=0;
    s["StartStopManager_iMinSeedingTime"]=0;s["StartStopManager_iMinSeedingTimeWhenActive"]=0;
    s["Enable Light Seeding"]=0;s["Light Seed Slots Reserved"]=0;s["Flexible Seed Slots"]=0;
    s["Max Download Speed KBs"]=seed?48:GlobalDownload/1024;s["Max Upload Speed KBs"]=seed?48:GlobalUpload/1024;
    s["Max Upload Speed Seeding KBs"]=seed?SeedGlobalUpload/1024:GlobalUpload/1024;s["enable.seedingonly.upload.rate"]=1;
    write(profile+"/biglybt.config",encode(s));
  }
  biglybtc::core::LibrarySnapshot saved()const{
    const auto raw=read(profile+"/downloads.config",1024*1024);auto d=biglybtc::core::Document::parse(raw.toStdString());require(d.has_value(),"Saved original inventory malformed");
    auto snapshot=biglybtc::core::readLibrary(*d);require(snapshot&&snapshot->issues.empty()&&snapshot->downloads.size()==3,"Saved original inventory lost exact three rows");return std::move(*snapshot);
  }
};
cp::SessionOptions zeroHop(){return {{"inbound.length","0"},{"outbound.length","0"},{"inbound.lengthVariance","0"},{"outbound.lengthVariance","0"},{"inbound.quantity","1"},{"outbound.quantity","1"},{"inbound.backupQuantity","0"},{"outbound.backupQuantity","0"},{"inbound.allowZeroHop","true"},{"outbound.allowZeroHop","true"},{"i2cp.leaseSetType","3"},{"i2cp.leaseSetEncType","4"},{"i2cp.dontPublishLeaseSet","false"},{"i2cp.fastReceive","true"},{"i2cp.messageReliability","none"}};}
// A genuine external caller worker owns every seed service call and destructor.
// No provider wait/join is moved into the product GUI owner by this fixture.
class SeedWorker {
  QThread thread_;QObject dispatch_;std::unique_ptr<app::OwnedI2pService> service_;
  template<class F>void call(F&& f){std::exception_ptr error;require(QMetaObject::invokeMethod(&dispatch_,[&]{try{f();}catch(...){error=std::current_exception();}},Qt::BlockingQueuedConnection),"Seed caller worker unavailable");if(error){std::rethrow_exception(error);}}
public:
  SeedWorker(){require(dispatch_.moveToThread(&thread_),"Cannot move seed caller worker");thread_.start();require(thread_.isRunning(),"Cannot start seed caller worker");}
  ~SeedWorker(){try{close();call([&]{require(dispatch_.moveToThread(QCoreApplication::instance()->thread()),"Cannot return seed dispatch affinity");});}catch(...){}thread_.quit();thread_.wait();}
  void start(const biglybtc::storage::ProfileLease& lease,const std::vector<Torrent>& torrents,const app::I2pServiceOptions& options){call([&]{
    auto s=app::OwnedI2pService::start(lease,torrents[0].hash,options);if(!s){throw std::runtime_error(s.error().message);}service_=std::move(*s);
    auto added=service_->add(torrents[1].hash);if(!added){throw std::runtime_error(added.error().message);}
  });}
  std::vector<app::OwnedI2pService::TorrentObservation> observations(){std::vector<app::OwnedI2pService::TorrentObservation> rows;call([&]{require(service_!=nullptr,"Seed service already closed");rows=service_->observations();});return rows;}
  void close(){if(!thread_.isRunning()){return;}call([&]{
    if(service_){auto closed=service_->close();service_.reset();if(!closed){throw std::runtime_error(closed.error().message);}}
  });}
};
bool selectTorrent(ui::LibraryWindow& window,const QString& hash){
  const auto row=window.model().rowFor(hash);if(row<0){return false;}const auto source=window.model().index(row,0);
  for(auto* table:{&window.incompleteTable(),&window.completeTable()}){auto* proxy=qobject_cast<QAbstractProxyModel*>(table->model());if(!proxy){continue;}const auto index=proxy->mapFromSource(source);if(!index.isValid()){continue;}
    table->selectionModel()->setCurrentIndex(index,QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);table->setFocus();return true;}return false;
}
QJsonObject packetJson(const ui::I2pPresentation& p){
  if(!p){return {};}QJsonArray rows;for(const auto& row:p->torrents){rows.push_back(QJsonObject{{"hash",row.hash},{"state",row.state},{"verified_bytes",row.verifiedBytes},{"payload_downloaded",row.payloadDownloaded},{"payload_uploaded",row.payloadUploaded},{"queued",row.queued},{"admission",QString::number(row.admission)},{"generation",QString::number(row.generation)},{"download_limit",row.downloadLimit},{"upload_limit",row.uploadLimit},{"queue_unavailable",static_cast<qint64>(row.queueUnavailable)}});}
  return {{"session",QString::number(p->session)},{"sequence",QString::number(p->sequence)},{"failure",p->failure},{"global_download_limit",p->globalDownloadLimit},{"global_upload_limit",p->globalUploadLimit},{"loopback_peer_classes",static_cast<qint64>(p->loopbackPeerClasses)},{"torrents",rows}};
}
enum class Stage{Initial,Concurrent,PauseA,PausedA,ResumeA,ResumedA,StopA,StoppedA,QueueA,QueuedA,PauseB,PausedB,FirstClose,FirstClosed,WaitRetired,Reopen,Reopened,ResumeB,SlotLimited,StopRunning,Handoff,RestartStopped,FinishTransfers,StopFinalA,FinalStoppedA,StopFinalB,FinalStoppedB,FinalClose,FinalClosed};
}
int main(int argc,char** argv){
  QApplication application(argc,argv);application.setQuitOnLastWindowClosed(false);
  const auto args=application.arguments();if(args.size()!=3){return 2;}bool valid=false;const auto port=args[1].toUInt(&valid);
  if(!valid||port<1024||port>65535||args[2]!="/work"||QGuiApplication::platformName()!="offscreen"){return 2;}
  const QString work=args[2];QElapsedTimer elapsed;elapsed.start();QTimer poll;
  std::unique_ptr<app::SessionBridge> bridge;std::unique_ptr<SeedWorker> seed;
  ui::LibraryWindow window;window.resize(1180,820);window.enableSessionCloseHandshake();
  ui::Presentation snapshot;ui::I2pPresentation packet;quint64 session=0,initialSession=0;
  bool done=false,inTurn=false,joined=false,released=false,closeRequested=false,shutdownDone=false;int code=1;
  QString callbackFailure;Stage stage=Stage::Initial;QJsonObject result{{"status","FAIL"},{"scope","actual-shared-profile-multi-I2P-Host-transfer"}};
  std::array<std::array<unsigned,10>,3> actions{};std::array<quint64,2> initialAdmissions{},initialGenerations{};
  quint64 stableAdmission=0,stableGeneration=0,restartGeneration=0,slotQueuedAdmission=0,slotRunningAdmission=0;int running=0,waiting=1;
  qint64 siblingBaseline=0,firstSessionBytes=0;QElapsedTimer slotAge;bool slotObserved=false;QJsonObject beforeRestart;std::array<qint64,3> savedReceived{};
  const auto diagnostic=[&](const QString& name,const QJsonObject& value)noexcept{try{output(work+'/'+name,value);}catch(...){std::cerr<<"Cannot write bounded progress marker\n";}};
  const auto mark=[&](const char* value)noexcept{try{diagnostic("native-phase.json",{{"phase",QString::fromLatin1(value)},{"elapsed_ms",elapsed.elapsed()}});}catch(...){}};
  const auto shutdown=[&]{if(shutdownDone){return;}shutdownDone=true;mark("before-Host-destruction");bridge.reset();mark("after-Host-destruction");if(seed){mark("before-seed-close");seed->close();seed.reset();mark("after-seed-join");}};
  const auto finish=[&](bool ok,const QString& reason){
    if(done){return;}done=true;poll.stop();diagnostic("native-terminal.json",{{"reason",reason.left(512)},{"intended_success",ok},{"elapsed_ms",elapsed.elapsed()}});
    try{shutdown();}catch(const std::exception& error){ok=false;result.insert("shutdown_error",QString::fromUtf8(error.what()).left(512));}
    result.insert("status",ok?"PASS":"FAIL");result.insert("reason",reason.left(512));result.insert("elapsed_ms",elapsed.elapsed());result.insert("last_i2p",packetJson(packet));
    try{output(work+"/native-result.json",result);}catch(...){ok=false;}code=ok?0:1;application.exit(code);
  };
  try{
    const QString tracker=QString::fromLatin1(read(work+"/tracker.url",256));require(tracker.startsWith("http://")&&tracker.endsWith(".b32.i2p/announce")&&!tracker.contains('\n'),"Owned tracker URL required");
    Fixture a(work,"seed",true,tracker),b(work,"leech",false,tracker);require(a.identity!=b.identity,"Distinct profile identities required");
    QByteArray hashes;for(std::size_t i=0;i<3;++i){require(a.torrents[i].hash==b.torrents[i].hash,"Seed/leech metainfo differ");hashes+=QByteArray::fromHex(a.torrents[i].text().toLatin1());}
    write(work+"/torrent.hashes",hashes);
    auto seedLease=biglybtc::storage::ProfileLease::acquire(a.profile.toStdString());require(seedLease.has_value(),"Cannot own seed profile");
    app::I2pServiceOptions options;options.routerPort=static_cast<quint16>(port);options.sessionOptions=zeroHop();options.peerPort=6881;
    seed=std::make_unique<SeedWorker>();seed->start(*seedLease,a.torrents,options);
    bridge=std::make_unique<app::SessionBridge>(nullptr,app::SessionBridge::CountryProviderFactory{},app::SessionBridge::AzureusServiceFactory{},app::SessionBridge::SchedulerFactory{},options);
    QObject::connect(bridge.get(),&app::SessionBridge::snapshotReady,&window,[&](ui::Presentation p){snapshot=p;window.setPresentation(std::move(p));});
    QObject::connect(bridge.get(),&app::SessionBridge::progressReady,&window,&ui::LibraryWindow::setTelemetry);
    QObject::connect(bridge.get(),&app::SessionBridge::detailsReady,&window,&ui::LibraryWindow::setDetails);
    QObject::connect(bridge.get(),&app::SessionBridge::busyChanged,&window,&ui::LibraryWindow::setBusy);
    QObject::connect(bridge.get(),&app::SessionBridge::messageReady,&window,&ui::LibraryWindow::setMessage);
    QObject::connect(bridge.get(),&app::SessionBridge::i2pReady,&window,[&](ui::I2pPresentation p){if(p&&p->session==session&&(!packet||p->sequence>packet->sequence)){packet=p;}window.setI2p(std::move(p));});
    QObject::connect(bridge.get(),&app::SessionBridge::columnLayoutSessionReady,&window,[&](quint64 value){session=value;window.beginColumnLayoutSession(value);});
    QObject::connect(&window,&ui::LibraryWindow::refreshRequested,&window,[&]{if(bridge){static_cast<void>(bridge->refresh());}});
    QObject::connect(&window,&ui::LibraryWindow::detailsRequested,&window,[&](ui::Target target){if(bridge){static_cast<void>(bridge->details(target));}});
    QObject::connect(&window,&ui::LibraryWindow::downloadActionRequested,&window,[&](ui::DownloadTargets targets,Action action){
      if(targets.size()!=1||!window.model().accepts(targets.front())){callbackFailure="Stale UI action target";return;}
      std::size_t index=3;for(std::size_t i=0;i<3;++i){if(targets.front().hash==b.torrents[i].text()){index=i;}}
      const auto ordinal=static_cast<std::size_t>(action);if(index==3||ordinal>=10||!bridge->downloadAction(std::move(targets),action)){callbackFailure="Host refused actual per-hash UI action";return;}++actions[index][ordinal];
    });
    QObject::connect(&window,&ui::LibraryWindow::sessionCloseRequested,&window,[&]{closeRequested=true;if(!bridge->closeSession()){callbackFailure="Host refused joined close";}});
    QObject::connect(bridge.get(),&app::SessionBridge::sessionClosed,&window,[&](quint64 closed,bool didJoin,bool didRelease){if(closed!=session){callbackFailure="Close acknowledged another Host session";return;}joined=didJoin;released=didRelease;window.completeSessionClose();});
    QObject::connect(bridge.get(),&app::SessionBridge::sessionCloseFailed,&window,[&]{callbackFailure="Host close/checkpoint failed";});
    const auto row=[&](std::size_t index)->const ui::LibraryItem*{if(!snapshot){return nullptr;}const auto found=std::find_if(snapshot->items.begin(),snapshot->items.end(),[&](const auto& value){return value.hash==b.torrents[index].text();});return found==snapshot->items.end()?nullptr:&*found;};
    const auto live=[&](std::size_t index)->const ui::I2pTorrentPacket*{if(!packet){return nullptr;}const auto found=std::find_if(packet->torrents.begin(),packet->torrents.end(),[&](const auto& value){return value.hash==b.torrents[index].text();});return found==packet->torrents.end()?nullptr:&*found;};
    const auto trigger=[&](std::size_t index,const char* id,Stage next){if(bridge->busy()||!selectTorrent(window,b.torrents[index].text())){return false;}auto* action=window.findChild<QAction*>(QString::fromLatin1(id));require(action!=nullptr,"Required real UI QAction absent");if(!action->isEnabled()){return false;}stage=next;action->trigger();return true;};
    const auto assertSibling=[&](std::size_t index){const auto* value=live(index);require(value&&value->admission==stableAdmission&&value->generation==stableGeneration&&packet->session==initialSession,"Sibling admission/shared session changed during target command");};
    const auto savedRow=[&](const biglybtc::core::LibrarySnapshot& saved,std::size_t i)->const biglybtc::core::SavedDownload&{const auto found=std::find_if(saved.downloads.begin(),saved.downloads.end(),[&](const auto& x){return x.hash&&*x.hash==b.torrents[i].hash;});require(found!=saved.downloads.end(),"Saved full hash missing");return *found;};
    QElapsedTimer progressAge;progressAge.start();window.show();require(bridge->openNormal(b.profile.toStdString()),"Host refused original multi-torrent profile");
    QObject::connect(&poll,&QTimer::timeout,&application,[&]{
      if(done||inTurn){return;}inTurn=true;struct Turn{bool& value;~Turn(){value=false;}} turn{inTurn};
      try{
        require(elapsed.elapsed()<240000,"Absolute multi-torrent Host budget exceeded");require(callbackFailure.isEmpty(),callbackFailure.toUtf8().constData());
        mark("before-seed-observations");const auto seeds=seed->observations();mark("after-seed-observations");require(seeds.size()==2,"Seed lost a shared-session member");
        for(const auto& x:seeds){require(x.value.publicIdentity==a.identity&&x.value.failure.empty()&&x.value.phase!=app::OwnedI2pService::Phase::Withdrawn,"Seed shared identity/source lost");}
        require(seeds[0].value.generation==seeds[1].value.generation&&seeds[0].value.admission!=seeds[1].value.admission,"Seed did not share one generation with independent admissions");
        if(packet){require(packet->session==session&&packet->failure.isEmpty(),"Host emitted foreign/failed I2P packet");for(const auto& x:packet->torrents){require(x.failure.isEmpty(),"Target I2P source failed");}}
        if(progressAge.elapsed()>=1000){progressAge.restart();QJsonArray actual;for(const auto& x:seeds){actual.push_back(QJsonObject{{"hash",QString::fromStdString(x.hash.hex())},{"verified_bytes",static_cast<qint64>(x.value.verifiedBytes)},{"global_download_limit",x.value.globalDownloadLimit},{"global_upload_limit",x.value.globalUploadLimit},{"loopback_peer_classes",static_cast<qint64>(x.value.loopbackPeerClasses)}});}diagnostic("native-progress.json",{{"stage",static_cast<int>(stage)},{"elapsed_ms",elapsed.elapsed()},{"Host_busy",bridge->busy()},{"i2p",packetJson(packet)},{"seed",actual}});}
        const auto* x=live(0);const auto* y=live(1);const auto* z=row(2);require(!live(2),"Stopped sentinel acquired provider admission");if(z){require(z->state=="Stopped","Stopped sentinel intent changed");}
        switch(stage){
        case Stage::Initial:
          if(!bridge->busy()&&session&&snapshot&&snapshot->items.size()==3&&x&&y&&x->generation&&y->generation){
            require(x->generation==y->generation&&x->admission!=y->admission,"Host did not share generation/unique admissions");initialSession=session;initialAdmissions={x->admission,y->admission};initialGenerations={x->generation,y->generation};
            output(work+"/native-started.json",{{"status","ACTUAL_TWO_SHARED_PROFILE_SERVICES_STARTED"},{"seed_destination_sha256",hex(a.identity)},{"leech_destination_sha256",hex(b.identity)},{"active_hashes",2},{"saved_hashes",3}});stage=Stage::Concurrent;
          }break;
        case Stage::Concurrent:
          if(x&&y&&!x->queued&&!y->queued&&x->payloadDownloaded>=16384&&y->payloadDownloaded>=16384){
            require(x->verifiedBytes<PayloadBytes&&y->verifiedBytes<PayloadBytes,"Controls began after transfer already completed");
            require(packet->globalDownloadLimit==GlobalDownload&&packet->globalUploadLimit==GlobalUpload&&(packet->loopbackPeerClasses&1u)!=0,"Actual Host global limit/loopback class filter differs");
            require(x->downloadLimit==8*1024&&y->downloadLimit==12*1024&&x->uploadLimit==10*1024&&y->uploadLimit==14*1024,"Actual per-hash limits differ from saved limits");
            for(const auto& s:seeds){
              require(s.value.globalDownloadLimit==48*1024&&s.value.globalUploadLimit==SeedGlobalUpload&&(s.value.loopbackPeerClasses&1u)!=0,"Shared actual global limit/loopback peer-class filter unavailable");
              const bool first=s.hash==a.torrents[0].hash;require(first||s.hash==a.torrents[1].hash,"Foreign seed full hash");
              require(s.value.downloadLimit==24*1024&&s.value.uploadLimit==(first?24:32)*1024,"Actual seed per-hash limits differ");
            }
            result.insert("concurrent_initial",packetJson(packet));result.insert("actual_seed_global_upload_limit",SeedGlobalUpload);result.insert("actual_seed_global_download_limit",48*1024);result.insert("loopback_global_peer_class",true);
            stableAdmission=y->admission;stableGeneration=y->generation;siblingBaseline=y->payloadDownloaded;stage=Stage::PauseA;
          }break;
        case Stage::PauseA:static_cast<void>(trigger(0,"classic-pause",Stage::PausedA));break;
        case Stage::PausedA:
          if(!bridge->busy()&&row(0)&&row(0)->state.startsWith("Paused")&&!x){assertSibling(1);if(y->payloadDownloaded>=siblingBaseline+16384){result.insert("pause_A_preserved_B",packetJson(packet));stage=Stage::ResumeA;}}break;
        case Stage::ResumeA:static_cast<void>(trigger(0,"classic-resume",Stage::ResumedA));break;
        case Stage::ResumedA:
          if(!bridge->busy()&&x&&y&&!x->queued&&x->payloadDownloaded>=16384){assertSibling(1);require(x->admission!=initialAdmissions[0]&&x->generation==stableGeneration,"Resumed target reused admission or replaced shared generation");siblingBaseline=y->payloadDownloaded;stage=Stage::StopA;}break;
        case Stage::StopA:static_cast<void>(trigger(0,"classic-stop",Stage::StoppedA));break;
        case Stage::StoppedA:
          if(!bridge->busy()&&!x&&row(0)&&row(0)->state=="Stopped"){assertSibling(1);if(y->payloadDownloaded>=siblingBaseline+16384){result.insert("stop_A_preserved_B",packetJson(packet));stage=Stage::QueueA;}}break;
        case Stage::QueueA:static_cast<void>(trigger(0,"classic-queue",Stage::QueuedA));break;
        case Stage::QueuedA:
          if(!bridge->busy()&&x&&y&&x->generation==stableGeneration){assertSibling(1);require(x->verifiedBytes<PayloadBytes&&y->verifiedBytes<PayloadBytes,"No incomplete work remains for restart slot check");stage=Stage::PauseB;}break;
        case Stage::PauseB:static_cast<void>(trigger(1,"classic-pause",Stage::PausedB));break;
        case Stage::PausedB:
          if(!bridge->busy()&&x&&!y&&row(1)&&row(1)->state.startsWith("Paused")){
            require(x->generation==initialGenerations[0],"Sibling pause replaced shared session");beforeRestart=packetJson(packet);
            output(work+"/shared-controls-ready.json",{{"status","ACTUAL_SHARED_TARGET_CONTROLS_COMPLETED"},{"session",QString::number(session)}});stage=Stage::FirstClose;
          }break;
        case Stage::FirstClose:
          if(QFile::exists(work+"/shared-session-precondition.json")){stage=Stage::FirstClosed;joined=released=false;closeRequested=true;require(bridge->closeSession(),"Host refused first joined profile close");}break;
        case Stage::FirstClosed:
          if(closeRequested&&joined&&released&&!bridge->busy()){
            const auto saved=b.saved();require(savedRow(saved,0).originalState==75&&savedRow(saved,1).originalState==70&&savedRow(saved,2).originalState==70,"Joined restart lost exact original75/70/70 intent");
            require(saved.paused.size()==1&&saved.paused.front().hash==b.torrents[1].hash,"Joined restart lost exact saved pause owner");
            for(std::size_t i=0;i<3;++i){require(savedRow(saved,i).downloaded.has_value(),"Joined original payload counter absent");savedReceived[i]=*savedRow(saved,i).downloaded;}
            require(savedReceived[0]>0&&savedReceived[1]>0&&savedReceived[2]==0,"Saved original per-target traffic counters incorrect");firstSessionBytes=savedReceived[0]+savedReceived[1];
            auto free=biglybtc::storage::ProfileLease::acquire(b.profile.toStdString());require(free.has_value(),"Joined Host retained profile lock");
            b.writeSettings(false,1);result.insert("before_restart",beforeRestart);output(work+"/leech-restart-ready.json",{{"status","ACTUAL_HOST_CLOSED_FOR_SAME_IDENTITY_RESTART"},{"original_state_A",75},{"original_state_B",70},{"original_state_C",70},{"B_paused",true},{"saved_received",firstSessionBytes}});stage=Stage::WaitRetired;
          }break;
        case Stage::WaitRetired:
          if(QFile::exists(work+"/leech-retired.json")){stage=Stage::Reopen;packet.reset();snapshot.reset();session=0;window.show();require(bridge->openNormal(b.profile.toStdString()),"Host refused real same-profile restart");}break;
        case Stage::Reopen:
          if(!bridge->busy()&&session&&session!=initialSession&&snapshot&&x&&!y&&row(1)&&row(1)->state.startsWith("Paused")){
            require(x->generation&&x->generation!=initialGenerations[0],"Restart did not establish a fresh actual generation");restartGeneration=x->generation;
            require(x->verifiedBytes<PayloadBytes,"Restart capacity check has no incomplete work");output(work+"/native-restarted.json",{{"status","ACTUAL_SAME_PROFILE_RESTARTED"},{"leech_destination_sha256",hex(b.identity)},{"session",QString::number(session)}});stage=Stage::Reopened;
          }break;
        case Stage::Reopened:
          if(QFile::exists(work+"/cache-restart-precondition.json")){stage=Stage::ResumeB;}break;
        case Stage::ResumeB:static_cast<void>(trigger(1,"classic-resume",Stage::SlotLimited));break;
        case Stage::SlotLimited:
          if(!bridge->busy()&&x&&y&&x->generation==restartGeneration&&y->generation==restartGeneration&&x->queued!=y->queued){
            require(x->verifiedBytes<PayloadBytes&&y->verifiedBytes<PayloadBytes,"Capacity proof requires two incomplete eligible torrents");running=x->queued?1:0;waiting=1-running;const auto* r=live(static_cast<std::size_t>(running));const auto* w=live(static_cast<std::size_t>(waiting));
            if(r->payloadDownloaded<16384||w->payloadDownloaded<0){break;}
            if(!slotObserved){slotObserved=true;slotAge.start();slotQueuedAdmission=w->admission;slotRunningAdmission=r->admission;siblingBaseline=w->payloadDownloaded;break;}
            if(w->admission!=slotQueuedAdmission||r->admission!=slotRunningAdmission||w->payloadDownloaded!=siblingBaseline){slotObserved=false;break;}
            if(slotAge.elapsed()<1000){break;}stableAdmission=w->admission;stableGeneration=w->generation;siblingBaseline=w->payloadDownloaded;result.insert("one_download_slot",packetJson(packet));stage=Stage::StopRunning;
          }break;
        case Stage::StopRunning:static_cast<void>(trigger(static_cast<std::size_t>(running),"classic-stop",Stage::Handoff));break;
        case Stage::Handoff:{
          const auto* active=live(static_cast<std::size_t>(waiting));const auto* stoppedRow=row(static_cast<std::size_t>(running));
          if(!bridge->busy()&&!live(static_cast<std::size_t>(running))&&active&&!active->queued&&stoppedRow&&stoppedRow->state=="Stopped"){
            require(active->admission==stableAdmission&&active->generation==stableGeneration,"Queue handoff readmitted the sibling or replaced session");
            if(active->payloadDownloaded>=siblingBaseline+16384){result.insert("slot_handoff",packetJson(packet));stage=Stage::RestartStopped;}
          }break;
        }
        case Stage::RestartStopped:static_cast<void>(trigger(static_cast<std::size_t>(running),"classic-start",Stage::FinishTransfers));break;
        case Stage::FinishTransfers:
          if(!bridge->busy()&&x&&y&&x->state=="Seeding — I2P"&&y->state=="Seeding — I2P"&&x->verifiedBytes==PayloadBytes&&y->verifiedBytes==PayloadBytes&&row(0)&&row(1)&&row(0)->completed==PayloadBytes&&row(1)->completed==PayloadBytes){
            require(x->generation==restartGeneration&&y->generation==restartGeneration,"Finishing sibling replaced shared generation");result.insert("completed",packetJson(packet));require(window.grab().save(work+"/i2p-multi-preview.png"),"Cannot retain actual multi-torrent window");stage=Stage::StopFinalA;
          }break;
        case Stage::StopFinalA:static_cast<void>(trigger(0,"classic-stop",Stage::FinalStoppedA));break;
        case Stage::FinalStoppedA:if(!bridge->busy()&&!x&&row(0)&&row(0)->state=="Stopped"){stage=Stage::StopFinalB;}break;
        case Stage::StopFinalB:static_cast<void>(trigger(1,"classic-stop",Stage::FinalStoppedB));break;
        case Stage::FinalStoppedB:if(!bridge->busy()&&!y&&row(1)&&row(1)->state=="Stopped"){stage=Stage::FinalClose;}break;
        case Stage::FinalClose:stage=Stage::FinalClosed;joined=released=false;closeRequested=false;window.close();break;
        case Stage::FinalClosed:
          if(closeRequested&&joined&&released&&!bridge->busy()){
            shutdown();const auto saved=b.saved();QJsonArray transfers;for(std::size_t i=0;i<2;++i){const auto& sr=savedRow(saved,i);require(sr.downloaded&&*sr.downloaded>=PayloadBytes&&*sr.downloaded>=savedReceived[i],"Restart/checkpoint lost original per-hash payload totals");require(b.torrents[i].intact(true)&&a.torrents[i].intact(true),"Joined transferred payload bytes or original inode differ");transfers.push_back(QJsonObject{{"info_hash",b.torrents[i].text()},{"verified_bytes",static_cast<qint64>(PayloadBytes)},{"saved_downloaded",static_cast<qint64>(*sr.downloaded)},{"payload_sha256",QString::fromLatin1(QCryptographicHash::hash(b.torrents[i].bytes,QCryptographicHash::Sha256).toHex())}});}
            require(b.torrents[2].intact(false)&&a.torrents[2].intact(true)&&savedRow(saved,2).originalState==70&&savedRow(saved,2).downloaded==0,"Stopped sentinel was activated/modified");
            auto free=biglybtc::storage::ProfileLease::acquire(b.profile.toStdString());require(free.has_value(),"Final joined Host retained profile lock");
            for(std::size_t i=0;i<2;++i){for(auto action:{Action::Pause,Action::Resume,Action::Stop}){require(actions[i][static_cast<std::size_t>(action)]>0,"Required real per-target UI lifecycle action absent");}}
            result.insert("transfers",transfers);result.insert("seed_destination_sha256",hex(a.identity));result.insert("leech_destination_sha256",hex(b.identity));result.insert("verified_bytes",static_cast<qint64>(2*PayloadBytes));result.insert("same_identity_router_barriers",true);result.insert("Host_shared_session_controls",true);result.insert("one_download_limit_handoff",true);result.insert("saved_75_70_pause_restored",true);result.insert("per_target_counters_preserved",true);result.insert("both_payload_inodes_preserved",true);result.insert("joined_before_disk_read",true);result.insert("Host_profile_lock_released",true);result.insert("stopped_sentinel_untouched",true);result.insert("direct_peer_injection",false);finish(true,"Actual two simultaneous tracker-discovered I2P torrents, independent controls and same-profile slot-limited restart");
          }break;
        }
      }catch(const std::exception& error){finish(false,QString::fromUtf8(error.what()));}catch(...){finish(false,"Unexpected bounded multi-torrent resource failure");}
    });
    poll.start(25);application.exec();mark("application-loop-exited");shutdown();return code;
  }catch(const std::exception& error){finish(false,QString::fromUtf8(error.what()));return 1;}catch(...){finish(false,"Unexpected multi-torrent setup failure");return 1;}
}
