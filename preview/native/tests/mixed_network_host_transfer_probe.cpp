// Test-only ONE mixed torrent/ONE disk owner, plus Public-only control and stopped I2P sentinel.
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
#include <libtorrent/peer_info.hpp>
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
  struct Physical {QString path;struct stat original{};};
  QString name,path;QByteArray bytes,initial;biglybtc::core::TorrentHash hash;std::vector<Physical> physical;
  static bool multifile(const QString& name){return name=="mixed.bundle";}
  static lt::entry info(const QString& name,const QByteArray& all){
    lt::entry result(lt::entry::dictionary_type{});result["name"]=name.toStdString();result["piece length"]=16384;
    if(multifile(name)){
      lt::entry::list_type files;for(const auto* leaf:{"first.bin","second.bin"}){lt::entry file(lt::entry::dictionary_type{});file["length"]=PayloadBytes/2;file["path"]=lt::entry::list_type{lt::entry(leaf)};files.push_back(std::move(file));}result["files"]=std::move(files);
    }else{result["length"]=PayloadBytes;}
    std::string pieces;for(qsizetype offset=0;offset<all.size();offset+=16384){pieces+=QCryptographicHash::hash(all.mid(offset,16384),QCryptographicHash::Sha1).toStdString();}result["pieces"]=pieces;return result;
  }
  static biglybtc::core::TorrentHash makeHash(const QString& name,const QByteArray& all){
    const auto bytes=QCryptographicHash::hash(encode(info(name,all)),QCryptographicHash::Sha1);
    auto h=biglybtc::core::TorrentHash::parse(std::string_view(bytes.constData(),static_cast<std::size_t>(bytes.size())));require(h.has_value(),"Generated full torrent hash rejected");return *h;
  }
  Torrent(QString file,QString parent,int index,bool seed):name(std::move(file)),path(parent+'/'+name),bytes(expectedPayload(index)),initial(seed?bytes:QByteArray(PayloadBytes,'\0')),hash(makeHash(name,bytes)){
    if(multifile(name)&&seed){std::fill(initial.begin(),initial.begin()+PayloadBytes/2,'\0');}
    const auto add=[&](const QString& target,const QByteArray& content){write(target,content);Physical p{target,{}};require(::stat(target.toUtf8().constData(),&p.original)==0,"Cannot stat original payload");physical.push_back(p);};
    if(multifile(name)){add(path+"/first.bin",initial.first(PayloadBytes/2));add(path+"/second.bin",initial.last(PayloadBytes/2));}else{add(path,initial);}
  }
  QString text()const{return QString::fromStdString(hash.hex());}
  bool intact(bool completed)const{
    QByteArray actual;for(const auto& file:physical){struct stat now{};if(::stat(file.path.toUtf8().constData(),&now)!=0||now.st_dev!=file.original.st_dev||now.st_ino!=file.original.st_ino){return false;}actual+=read(file.path,PayloadBytes);}return actual==(completed?bytes:initial);
  }
};
struct Fixture {
  QString profile,payloadParent;std::vector<Torrent> torrents;i2p::Hash identity{};bool sourceRole=false;
  static qint64 receivedPrefix(std::size_t i){return static_cast<qint64>((i+1)*1000);}
  static qint64 sentPrefix(std::size_t i){return static_cast<qint64>((i+1)*10);}
  Fixture(const QString& work,const QString& role,bool seed,const QString& i2pTracker,const QString& publicTracker,const QString& mixedPublicTracker):profile(work+'/'+role+"-profile"),payloadParent(work+'/'+role+"-payload"),sourceRole(seed){
    for(const auto& path:{profile,profile+"/active",profile+"/plugins",profile+"/plugins/azneti2phelper",payloadParent}){privateDirectory(path);}
    SecretBytes keys{read(work+'/'+role+".keys",1024)};auto secret=i2p::decodeClientIdentity(view(keys.bytes));
    auto destination=i2p::decodeDestination(view(read(work+'/'+role+".dest",1024)));require(secret&&destination&&secret->destination()->hash==destination->hash,"Actual original key/Destination mismatch");
    identity=destination->hash;write(profile+"/plugins/azneti2phelper/"+(seed?"dest_key1.dat":"dest_key.dat"),keys.bytes);
    lt::entry inventory(lt::entry::dictionary_type{});inventory["downloads"]=lt::entry::list_type{};
    for(int i=0;i<3;++i){
      torrents.emplace_back(i==1?QStringLiteral("mixed.bundle"):QStringLiteral("sample-%1.bin").arg(i),payloadParent,i,seed);const auto& t=torrents.back();
      const auto state=profile+"/active/"+t.text()+".dat";lt::entry torrent(lt::entry::dictionary_type{});torrent["info"]=Torrent::info(t.name,t.bytes);torrent["announce"]=(i==0?publicTracker:i==1&&!seed?mixedPublicTracker:i2pTracker).toStdString();
      if(i==1&&!seed){torrent["announce-list"]=lt::entry::list_type{lt::entry(lt::entry::list_type{lt::entry(mixedPublicTracker.toStdString())}),lt::entry(lt::entry::list_type{lt::entry(i2pTracker.toStdString())})};}
      torrent["attributes"]["networks"]=i==1&&!seed?lt::entry::list_type{lt::entry("Public"),lt::entry("I2P")}:lt::entry::list_type{lt::entry(i==0?"Public":"I2P")};torrent["attributes"]["peersources"]=lt::entry::list_type{lt::entry("Tracker")};torrent["attributes"]["storetypes"]=lt::entry::list_type(i==1?2:1,lt::entry("L"));
      torrent["attributes"]["opaque.fixture"]=std::string("\0\xffQ",3);write(state,encode(torrent));
      lt::entry row(lt::entry::dictionary_type{});row["torrent_hash"]=QByteArray::fromHex(t.text().toLatin1()).toStdString();row["torrent"]=state.toStdString();row["save_dir"]=payloadParent.toStdString();row["save_file"]=t.name.toStdString();
      row["persistent"]=1;row["state"]=i==2?70:75;row["position"]=i+1;row["completedbytes"]=seed?(i==1?PayloadBytes/2:PayloadBytes):0;row["downloaded"]=seed?0:receivedPrefix(static_cast<std::size_t>(i));row["uploaded"]=seed?0:sentPrefix(static_cast<std::size_t>(i));
      row["maxdl"]=(seed?24:(i==0?4:8))*1024;row["maxul"]=(seed?24:10)*1024;row["file_priorities"]=i==1?lt::entry::list_type{lt::entry(seed?0:1),lt::entry(1)}:lt::entry::list_type{lt::entry(1)};inventory["downloads"].list().push_back(row);
    }
    inventory["pause_data"]=lt::entry::list_type{};
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
      const bool mixed=i==1&&!sourceRole;require(ns&&ns->type==biglybtc::core::Node::Type::List&&ns->children.size()==(mixed?2:1)&&ns->children[0].type==biglybtc::core::Node::Type::Bytes&&ns->children[0].bytes==(i==0||mixed?"Public":"I2P")&&(!mixed||(ns->children[1].type==biglybtc::core::Node::Type::Bytes&&ns->children[1].bytes=="I2P")),"Original per-hash networks changed or broadened");
      require(opaque&&opaque->type==biglybtc::core::Node::Type::Bytes&&opaque->bytes==std::string("\0\xffQ",3),"Unknown original attributes changed");
    }
  }
};

