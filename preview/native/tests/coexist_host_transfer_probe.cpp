// Test-only Public + pure-I2P coexistence through one actual Host/profile.
// Seed service lives on its caller worker; all leech controls use actual Host/UI.
#include "owned_i2p_service.hpp"
#include "../services/i2p_client/identity.hpp"
#include "../services/i2cp_session/session.hpp"
#include "profile.hpp"
#include "biglybtc/library.hpp"
#include <libtorrent/bencode.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/ip_filter.hpp>
#include <QTcpServer>
#include <QMap>
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
#include <QStatusBar>
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
constexpr qsizetype PayloadBytes=2097152;
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
  static qint64 receivedPrefix(std::size_t i){return static_cast<qint64>((i+1)*1000);}
  static qint64 sentPrefix(std::size_t i){return static_cast<qint64>((i+1)*10);}
  Fixture(const QString& work,const QString& role,bool seed,const QString& i2pTracker,const QString& publicTracker):profile(work+'/'+role+"-profile"),payloadParent(work+'/'+role+"-payload"){
    for(const auto& path:{profile,profile+"/active",profile+"/plugins",profile+"/plugins/azneti2phelper",payloadParent}){privateDirectory(path);}
    SecretBytes keys{read(work+'/'+role+".keys",1024)};auto secret=i2p::decodeClientIdentity(view(keys.bytes));
    auto destination=i2p::decodeDestination(view(read(work+'/'+role+".dest",1024)));require(secret&&destination&&secret->destination()->hash==destination->hash,"Actual original key/Destination mismatch");
    identity=destination->hash;write(profile+"/plugins/azneti2phelper/dest_key1.dat",keys.bytes);
    lt::entry inventory(lt::entry::dictionary_type{});inventory["downloads"]=lt::entry::list_type{};
    for(int i=0;i<3;++i){
      torrents.emplace_back(QStringLiteral("sample-%1.bin").arg(i),payloadParent,i,seed);const auto& t=torrents.back();
      const auto state=profile+"/active/"+t.text()+".dat";lt::entry torrent(lt::entry::dictionary_type{});torrent["info"]=Torrent::info(t.name,t.bytes);torrent["announce"]=(i==0?publicTracker:i2pTracker).toStdString();
      torrent["attributes"]["networks"]=lt::entry::list_type{lt::entry(i==0?"Public":"I2P")};torrent["attributes"]["peersources"]=lt::entry::list_type{lt::entry("Tracker")};torrent["attributes"]["storetypes"]=lt::entry::list_type{lt::entry("L")};
      torrent["attributes"]["opaque.fixture"]=std::string("\0\xffQ",3);write(state,encode(torrent));
      lt::entry row(lt::entry::dictionary_type{});row["torrent_hash"]=QByteArray::fromHex(t.text().toLatin1()).toStdString();row["torrent"]=state.toStdString();row["save_dir"]=payloadParent.toStdString();row["save_file"]=t.name.toStdString();
      row["persistent"]=1;row["state"]=i==2?70:75;row["position"]=i+1;row["completedbytes"]=seed?PayloadBytes:0;row["downloaded"]=seed?0:receivedPrefix(static_cast<std::size_t>(i));row["uploaded"]=seed?0:sentPrefix(static_cast<std::size_t>(i));
      row["maxdl"]=(seed?24:(i==0?4:8))*1024;row["maxul"]=(seed?24:10)*1024;row["file_priorities"]=lt::entry::list_type{lt::entry(1)};inventory["downloads"].list().push_back(row);
    }
    lt::entry paused(lt::entry::dictionary_type{});paused["hash"]=QByteArray::fromHex(torrents[2].text().toLatin1()).toStdString();paused["force"]=0;inventory["pause_data"]=lt::entry::list_type{paused};
    write(profile+"/downloads.config",encode(inventory));writeSettings(seed,2);write(profile+"/tables.config",QByteArray("de"));
  }
  void writeSettings(bool seed,int downloads)const{
    lt::entry s(lt::entry::dictionary_type{});s["PluginInfo.azneti2phelper.enabled"]=1;s["Plugin.azneti2phelper.enable"]=1;s["Plugin.azneti2phelper.azi2phelper.pure.enabled"]=1;
    s["Bind IP"]="127.0.0.1";s["TCP.Listen.Port.Enable"]=0;s["peercontrol.udp.public.enable"]=0;
    s["Peer Source Selection Default.Tracker"]=1;s["Peer Source Selection Default.DHT"]=0;s["Peer Source Selection Default.PeerExchange"]=0;s["Plugin.DHT.dht.enabled"]=0;
    s["max downloads"]=downloads;s["max active torrents"]=4;s["min downloads"]=0;s["StartStopManager_iRankType"]=0;
    s["StartStopManager_iFirstPriority_Type"]=1;s["StartStopManager_iFirstPriority_ShareRatio"]=0;s["StartStopManager_bFirstPriority_ignore0Peer"]=0;s["StartStopManager_bIgnore0Peers"]=0;
    s["StartStopManager_iMinSeedingTime"]=0;s["StartStopManager_iMinSeedingTimeWhenActive"]=0;s["Enable Light Seeding"]=0;s["Light Seed Slots Reserved"]=0;s["Flexible Seed Slots"]=0;
    s["Max Download Speed KBs"]=seed?48:GlobalDownload/1024;s["Max Upload Speed KBs"]=seed?48:GlobalUpload/1024;s["Max Upload Speed Seeding KBs"]=seed?SeedGlobalUpload/1024:GlobalUpload/1024;s["enable.seedingonly.upload.rate"]=1;
    s["opaque.coexist.fixture"]=std::string("\0\xff" "C",3);write(profile+"/biglybt.config",encode(s));
  }
  biglybtc::core::LibrarySnapshot saved()const{
    auto d=biglybtc::core::Document::parse(read(profile+"/downloads.config",1024*1024).toStdString());require(d.has_value(),"Saved original inventory malformed");
    auto snapshot=biglybtc::core::readLibrary(*d);require(snapshot&&snapshot->issues.empty()&&snapshot->downloads.size()==3,"Saved original inventory lost exact three rows");return std::move(*snapshot);
  }
  void networksIntact()const{
    for(std::size_t i=0;i<torrents.size();++i){auto d=biglybtc::core::Document::parse(read(profile+"/active/"+torrents[i].text()+".dat",1024*1024).toStdString());require(d.has_value(),"Saved state malformed");
      const auto* a=d->find("attributes");const auto* ns=a?a->find("networks"):nullptr;const auto* opaque=a?a->find("opaque.fixture"):nullptr;
      require(ns&&ns->type==biglybtc::core::Node::Type::List&&ns->children.size()==1&&ns->children[0].type==biglybtc::core::Node::Type::Bytes&&ns->children[0].bytes==(i==0?"Public":"I2P"),"Original per-hash networks changed or broadened");
      require(opaque&&opaque->type==biglybtc::core::Node::Type::Bytes&&opaque->bytes==std::string("\0\xffQ",3),"Unknown original attributes changed");
    }
  }
};

