// Explicit test-only Host/UI I2P transfer in the owned five-namespace fixture.
// The seed uses the qualified low-level owner; the leech uses only real UI actions.
#include "owner.hpp"
#include "profile.hpp"
#include "biglybtc/library.hpp"
#include <libtorrent/bencode.hpp>
#include <libtorrent/entry.hpp>
#include "session_bridge.hpp"
#include "library_window.hpp"
#include "statistics_page.hpp"
#include "i2p_statistics_view.hpp"
#include <QApplication>
#include <QAction>
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
namespace provider=i2p::provider;
namespace lt=libtorrent;
namespace {
constexpr qsizetype PayloadBytes=131072;
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
QByteArray expectedPayload(){QByteArray result(PayloadBytes,'\0');for(qsizetype n=0;n<result.size();++n){result[n]=static_cast<char>((n*31+17)%251);}return result;}

struct Fixture {
  QString profile,payload,key;
  biglybtc::core::TorrentHash hash;
  i2p::Hash identity;
  struct stat original{};
  Fixture(const QString& work,const QString& role,bool seed,const QString& tracker,const QByteArray& all)
      :profile(work+'/'+role+"-profile"),payload(work+'/'+role+"-payload/sample.bin"),
       key(profile+"/plugins/azneti2phelper/dest_key1.dat"),hash(makeHash(all)){
    privateDirectory(profile);privateDirectory(profile+"/active");privateDirectory(profile+"/plugins");
    privateDirectory(profile+"/plugins/azneti2phelper");privateDirectory(QFileInfo(payload).absolutePath());
    SecretBytes secret{read(work+'/'+role+".keys",1024)};
    auto decoded=i2p::decodeClientIdentity(view(secret.bytes));require(decoded.has_value(),"Actual Java identity stream rejected");
    auto destination=i2p::decodeDestination(view(read(work+'/'+role+".dest",1024)));
    require(destination&&destination->hash==decoded->destination()->hash,"Actual Java public/private identity differs");
    identity=destination->hash;write(key,secret.bytes);
    write(payload,seed?all:QByteArray(all.size(),'\0'));require(::stat(payload.toUtf8().constData(),&original)==0,"Cannot stat original payload inode");
    const auto state=profile+"/active/"+QString::fromStdString(hash.hex())+".dat";
    lt::entry torrent(lt::entry::dictionary_type{});torrent["info"]=info(all);torrent["announce"]=tracker.toStdString();
    torrent["attributes"]["networks"]=lt::entry::list_type{lt::entry("I2P")};
    torrent["attributes"]["peersources"]=lt::entry::list_type{lt::entry("Tracker")};
    torrent["attributes"]["storetypes"]=lt::entry::list_type{lt::entry("L")};
    torrent["attributes"]["opaque.fixture"]=std::string("\0\xffQ",3);write(state,encode(torrent));
    lt::entry row(lt::entry::dictionary_type{});const auto raw=QByteArray::fromHex(QByteArray::fromStdString(hash.hex()));
    row["torrent_hash"]=raw.toStdString();row["torrent"]=state.toStdString();row["save_dir"]=QFileInfo(payload).absolutePath().toStdString();row["save_file"]="sample.bin";
    row["persistent"]=1;row["state"]=seed?75:70;row["position"]=1;row["completedbytes"]=seed?PayloadBytes:0;
    row["downloaded"]=0;row["uploaded"]=0;row["file_priorities"]=lt::entry::list_type{lt::entry(1)};
    lt::entry inventory(lt::entry::dictionary_type{});inventory["downloads"]=lt::entry::list_type{row};write(profile+"/downloads.config",encode(inventory));
    lt::entry settings(lt::entry::dictionary_type{});settings["PluginInfo.azneti2phelper.enabled"]=1;
    settings["Plugin.azneti2phelper.enable"]=1;settings["Plugin.azneti2phelper.azi2phelper.pure.enabled"]=1;
    write(profile+"/biglybt.config",encode(settings));write(profile+"/tables.config",QByteArray("de"));
  }
  static lt::entry info(const QByteArray& all){
    lt::entry result(lt::entry::dictionary_type{});result["name"]="sample.bin";result["length"]=PayloadBytes;result["piece length"]=16384;
    std::string pieces;for(qsizetype offset=0;offset<all.size();offset+=16384){pieces+=QCryptographicHash::hash(all.mid(offset,16384),QCryptographicHash::Sha1).toStdString();}result["pieces"]=pieces;return result;
  }
  static biglybtc::core::TorrentHash makeHash(const QByteArray& all){
    const auto bytes=QCryptographicHash::hash(encode(info(all)),QCryptographicHash::Sha1);const auto hash=biglybtc::core::TorrentHash::parse(std::string_view(bytes.constData(),static_cast<std::size_t>(bytes.size())));
    require(hash.has_value(),"Generated actual metainfo hash rejected");return *hash;
  }
  bool intact(const QByteArray& expected)const{
    struct stat now{};return ::stat(payload.toUtf8().constData(),&now)==0&&now.st_dev==original.st_dev&&now.st_ino==original.st_ino&&read(payload,PayloadBytes)==expected;
  }
};
cp::SessionOptions zeroHop(){return {{"inbound.length","0"},{"outbound.length","0"},{"inbound.lengthVariance","0"},{"outbound.lengthVariance","0"},{"inbound.quantity","1"},{"outbound.quantity","1"},{"inbound.backupQuantity","0"},{"outbound.backupQuantity","0"},{"inbound.allowZeroHop","true"},{"outbound.allowZeroHop","true"},{"i2cp.leaseSetType","3"},{"i2cp.leaseSetEncType","4"},{"i2cp.dontPublishLeaseSet","false"},{"i2cp.fastReceive","true"},{"i2cp.messageReliability","none"}};}

namespace app=biglybtc::app;
namespace ui=biglybtc::ui;
using Action=biglybtc::core::DownloadAction;

enum class Stage {Open,Start,Transfer,Statistics,Library,Pause,Paused,Resume,Resumed,Stop,Stopped,Close,Closed};

bool selectTorrent(ui::LibraryWindow& window,const QString& hash){
  const int row=window.model().rowFor(hash);if(row<0){return false;}
  const auto source=window.model().index(row,0);
  for(auto* table:{&window.incompleteTable(),&window.completeTable()}){
    auto* proxy=qobject_cast<QAbstractProxyModel*>(table->model());
    if(!proxy){continue;}const auto index=proxy->mapFromSource(source);if(!index.isValid()){continue;}
    table->selectionModel()->setCurrentIndex(index,QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
    table->setFocus();return true;
  }
  return false;
}
QJsonObject packetJson(const ui::I2pPresentation& p){
  if(!p){return {};}
  return {{"session",QString::number(p->session)},{"sequence",QString::number(p->sequence)},
    {"state",p->state},{"hash",p->hash},{"verified_bytes",p->verifiedBytes},
    {"payload_downloaded",p->payloadDownloaded},{"payload_uploaded",p->payloadUploaded},
    {"download_rate",p->downloadRate},{"upload_rate",p->uploadRate},{"peers",p->peers},
    {"tracker_announces",QString::number(p->trackerAnnounces)},
    {"tracker_replies",QString::number(p->trackerReplies)},
    {"tracker_errors",QString::number(p->trackerErrors)},{"failure",p->failure.left(512)}};
}
}

int main(int argc,char** argv){
  QApplication application(argc,argv);application.setQuitOnLastWindowClosed(false);
  const auto args=application.arguments();if(args.size()!=3){std::cerr<<"probe RANDOM_I2CP_PORT /work\n";return 2;}
  bool valid=false;const auto port=args[1].toUInt(&valid);
  if(!valid||port<1024||port>65535||args[2]!="/work"||QGuiApplication::platformName()!="offscreen"){return 2;}
  const auto work=args[2];QElapsedTimer elapsed;elapsed.start();QTimer poll;
  std::unique_ptr<provider::OwnedTorrent> seed;
  std::unique_ptr<app::SessionBridge> bridge;
  ui::LibraryWindow window;window.resize(1180,820);window.enableSessionCloseHandshake();
  QJsonObject result{{"status","FAIL"},{"scope","actual-Host-UI-I2P-transfer"}};
  ui::Presentation snapshot;ui::I2pPresentation packet;quint64 session=0;
  QString callbackFailure;bool done=false,stopped=false,inTurn=false,started=false;
  bool closeRequested=false,closeJoined=false,closeStorageReleased=false;
  bool statisticsShortcut=false,libraryShortcut=false;
  std::array<unsigned,4> actualActions{};int code=1;Stage stage=Stage::Open;
  const auto diagnostic=[&](const QString& leaf,const QJsonObject& value)noexcept{
    try{output(work+'/'+leaf,value);}catch(...){std::cerr<<"Cannot write bounded diagnostic marker\n";}
  };
  const auto mark=[&](const char* phase)noexcept{
    try{diagnostic("native-phase.json",{{"phase",QString::fromLatin1(phase)},{"elapsed_ms",elapsed.elapsed()}});}catch(...){}
  };
  const auto shutdown=[&]{
    if(stopped){return;}stopped=true;
    mark("before-Host-destruction");bridge.reset();mark("after-Host-destruction");
    mark("before-seed-close");if(seed){seed->close();}mark("after-seed-close");
    mark("before-seed-join");if(seed){seed->join();}mark("after-seed-join");
  };
  const auto finish=[&](bool ok,const QString& reason){
    if(done){return;}done=true;poll.stop();
    diagnostic("native-terminal.json",{{"phase","before-finish-shutdown"},{"intended_success",ok},
      {"reason",reason.left(512)},{"elapsed_ms",elapsed.elapsed()}});
    shutdown();result.insert("status",ok?"PASS":"FAIL");result.insert("reason",reason.left(512));
    result.insert("elapsed_ms",elapsed.elapsed());result.insert("last_i2p",packetJson(packet));
    try{output(work+"/native-result.json",result);}catch(...){ok=false;}
    // Every real owner has already joined. QApplication::quit() can ask
    // the visible window to close and be vetoed by its unacknowledged session
    // handshake on a FAIL path. A PASS reaches here only after that handshake.
    code=ok?0:1;application.exit(code);
  };
  try{
    const auto all=expectedPayload();const auto tracker=QString::fromLatin1(read(work+"/tracker.url",256));
    require(tracker.startsWith("http://")&&tracker.endsWith(".b32.i2p/announce")&&!tracker.contains('\n'),"Exact generated I2P tracker URL required");
    Fixture a(work,"seed",true,tracker,all),b(work,"leech",false,tracker,all);
    require(a.hash==b.hash&&a.identity!=b.identity,"Distinct original identities/same torrent required");
    const auto hash=QString::fromStdString(b.hash.hex());
    write(work+"/torrent.infohash",QByteArray::fromHex(hash.toLatin1()));
    auto lease=biglybtc::storage::ProfileLease::acquire(a.profile.toStdString());
    if(!lease){throw std::runtime_error(lease.error().message);}
    auto prepared=provider::OwnedTorrent::prepare(*lease,a.hash);
    if(!prepared){throw std::runtime_error(prepared.error().message);}seed=std::move(*prepared);
    provider::OwnedTorrent::Options seedOptions;
    seedOptions.router={QHostAddress::LocalHost,static_cast<quint16>(port),"2.13.0",false,30000,false};
    seedOptions.session=zeroHop();seedOptions.ports={6881,6881};require(seed->begin(seedOptions),"Actual seed startup refused");
    app::I2pServiceOptions hostOptions;hostOptions.routerPort=static_cast<quint16>(port);
    hostOptions.sessionOptions=zeroHop();hostOptions.peerPort=6881;
    bridge=std::make_unique<app::SessionBridge>(nullptr,app::SessionBridge::CountryProviderFactory{},
      app::SessionBridge::AzureusServiceFactory{},app::SessionBridge::SchedulerFactory{},hostOptions);
    QObject::connect(bridge.get(),&app::SessionBridge::snapshotReady,&window,[&](ui::Presentation p){snapshot=p;window.setPresentation(p);});
    QObject::connect(bridge.get(),&app::SessionBridge::progressReady,&window,&ui::LibraryWindow::setTelemetry);
    QObject::connect(bridge.get(),&app::SessionBridge::detailsReady,&window,&ui::LibraryWindow::setDetails);
    QObject::connect(bridge.get(),&app::SessionBridge::busyChanged,&window,&ui::LibraryWindow::setBusy);
    QObject::connect(bridge.get(),&app::SessionBridge::messageReady,&window,&ui::LibraryWindow::setMessage);
    QObject::connect(bridge.get(),&app::SessionBridge::i2pReady,&window,[&](ui::I2pPresentation p){
      if(p&&p->session==session&&(!packet||p->sequence>packet->sequence)){packet=p;}window.setI2p(std::move(p));
    });
    QObject::connect(bridge.get(),&app::SessionBridge::columnLayoutSessionReady,&window,[&](quint64 value){session=value;window.beginColumnLayoutSession(value);});
    QObject::connect(&window,&ui::LibraryWindow::downloadActionRequested,&window,[&](ui::DownloadTargets targets,Action action){
      if(targets.size()!=1||targets.front().hash!=hash||!window.model().accepts(targets.front())){callbackFailure="UI dispatched a stale or foreign torrent target";return;}
      std::size_t index=0;
      switch(action){case Action::Start:index=0;break;case Action::Pause:index=1;break;
        case Action::Resume:index=2;break;case Action::Stop:index=3;break;default:callbackFailure="Unexpected UI action";return;}
      if(!bridge||!bridge->downloadAction(std::move(targets),action)){callbackFailure="Host refused the actual UI action";return;}
      ++actualActions[index];
    });
    QObject::connect(&window,&ui::LibraryWindow::refreshRequested,&window,[&]{if(bridge){static_cast<void>(bridge->refresh());}});
    QObject::connect(&window,&ui::LibraryWindow::detailsRequested,&window,[&](ui::Target target){if(bridge){static_cast<void>(bridge->details(target));}});
    QObject::connect(&window,&ui::LibraryWindow::sessionCloseRequested,&window,[&]{
      closeRequested=true;if(!bridge||!bridge->closeSession()){callbackFailure="Host rejected actual window session close";}
    });
    QObject::connect(bridge.get(),&app::SessionBridge::sessionClosed,&window,[&](quint64 value,bool joined,bool released){
      if(value!=session){callbackFailure="Host close acknowledged another session";return;}
      closeJoined=joined;closeStorageReleased=released;window.completeSessionClose();
    });
    QObject::connect(bridge.get(),&app::SessionBridge::sessionCloseFailed,&window,[&]{callbackFailure="Host session close failed";});
    auto* statisticsAction=window.findChild<QAction*>("classic-statistics");
    auto* libraryAction=window.findChild<QAction*>("classic-my-torrents");
    require(statisticsAction&&libraryAction,"Actual Ctrl1/Ctrl5 actions absent");
    QObject::connect(statisticsAction,&QAction::triggered,&window,[&]{statisticsShortcut=true;});
    QObject::connect(libraryAction,&QAction::triggered,&window,[&]{libraryShortcut=true;});
    window.show();require(bridge->openNormal(b.profile.toStdString()),"Host refused actual original leech profile");
    const auto row=[&]() -> const ui::LibraryItem* {
      if(!snapshot){return nullptr;}const auto item=std::find_if(snapshot->items.begin(),snapshot->items.end(),[&](const auto& value){return value.hash==hash;});
      return item==snapshot->items.end()?nullptr:&*item;
    };
    const auto trigger=[&](const char* name,Stage next){
      if(bridge->busy()||!selectTorrent(window,hash)){return false;}
      auto* action=window.findChild<QAction*>(QString::fromLatin1(name));require(action!=nullptr,"Actual torrent QAction absent");
      if(!action->isEnabled()){return false;}stage=next;action->trigger();return true;
    };
    QJsonObject previous;QElapsedTimer progressAge;progressAge.start();
    QObject::connect(&poll,&QTimer::timeout,&application,[&]{
      if(done||inTurn){return;}inTurn=true;struct Turn {bool& flag;~Turn(){flag=false;}} turn{inTurn};
      try{
        require(elapsed.elapsed()<150000,"Absolute Host/UI transfer/lifecycle budget exceeded");
        require(callbackFailure.isEmpty(),callbackFailure.toUtf8().constData());
        mark("before-seed-observation");const auto sa=seed->observation();mark("after-seed-observation");
        require(sa.publicIdentity==a.identity&&sa.phase!=provider::OwnedTorrent::Phase::Withdrawn,"Actual seed identity/authority lost");
        if(packet){require(packet->session==session&&(packet->hash.isEmpty()||packet->hash==hash),"Host published a foreign I2P packet");require(packet->failure.isEmpty(),(QStringLiteral("Host I2P service reported failure: ")+packet->failure).toUtf8().constData());}
        const auto* current=row();
        QJsonObject progress{{"stage",static_cast<int>(stage)},{"i2p",packetJson(packet)},
          {"seed_phase",static_cast<int>(sa.phase)},{"seed_verified_bytes",static_cast<qint64>(sa.verifiedBytes)},{"seed_seeding",sa.seeding},
          {"row_state",current?current->state:QString{}},{"row_completed",current&&current->completed?*current->completed:qint64{-1}},
          {"Host_busy",bridge->busy()}};
        if(progress!=previous||progressAge.elapsed()>=1000){previous=progress;progressAge.restart();progress.insert("elapsed_ms",elapsed.elapsed());diagnostic("native-progress.json",progress);}
        switch(stage){
        case Stage::Open:
          if(!bridge->busy()&&session&&current&&current->state=="Stopped"){
            require(!packet||packet->hash.isEmpty()||packet->verifiedBytes==0,"Stopped original torrent was started without the UI action");stage=Stage::Start;
          }break;
        case Stage::Start:static_cast<void>(trigger("classic-start",Stage::Transfer));break;
        case Stage::Transfer:
          // Connecting is a genuine started service. Cache warming happens only
          // after this marker; waiting for bytes here would deadlock the fixture.
          if(!started&&actualActions[0]==1&&packet&&packet->hash==hash&&packet->state.endsWith(" — I2P")&&sa.phase==provider::OwnedTorrent::Phase::Active){
            output(work+"/native-started.json",{{"status","ACTUAL_TWO_PROVIDER_OWNERS_STARTED"},{"seed_destination_sha256",hex(a.identity)},
              {"leech_destination_sha256",hex(b.identity)},{"torrent_infohash",hash},{"leech_entry","SessionBridge UI Start"}});started=true;
          }
          if(started&&!bridge->busy()&&packet&&packet->state=="Seeding — I2P"&&packet->verifiedBytes==PayloadBytes
              &&packet->trackerAnnounces>0&&packet->trackerReplies>0&&current&&current->completed==PayloadBytes&&current->state=="Seeding — I2P"
              &&sa.seeding&&sa.verifiedBytes==PayloadBytes){
            result.insert("transfer_i2p",packetJson(packet));result.insert("verified_bytes",PayloadBytes);
            require(packet->payloadDownloaded>=PayloadBytes,"Host I2P traffic did not record the actual payload");
            stage=Stage::Statistics;window.activateWindow();window.setFocus();QTest::keyClick(&window,Qt::Key_5,Qt::ControlModifier);
          }break;
        case Stage::Statistics:{
          auto* page=window.statisticsPage();if(!statisticsShortcut||!page||window.mainTabs().currentWidget()!=page){break;}
          auto* widget=page->findChild<QWidget*>("statistics-i2p");auto* stats=dynamic_cast<ui::I2pStatisticsView*>(widget);
          require(stats&&stats->presentation(),"Actual I2P statistics packet absent");const auto shown=stats->presentation();
          require(shown->session==session&&shown->hash==hash&&shown->verifiedBytes==PayloadBytes&&shown->trackerReplies>0,"I2P statistics differ from the actual Host packet");
          auto* state=stats->findChild<QLabel*>("i2p-state");auto* traffic=stats->findChild<QLabel*>("i2p-traffic");
          require(state&&traffic&&state->text().contains(hash)&&traffic->text().contains(QString::number(PayloadBytes)),"Actual I2P statistics labels omit transfer facts");
          auto* tabs=stats->parentWidget();if(auto* tab=qobject_cast<QTabWidget*>(tabs)){tab->setCurrentWidget(stats);}
          // QTabWidget reparents pages to a stack; find the actual owning tabs.
          for(auto* tab:page->findChildren<QTabWidget*>()){if(tab->indexOf(stats)>=0){tab->setCurrentWidget(stats);}}
          require(window.grab().save(work+"/i2p-preview.png"),"Cannot retain actual offscreen I2P window screenshot");
          result.insert("statistics_i2p",packetJson(shown));stage=Stage::Library;
          QTest::keyClick(&window,Qt::Key_1,Qt::ControlModifier);break;
        }
        case Stage::Library:
          if(libraryShortcut&&window.mainTabs().currentWidget()->objectName()=="classic-my-torrents-page"){
            require(window.model().rowFor(hash)>=0&&current&&current->completed==PayloadBytes,"Ctrl1 lost the actual torrent model");stage=Stage::Pause;
          }break;
        case Stage::Pause:static_cast<void>(trigger("classic-pause",Stage::Paused));break;
        case Stage::Paused:
          if(!bridge->busy()&&actualActions[1]==1&&current&&current->state.startsWith("Paused")){
            require(packet&&packet->state=="Paused — I2P","Host did not publish actual I2P Pause");stage=Stage::Resume;
          }break;
        case Stage::Resume:static_cast<void>(trigger("classic-resume",Stage::Resumed));break;
        case Stage::Resumed:
          if(!bridge->busy()&&actualActions[2]==1&&packet&&packet->state=="Seeding — I2P"&&packet->verifiedBytes==PayloadBytes
              &&current&&current->completed==PayloadBytes&&current->state=="Seeding — I2P"){
            // The resumed owner genuinely rechecks retained bytes. This is no
            // second remote-transfer or post-restart cache-discovery claim.
            result.insert("resume_rechecked_verified_bytes",packet->verifiedBytes);stage=Stage::Stop;
          }break;
        case Stage::Stop:static_cast<void>(trigger("classic-stop",Stage::Stopped));break;
        case Stage::Stopped:
          if(!bridge->busy()&&actualActions[3]==1&&current&&current->state.startsWith("Stopped")){
            require(packet&&packet->state=="Stopped — I2P","Host did not publish actual I2P Stop");stage=Stage::Close;
          }break;
        case Stage::Close:stage=Stage::Closed;window.close();break;
        case Stage::Closed:
          if(closeRequested&&closeJoined&&closeStorageReleased&&!bridge->busy()){
            require(std::all_of(actualActions.begin(),actualActions.end(),[](unsigned count){return count==1;}),"UI commands were omitted or repeated");
            shutdown();auto released=biglybtc::storage::ProfileLease::acquire(b.profile.toStdString());
            require(released.has_value(),"Host close retained the original profile lock");
            require(a.intact(all)&&b.intact(all),"Joined payload bytes/original inodes differ");
            result.insert("payload_sha256",QString::fromLatin1(QCryptographicHash::hash(all,QCryptographicHash::Sha256).toHex()));
            result.insert("torrent_infohash",hash);result.insert("seed_destination_sha256",hex(a.identity));result.insert("leech_destination_sha256",hex(b.identity));
            result.insert("both_payload_inodes_preserved",true);result.insert("joined_before_disk_read",true);
            result.insert("Host_snapshot_seeding",true);result.insert("actual_UI_start_pause_resume_stop",true);
            result.insert("Ctrl1_and_Ctrl5",statisticsShortcut&&libraryShortcut);result.insert("Host_session_close_joined",true);result.insert("Host_profile_lock_released",true);
            result.insert("direct_peer_injection",false);result.insert("late_tracker_response_qualified",false);
            finish(true,"Actual tracker-discovered Host/UI I2P transfer, statistics and joined lifecycle controls");
          }break;
        }
      }catch(const std::exception& error){finish(false,QString::fromUtf8(error.what()));}
      catch(...){finish(false,"Unexpected bounded Host/UI acceptance resource failure");}
    });
    poll.start(25);application.exec();mark("application-loop-exited");shutdown();return code;
  }catch(const std::exception& error){finish(false,QString::fromUtf8(error.what()));return 1;}
  catch(...){finish(false,"Unexpected Host/UI setup resource failure");return 1;}
}
