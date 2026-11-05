// Explicit test-only cold-LS2 + original-helper DHT + real BT/BEP9 Host probe.
// No connect_peer, tracker, x.pe, webseed, manual LeaseSet store or raw-channel grant.
#include "session_bridge.hpp"
#include "owned_i2p_service.hpp"
#include "../services/i2p_client/identity.hpp"
#include "../services/i2cp_datagram/messages.hpp"
#include "profile.hpp"
#include "biglybtc/library.hpp"
#include <libtorrent/bencode.hpp>
#include <libtorrent/bdecode.hpp>
#include <libtorrent/entry.hpp>
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>
#include <openssl/crypto.h>
#include <sys/stat.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
namespace app=biglybtc::app;namespace ui=biglybtc::ui;namespace storage=biglybtc::storage;
namespace i2p=biglybtc::services::i2p;namespace lt=libtorrent;
namespace {
constexpr qint64 PayloadBytes=262144;
const std::array<const char*,4> Cases{"pure-known","mixed-known","pure-magnet","mixed-magnet"};
void require(bool ok,const char* why){if(!ok){throw std::runtime_error(why);}}
QByteArray read(const QString& path,qint64 bound){QFile f(path);require(f.open(QIODevice::ReadOnly)&&f.size()>=0&&f.size()<=bound,"Bounded fixture input failed");auto value=f.readAll();require(f.error()==QFileDevice::NoError,"Fixture read failed");return value;}
void write(const QString& path,const QByteArray& value){require(QDir().mkpath(QFileInfo(path).absolutePath()),"Owned output parent failed");QSaveFile f(path);require(f.open(QIODevice::WriteOnly)&&f.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner),"Owned output failed");require(f.write(value)==value.size()&&f.commit(),"Owned output commit failed");}
void json(const QString& path,const QJsonObject& value){write(path,QJsonDocument(value).toJson());}
QJsonObject object(const QString& path){QJsonParseError error;auto d=QJsonDocument::fromJson(read(path,65536),&error);require(error.error==QJsonParseError::NoError&&d.isObject(),"Fixture JSON failed");return d.object();}
QString digest(const QByteArray& bytes){return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
QByteArray encode(const lt::entry& value){std::string b;lt::bencode(std::back_inserter(b),value);return QByteArray::fromStdString(b);}
lt::entry decode(const QByteArray& bytes){lt::error_code error;lt::bdecode_node value;lt::bdecode(bytes.constData(),bytes.constData()+bytes.size(),value,error);require(!error,"Invalid original bencode fixture");return lt::entry(value);}
QByteArray payload(int index){QByteArray value(PayloadBytes,'\0');for(qsizetype i=0;i<value.size();++i){value[i]=static_cast<char>((i*31+(i>>8)*7+index*53)&255);}return value;}
struct Secret {QByteArray bytes;~Secret(){OPENSSL_cleanse(bytes.data(),static_cast<std::size_t>(bytes.size()));}};
void directory(const QString& p){require(QDir().mkpath(p)&&::chmod(p.toUtf8().constData(),0700)==0,"Private fixture directory failed");}
app::I2pServiceOptions options(quint16 port,bool mixed,const QByteArray& bootstrap,const QByteArray& destination){
  require(bootstrap.size()==54,"Original compact bootstrap bound");app::I2pDiscoverySeed seed;
  for(std::size_t i=0;i<seed.nodeId.size();++i){seed.nodeId[i]=static_cast<std::uint8_t>(bootstrap.at(static_cast<qsizetype>(i)));}
  for(std::size_t i=0;i<seed.destinationHash.size();++i){seed.destinationHash[i]=static_cast<std::uint8_t>(bootstrap.at(static_cast<qsizetype>(i+20)));}
  seed.queryPort=static_cast<quint16>((static_cast<quint16>(static_cast<unsigned char>(bootstrap.at(52)))<<8)|static_cast<unsigned char>(bootstrap.at(53)));
  seed.destination.assign(reinterpret_cast<const std::uint8_t*>(destination.constData()),reinterpret_cast<const std::uint8_t*>(destination.constData()+destination.size()));
  app::I2pDiscoveryOptions discovery;discovery.queryPort=17401;discovery.seeds.push_back(std::move(seed));
  app::I2pServiceOptions result;result.routerPort=port;result.peerPort=6881;result.publicCoexistence=mixed;result.discovery=std::move(discovery);
  result.sessionOptions={{"inbound.length","0"},{"outbound.length","1"},{"inbound.lengthVariance","0"},{"outbound.lengthVariance","0"},{"inbound.quantity","1"},{"outbound.quantity","1"},{"inbound.backupQuantity","0"},{"outbound.backupQuantity","0"},{"inbound.allowZeroHop","true"},{"outbound.allowZeroHop","false"},{"i2cp.leaseSetType","3"},{"i2cp.leaseSetEncType","4"},{"i2cp.dontPublishLeaseSet","false"},{"i2cp.fastReceive","true"},{"i2cp.messageReliability","none"}};return result;
}
struct Fixture {
  QString name,profile,parent,file,hash;bool mixed=false,magnet=false;QByteArray expected;std::optional<struct stat> initial;
  Fixture(const QString& work,int index):name(QString::fromLatin1(Cases[static_cast<std::size_t>(index)])),profile(work+'/'+name+"-profile"),parent(work+'/'+name+"-payload"),file(parent+'/'+name+".bin"),mixed(name.startsWith("mixed")),magnet(name.endsWith("magnet")),expected(payload(index)){
    const auto spec=object(work+'/'+name+"-spec.json");const auto rawHash=QByteArray::fromHex(spec["info_hash"].toString().toLatin1());auto parsedHash=biglybtc::core::TorrentHash::parse(std::string_view(rawHash.constData(),static_cast<std::size_t>(rawHash.size())));require(parsedHash.has_value(),"Original selected hash invalid");hash=QString::fromStdString(parsedHash->hex());require(rawHash.size()==20&&spec["payload_bytes"].toInteger()==PayloadBytes&&spec["payload_sha256"].toString()==digest(expected),"Original peer fixture spec mismatch");
    for(const auto& p:{profile,profile+"/active",profile+"/plugins/azneti2phelper",parent}){directory(p);}
    Secret keys{read(work+'/'+name+"-native.keys",1024)};auto identity=i2p::decodeClientIdentity({reinterpret_cast<const i2p::Byte*>(keys.bytes.constData()),static_cast<std::size_t>(keys.bytes.size())});const auto dest=read(work+'/'+name+"-native.dest",1024);
    require(identity&&identity->destination()->wire==i2p::Bytes(reinterpret_cast<const i2p::Byte*>(dest.constData()),reinterpret_cast<const i2p::Byte*>(dest.constData()+dest.size())),"Actual original Native identity mismatch");
    write(profile+"/plugins/azneti2phelper/"+(mixed?"dest_key.dat":"dest_key1.dat"),keys.bytes);
    lt::entry config(lt::entry::dictionary_type{});config["PluginInfo.azneti2phelper.enabled"]=1;config["Plugin.azneti2phelper.enable"]=1;config["Plugin.azneti2phelper.azi2phelper.pure.enabled"]=1;config["Plugin.azneti2phelper.azi2phelper.dht.enable"]=1;
    config["Bind IP"]="127.0.0.1";config["TCP.Listen.Port.Enable"]=0;config["peercontrol.udp.public.enable"]=0;config["Plugin.DHT.dht.enabled"]=0;config["Peer Source Selection Default.Tracker"]=0;config["Peer Source Selection Default.DHT"]=1;config["Peer Source Selection Default.PeerExchange"]=0;config["max downloads"]=1;config["max active torrents"]=1;config["min downloads"]=0;config["StartStopManager_iRankType"]=0;config["opaque.discovery.fixture"]=std::string("x\0y",3);write(profile+"/biglybt.config",encode(config));write(profile+"/tables.config",QByteArray("de"));
    lt::entry inventory(lt::entry::dictionary_type{});inventory["downloads"]=lt::entry::list_type{};
    if(!magnet){
      auto torrent=decode(read(work+'/'+name+".torrent",131072));require(torrent.dict().size()==1&&torrent.find_key("info"),"Known torrent has an external source");require(QCryptographicHash::hash(encode(torrent["info"]),QCryptographicHash::Sha1)==QByteArray::fromHex(hash.toLatin1()),"Known torrent full hash mismatch");
      auto& attributes=torrent["attributes"];attributes["networks"]=mixed?lt::entry::list_type{lt::entry("Public"),lt::entry("I2P")}:lt::entry::list_type{lt::entry("I2P")};attributes["peersources"]=lt::entry::list_type{lt::entry("DHT")};
      const auto state=profile+"/active/"+hash+".dat";write(state,encode(torrent));write(file,QByteArray(PayloadBytes,'\0'));struct stat st{};require(::stat(file.toUtf8().constData(),&st)==0,"Known payload inode unavailable");initial=st;
      lt::entry row(lt::entry::dictionary_type{});row["torrent_hash"]=QByteArray::fromHex(hash.toLatin1()).toStdString();row["torrent"]=state.toStdString();row["save_dir"]=parent.toStdString();row["save_file"]=(name+".bin").toStdString();row["persistent"]=1;row["state"]=75;row["position"]=1;row["downloaded"]=1000;row["uploaded"]=10;inventory["downloads"].list().push_back(std::move(row));
    }else{require(!QFileInfo::exists(file),"Magnet created payload before metadata");require(!QFileInfo::exists(work+'/'+name+".torrent"),"Magnet fixture exposed complete metainfo");}
    write(profile+"/downloads.config",encode(inventory));
  }
  QString uri()const{return QStringLiteral("magnet:?xt=urn:btih:")+hash+(mixed?"&net=Public&net=I2P":"&net=I2P");}
  void validateAfterJoin()const{
    require(read(file,PayloadBytes)==expected,"Joined real payload bytes differ");if(initial){struct stat current{};require(::stat(file.toUtf8().constData(),&current)==0&&current.st_ino==initial->st_ino&&current.st_dev==initial->st_dev,"Known payload inode changed");}
    auto lease=storage::ProfileLease::acquire(profile.toStdString());require(lease.has_value(),"Closed Host retained profile lock");auto saved=storage::ConfigFile::load(*lease,"downloads.config");require(saved.has_value(),"Joined original inventory unreadable");
    auto state=decode(read(profile+"/active/"+hash+".dat",262144));const auto& networks=state["attributes"]["networks"].list();require(networks.size()==(mixed?2U:1U)&&networks.back().string()=="I2P"&&(!mixed||networks.front().string()=="Public"),"Original imported network tokens changed");const auto& sources=state["attributes"]["peersources"].list();require(sources.size()==1&&sources.front().string()==(magnet?"Tracker":"DHT"),"Original source permission projection changed");
    require(QCryptographicHash::hash(encode(state["info"]),QCryptographicHash::Sha1)==QByteArray::fromHex(hash.toLatin1()),"Actual metadata raw identity changed");require(!state["info"].find_key("private")&&state["info"]["x-owned-padding"].string()==std::string(33000,'d'),"Actual unknown metadata/private eligibility changed");
    const auto* announce=state.find_key("announce");
    if(magnet){
      const auto marker=(QStringLiteral("dht://")+hash.toUpper()+".dht/announce").toStdString();require(announce&&announce->string()==marker,"Original decentralized marker changed");
      if(const auto* tiers=state.find_key("announce-list")){for(const auto& tier:tiers->list()){for(const auto& value:tier.list()){require(value.string()==marker,"Unexpected actual HTTP tracker URI");}}}
    }else{require(!announce,"Known DHT-only source gained a tracker");}
    for(const auto* key:{"url-list","httpseeds"}){if(const auto* urls=state.find_key(key)){require(urls->list().empty(),"Actual trackerless completion gained webseeds");}}
  }
};
// Byte-for-byte I2CP proxy. Only the first matching real hash Host38 is held;
// no message is forged, decoded as authority, reordered, or acknowledged here.
class LookupHold final:public QObject {
  QTcpServer server_;std::unique_ptr<QTcpSocket> upstream_;QTcpSocket* client_=nullptr;QByteArray input_,serverInput_,held_;QString work_,name_,error_;QByteArray target_;QElapsedTimer heldAge_;bool greeting_=false,released_=false;unsigned accepts_=0;quint32 timeout_=0,request_=0;
  // Fixed observational counters only: never packet authority or a reply.
  struct MuxCount {quint32 frames=0;quint16 from=0,to=0;};
  std::array<std::array<quint32,256>,2> types_{};
  std::array<std::array<MuxCount,2>,2> mux_{};
  quint32 diagnosticErrors_=0;
  static void increment(quint32& value)noexcept{if(value<std::numeric_limits<quint32>::max()){++value;}}
  void observeFrame(const QByteArray& bytes,bool outbound)noexcept{
    try{
      if(bytes.size()<5){return;}const auto direction=outbound?0U:1U;
      const auto type=static_cast<unsigned char>(bytes[4]);increment(types_[direction][type]);
      namespace cp=i2p::i2cp;
      if((outbound&&type!=cp::SendMessageExpiresType)||(!outbound&&type!=cp::MessagePayloadType)){return;}
      const auto* begin=reinterpret_cast<const i2p::Byte*>(bytes.constData()+5);
      cp::Frame frame{type,i2p::Bytes(begin,begin+static_cast<std::size_t>(bytes.size()-5))};
      i2p::Bytes payload;
      if(outbound){auto send=cp::decodeDhtSendDatagram(frame);if(!send){return;}payload=std::move(send->payload);}
      else{auto received=cp::decodeMessagePayload(frame);if(!received){increment(diagnosticErrors_);return;}payload=std::move(received->payload);}
      if(payload.size()<10||(payload[9]!=17&&payload[9]!=18)){return;}
      auto mux=cp::decodeDhtMuxDatagram(payload);if(!mux){increment(diagnosticErrors_);return;}
      auto& count=mux_[direction][mux->protocol==17?0U:1U];increment(count.frames);count.from=mux->fromPort;count.to=mux->toPort;
    }catch(...){increment(diagnosticErrors_);}
  }
  void failed(const char* text){if(error_.isEmpty()){error_=QString::fromLatin1(text);}if(client_){client_->abort();}if(upstream_){upstream_->abort();}}
  void pump(){
    if(!client_||!upstream_||upstream_->state()!=QAbstractSocket::ConnectedState||!error_.isEmpty()){return;}
    try{
      require(input_.size()+client_->bytesAvailable()<=131082,"Host38 proxy client buffer bound");input_+=client_->readAll();
      if(!greeting_&&!input_.isEmpty()){require(static_cast<unsigned char>(input_[0])==0x2a,"I2CP greeting mismatch");require(upstream_->write(input_.first(1))==1,"I2CP greeting enqueue failed");input_.remove(0,1);greeting_=true;}
      while(held_.isEmpty()&&input_.size()>=5&&upstream_->bytesToWrite()<65536){
        const auto size=qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(input_.constData()));require(size<=65536,"Host38 proxy frame bound");const auto total=static_cast<qsizetype>(size)+5;if(input_.size()<total){return;}const auto frame=input_.first(total);input_.remove(0,total);observeFrame(frame,true);
        if(!released_&&static_cast<unsigned char>(frame[4])==38&&frame.size()==48&&static_cast<unsigned char>(frame[15])==0&&frame.mid(16,32)==target_){
          held_=frame;request_=qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(frame.constData()+7));timeout_=qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(frame.constData()+11));require(timeout_>0&&timeout_<=30000,"Original Host38 timeout changed");heldAge_.start();json(work_+'/'+name_+"-host38-held.json",{{"status","ACTUAL_TARGET_HOST38_HELD"},{"frame_sha256",digest(held_)},{"timeout_ms",static_cast<qint64>(timeout_)},{"request",QString::number(qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(held_.constData()+7)))}});return;
        }
        require(upstream_->write(frame)==frame.size(),"Original I2CP frame enqueue failed");
      }
    }catch(const std::exception& e){failed(e.what());}
  }
  void back(){
    if(!upstream_||!client_||!error_.isEmpty()){return;}
    try{
      require(serverInput_.size()+upstream_->bytesAvailable()<=131082,"Host39 proxy router buffer bound");serverInput_+=upstream_->readAll();
      while(serverInput_.size()>=5&&client_->bytesToWrite()<65536){
        const auto length=qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(serverInput_.constData()));require(length<=65536,"Router I2CP frame bound");const auto total=static_cast<qsizetype>(length)+5;if(serverInput_.size()<total){return;}const auto frame=serverInput_.first(total);serverInput_.remove(0,total);observeFrame(frame,false);
        if(released_&&static_cast<unsigned char>(frame[4])==39&&frame.size()>=12&&qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(frame.constData()+7))==request_){
          const auto result=static_cast<unsigned char>(frame[11]);QJsonObject observation{{"status","ACTUAL_CORRELATED_HOST39_OBSERVED"},{"result",static_cast<int>(result)},{"frame_sha256",digest(frame)},{"request",QString::number(request_)}};
          if(result==0){const auto bytes=frame.mid(12);auto destination=i2p::decodeDestination({reinterpret_cast<const i2p::Byte*>(bytes.constData()),static_cast<std::size_t>(bytes.size())});require(destination&&QByteArray(reinterpret_cast<const char*>(destination->hash.data()),32)==target_,"Actual Host39 returned another Destination");observation.insert("requested_destination_exact",true);}
          json(work_+'/'+name_+"-host39.json",observation);
        }
        require(client_->write(frame)==frame.size(),"Original router I2CP frame enqueue failed");
      }
    }catch(const std::exception& e){failed(e.what());}
  }