// Only actual HTTP announcements register a Public seed endpoint. No engine
// peer API is called, and the I2P hash is never admitted to this tracker.
class PublicTracker final {
  QTcpServer server_;QByteArray hash_;QByteArray seedPeer_;
  quint16 registeredPort_=0;quint32 registeredAddress_=0;bool seedPresent_=false,leechPresent_=false,leechComplete_=false;
  unsigned accepted_=0,seedAnnounces_=0,leechAnnounces_=0,replies_=0,advertisements_=0,eof_=0,completed_=0;QString failure_;
  std::function<quint16()> seedPort_;
  static QByteArray unescape(const QByteArray& value){
    QByteArray out;for(qsizetype i=0;i<value.size();++i){const auto c=static_cast<unsigned char>(value[i]);
      if(c=='%'){require(i+2<value.size(),"Short Public tracker escape");const auto part=value.mid(i+1,2);for(const auto digit:part){require((digit>='0'&&digit<='9')||(digit>='a'&&digit<='f')||(digit>='A'&&digit<='F'),"Malformed Public tracker escape");}out+=QByteArray::fromHex(part);i+=2;}
      else{require(c>=33&&c<127,"Non-ASCII Public tracker query");out+=static_cast<char>(c);}
    }return out;
  }
public:
  explicit PublicTracker(const biglybtc::core::TorrentHash& hash):hash_(QByteArray::fromHex(QString::fromStdString(hash.hex()).toLatin1())){
    require(server_.listen(QHostAddress::LocalHost,0),"Cannot bind owned Public HTTP tracker");
    QObject::connect(&server_,&QTcpServer::newConnection,&server_,[this]{while(server_.hasPendingConnections()){
      auto* socket=server_.nextPendingConnection();if(++accepted_>256||server_.findChildren<QTcpSocket*>().size()>8){failure_="Public tracker connection bound";socket->abort();socket->deleteLater();continue;}
      auto pending=std::make_shared<QByteArray>();auto complete=std::make_shared<bool>(false);auto* timer=new QTimer(socket);timer->setSingleShot(true);timer->start(5000);
      QObject::connect(timer,&QTimer::timeout,socket,[socket]{socket->abort();});
      QObject::connect(socket,&QTcpSocket::disconnected,socket,[this,socket,complete]{if(!*complete){++eof_;}socket->deleteLater();});
      QObject::connect(socket,&QTcpSocket::readyRead,socket,[this,socket,pending,complete,timer]{if(*complete){return;}try{
        require(socket->peerAddress()==QHostAddress::LocalHost&&socket->localAddress()==QHostAddress::LocalHost,"Public tracker nonloopback socket");
        *pending+=socket->readAll();require(pending->size()<=16384,"Public tracker header bound");if(!pending->contains("\r\n\r\n")){return;}
        for(const auto c:*pending){const auto x=static_cast<unsigned char>(c);require(x==9||x==10||x==13||(x>=32&&x<127),"Non-ASCII Public tracker header");}
        const auto first=pending->first(pending->indexOf("\r\n")).split(' ');require(first.size()==3&&first[0]=="GET"&&(first[2]=="HTTP/1.0"||first[2]=="HTTP/1.1"),"Malformed actual Public HTTP request");
        const bool scrape=first[1].startsWith("/scrape?");require(scrape||first[1].startsWith("/announce?"),"Unsupported actual Public tracker path");
        QMap<QByteArray,QByteArray> fields;for(const auto& item:first[1].mid(scrape?8:10).split('&')){const auto at=item.indexOf('=');require(at>0&&fields.size()<32&&!fields.contains(item.first(at)),"Duplicate/bounded Public tracker fields");fields.insert(item.first(at),unescape(item.mid(at+1)));}
        require(fields.value("info_hash")==hash_,"Wrong network or hash reached Public tracker");
        bool isSeed=false;if(!scrape){
          require(fields.value("peer_id").size()==20,"Actual Public tracker peer id missing");bool okPort=false,okLeft=false;const auto port=fields.value("port").toUInt(&okPort);const auto left=fields.value("left").toULongLong(&okLeft);
          require(okPort&&port<=65535&&okLeft&&left<=static_cast<qulonglong>(PayloadBytes),"Actual Public tracker announce fields malformed");
          isSeed=seedPort_&&port!=0&&port==seedPort_();const auto event=fields.value("event");require(event.isEmpty()||event=="started"||event=="stopped"||event=="completed","Unexpected Public tracker event");
          if(isSeed){require(left==0&&(seedPeer_.isEmpty()||seedPeer_==fields.value("peer_id")),"Public seed announce identity changed");seedPeer_=fields.value("peer_id");registeredPort_=static_cast<quint16>(port);registeredAddress_=socket->peerAddress().toIPv4Address();seedPresent_=event!="stopped";++seedAnnounces_;}
          else{require(fields.value("peer_id")!=seedPeer_,"Public seed advertised a different endpoint");leechPresent_=event!="stopped";leechComplete_=left==0;++leechAnnounces_;if(event=="completed"){++completed_;}}
        }
        const auto completeCount=(seedPresent_?1:0)+(leechPresent_&&leechComplete_?1:0),incomplete=leechPresent_&&!leechComplete_?1:0;QByteArray body;
        if(scrape){body="d5:filesd20:"+hash_+"d8:completei"+QByteArray::number(completeCount)+"e10:downloadedi"+QByteArray::number(completed_)+"e10:incompletei"+QByteArray::number(incomplete)+"eeee";}
        else{QByteArray peer;if(!isSeed&&seedPresent_&&registeredPort_){for(int shift=24;shift>=0;shift-=8){peer+=static_cast<char>(registeredAddress_>>shift);}peer+=static_cast<char>(registeredPort_>>8);peer+=static_cast<char>(registeredPort_);++advertisements_;}
          body="d8:completei"+QByteArray::number(completeCount)+"e10:incompletei"+QByteArray::number(incomplete)+"e8:intervali30e12:min intervali1e5:peers"+QByteArray::number(peer.size())+':'+peer+'e';}
        *complete=true;timer->stop();++replies_;socket->write("HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);socket->disconnectFromHost();
      }catch(const std::exception& e){failure_=QString::fromUtf8(e.what()).left(256);socket->abort();}});
    }});
  }
  QString url()const{return QStringLiteral("http://127.0.0.1:%1/announce").arg(server_.serverPort());}
  void seedPort(std::function<quint16()> value){seedPort_=std::move(value);}
  const QString& failure()const{return failure_;}
  bool discovered()const{return seedAnnounces_>0&&leechAnnounces_>0&&advertisements_>0&&replies_>0;}
  QJsonObject observation()const{return {{"accepted",static_cast<qint64>(accepted_)},{"seed_announces",static_cast<qint64>(seedAnnounces_)},{"leech_announces",static_cast<qint64>(leechAnnounces_)},{"replies",static_cast<qint64>(replies_)},{"actual_peer_advertisements",static_cast<qint64>(advertisements_)},{"incomplete_eof",static_cast<qint64>(eof_)},{"registered_seed_port",static_cast<int>(registeredPort_)},{"failure",failure_}};}
};
class PublicSeed final {
  std::unique_ptr<lt::session> session_;lt::torrent_handle handle_;
public:
  explicit PublicSeed(const Fixture& fixture){
    lt::settings_pack s;s.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");s.set_str(lt::settings_pack::outgoing_interfaces,"127.0.0.1");s.set_str(lt::settings_pack::dht_bootstrap_nodes,"");
    for(auto k:{lt::settings_pack::enable_dht,lt::settings_pack::enable_lsd,lt::settings_pack::enable_upnp,lt::settings_pack::enable_natpmp,lt::settings_pack::enable_incoming_utp,lt::settings_pack::enable_outgoing_utp}){s.set_bool(k,false);}
    s.set_int(lt::settings_pack::upload_rate_limit,16*1024);session_=std::make_unique<lt::session>(s);
    auto filter=session_->get_peer_class_filter();filter.add_rule(lt::make_address("127.0.0.1"),lt::make_address("127.0.0.1"),1u);session_->set_peer_class_filter(filter);
    const auto raw=read(fixture.profile+"/active/"+fixture.torrents[0].text()+".dat",1024*1024);lt::error_code ec;
    auto metadata=std::make_shared<lt::torrent_info>(lt::span<const char>(raw.constData(),static_cast<std::size_t>(raw.size())),ec,lt::from_span);require(!ec,"Actual Public seed metadata refused");
    lt::add_torrent_params p;p.ti=std::move(metadata);p.save_path=fixture.payloadParent.toStdString();p.flags&=~(lt::torrent_flags::paused|lt::torrent_flags::auto_managed);p.flags|=lt::torrent_flags::disable_dht|lt::torrent_flags::disable_lsd|lt::torrent_flags::disable_pex;
    handle_=session_->add_torrent(p,ec);require(!ec&&handle_.is_valid(),"Cannot add real owned Public seed");
  }
  quint16 port()const{return session_?session_->listen_port():0;}
  bool ready()const{return session_&&port()&&handle_.status().is_seeding;}
  void close(){session_.reset();}
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
    auto s=app::OwnedI2pService::start(lease,torrents[1].hash,options);if(!s){throw std::runtime_error(s.error().message);}service_=std::move(*s);
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
  if(!p){return {};}QJsonArray rows;for(const auto& row:p->torrents){rows.push_back(QJsonObject{{"hash",row.hash},{"state",row.state},{"verified_bytes",row.verifiedBytes},{"payload_downloaded",row.payloadDownloaded},{"payload_uploaded",row.payloadUploaded},{"peers",row.peers},{"download_rate",row.downloadRate},{"upload_rate",row.uploadRate},{"tracker_counters_available",false},{"queued",row.queued},{"admission",QString::number(row.admission)},{"generation",QString::number(row.generation)},{"download_limit",row.downloadLimit},{"upload_limit",row.uploadLimit},{"queue_unavailable",static_cast<qint64>(row.queueUnavailable)}});}
  return {{"session",QString::number(p->session)},{"sequence",QString::number(p->sequence)},{"failure",p->failure},{"global_download_limit",p->globalDownloadLimit},{"global_upload_limit",p->globalUploadLimit},{"loopback_peer_classes",static_cast<qint64>(p->loopbackPeerClasses)},{"tracker_announces",QString::number(p->trackerAnnounces)},{"tracker_replies",QString::number(p->trackerReplies)},{"tracker_errors",QString::number(p->trackerErrors)},{"torrents",rows}};
}
QJsonArray seedJson(const std::vector<app::OwnedI2pService::TorrentObservation>& rows){
  QJsonArray result;
  for(const auto& row:rows){const auto& value=row.value;
    result.push_back(QJsonObject{{"hash",QString::fromStdString(row.hash.hex())},
      {"phase",static_cast<int>(value.phase)},
      {"failure",QString::fromUtf8(value.failure.data(),static_cast<qsizetype>(std::min<std::size_t>(value.failure.size(),512)))},
      {"admission",QString::number(value.admission)},{"generation",QString::number(value.generation)},
      {"public_identity",hex(value.publicIdentity)},
      {"verified_bytes",static_cast<qint64>(value.verifiedBytes)},
      {"payload_downloaded",static_cast<qint64>(value.payloadDownloaded)},{"payload_uploaded",static_cast<qint64>(value.payloadUploaded)},
      {"peers",value.peers},{"download_rate",value.downloadRate},{"upload_rate",value.uploadRate},
      {"tracker_announces",QString::number(value.trackerAnnounces)},{"tracker_replies",QString::number(value.trackerReplies)},{"tracker_errors",QString::number(value.trackerErrors)},
      {"queued",value.queued},{"checked",value.checked},{"seeding",value.seeding},
      {"global_download_limit",value.globalDownloadLimit},{"global_upload_limit",value.globalUploadLimit},
      {"loopback_peer_classes",static_cast<qint64>(value.loopbackPeerClasses)}});
  }return result;
}
enum class Stage{Initial,Concurrent,PausePublic,PublicPaused,ResumePublic,PublicResumed,StopPublic,PublicStopped,QueuePublic,PublicQueued,PauseI2p,I2pPaused,ResumeI2p,I2pResumed,StopI2p,I2pStopped,QueueI2p,I2pQueued,PauseForRestart,PausedForRestart,FirstClose,FirstClosed,WaitRetired,Reopen,ResumeAfterRestart,RestartStarted,WaitCache,SlotLimited,StopRunning,Handoff,RestartStopped,FinishTransfers,StopFinalPublic,PublicFinalStopped,StopFinalI2p,I2pFinalStopped,FinalClose,FinalClosed};
}
int main(int argc,char** argv){
  QApplication application(argc,argv);application.setQuitOnLastWindowClosed(false);
  const auto args=application.arguments();if(args.size()!=3){return 2;}bool valid=false;const auto port=args[1].toUInt(&valid);
  if(!valid||port<1024||port>65535||args[2]!="/work"||QGuiApplication::platformName()!="offscreen"){return 2;}
  const QString work=args[2];QElapsedTimer elapsed;elapsed.start();QTimer poll;
  std::unique_ptr<app::SessionBridge> bridge;std::unique_ptr<SeedWorker> seed;std::unique_ptr<PublicSeed> publicSeed;
  ui::LibraryWindow window;window.resize(1180,820);window.enableSessionCloseHandshake();
  ui::Presentation snapshot;ui::I2pPresentation packet;quint64 session=0,initialSession=0;
  bool done=false,inTurn=false,joined=false,released=false,closeRequested=false,shutdownDone=false;int code=1;
  QJsonArray latestSeed;QString latestHostMessage,callbackFailure;Stage stage=Stage::Initial;
  QJsonObject result{{"status","FAIL"},{"scope","actual-Public-and-pure-I2P-Host-coexistence"}};
  std::optional<std::pair<std::size_t,Action>> expectedAction;std::array<std::array<unsigned,10>,3> actions{};
  std::array<quint64,2> initialAdmissions{},initialGenerations{};std::array<qint64,3> lastReceived{},lastSent{},savedReceived{};
  for(std::size_t i=0;i<3;++i){lastReceived[i]=Fixture::receivedPrefix(i);lastSent[i]=Fixture::sentPrefix(i);}
  quint64 stableAdmission=0,stableGeneration=0,slotQueuedAdmission=0,slotRunningAdmission=0;int running=0,waiting=1;
  qint64 siblingBaseline=0,resumeBaseline=0;QElapsedTimer slotAge;bool slotObserved=false;
  const auto uiJson=[&](){QJsonArray rows,counters;if(snapshot){for(const auto& item:snapshot->items){rows.push_back(QJsonObject{{"hash",item.hash},{"state",item.state},{"completed",item.completed.value_or(-1)},{"received",item.received.value_or(-1)},{"sent",item.sent.value_or(-1)},{"admission",QString::number(item.providerAdmission)},{"generation",QString::number(item.providerGeneration)}});}}
    for(const auto& row:actions){QJsonArray values;for(const auto count:row){values.push_back(static_cast<qint64>(count));}counters.push_back(values);}
    return QJsonObject{{"session",QString::number(session)},{"revision",QString::number(snapshot?snapshot->revision:0)},{"rendered_revision",QString::number(window.model().revision())},{"rows",rows},{"last_Host_message",latestHostMessage},{"accepted_actions",counters}};
  };
  const auto diagnostic=[&](const QString& name,const QJsonObject& value)noexcept{try{output(work+'/'+name,value);}catch(...){std::cerr<<"Cannot write bounded progress marker\n";}};
  const auto mark=[&](const char* phase)noexcept{try{diagnostic("native-phase.json",{{"phase",QString::fromLatin1(phase)},{"elapsed_ms",elapsed.elapsed()}});}catch(...){}};
  const auto shutdown=[&]{if(shutdownDone){return;}shutdownDone=true;mark("before-Host-destruction");bridge.reset();mark("after-Host-destruction");if(seed){mark("before-I2P-seed-close");seed->close();seed.reset();mark("after-I2P-seed-join");}if(publicSeed){mark("before-Public-seed-close");publicSeed->close();publicSeed.reset();mark("after-Public-seed-join");}};
  const auto finish=[&](bool ok,const QString& reason){if(done){return;}done=true;poll.stop();const auto ui=uiJson();diagnostic("native-terminal.json",{{"reason",reason.left(512)},{"intended_success",ok},{"elapsed_ms",elapsed.elapsed()},{"stage",static_cast<int>(stage)},{"seed",latestSeed},{"i2p",packetJson(packet)},{"ui",ui}});
    try{shutdown();}catch(const std::exception& error){ok=false;result.insert("shutdown_error",QString::fromUtf8(error.what()).left(512));}
    result.insert("status",ok?"PASS":"FAIL");result.insert("reason",reason.left(512));result.insert("elapsed_ms",elapsed.elapsed());result.insert("last_ui",ui);result.insert("last_i2p",packetJson(packet));result.insert("last_seed",latestSeed);
    try{output(work+"/native-result.json",result);}catch(...){ok=false;}code=ok?0:1;application.exit(code);
  };
  try{
    const QString tracker=QString::fromLatin1(read(work+"/tracker.url",256));require(tracker.startsWith("http://")&&tracker.endsWith(".b32.i2p/announce")&&!tracker.contains('\n'),"Owned I2P tracker URL required");
    PublicTracker publicTracker(Torrent::makeHash(QStringLiteral("sample-0.bin"),expectedPayload(0)));
    Fixture a(work,"seed",true,tracker,publicTracker.url()),b(work,"leech",false,tracker,publicTracker.url());require(a.identity!=b.identity,"Distinct original I2P identities required");
    for(std::size_t i=0;i<3;++i){require(a.torrents[i].hash==b.torrents[i].hash,"Seed/leech metainfo differ");}
    write(work+"/torrent.hashes",QByteArray::fromHex(b.torrents[1].text().toLatin1()));
    write(work+"/coexist.hashes",QByteArray::fromHex(b.torrents[0].text().toLatin1())+QByteArray::fromHex(b.torrents[1].text().toLatin1()));
    publicSeed=std::make_unique<PublicSeed>(a);publicTracker.seedPort([&]{return publicSeed?publicSeed->port():0;});
    auto seedLease=biglybtc::storage::ProfileLease::acquire(a.profile.toStdString());require(seedLease.has_value(),"Cannot own I2P seed profile");
    app::I2pServiceOptions options;options.routerPort=static_cast<quint16>(port);options.sessionOptions=zeroHop();options.peerPort=6881;
    seed=std::make_unique<SeedWorker>();seed->start(*seedLease,a.torrents,options);options.publicCoexistence=true;
    bridge=std::make_unique<app::SessionBridge>(nullptr,app::SessionBridge::CountryProviderFactory{},app::SessionBridge::AzureusServiceFactory{},app::SessionBridge::SchedulerFactory{},options);
    QObject::connect(bridge.get(),&app::SessionBridge::snapshotReady,&window,[&](ui::Presentation p){snapshot=p;window.setPresentation(std::move(p));});
    QObject::connect(bridge.get(),&app::SessionBridge::progressReady,&window,&ui::LibraryWindow::setTelemetry);QObject::connect(bridge.get(),&app::SessionBridge::detailsReady,&window,&ui::LibraryWindow::setDetails);
    QObject::connect(bridge.get(),&app::SessionBridge::busyChanged,&window,&ui::LibraryWindow::setBusy);QObject::connect(bridge.get(),&app::SessionBridge::messageReady,&window,&ui::LibraryWindow::setMessage);
    QObject::connect(bridge.get(),&app::SessionBridge::messageReady,&window,[&](const QString& value){latestHostMessage=value.left(512);});
    QObject::connect(bridge.get(),&app::SessionBridge::i2pReady,&window,[&](ui::I2pPresentation p){if(p&&p->session==session&&(!packet||p->sequence>packet->sequence)){packet=p;}window.setI2p(std::move(p));});
    QObject::connect(bridge.get(),&app::SessionBridge::columnLayoutSessionReady,&window,[&](quint64 value){session=value;window.beginColumnLayoutSession(value);});
    QObject::connect(&window,&ui::LibraryWindow::refreshRequested,&window,[&]{if(bridge){static_cast<void>(bridge->refresh());}});QObject::connect(&window,&ui::LibraryWindow::detailsRequested,&window,[&](ui::Target value){if(bridge){static_cast<void>(bridge->details(value));}});
    QObject::connect(&window,&ui::LibraryWindow::downloadActionRequested,&window,[&](ui::DownloadTargets targets,Action action){
      if(targets.size()!=1||!window.model().accepts(targets.front())){callbackFailure="Stale actual UI target";return;}std::size_t index=3;for(std::size_t i=0;i<3;++i){if(targets.front().hash==b.torrents[i].text()){index=i;}}
      const auto ordinal=static_cast<std::size_t>(action);if(!expectedAction||index!=expectedAction->first||action!=expectedAction->second){callbackFailure="Actual QAction dispatched another target/action";return;}
      if(index==3||ordinal>=10||!bridge->downloadAction(std::move(targets),action)){callbackFailure="Host refused actual UI action";return;}++actions[index][ordinal];
    });
    QObject::connect(&window,&ui::LibraryWindow::sessionCloseRequested,&window,[&]{closeRequested=true;if(!bridge->closeSession()){callbackFailure="Host refused joined close";}});
    QObject::connect(bridge.get(),&app::SessionBridge::sessionClosed,&window,[&](quint64 closed,bool didJoin,bool didRelease){if(closed!=session){callbackFailure="Close acknowledged another session";return;}joined=didJoin;released=didRelease;window.completeSessionClose();});
    QObject::connect(bridge.get(),&app::SessionBridge::sessionCloseFailed,&window,[&]{callbackFailure="Host checkpoint/join failed";});
    const auto row=[&](std::size_t i)->const ui::LibraryItem*{if(!snapshot){return nullptr;}const auto it=std::find_if(snapshot->items.begin(),snapshot->items.end(),[&](const auto& v){return v.hash==b.torrents[i].text();});return it==snapshot->items.end()?nullptr:&*it;};
    const auto active=[&](std::size_t i)->const ui::LibraryItem*{const auto* v=row(i);return v&&v->providerAdmission&&v->providerGeneration?v:nullptr;};
    const auto hasTraffic=[&](std::size_t i){const auto* v=row(i);return v&&v->received&&v->sent;};
    const auto received=[&](std::size_t i){const auto* v=row(i);require(v&&v->received&&*v->received>=Fixture::receivedPrefix(i),"Cumulative library Received unavailable/regressed");return *v->received;};
    const auto incomplete=[&](std::size_t i){const auto* v=row(i);return v&&v->completed&&*v->completed<PayloadBytes;};
    const auto queued=[&](std::size_t i){const auto* v=row(i);return v&&v->state.startsWith("Queued");};
    const auto trigger=[&](std::size_t i,const char* id,Action action,Stage next){if(bridge->busy()||!selectTorrent(window,b.torrents[i].text())){return false;}auto* command=window.findChild<QAction*>(QString::fromLatin1(id));require(command,"Actual QAction missing");if(!command->isEnabled()){return false;}
      const auto ordinal=static_cast<std::size_t>(action);const auto before=actions[i][ordinal];expectedAction=std::pair{i,action};struct Reset{decltype(expectedAction)& v;~Reset(){v.reset();}}reset{expectedAction};command->trigger();require(callbackFailure.isEmpty(),callbackFailure.toUtf8().constData());const auto after=actions[i][ordinal];require(after==before||after==before+1,"Duplicate accepted action");if(after==before){return false;}stage=next;return true;
    };
    const auto rememberSibling=[&](std::size_t i){const auto* v=active(i);require(v,"No actual sibling admission");stableAdmission=v->providerAdmission;stableGeneration=v->providerGeneration;siblingBaseline=received(i);};
    const auto siblingProgress=[&](std::size_t i){const auto* v=active(i);require(v&&v->providerAdmission==stableAdmission&&v->providerGeneration==stableGeneration&&session==initialSession,"Independent sibling owner/session changed");return hasTraffic(i)&&received(i)>=siblingBaseline+16384;};
    const auto savedRow=[&](const biglybtc::core::LibrarySnapshot& saved,std::size_t i)->const biglybtc::core::SavedDownload&{const auto found=std::find_if(saved.downloads.begin(),saved.downloads.end(),[&](const auto& v){return v.hash&&*v.hash==b.torrents[i].hash;});require(found!=saved.downloads.end(),"Original saved hash missing");return *found;};
    QElapsedTimer progressAge;progressAge.start();window.show();require(bridge->openNormal(b.profile.toStdString()),"Host refused coexistence profile");
    QObject::connect(&poll,&QTimer::timeout,&application,[&]{if(done||inTurn){return;}inTurn=true;struct Turn{bool& v;~Turn(){v=false;}}turn{inTurn};try{
      require(elapsed.elapsed()<1000000,"Absolute coexistence1000s budget exceeded");require(callbackFailure.isEmpty(),callbackFailure.toUtf8().constData());require(publicTracker.failure().isEmpty(),publicTracker.failure().toUtf8().constData());
      mark("before-I2P-seed-observations");const auto seeds=seed->observations();mark("after-I2P-seed-observations");latestSeed=seedJson(seeds);require(seeds.size()==1&&seeds[0].hash==a.torrents[1].hash,"I2P seed admitted a wrong-network hash");require(seeds[0].value.publicIdentity==a.identity&&seeds[0].value.failure.empty()&&seeds[0].value.phase!=app::OwnedI2pService::Phase::Withdrawn,"I2P seed identity/source lost");
      if(packet){require(packet->session==session&&packet->failure.isEmpty(),"Foreign/failed I2P packet");for(const auto& v:packet->torrents){require(v.hash==b.torrents[1].text()&&v.failure.isEmpty(),"Public/sentinel hash gained I2P authority");}}
      if(snapshot&&session){for(std::size_t i=0;i<3;++i){const auto* v=row(i);if(!v){continue;}if(v->received){require(*v->received>=lastReceived[i],"Cumulative library Received regressed at retire/restart");lastReceived[i]=*v->received;}if(v->sent){require(*v->sent>=lastSent[i],"Cumulative library Sent regressed at retire/restart");lastSent[i]=*v->sent;}}}
      if(progressAge.elapsed()>=1000){progressAge.restart();diagnostic("native-progress.json",{{"stage",static_cast<int>(stage)},{"elapsed_ms",elapsed.elapsed()},{"Host_busy",bridge->busy()},{"i2p",packetJson(packet)},{"seed",latestSeed},{"Public_tracker",publicTracker.observation()},{"ui",uiJson()}});}
      const auto* x=active(0);const auto* y=active(1);const auto* z=row(2);require(!active(2),"Paused sentinel acquired owner authority");if(z){require(z->state.startsWith("Paused"),"Paused sentinel changed intent");}
      switch(stage){
      case Stage::Initial:if(!bridge->busy()&&session&&snapshot&&snapshot->items.size()==3&&x&&y&&publicSeed->ready()){
        initialSession=session;initialAdmissions={x->providerAdmission,y->providerAdmission};initialGenerations={x->providerGeneration,y->providerGeneration};output(work+"/native-started.json",{{"status","ACTUAL_PUBLIC_AND_I2P_HOST_STARTED"},{"seed_destination_sha256",hex(a.identity)},{"leech_destination_sha256",hex(b.identity)},{"active_hashes",2},{"saved_hashes",3}});stage=Stage::Concurrent;}break;
      case Stage::Concurrent:if(x&&y&&hasTraffic(0)&&hasTraffic(1)&&!queued(0)&&!queued(1)&&received(0)>=Fixture::receivedPrefix(0)+16384&&received(1)>=Fixture::receivedPrefix(1)+16384&&publicTracker.discovered()){
        require(incomplete(0)&&incomplete(1),"No simultaneous incomplete Public/I2P transfers");result.insert("simultaneous_networks",uiJson());rememberSibling(1);stage=Stage::PausePublic;}break;
      case Stage::PausePublic:static_cast<void>(trigger(0,"classic-pause",Action::Pause,Stage::PublicPaused));break;
      case Stage::PublicPaused:if(!bridge->busy()&&row(0)&&row(0)->state.startsWith("Paused")&&hasTraffic(0)&&siblingProgress(1)){result.insert("pause_Public_preserved_I2P",uiJson());resumeBaseline=received(0);stage=Stage::ResumePublic;}break;
      case Stage::ResumePublic:static_cast<void>(trigger(0,"classic-resume",Action::Resume,Stage::PublicResumed));break;
      case Stage::PublicResumed:if(!bridge->busy()&&x&&hasTraffic(0)&&!queued(0)&&received(0)>=resumeBaseline+16384){require(x->providerAdmission==initialAdmissions[0]&&x->providerGeneration==initialGenerations[0],"Public pause/resume replaced its retained owner");static_cast<void>(siblingProgress(1));rememberSibling(1);stage=Stage::StopPublic;}break;
      case Stage::StopPublic:static_cast<void>(trigger(0,"classic-stop",Action::Stop,Stage::PublicStopped));break;
      case Stage::PublicStopped:if(!bridge->busy()&&!x&&row(0)&&row(0)->state=="Stopped"&&siblingProgress(1)){result.insert("stop_Public_preserved_I2P",uiJson());stage=Stage::QueuePublic;}break;
      case Stage::QueuePublic:static_cast<void>(trigger(0,"classic-queue",Action::Queue,Stage::PublicQueued));break;
      case Stage::PublicQueued:if(!bridge->busy()&&x&&y&&hasTraffic(0)&&hasTraffic(1)&&!queued(0)){require(incomplete(0)&&incomplete(1),"Control workload completed too early");rememberSibling(0);stage=Stage::PauseI2p;}break;
      case Stage::PauseI2p:static_cast<void>(trigger(1,"classic-pause",Action::Pause,Stage::I2pPaused));break;
      case Stage::I2pPaused:if(!bridge->busy()&&row(1)&&row(1)->state.startsWith("Paused")&&hasTraffic(1)&&siblingProgress(0)){result.insert("pause_I2P_preserved_Public",uiJson());resumeBaseline=received(1);stage=Stage::ResumeI2p;}break;
      case Stage::ResumeI2p:static_cast<void>(trigger(1,"classic-resume",Action::Resume,Stage::I2pResumed));break;
      case Stage::I2pResumed:if(!bridge->busy()&&x&&y&&hasTraffic(1)&&!queued(1)&&received(1)>=resumeBaseline+16384){require(y->providerAdmission!=initialAdmissions[1]&&y->providerGeneration==initialGenerations[1],"I2P resume replaced session/reused admission");static_cast<void>(siblingProgress(0));rememberSibling(0);stage=Stage::StopI2p;}break;
      case Stage::StopI2p:static_cast<void>(trigger(1,"classic-stop",Action::Stop,Stage::I2pStopped));break;
      case Stage::I2pStopped:if(!bridge->busy()&&!y&&row(1)&&row(1)->state=="Stopped"&&siblingProgress(0)){result.insert("stop_I2P_preserved_Public",uiJson());stage=Stage::QueueI2p;}break;
      case Stage::QueueI2p:static_cast<void>(trigger(1,"classic-queue",Action::Queue,Stage::I2pQueued));break;
      case Stage::I2pQueued:if(!bridge->busy()&&x&&y&&!queued(1)){require(incomplete(0)&&incomplete(1),"No incomplete work for shared queue restart");stage=Stage::PauseForRestart;}break;
      case Stage::PauseForRestart:static_cast<void>(trigger(1,"classic-pause",Action::Pause,Stage::PausedForRestart));break;
      case Stage::PausedForRestart:if(!bridge->busy()&&x&&row(1)&&row(1)->state.startsWith("Paused")){output(work+"/shared-controls-ready.json",{{"status","ACTUAL_CROSS_NETWORK_CONTROLS_COMPLETED"}});stage=Stage::FirstClose;}break;
      case Stage::FirstClose:if(QFile::exists(work+"/shared-session-precondition.json")){stage=Stage::FirstClosed;joined=released=false;closeRequested=true;require(bridge->closeSession(),"Host refused first joined close");}break;
      case Stage::FirstClosed:if(joined&&released&&!bridge->busy()){
        const auto saved=b.saved();require(savedRow(saved,0).originalState==75&&savedRow(saved,1).originalState==70&&savedRow(saved,2).originalState==70,"Original75/70/70 checkpoint changed");
        require(saved.paused.size()==2&&std::ranges::any_of(saved.paused,[&](const auto& p){return p.hash==b.torrents[1].hash;})&&std::ranges::any_of(saved.paused,[&](const auto& p){return p.hash==b.torrents[2].hash;}),"Original paused rows not preserved");
        for(std::size_t i=0;i<3;++i){const auto& v=savedRow(saved,i);require(v.downloaded&&v.uploaded&&*v.downloaded>=lastReceived[i]&&*v.uploaded>=lastSent[i],"Joined checkpoint lost current library counters");savedReceived[i]=*v.downloaded;}
        b.networksIntact();auto free=biglybtc::storage::ProfileLease::acquire(b.profile.toStdString());require(free.has_value(),"Joined Host retained profile lock");b.writeSettings(false,1);output(work+"/leech-restart-ready.json",{{"status","ACTUAL_HOST_CLOSED_FOR_SAME_IDENTITY_RESTART"},{"original_state_A",75},{"original_state_B",70},{"original_state_C",70},{"B_paused",true},{"C_paused",true}});stage=Stage::WaitRetired;}break;
      case Stage::WaitRetired:if(QFile::exists(work+"/leech-retired.json")){snapshot.reset();packet.reset();session=0;stage=Stage::Reopen;window.show();require(bridge->openNormal(b.profile.toStdString()),"Host refused same-profile reopen");}break;
      case Stage::Reopen:if(!bridge->busy()&&session&&session!=initialSession&&x&&row(1)&&row(1)->state.startsWith("Paused")){require(incomplete(0)&&x->providerGeneration!=initialGenerations[0],"Restart lacks new Public owner/incomplete work");stage=Stage::ResumeAfterRestart;}break;
      case Stage::ResumeAfterRestart:static_cast<void>(trigger(1,"classic-resume",Action::Resume,Stage::RestartStarted));break;
      case Stage::RestartStarted:if(!bridge->busy()&&x&&y){require(y->providerGeneration!=initialGenerations[1],"I2P restart reused old generation");output(work+"/native-restarted.json",{{"status","ACTUAL_SAME_PROFILE_RESTARTED"},{"leech_destination_sha256",hex(b.identity)},{"session",QString::number(session)}});stage=Stage::WaitCache;}break;
      case Stage::WaitCache:if(QFile::exists(work+"/cache-restart-precondition.json")){stage=Stage::SlotLimited;}break;
      case Stage::SlotLimited:if(!bridge->busy()&&x&&y&&hasTraffic(0)&&hasTraffic(1)&&queued(0)!=queued(1)){
        require(incomplete(0)&&incomplete(1),"One-slot proof needs both incomplete networks");running=queued(0)?1:0;waiting=1-running;const auto* r=active(static_cast<std::size_t>(running));const auto* w=active(static_cast<std::size_t>(waiting));
        if(received(static_cast<std::size_t>(running))<savedReceived[static_cast<std::size_t>(running)]+16384){break;}
        if(!slotObserved){slotObserved=true;slotAge.start();slotRunningAdmission=r->providerAdmission;slotQueuedAdmission=w->providerAdmission;siblingBaseline=received(static_cast<std::size_t>(waiting));break;}
        if(r->providerAdmission!=slotRunningAdmission||w->providerAdmission!=slotQueuedAdmission||received(static_cast<std::size_t>(waiting))!=siblingBaseline){slotObserved=false;break;}
        if(slotAge.elapsed()<1000){break;}stableAdmission=w->providerAdmission;stableGeneration=w->providerGeneration;result.insert("one_shared_download_slot",uiJson());stage=Stage::StopRunning;}break;
      case Stage::StopRunning:static_cast<void>(trigger(static_cast<std::size_t>(running),"classic-stop",Action::Stop,Stage::Handoff));break;
      case Stage::Handoff:{const auto i=static_cast<std::size_t>(waiting);const auto* v=active(i);if(!bridge->busy()&&!active(static_cast<std::size_t>(running))&&v&&hasTraffic(i)&&!queued(i)&&row(static_cast<std::size_t>(running))->state=="Stopped"){
        require(v->providerAdmission==stableAdmission&&v->providerGeneration==stableGeneration,"Cross-owner handoff readmitted sibling");if(received(i)>=siblingBaseline+16384){result.insert("cross_owner_slot_handoff",uiJson());stage=Stage::RestartStopped;}}break;}
      case Stage::RestartStopped:static_cast<void>(trigger(static_cast<std::size_t>(running),"classic-start",Action::Start,Stage::FinishTransfers));break;
      case Stage::FinishTransfers:if(!bridge->busy()&&x&&y&&x->state.startsWith("Seeding")&&y->state.startsWith("Seeding")&&x->completed==PayloadBytes&&y->completed==PayloadBytes){require(publicTracker.discovered(),"No actual Public tracker discovery");result.insert("completed",uiJson());result.insert("Public_tracker",publicTracker.observation());require(window.grab().save(work+"/coexist-preview.png"),"Cannot retain actual coexistence screenshot");stage=Stage::StopFinalPublic;}break;
      case Stage::StopFinalPublic:static_cast<void>(trigger(0,"classic-stop",Action::Stop,Stage::PublicFinalStopped));break;
      case Stage::PublicFinalStopped:if(!bridge->busy()&&!x&&row(0)&&row(0)->state=="Stopped"){stage=Stage::StopFinalI2p;}break;
      case Stage::StopFinalI2p:static_cast<void>(trigger(1,"classic-stop",Action::Stop,Stage::I2pFinalStopped));break;
      case Stage::I2pFinalStopped:if(!bridge->busy()&&!y&&row(1)&&row(1)->state=="Stopped"){stage=Stage::FinalClose;}break;
      case Stage::FinalClose:stage=Stage::FinalClosed;joined=released=false;closeRequested=false;window.close();break;
      case Stage::FinalClosed:if(closeRequested&&joined&&released&&!bridge->busy()){
        shutdown();const auto saved=b.saved();QJsonArray transfers;
        for(std::size_t i=0;i<2;++i){const auto& sr=savedRow(saved,i);require(sr.downloaded&&*sr.downloaded>=Fixture::receivedPrefix(i)+PayloadBytes&&*sr.downloaded>=savedReceived[i]&&sr.uploaded&&*sr.uploaded>=Fixture::sentPrefix(i),"Final original cumulative traffic missing");require(b.torrents[i].intact(true)&&a.torrents[i].intact(true),"Joined payload bytes or inode differ");
          for(const auto action:{Action::Pause,Action::Resume,Action::Stop,Action::Queue}){require(actions[i][static_cast<std::size_t>(action)]>0,"Missing real independent lifecycle action");}
          transfers.push_back(QJsonObject{{"network",i==0?"Public":"I2P"},{"info_hash",b.torrents[i].text()},{"verified_bytes",static_cast<qint64>(PayloadBytes)},{"saved_downloaded",static_cast<qint64>(*sr.downloaded)},{"initial_downloaded",Fixture::receivedPrefix(i)},{"payload_sha256",QString::fromLatin1(QCryptographicHash::hash(b.torrents[i].bytes,QCryptographicHash::Sha256).toHex())}});
        }
        b.networksIntact();require(b.torrents[2].intact(false)&&a.torrents[2].intact(true)&&savedRow(saved,2).originalState==70&&savedRow(saved,2).downloaded==Fixture::receivedPrefix(2),"Paused sentinel activated or changed");auto free=biglybtc::storage::ProfileLease::acquire(b.profile.toStdString());require(free.has_value(),"Final Host retained profile lock");
        result.insert("transfers",transfers);result.insert("verified_bytes",static_cast<qint64>(2*PayloadBytes));result.insert("seed_destination_sha256",hex(a.identity));result.insert("leech_destination_sha256",hex(b.identity));
        for(const auto key:{"simultaneous_Public_and_I2P","independent_controls","shared_download_slot_handoff","original_networks_preserved","cumulative_library_traffic_preserved","saved_75_70_pause_restored","both_payload_inodes_preserved","joined_before_disk_read","Host_profile_lock_released","paused_sentinel_untouched"}){result.insert(key,true);}result.insert("direct_peer_injection",false);finish(true,"Actual tracker-discovered Public and pure-I2P coexistence, shared queue and same-profile restart");}break;
      }
    }catch(const std::exception& error){finish(false,QString::fromUtf8(error.what()));}catch(...){finish(false,"Unexpected bounded coexistence resource failure");}});
    poll.start(25);application.exec();mark("application-loop-exited");shutdown();return code;
  }catch(const std::exception& error){finish(false,QString::fromUtf8(error.what()));return 1;}catch(...){finish(false,"Unexpected coexistence setup failure");return 1;}
}