// Only actual HTTP announcements register a Public seed endpoint. No engine
// peer API is called; only this explicitly selected Public or mixed hash is admitted.
class PublicTracker final {
  QTcpServer server_;QByteArray hash_;QByteArray seedPeer_;
  quint16 registeredPort_=0;quint32 registeredAddress_=0;bool seedPresent_=false,seedComplete_=false,leechPresent_=false,leechComplete_=false,partial_=false;
  qint64 seedLeft_=-1,leechLeft_=-1;
  unsigned accepted_=0,seedAnnounces_=0,leechAnnounces_=0,replies_=0,advertisements_=0,eof_=0,completed_=0;QString failure_;
  std::function<quint16()> seedPort_;
  static QByteArray unescape(const QByteArray& value){
    QByteArray out;for(qsizetype i=0;i<value.size();++i){const auto c=static_cast<unsigned char>(value[i]);
      if(c=='%'){require(i+2<value.size(),"Short Public tracker escape");const auto part=value.mid(i+1,2);for(const auto digit:part){require((digit>='0'&&digit<='9')||(digit>='a'&&digit<='f')||(digit>='A'&&digit<='F'),"Malformed Public tracker escape");}out+=QByteArray::fromHex(part);i+=2;}
      else{require(c>=33&&c<127,"Non-ASCII Public tracker query");out+=static_cast<char>(c);}
    }return out;
  }
public:
  explicit PublicTracker(const biglybtc::core::TorrentHash& hash,bool partial=false):hash_(QByteArray::fromHex(QString::fromStdString(hash.hex()).toLatin1())),partial_(partial){
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
          isSeed=seedPort_&&port!=0&&port==seedPort_();const auto event=fields.value("event");require(event.isEmpty()||event=="started"||event=="stopped"||event=="completed"||(partial_&&isSeed&&event=="paused"),"Unexpected Public tracker event");
          if(isSeed){require(left==static_cast<qulonglong>(partial_?PayloadBytes/2:0)&&(seedPeer_.isEmpty()||seedPeer_==fields.value("peer_id")),"Public seed announce identity changed");seedPeer_=fields.value("peer_id");registeredPort_=static_cast<quint16>(port);registeredAddress_=socket->peerAddress().toIPv4Address();seedPresent_=event!="stopped";seedComplete_=left==0;seedLeft_=static_cast<qint64>(left);++seedAnnounces_;}
          else{require(fields.value("peer_id")!=seedPeer_,"Public seed advertised a different endpoint");leechPresent_=event!="stopped";leechComplete_=left==0;leechLeft_=static_cast<qint64>(left);++leechAnnounces_;if(event=="completed"){++completed_;}}
        }
        const auto completeCount=(seedPresent_&&seedComplete_?1:0)+(leechPresent_&&leechComplete_?1:0),incomplete=(seedPresent_&&!seedComplete_?1:0)+(leechPresent_&&!leechComplete_?1:0);QByteArray body;
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
  QJsonObject observation()const{return {{"accepted",static_cast<qint64>(accepted_)},{"seed_announces",static_cast<qint64>(seedAnnounces_)},{"leech_announces",static_cast<qint64>(leechAnnounces_)},{"replies",static_cast<qint64>(replies_)},{"actual_peer_advertisements",static_cast<qint64>(advertisements_)},{"incomplete_eof",static_cast<qint64>(eof_)},{"registered_seed_port",static_cast<int>(registeredPort_)},{"source_left",seedLeft_},{"leech_left",leechLeft_},{"failure",failure_}};}
};
class PublicSeed final {
  std::unique_ptr<lt::session> session_;std::array<lt::torrent_handle,2> handles_;QString partialRoot_;std::array<struct stat,2> original_{};QByteArray partialBytes_;
public:
  PublicSeed(const Fixture& fixture,const QString& mixedTracker):partialRoot_(QFileInfo(fixture.payloadParent).absolutePath()+"/public-source-payload"),partialBytes_(fixture.torrents[1].bytes.first(PayloadBytes/2)+QByteArray(PayloadBytes/2,'\0')){
    privateDirectory(partialRoot_);privateDirectory(partialRoot_+"/mixed.bundle");
    for(int n=0;n<2;++n){const auto path=partialRoot_+"/mixed.bundle/"+(n==0?"first.bin":"second.bin");write(path,partialBytes_.mid(n*PayloadBytes/2,PayloadBytes/2));require(::stat(path.toUtf8().constData(),&original_[static_cast<std::size_t>(n)])==0,"Cannot stat Public partial source");}
    lt::settings_pack settings;settings.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");settings.set_str(lt::settings_pack::outgoing_interfaces,"127.0.0.1");settings.set_str(lt::settings_pack::dht_bootstrap_nodes,"");
    for(auto key:{lt::settings_pack::enable_dht,lt::settings_pack::enable_lsd,lt::settings_pack::enable_upnp,lt::settings_pack::enable_natpmp,lt::settings_pack::enable_incoming_utp,lt::settings_pack::enable_outgoing_utp}){settings.set_bool(key,false);}
    settings.set_int(lt::settings_pack::upload_rate_limit,16*1024);session_=std::make_unique<lt::session>(settings);
    auto filter=session_->get_peer_class_filter();filter.add_rule(lt::make_address("127.0.0.1"),lt::make_address("127.0.0.1"),1u);session_->set_peer_class_filter(filter);
    for(std::size_t n=0;n<2;++n){
      QByteArray raw;if(n==0){raw=read(fixture.profile+"/active/"+fixture.torrents[0].text()+".dat",1024*1024);}else{lt::entry torrent(lt::entry::dictionary_type{});torrent["info"]=Torrent::info(fixture.torrents[1].name,fixture.torrents[1].bytes);torrent["announce"]=mixedTracker.toStdString();raw=encode(torrent);}
      lt::error_code error;auto metadata=std::make_shared<lt::torrent_info>(lt::span<const char>(raw.constData(),static_cast<std::size_t>(raw.size())),error,lt::from_span);require(!error,"Actual Public source metadata refused");
      lt::add_torrent_params params;params.ti=std::move(metadata);params.save_path=(n==0?fixture.payloadParent:partialRoot_).toStdString();
      if(n==1){params.file_priorities={lt::download_priority_t{4},lt::download_priority_t{0}};}
      params.flags&=~(lt::torrent_flags::paused|lt::torrent_flags::auto_managed);params.flags|=lt::torrent_flags::disable_dht|lt::torrent_flags::disable_lsd|lt::torrent_flags::disable_pex;
      handles_[n]=session_->add_torrent(params,error);require(!error&&handles_[n].is_valid(),"Cannot add genuine Public source");
    }
  }
  quint16 port()const{return session_?session_->listen_port():0;}
  bool ready()const{
    if(!session_||!port()||!handles_[0].status().is_seeding){return false;}const auto s=handles_[1].status(lt::torrent_handle::query_pieces);
    if(s.state==lt::torrent_status::checking_files||s.state==lt::torrent_status::checking_resume_data||s.is_seeding||s.pieces.size()!=PayloadBytes/16384){return false;}
    for(int n=0;n<s.pieces.size();++n){if(s.pieces[lt::piece_index_t{n}]!=(n<PayloadBytes/32768)){return false;}}return true;
  }
  void pauseMixed(){handles_[1].pause();}
  void resumeMixed(){handles_[1].resume();}
  bool mixedPaused()const{return bool(handles_[1].status().flags&lt::torrent_flags::paused);}
  std::vector<std::array<std::uint8_t,20>> peerIds()const{
    std::vector<lt::peer_info> peers;handles_[1].get_peer_info(peers);std::vector<std::array<std::uint8_t,20>> out;
    for(const auto& p:peers){if((p.flags&lt::peer_info::i2p_socket)||p.pid.is_all_zeros()){continue;}std::array<std::uint8_t,20> id{};std::copy_n(p.pid.begin(),id.size(),id.begin());out.push_back(id);if(out.size()==4){break;}}return out;
  }
  bool intact()const{
    QByteArray all;for(int n=0;n<2;++n){const auto path=partialRoot_+"/mixed.bundle/"+(n==0?"first.bin":"second.bin");struct stat now{};if(::stat(path.toUtf8().constData(),&now)!=0||now.st_dev!=original_[static_cast<std::size_t>(n)].st_dev||now.st_ino!=original_[static_cast<std::size_t>(n)].st_ino){return false;}all+=read(path,PayloadBytes/2);}return all==partialBytes_;
  }
  void close(){session_.reset();}
};
cp::SessionOptions zeroHop(){return {{"inbound.length","0"},{"outbound.length","0"},{"inbound.lengthVariance","0"},{"outbound.lengthVariance","0"},{"inbound.quantity","1"},{"outbound.quantity","1"},{"inbound.backupQuantity","0"},{"outbound.backupQuantity","0"},{"inbound.allowZeroHop","true"},{"outbound.allowZeroHop","true"},{"i2cp.leaseSetType","3"},{"i2cp.leaseSetEncType","4"},{"i2cp.dontPublishLeaseSet","false"},{"i2cp.fastReceive","true"},{"i2cp.messageReliability","none"}};}
// Only the mixed leech's real I2CP TCP bytes cross this owned loopback pair.
// The router, pure source and tracker remain directly connected. No parsing,
// payload synthesis, packet logging, public route or privilege is added.
class RouterProxy final {
  QTcpServer server_;QPointer<QTcpSocket> client_,router_;quint16 target_;bool enabled_=true;
  unsigned accepted_=0;QString failure_;
  void drop(){
    const std::array<QPointer<QTcpSocket>,2> sockets{client_,router_};client_.clear();router_.clear();
    for(const auto& p:sockets){if(p){QObject::disconnect(p,nullptr,&server_,nullptr);p->abort();p->deleteLater();}}
  }
  void pump(QPointer<QTcpSocket> from,QPointer<QTcpSocket> to){
    if(!from||!to||to->state()!=QAbstractSocket::ConnectedState){return;}
    while(from->bytesAvailable()>0&&to->bytesToWrite()<16384){
      const auto n=std::min<qint64>(4096,16384-to->bytesToWrite());const auto bytes=from->read(n);
      if(bytes.isEmpty()||to->write(bytes)!=bytes.size()){failure_="Bounded I2CP proxy write failed";drop();return;}
    }
  }
public:
  explicit RouterProxy(quint16 routerPort):target_(routerPort){
    require(server_.listen(QHostAddress::LocalHost,0),"Cannot bind owned mixed I2CP proxy");
    QObject::connect(&server_,&QTcpServer::newConnection,&server_,[this]{while(server_.hasPendingConnections()){
      auto* incoming=server_.nextPendingConnection();++accepted_;if(accepted_>8){failure_="Mixed I2CP proxy accept bound exceeded";}if(!enabled_||client_||accepted_>8){incoming->abort();incoming->deleteLater();continue;}
      client_=incoming;router_=new QTcpSocket(&server_);client_->setReadBufferSize(16384);router_->setReadBufferSize(16384);
      QObject::connect(client_,&QTcpSocket::readyRead,&server_,[this]{pump(client_,router_);});
      QObject::connect(router_,&QTcpSocket::readyRead,&server_,[this]{pump(router_,client_);});
      QObject::connect(router_,&QTcpSocket::connected,&server_,[this]{pump(client_,router_);});
      QObject::connect(router_,&QTcpSocket::bytesWritten,&server_,[this]{pump(client_,router_);});
      QObject::connect(client_,&QTcpSocket::bytesWritten,&server_,[this]{pump(router_,client_);});
      QObject::connect(client_,&QTcpSocket::disconnected,&server_,[this]{drop();});
      QObject::connect(router_,&QTcpSocket::disconnected,&server_,[this]{drop();});
      router_->connectToHost(QHostAddress::LocalHost,target_);
    }});
  }
  ~RouterProxy(){enabled_=false;drop();}
  quint16 port()const{return server_.serverPort();}
  void withdraw(){enabled_=false;drop();}
  void enable(){enabled_=true;}
  const QString& failure()const{return failure_;}
  QJsonObject observation()const{return {{"enabled",enabled_},{"accepted",static_cast<qint64>(accepted_)},{"pair_connected",client_&&router_&&client_->state()==QAbstractSocket::ConnectedState&&router_->state()==QAbstractSocket::ConnectedState},{"failure",failure_}};}
};
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
  std::vector<std::array<std::uint8_t,20>> peerIds(const biglybtc::core::TorrentHash& hash){std::vector<std::array<std::uint8_t,20>> out;call([&]{require(service_!=nullptr,"Seed service already closed");out=service_->peerIds(hash);});return out;}
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
enum class Stage{Initial,Concurrent,WithdrawPublicSource,PublicSourceWithdrawn,ResumePublicSource,MixedRouterLost,RetryMixedRouter,MixedRouterRetryStarted,WaitRouteCache,PausePublic,PublicPaused,ResumePublic,PublicResumed,StopPublic,PublicStopped,QueuePublic,PublicQueued,PauseI2p,I2pPaused,ResumeI2p,I2pResumed,StopI2p,I2pStopped,QueueI2p,I2pQueued,PauseForRestart,PausedForRestart,FirstClose,FirstClosed,WaitRetired,Reopen,ResumeAfterRestart,RestartStarted,WaitCache,SlotLimited,StopRunning,Handoff,RestartStopped,FinishTransfers,StopFinalPublic,PublicFinalStopped,StopFinalI2p,I2pFinalStopped,FinalClose,FinalClosed};
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
  QJsonObject result{{"status","FAIL"},{"scope","actual-single-owner-mixed-Public-I2P-Host"}};
  std::optional<std::pair<std::size_t,Action>> expectedAction;std::array<std::array<unsigned,10>,3> actions{};
  std::array<quint64,2> initialAdmissions{},initialGenerations{};std::array<qint64,3> lastReceived{},lastSent{},savedReceived{};
  for(std::size_t i=0;i<3;++i){lastReceived[i]=Fixture::receivedPrefix(i);lastSent[i]=Fixture::sentPrefix(i);}
  quint64 stableAdmission=0,stableGeneration=0,slotQueuedAdmission=0,slotRunningAdmission=0;int running=0,waiting=1;
  qint64 siblingBaseline=0,resumeBaseline=0;QElapsedTimer slotAge;bool slotObserved=false,sourcePauseObserved=false,publicSourcesIntact=false;
  qint64 sourceBaseline=0,sourceVerifiedBaseline=0,routerMixedBaseline=0,routerControlBaseline=0,routerVerifiedBaseline=0;
  quint64 routeAdmission=0,routeGeneration=0;unsigned handshakeSamples=0;std::optional<std::array<std::uint8_t,20>> initialWireId;bool firstHandler=true;
  const auto uiJson=[&](){QJsonArray rows,counters;if(snapshot){for(const auto& item:snapshot->items){rows.push_back(QJsonObject{{"hash",item.hash},{"state",item.state},{"completed",item.completed.value_or(-1)},{"received",item.received.value_or(-1)},{"sent",item.sent.value_or(-1)},{"admission",QString::number(item.providerAdmission)},{"generation",QString::number(item.providerGeneration)}});}}
    for(const auto& row:actions){QJsonArray values;for(const auto count:row){values.push_back(static_cast<qint64>(count));}counters.push_back(values);}
    return QJsonObject{{"session",QString::number(session)},{"revision",QString::number(snapshot?snapshot->revision:0)},{"rendered_revision",QString::number(window.model().revision())},{"rows",rows},{"last_Host_message",latestHostMessage},{"accepted_actions",counters}};
  };
  const auto diagnostic=[&](const QString& name,const QJsonObject& value)noexcept{try{output(work+'/'+name,value);}catch(...){std::cerr<<"Cannot write bounded progress marker\n";}};
  const auto mark=[&](const char* phase)noexcept{try{diagnostic("native-phase.json",{{"phase",QString::fromLatin1(phase)},{"elapsed_ms",elapsed.elapsed()}});}catch(...){}};
  const auto shutdown=[&]{if(shutdownDone){return;}shutdownDone=true;mark("before-Host-destruction");bridge.reset();mark("after-Host-destruction");if(seed){mark("before-I2P-seed-close");seed->close();seed.reset();mark("after-I2P-seed-join");}if(publicSeed){mark("before-Public-seed-close");publicSeed->close();publicSourcesIntact=publicSeed->intact();publicSeed.reset();mark("after-Public-seed-join");}};
  const auto finish=[&](bool ok,const QString& reason){if(done){return;}done=true;poll.stop();const auto ui=uiJson();diagnostic("native-terminal.json",{{"reason",reason.left(512)},{"intended_success",ok},{"elapsed_ms",elapsed.elapsed()},{"stage",static_cast<int>(stage)},{"seed",latestSeed},{"i2p",packetJson(packet)},{"ui",ui}});
    try{shutdown();}catch(const std::exception& error){ok=false;result.insert("shutdown_error",QString::fromUtf8(error.what()).left(512));}
    result.insert("status",ok?"PASS":"FAIL");result.insert("reason",reason.left(512));result.insert("elapsed_ms",elapsed.elapsed());result.insert("last_ui",ui);result.insert("last_i2p",packetJson(packet));result.insert("last_seed",latestSeed);
    try{output(work+"/native-result.json",result);}catch(...){ok=false;}code=ok?0:1;application.exit(code);
  };
  try{
    const QString tracker=QString::fromLatin1(read(work+"/tracker.url",256));require(tracker.startsWith("http://")&&tracker.endsWith(".b32.i2p/announce")&&!tracker.contains('\n'),"Owned I2P tracker URL required");
    PublicTracker publicTracker(Torrent::makeHash(QStringLiteral("sample-0.bin"),expectedPayload(0)));
    PublicTracker mixedPublicTracker(Torrent::makeHash(QStringLiteral("mixed.bundle"),expectedPayload(1)),true);
    RouterProxy routerProxy(static_cast<quint16>(port));
    Fixture a(work,"seed",true,tracker,publicTracker.url(),mixedPublicTracker.url()),b(work,"leech",false,tracker,publicTracker.url(),mixedPublicTracker.url());require(a.identity!=b.identity,"Distinct original I2P identities required");
    for(std::size_t i=0;i<3;++i){require(a.torrents[i].hash==b.torrents[i].hash,"Seed/leech metainfo differ");}
    write(work+"/torrent.hashes",QByteArray::fromHex(b.torrents[1].text().toLatin1()));
    write(work+"/coexist.hashes",QByteArray::fromHex(b.torrents[0].text().toLatin1())+QByteArray::fromHex(b.torrents[1].text().toLatin1()));
    publicSeed=std::make_unique<PublicSeed>(a,mixedPublicTracker.url());publicTracker.seedPort([&]{return publicSeed?publicSeed->port():0;});mixedPublicTracker.seedPort([&]{return publicSeed?publicSeed->port():0;});
    auto seedLease=biglybtc::storage::ProfileLease::acquire(a.profile.toStdString());require(seedLease.has_value(),"Cannot own I2P seed profile");
    app::I2pServiceOptions options;options.routerPort=static_cast<quint16>(port);options.sessionOptions=zeroHop();options.peerPort=6881;
    seed=std::make_unique<SeedWorker>();seed->start(*seedLease,a.torrents,options);options.publicCoexistence=true;options.routerPort=routerProxy.port();
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
      require(elapsed.elapsed()<1000000,"Absolute mixed1000s budget exceeded");require(callbackFailure.isEmpty(),callbackFailure.toUtf8().constData());require(publicTracker.failure().isEmpty(),publicTracker.failure().toUtf8().constData());require(mixedPublicTracker.failure().isEmpty(),mixedPublicTracker.failure().toUtf8().constData());require(routerProxy.failure().isEmpty(),routerProxy.failure().toUtf8().constData());
      mark("before-I2P-seed-observations");const auto seeds=seed->observations();mark("after-I2P-seed-observations");latestSeed=seedJson(seeds);require(seeds.size()==1&&seeds[0].hash==a.torrents[1].hash,"I2P seed admitted a wrong-network hash");if(seeds[0].value.checked){require(seeds[0].value.verifiedBytes==PayloadBytes/2&&!seeds[0].value.seeding,"Pure I2P source gained data outside its complementary half");}require(seeds[0].value.publicIdentity==a.identity&&seeds[0].value.failure.empty()&&seeds[0].value.phase!=app::OwnedI2pService::Phase::Withdrawn,"I2P seed identity/source lost");
      const bool expectedRouterLoss=stage==Stage::MixedRouterLost||stage==Stage::RetryMixedRouter||stage==Stage::MixedRouterRetryStarted||stage==Stage::WaitRouteCache;
      if(packet){require(packet->session==session&&(packet->failure.isEmpty()||expectedRouterLoss),"Foreign/failed I2P packet");for(const auto& v:packet->torrents){require(v.hash==b.torrents[1].text()&&(v.failure.isEmpty()||expectedRouterLoss),"Public/sentinel hash gained I2P authority");}}
      if(firstHandler){
        const auto publicIds=publicSeed->peerIds(),privateIds=seed->peerIds(a.torrents[1].hash);
        if(!publicIds.empty()&&!privateIds.empty()){
          require(publicIds.size()==1&&privateIds.size()==1,"Unexpected actual mixed BT handshake identity count");const auto& plain=publicIds.front();const auto& hidden=privateIds.front();
          require(std::equal(plain.begin(),plain.begin()+8,hidden.begin())&&!std::equal(plain.begin()+8,plain.end(),hidden.begin()+8),"Original I2P BT handshake prefix/rewritten-tail relation differs");
          if(initialWireId){require(*initialWireId==hidden,"Mixed handler changed its actual I2P wire identity before any route close");}else{initialWireId=hidden;}
          if(handshakeSamples<2){++handshakeSamples;}
        }
      }
      if(snapshot&&session){for(std::size_t i=0;i<3;++i){const auto* v=row(i);if(!v){continue;}if(v->received){require(*v->received>=lastReceived[i],"Cumulative library Received regressed at retire/restart");lastReceived[i]=*v->received;}if(v->sent){require(*v->sent>=lastSent[i],"Cumulative library Sent regressed at retire/restart");lastSent[i]=*v->sent;}}}
      if(progressAge.elapsed()>=1000){progressAge.restart();diagnostic("native-progress.json",{{"stage",static_cast<int>(stage)},{"elapsed_ms",elapsed.elapsed()},{"Host_busy",bridge->busy()},{"i2p",packetJson(packet)},{"seed",latestSeed},{"Public_tracker",publicTracker.observation()},{"Mixed_Public_tracker",mixedPublicTracker.observation()},{"mixed_router_proxy",routerProxy.observation()},{"ui",uiJson()}});}
      const auto* x=active(0);const auto* y=active(1);const auto* z=row(2);require(!active(2),"Stopped private sentinel acquired owner authority");if(z){require(z->state=="Stopped","Stopped private sentinel changed intent");}
      switch(stage){
      case Stage::Initial:if(!bridge->busy()&&session&&snapshot&&snapshot->items.size()==3&&x&&y&&publicSeed->ready()&&seeds[0].value.checked&&seeds[0].value.verifiedBytes==PayloadBytes/2&&!seeds[0].value.seeding){
        for(std::size_t n=0;n<3;++n){require(std::ranges::count_if(snapshot->items,[&](const auto& v){return v.hash==b.torrents[n].text();})==1,"Host must render exactly one row per original torrent");}require(x->providerGeneration==y->providerGeneration&&x->providerAdmission!=y->providerAdmission,"Public control and mixed torrent must share one provider generation with distinct admissions");result.insert("one_mixed_library_row",true);result.insert("source_checked_partition",QJsonObject{{"piece_bytes",16384},{"Public_first_piece",0},{"Public_piece_count",static_cast<int>(PayloadBytes/32768)},{"I2P_first_piece",static_cast<int>(PayloadBytes/32768)},{"I2P_piece_count",static_cast<int>(PayloadBytes/32768)},{"I2P_verified_bytes",static_cast<qint64>(seeds[0].value.verifiedBytes)}});initialSession=session;initialAdmissions={x->providerAdmission,y->providerAdmission};initialGenerations={x->providerGeneration,y->providerGeneration};output(work+"/native-started.json",{{"status","ACTUAL_PUBLIC_CONTROL_AND_MIXED_HOST_STARTED"},{"seed_destination_sha256",hex(a.identity)},{"leech_destination_sha256",hex(b.identity)},{"active_hashes",2},{"saved_hashes",3}});stage=Stage::Concurrent;}break;
      case Stage::Concurrent:if(x&&y&&hasTraffic(0)&&hasTraffic(1)&&!queued(0)&&!queued(1)&&received(0)>=Fixture::receivedPrefix(0)+16384&&received(1)>=Fixture::receivedPrefix(1)+16384&&publicTracker.discovered()&&mixedPublicTracker.discovered()&&handshakeSamples>=2){
        require(incomplete(0)&&incomplete(1),"No simultaneous incomplete Public/I2P transfers");result.insert("simultaneous_networks",uiJson());result.insert("actual_BT_wire_identity_prefix_and_private_tail",true);stage=Stage::WithdrawPublicSource;}break;
      case Stage::WithdrawPublicSource:publicSeed->pauseMixed();stage=Stage::PublicSourceWithdrawn;break;
      case Stage::PublicSourceWithdrawn:if(publicSeed->mixedPaused()&&y&&y->completed&&hasTraffic(1)){
        if(!sourcePauseObserved){sourcePauseObserved=true;sourceBaseline=received(1);sourceVerifiedBaseline=*y->completed;break;}
        if(received(1)>=sourceBaseline+16384&&*y->completed>=sourceVerifiedBaseline+16384){require(incomplete(1),"Partial I2P source alone completed the whole torrent");result.insert("I2P_progress_without_Public_source",uiJson());publicSeed->resumeMixed();stage=Stage::ResumePublicSource;}}break;
      case Stage::ResumePublicSource:if(!publicSeed->mixedPaused()&&x&&y&&!publicSeed->peerIds().empty()){
        require(incomplete(0)&&incomplete(1)&&routerProxy.observation().value("pair_connected").toBool(),"No actual active mixed I2CP pair/incomplete work for router-loss control");routeAdmission=y->providerAdmission;routeGeneration=y->providerGeneration;routerMixedBaseline=received(1);routerControlBaseline=received(0);routerVerifiedBaseline=y->completed.value_or(0);firstHandler=false;routerProxy.withdraw();stage=Stage::MixedRouterLost;}break;
      case Stage::MixedRouterLost:if(!bridge->busy()&&x&&y&&y->networkStatus.contains("unavailable",Qt::CaseInsensitive)){
        require(y->providerAdmission==routeAdmission&&y->providerGeneration==routeGeneration,"Router EOF replaced the Public torrent owner");
        if(received(1)>=routerMixedBaseline+16384&&y->completed&&*y->completed>=routerVerifiedBaseline+16384&&received(0)>=routerControlBaseline+16384){
          require(incomplete(1),"No remaining mixed work for actual route retry");result.insert("Public_progress_after_mixed_router_EOF",uiJson());routerProxy.enable();stage=Stage::RetryMixedRouter;}}break;
      case Stage::RetryMixedRouter:static_cast<void>(trigger(1,"classic-start",Action::Start,Stage::MixedRouterRetryStarted));break;
      case Stage::MixedRouterRetryStarted:if(!bridge->busy()&&y&&routerProxy.observation().value("pair_connected").toBool()){
        require(y->providerAdmission==routeAdmission&&y->providerGeneration==routeGeneration,"Mixed retry replaced the Public torrent handle");
        output(work+"/mixed-route-restarted.json",{{"status","ACTUAL_MIXED_RETRY_ACCEPTED_TCP_CONNECTED"},{"leech_destination_sha256",hex(b.identity)},{"admission",QString::number(routeAdmission)}});stage=Stage::WaitRouteCache;}break;
      case Stage::WaitRouteCache:if(QFile::exists(work+"/mixed-route-cache-precondition.json")&&y&&!y->networkStatus.contains("unavailable",Qt::CaseInsensitive)){
        require(y->providerAdmission==routeAdmission&&y->providerGeneration==routeGeneration,"Mixed retry/cache warm lost retained torrent authority");rememberSibling(1);stage=Stage::PausePublic;}break;
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
      case Stage::I2pResumed:if(!bridge->busy()&&x&&y&&hasTraffic(1)&&!queued(1)&&received(1)>=resumeBaseline+16384){require(y->providerAdmission==initialAdmissions[1]&&y->providerGeneration==initialGenerations[1],"Mixed pause/resume replaced its retained Public owner");static_cast<void>(siblingProgress(0));rememberSibling(0);stage=Stage::StopI2p;}break;
      case Stage::StopI2p:static_cast<void>(trigger(1,"classic-stop",Action::Stop,Stage::I2pStopped));break;
      case Stage::I2pStopped:if(!bridge->busy()&&!y&&row(1)&&row(1)->state=="Stopped"&&siblingProgress(0)){result.insert("stop_I2P_preserved_Public",uiJson());stage=Stage::QueueI2p;}break;
      case Stage::QueueI2p:static_cast<void>(trigger(1,"classic-queue",Action::Queue,Stage::I2pQueued));break;
      case Stage::I2pQueued:if(!bridge->busy()&&x&&y&&!queued(1)){require(incomplete(0)&&incomplete(1),"No incomplete work for shared queue restart");stage=Stage::PauseForRestart;}break;
      case Stage::PauseForRestart:static_cast<void>(trigger(1,"classic-pause",Action::Pause,Stage::PausedForRestart));break;
      case Stage::PausedForRestart:if(!bridge->busy()&&x&&row(1)&&row(1)->state.startsWith("Paused")){output(work+"/shared-controls-ready.json",{{"status","ACTUAL_CROSS_NETWORK_CONTROLS_COMPLETED"}});stage=Stage::FirstClose;}break;
      case Stage::FirstClose:if(QFile::exists(work+"/shared-session-precondition.json")){stage=Stage::FirstClosed;joined=released=false;closeRequested=true;require(bridge->closeSession(),"Host refused first joined close");}break;
      case Stage::FirstClosed:if(joined&&released&&!bridge->busy()){
        const auto saved=b.saved();require(savedRow(saved,0).originalState==75&&savedRow(saved,1).originalState==70&&savedRow(saved,2).originalState==70,"Original75/70/70 checkpoint changed");
        require(saved.paused.size()==1&&saved.paused[0].hash==b.torrents[1].hash,"Original mixed pause and stopped private sentinel not preserved");
        for(std::size_t i=0;i<3;++i){const auto& v=savedRow(saved,i);require(v.downloaded&&v.uploaded&&*v.downloaded>=lastReceived[i]&&*v.uploaded>=lastSent[i],"Joined checkpoint lost current library counters");savedReceived[i]=*v.downloaded;}
        b.networksIntact();auto free=biglybtc::storage::ProfileLease::acquire(b.profile.toStdString());require(free.has_value(),"Joined Host retained profile lock");b.writeSettings(false,1);output(work+"/leech-restart-ready.json",{{"status","ACTUAL_HOST_CLOSED_FOR_SAME_IDENTITY_RESTART"},{"original_state_A",75},{"original_state_B",70},{"original_state_C",70},{"B_paused",true},{"C_paused",false}});stage=Stage::WaitRetired;}break;
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
      case Stage::FinishTransfers:if(!bridge->busy()&&x&&y&&x->state.startsWith("Seeding")&&y->state.startsWith("Seeding")&&x->completed==PayloadBytes&&y->completed==PayloadBytes){require(publicTracker.discovered()&&mixedPublicTracker.discovered(),"No actual Public/mixed tracker discovery");result.insert("completed",uiJson());result.insert("Public_tracker",publicTracker.observation());result.insert("Mixed_Public_tracker",mixedPublicTracker.observation());require(window.grab().save(work+"/mixed-preview.png"),"Cannot retain actual coexistence screenshot");stage=Stage::StopFinalPublic;}break;
      case Stage::StopFinalPublic:static_cast<void>(trigger(0,"classic-stop",Action::Stop,Stage::PublicFinalStopped));break;
      case Stage::PublicFinalStopped:if(!bridge->busy()&&!x&&row(0)&&row(0)->state=="Stopped"){stage=Stage::StopFinalI2p;}break;
      case Stage::StopFinalI2p:static_cast<void>(trigger(1,"classic-stop",Action::Stop,Stage::I2pFinalStopped));break;
      case Stage::I2pFinalStopped:if(!bridge->busy()&&!y&&row(1)&&row(1)->state=="Stopped"){stage=Stage::FinalClose;}break;
      case Stage::FinalClose:stage=Stage::FinalClosed;joined=released=false;closeRequested=false;window.close();break;
      case Stage::FinalClosed:if(closeRequested&&joined&&released&&!bridge->busy()){
        shutdown();const auto saved=b.saved();QJsonArray transfers;
        for(std::size_t i=0;i<2;++i){const auto& sr=savedRow(saved,i);require(sr.downloaded&&*sr.downloaded>=Fixture::receivedPrefix(i)+PayloadBytes&&*sr.downloaded>=savedReceived[i]&&sr.uploaded&&*sr.uploaded>=Fixture::sentPrefix(i),"Final original cumulative traffic missing");require(b.torrents[i].intact(true)&&a.torrents[i].intact(i==0),"Joined payload bytes or inode differ");
          for(const auto action:{Action::Pause,Action::Resume,Action::Stop,Action::Queue}){require(actions[i][static_cast<std::size_t>(action)]>0,"Missing real independent lifecycle action");}
          transfers.push_back(QJsonObject{{"network",i==0?"Public":"Public+I2P"},{"info_hash",b.torrents[i].text()},{"verified_bytes",static_cast<qint64>(PayloadBytes)},{"saved_downloaded",static_cast<qint64>(*sr.downloaded)},{"initial_downloaded",Fixture::receivedPrefix(i)},{"payload_sha256",QString::fromLatin1(QCryptographicHash::hash(b.torrents[i].bytes,QCryptographicHash::Sha256).toHex())}});
        }
        b.networksIntact();require(b.torrents[2].intact(false)&&a.torrents[2].intact(true)&&savedRow(saved,2).originalState==70&&savedRow(saved,2).downloaded==Fixture::receivedPrefix(2),"Stopped private sentinel activated or changed");auto free=biglybtc::storage::ProfileLease::acquire(b.profile.toStdString());require(free.has_value(),"Final Host retained profile lock");
        result.insert("transfers",transfers);result.insert("verified_bytes",static_cast<qint64>(2*PayloadBytes));result.insert("seed_destination_sha256",hex(a.identity));result.insert("leech_destination_sha256",hex(b.identity));
        for(const auto key:{"mixed_single_torrent_both_source_ranges_verified","independent_controls","shared_download_slot_handoff","original_networks_preserved","cumulative_library_traffic_preserved","saved_75_70_pause_restored","all_payload_inodes_preserved","joined_before_disk_read","Host_profile_lock_released","stopped_private_sentinel_untouched"}){result.insert(key,true);}require(publicSourcesIntact&&sourcePauseObserved&&handshakeSamples>=2&&result.contains("I2P_progress_without_Public_source")&&result.contains("Public_progress_after_mixed_router_EOF"),"Missing actual complementary source/withdrawal/wire-identity proof");result.insert("public_partial_source_unchanged",publicSourcesIntact);result.insert("direct_peer_injection",false);finish(true,"Actual one-owner mixed transfer, complementary verified sources, router loss continuation, shared queue and same-profile restart");}break;
      }
    }catch(const std::exception& error){finish(false,QString::fromUtf8(error.what()));}catch(...){finish(false,"Unexpected bounded mixed resource failure");}});
    poll.start(25);application.exec();mark("application-loop-exited");shutdown();return code;
  }catch(const std::exception& error){finish(false,QString::fromUtf8(error.what()));return 1;}catch(...){finish(false,"Unexpected mixed setup failure");return 1;}
}