public:
  LookupHold(QString work,QString name,quint16 routerPort,QByteArray target):work_(std::move(work)),name_(std::move(name)),target_(std::move(target)){
    require(target_.size()==32,"Target hash bound");server_.setMaxPendingConnections(1);require(server_.listen(QHostAddress::LocalHost,0),"Owned lookup proxy listen failed");
    QObject::connect(&server_,&QTcpServer::newConnection,this,[this,routerPort]{
      try{require(++accepts_==1&&!client_,"Unexpected Native I2CP reconnect");client_=server_.nextPendingConnection();require(client_!=nullptr,"Missing owned Native I2CP socket");client_->setParent(this);client_->setReadBufferSize(65541);upstream_=std::make_unique<QTcpSocket>();upstream_->setProxy(QNetworkProxy::NoProxy);upstream_->setReadBufferSize(65541);
        QObject::connect(client_,&QTcpSocket::readyRead,this,[this]{pump();});QObject::connect(upstream_.get(),&QTcpSocket::connected,this,[this]{pump();});QObject::connect(upstream_.get(),&QTcpSocket::bytesWritten,this,[this]{pump();});QObject::connect(upstream_.get(),&QTcpSocket::readyRead,this,[this]{back();});QObject::connect(client_,&QTcpSocket::bytesWritten,this,[this]{back();});QObject::connect(upstream_.get(),&QTcpSocket::disconnected,this,[this]{if(client_){client_->disconnectFromHost();}});QObject::connect(client_,&QTcpSocket::disconnected,this,[this]{if(upstream_){upstream_->disconnectFromHost();}});upstream_->connectToHost(QHostAddress::LocalHost,routerPort);
      }catch(const std::exception& e){failed(e.what());}
    });
  }
  QJsonObject diagnostic()const{
    QJsonObject result;
    for(unsigned direction=0;direction<2;++direction){
      QJsonObject types,mux;
      for(unsigned type=0;type<256;++type){if(types_[direction][type]){types.insert(QString::number(type),static_cast<qint64>(types_[direction][type]));}}
      for(unsigned protocol=17;protocol<=18;++protocol){const auto& count=mux_[direction][protocol-17];mux.insert(QString::number(protocol),QJsonObject{{"frames",static_cast<qint64>(count.frames)},{"last_from_port",count.from},{"last_to_port",count.to}});}
      result.insert(direction==0?"outbound_types":"inbound_types",types);result.insert(direction==0?"outbound_dht_mux":"inbound_dht_mux",mux);
    }
    result.insert("decode_errors",static_cast<qint64>(diagnosticErrors_));return result;
  }
  quint16 port()const{return server_.serverPort();}
  QString failure()const{return error_;}
  bool released()const{return released_;}
  bool held()const{return !held_.isEmpty();}
  void tick(){
    if(held_.isEmpty()){pump();return;}try{
      require(heldAge_.elapsed()<timeout_,"Held lookup exceeded its unchanged original deadline");const auto release=work_+'/'+name_+"-host38-release.json";if(!QFileInfo::exists(release)){return;}
      const auto command=object(release);const auto cold=work_+'/'+name_+"-cold.json";require(command["status"].toString()=="FORWARD_EXACT_HELD_HOST38"&&command["frame_sha256"].toString()==digest(held_)&&command["cold_sha256"].toString()==digest(read(cold,8192)),"Host38 causal release association failed");
      const auto proof=object(cold);require(proof["status"].toString()=="ACTUAL_CALLER_AND_ROUTER_TARGET_CACHE_EMPTY"&&!proof["manual_ls2_store"].toBool()&&!proof["target_local"].toBool(),"Cold proof did not precede target lookup");const auto before=digest(held_);require(upstream_->write(held_)==held_.size(),"Held original Host38 enqueue failed");const auto after=digest(held_);require(before==after,"Held frame changed");json(work_+'/'+name_+"-host38-forwarded.json",{{"status","ACTUAL_EXACT_HOST38_FORWARDED_AFTER_COLD"},{"before_sha256",before},{"after_sha256",after},{"cold_sha256",command["cold_sha256"]},{"held_ms",heldAge_.elapsed()},{"original_timeout_ms",static_cast<qint64>(timeout_)}});held_.clear();released_=true;pump();
    }catch(const std::exception& e){failed(e.what());}
  }
  ~LookupHold()override{server_.close();if(client_){client_->abort();}if(upstream_){upstream_->abort();}}
};
}
int main(int argc,char** argv){
  QApplication application(argc,argv);application.setQuitOnLastWindowClosed(false);if(application.arguments().size()!=3||application.arguments()[2]!="/work"||QGuiApplication::platformName()!="offscreen"){return 2;}
  bool okPort=false;const auto parsed=application.arguments()[1].toUInt(&okPort);if(!okPort||parsed<1024||parsed>65535){return 2;}
  const QString work="/work";QElapsedTimer elapsed,progressAge;elapsed.start();progressAge.start();QTimer poll;std::unique_ptr<Fixture> fixture;std::unique_ptr<LookupHold> proxy;std::unique_ptr<app::SessionBridge> bridge;ui::Presentation latest;ui::I2pPresentation packet;QJsonArray cases;QString callbackFailure;bool inTurn=false,done=false,closeJoined=false,storageReleased=false,magnetSubmitted=false;int index=0,code=1;enum class Phase{Opening,Transferring,Closing};Phase phase=Phase::Opening;
  const auto finish=[&](bool success,const QString& reason){if(done){return;}done=true;poll.stop();json(work+"/native-terminal.json",{{"phase","before-owned-Host-join"},{"elapsed_ms",elapsed.elapsed()},{"reason",reason.left(512)}});bridge.reset();proxy.reset();json(work+"/native-result.json",{{"status",success?"PASS":"FAIL"},{"reason",reason.left(512)},{"elapsed_ms",elapsed.elapsed()},{"cases",cases},{"direct_peer_injection",false},{"HTTP_tracker_URIs",0},{"manual_ls2_store",false},{"joined_before_raw_disk_read",success}});code=success?0:1;application.exit(code);};
  const auto startCase=[&]{
    fixture=std::make_unique<Fixture>(work,index);const auto& f=*fixture;const auto seed=read(work+'/'+f.name+"-seed.dest",1024);auto target=i2p::decodeDestination({reinterpret_cast<const i2p::Byte*>(seed.constData()),static_cast<std::size_t>(seed.size())});require(target.has_value(),"Original seed Destination invalid");proxy=std::make_unique<LookupHold>(work,f.name,static_cast<quint16>(parsed),QByteArray(reinterpret_cast<const char*>(target->hash.data()),32));
    auto configuration=options(proxy->port(),f.mixed,read(work+"/bootstrap.node",54),read(work+"/bootstrap.dest",1024));latest.reset();packet.reset();callbackFailure.clear();closeJoined=storageReleased=magnetSubmitted=false;
    bridge=std::make_unique<app::SessionBridge>(nullptr,app::SessionBridge::CountryProviderFactory{},app::SessionBridge::AzureusServiceFactory{},app::SessionBridge::SchedulerFactory{},configuration);
    QObject::connect(bridge.get(),&app::SessionBridge::snapshotReady,&application,[&](ui::Presentation value){latest=std::move(value);});QObject::connect(bridge.get(),&app::SessionBridge::i2pReady,&application,[&](ui::I2pPresentation value){packet=std::move(value);});QObject::connect(bridge.get(),&app::SessionBridge::messageReady,&application,[&](QString message){if(message.startsWith("Background update failed:")||message.contains("failed",Qt::CaseInsensitive)){callbackFailure=message.left(512);}});QObject::connect(bridge.get(),&app::SessionBridge::sessionClosed,&application,[&](quint64,bool joined,bool released){closeJoined=joined;storageReleased=released;});QObject::connect(bridge.get(),&app::SessionBridge::sessionCloseFailed,&application,[&]{callbackFailure="Actual Host close failed";});require(bridge->openNormal(f.profile.toStdString()),"Actual Host profile open refused");phase=Phase::Opening;
    json(work+'/'+f.name+"-native-started.json",{{"status","ACTUAL_TRACKERLESS_HOST_OPENING"},{"case",f.name},{"info_hash",f.hash},{"magnet",f.magnet},{"mixed",f.mixed}});
  };
  try{startCase();QObject::connect(&poll,&QTimer::timeout,&application,[&]{if(done||inTurn){return;}inTurn=true;struct Reset{bool& flag;~Reset(){flag=false;}}reset{inTurn};try{
      require(elapsed.elapsed()<1000000,"Absolute Native discovery acceptance deadline");require(callbackFailure.isEmpty(),callbackFailure.toUtf8().constData());proxy->tick();require(proxy->failure().isEmpty(),proxy->failure().toUtf8().constData());const auto& f=*fixture;const ui::LibraryItem* row=nullptr;if(latest){for(const auto& value:latest->items){if(value.hash.compare(f.hash,Qt::CaseInsensitive)==0){require(row==nullptr,"Duplicate Host handle/library row");row=&value;}}}
      if(progressAge.elapsed()>=1000){progressAge.restart();json(work+"/native-progress.json",{{"case",f.name},{"phase",static_cast<int>(phase)},{"elapsed_ms",elapsed.elapsed()},{"busy",bridge->busy()},{"held_host38",proxy->held()},{"forwarded_host38",proxy->released()},{"i2cp_proxy",proxy->diagnostic()},{"row_state",row?row->state:QString{}},{"network_status",row?row->networkStatus:QString{}},{"completed",row&&row->completed?*row->completed:qint64{-1}},{"received",row&&row->received?*row->received:qint64{-1}}});}
      if(phase==Phase::Opening&&!bridge->busy()&&latest){if(f.magnet&&!magnetSubmitted){require(latest->items.empty()&&!QFileInfo::exists(f.file),"Magnet admitted payload before metadata");require(bridge->magnetImport(f.uri(),f.parent),"Real Host trackerless magnet intake refused");magnetSubmitted=true;}phase=Phase::Transferring;}
      if(phase==Phase::Transferring){
        if(proxy->held()&&f.magnet){
          require(!QFileInfo::exists(f.file),"Magnet created payload before target lookup and metadata");
          const QDir payloadParent(f.parent);require(payloadParent.exists()&&QFileInfo(f.parent).isReadable()&&payloadParent.entryList(QDir::Files|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot).isEmpty(),"Magnet created payload-parent files before metadata");
          require(!QFileInfo::exists(f.profile+"/active/"+f.hash+".dat"),"Magnet created active torrent state before metadata");
          const auto inventory=decode(read(f.profile+"/downloads.config",262144));const auto* downloads=inventory.find_key("downloads");
          require(downloads&&downloads->type()==lt::entry::list_t&&downloads->list().empty(),"Magnet admitted an ordinary saved torrent before metadata");
          // The Host intentionally presents one pending metadata row. It must
          // carry no ordinary payload counters or provider admission.
          if(latest&&!latest->items.empty()){
            require(latest->items.size()==1&&row,"Unexpected library row before magnet metadata");
            require(row->state==QStringLiteral("Resolving metadata")&&!row->completed&&!row->received&&row->providerAdmission==0&&row->providerGeneration==0,"Pending magnet row gained payload state before metadata");
          }
        }
        if(packet){require(packet->failure.isEmpty(),packet->failure.toUtf8().constData());}
        if(row&&row->completed&&*row->completed==PayloadBytes&&row->state.contains("Seeding")){
          require(proxy->released()&&row->providerAdmission&&row->providerGeneration,"Actual cold lookup/provider admission missing");require(row->received&&*row->received>=(f.magnet?PayloadBytes:PayloadBytes+1000),"Actual payload counters missing");
          require(QFileInfo::exists(work+'/'+f.name+"-resolved.json"),"Actual post-Host38 signed target LS2 observation missing");require(bridge->closeSession(),"Actual Host close refused");phase=Phase::Closing;
        }
      }else if(phase==Phase::Closing&&closeJoined&&storageReleased&&!bridge->busy()){
        bridge.reset();proxy.reset();f.validateAfterJoin();const auto spec=object(work+'/'+f.name+"-spec.json");cases.append(QJsonObject{{"case",f.name},{"info_hash",f.hash},{"payload_bytes",PayloadBytes},{"payload_sha256",digest(f.expected)},{"actual_original_metadata_sha256",spec["metadata_sha256"]},{"joined_before_disk_read",true},{"original_networks_preserved",true},{"one_handle_one_library_row",true},{"magnet",f.magnet},{"mixed",f.mixed}});json(work+'/'+f.name+"-native-done.json",{{"status","ACTUAL_TRACKERLESS_CASE_JOINED_AND_VERIFIED"},{"case",f.name}});
        if(++index==static_cast<int>(Cases.size())){finish(true,"Four real cold LS2/trackerless DHT/BEP9/payload Host cases");}else{startCase();}
      }
    }catch(const std::exception& error){finish(false,QString::fromUtf8(error.what()));}});poll.start(25);QTimer::singleShot(1000000,&application,[&]{finish(false,"Absolute Native discovery acceptance deadline");});code=application.exec();
  }catch(const std::exception& error){finish(false,QString::fromUtf8(error.what()));}
  return code;
}
