// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "owned_engine_fixture.hpp"
#include "owned_mainline_krpc.hpp"
#include "session_bridge.hpp"
#include "network_policy.hpp"
#include "overall_presentation.hpp"
#include "native_session_presentation.hpp"
#include "biglybtc/overall_statistics.hpp"
#include "biglybtc/library.hpp"
#include <QElapsedTimer>
#include <QNetworkDatagram>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QtTest>
#include <set>
namespace biglybtc::test::daily {
using Field=core::SavedStatistic;namespace lt=libtorrent;
inline constexpr int Size=524288,Piece=16384;
inline constexpr std::int64_t OldDown=5000,OldUp=2000,OldUptime=40;
inline QByteArray payload(){QByteArray out(Size,'\0');for(qsizetype i=0;i<out.size();++i){out[i]=static_cast<char>((i*31+17)%251);}return out;}
inline std::uint32_t number(const QByteArray& bytes,qsizetype at){std::uint32_t out=0;for(int i=0;i<4;++i){out=(out<<8)|static_cast<unsigned char>(bytes[at+i]);}return out;}
inline void append(QByteArray& bytes,std::uint32_t value){for(int shift=24;shift>=0;shift-=8){bytes.append(static_cast<char>(value>>shift));}}
inline QByteArray initialStatistics(){lt::entry root(lt::entry::dictionary_type{});auto& all=root["all"];all["downloaded"]=OldDown;all["uploaded"]=OldUp;all["uptime"]=OldUptime;all["p_uploaded"]=11;all["d_uploaded"]=13;all["p_downloaded"]=17;all["d_downloaded"]=19;all["dht_up"]=23;all["dht_down"]=29;all["opaque.all"]=std::string("\0\xffkeep",6);root["opaque.root"]=std::string("\xff\0root",6);return QByteArray::fromStdString(encoded(root));}
inline core::Document document(const QString& path){const auto value=read(path);auto result=core::Document::parse(value.toStdString(),storage::compatibleConfigLimits());require(bool(result),"Actual daily fixture document");return std::move(*result);}
inline std::string raw(const core::Document& doc,const core::Node* value){require(value!=nullptr,"Actual owned subtree missing");return doc.originalBytes().substr(value->begin,value->end-value->begin);}
inline std::int64_t statistic(const QString& profile,Field field){const auto bytes=read(profile+"/BiglyBT.statistics");auto saved=core::readSavedStatistics(std::string_view(bytes.constData(),static_cast<std::size_t>(bytes.size())));require(saved&&saved->get(field).value.has_value(),"Actual compatible Overall scalar");return *saved->get(field).value;}
struct Profile {
 QTcpServer peerReservation,peerV6Reservation;QUdpSocket peerUdpReservation,mainlineReservation,peerUdpV6Reservation,mainlineV6Reservation;
 QString path,data,state,archive;QHostAddress address;unsigned short peerPort=0,mainlinePort=0;core::TorrentHash hash;QByteArray key,body;bool seed;std::int64_t preexistingSeedSeconds=0;
 Profile(const QString& root,const QString& name,bool v6,bool complete,const QString& tracker,int startup=75,bool dual=false):path(root+'/'+name),data(path+"-data/daily.bin"),address(dual?QHostAddress::LocalHost:(v6?QHostAddress::LocalHostIPv6:QHostAddress::LocalHost)),key(20,complete?'S':'L'),body(payload()),seed(complete),preexistingSeedSeconds(complete&&v6?40001:0){
  require(QDir().mkpath(path+"/active")&&QDir().mkpath(path+"/archive"),"Owned ordinary daily profile");require(peerReservation.listen(address,0),"Owned fixed peer listener reservation");peerPort=peerReservation.serverPort();require(peerUdpReservation.bind(address,peerPort,QUdpSocket::DontShareAddress)&&mainlineReservation.bind(address,0,QUdpSocket::DontShareAddress),"Owned fixed UDP reservations");mainlinePort=mainlineReservation.localPort();require(peerPort!=mainlinePort,"Separate Mainline/peer ports");if(dual){require(peerV6Reservation.listen(QHostAddress::LocalHostIPv6,peerPort)&&peerUdpV6Reservation.bind(QHostAddress::LocalHostIPv6,peerPort,QUdpSocket::DontShareAddress)&&mainlineV6Reservation.bind(QHostAddress::LocalHostIPv6,mainlinePort,QUdpSocket::DontShareAddress),"Actual simultaneous dual-family fixed ports");}
  lt::entry info(lt::entry::dictionary_type{});info["name"]="daily.bin";info["length"]=Size;info["piece length"]=Piece;std::string pieces;for(int at=0;at<Size;at+=Piece){pieces+=QCryptographicHash::hash(body.mid(at,Piece),QCryptographicHash::Sha1).toStdString();}info["pieces"]=pieces;
  const auto rawInfo=encoded(info);const auto digest=QCryptographicHash::hash(QByteArray::fromStdString(rawInfo),QCryptographicHash::Sha1);auto parsed=core::TorrentHash::parse(std::string_view(digest.constData(),20));require(bool(parsed),"Actual daily v1 identity");hash=*parsed;
  state=path+"/active/"+QString::fromStdString(hash.hex())+".dat";archive=path+"/archive/"+QString::fromStdString(hash.hex())+".torrent";
  lt::entry saved(lt::entry::dictionary_type{});saved["info"]=info;saved["announce"]=tracker.toStdString();auto& attrs=saved["attributes"];attrs["storetypes"]=lt::entry::list_type{lt::entry("L")};attrs["networks"]=lt::entry::list_type{lt::entry("Public"),lt::entry("I2P"),lt::entry("Tor")};attrs["resumecomplete"]=complete?2:1;attrs["opaque.daily"]=std::string("\0\xff" "attrs",7);
  saved["azureus_properties"]["dht_backup_enable"]=1;auto& resume=saved["resume"]["data"];std::string bitmap(Size/Piece,'\0');std::fill_n(bitmap.begin(),complete?Size/Piece:Size/Piece/2,'\1');resume["resume data"]=bitmap;resume["valid"]=1;resume["blocks"]=lt::entry::dictionary_type{};write(data,complete?body:body.first(Size/2)+QByteArray(Size/2,'\0'));
  // Java saves its verified map after the payload, preserving resume age.
  write(state,QByteArray::fromStdString(encoded(saved)));write(archive,QByteArray::fromStdString(encoded(saved)));
  lt::entry row(lt::entry::dictionary_type{});row["torrent_hash"]=digest.toStdString();row["torrent"]=archive.toStdString();row["save_dir"]=QFileInfo(data).absolutePath().toStdString();row["save_file"]="daily.bin";row["persistent"]=1;row["state"]=startup;row["position"]=1;row["forceStart"]=0;row["completedbytes"]=complete?Size:Size/2;row["downloaded"]=complete?Size:Size/2;row["uploaded"]=103;row["maxdl"]=65536;row["secondsOnlySeeding"]=preexistingSeedSeconds;row["secondsDownloading"]=preexistingSeedSeconds?3600:0;row["file_priorities"]=lt::entry::list_type{lt::entry(1)};row["opaque.row"]="preserve";lt::entry inventory(lt::entry::dictionary_type{});inventory["downloads"]=lt::entry::list_type{row};inventory["opaque.root"]="library";write(path+"/downloads.config",QByteArray::fromStdString(encoded(inventory)));write(path+"/tables.config","de");write(path+"/BiglyBT.statistics",initialStatistics());
  lt::entry config(lt::entry::dictionary_type{});config["Bind IP"]=dual?std::string("127.0.0.1;::1"):address.toString().toStdString();config["Enforce Bind IP"]=1;config["peercontrol.udp.public.enable"]=0;config["IPV6 Enable Support"]=(dual||v6)?1:0;config["TCP.Listen.Port"]=peerPort;config["UDP.Listen.Port"]=peerPort;config["UDP.NonData.Listen.Port"]=peerPort;config["UDP.NonData.Listen.Port.Same"]=1;config["Network Selection Default.Public"]=1;config["Network Selection Default.I2P"]=1;config["Network Selection Default.Tor"]=1;config["Peer Source Selection Default.PeerExchange"]=0;config["PluginInfo.mldht.enabled"]=1;config["Plugin.mlDHT.enable"]=1;config["Plugin.mlDHT.port"]=mainlinePort;config["Plugin.mlDHT.autostart.IPv4"]=(dual||!v6)?1:0;config["Plugin.mlDHT.autostart.IPv6"]=(dual||v6)?1:0;config["Plugin.mlDHT.onlyPeerBootstrap"]=0;config["Plugin.mlDHT.alwaysRestoreID"]=1;config["Plugin.UPnP.upnp.enable"]=0;config["Plugin.UPnP.natpmp.enable"]=0;config["Plugin.DHT.dht.enabled"]=1;config["StartStopManager_bAddForDownloadingSR1"]=0;config["StartStopManager_bAutoStart0Peers"]=0;config["StartStopManager_bFirstPriority_ignore0Peer"]=1;config["StartStopManager_bMaxActiveTorrentsWhenSeedingEnabled"]=0;config["StartStopManager_iFirstPriority_DLMinutes"]=300;config["StartStopManager_iFirstPriority_ShareRatio"]=50000;config["StartStopManager_iFirstPriority_ignoreSPRatio"]=60;config["StartStopManager_iIgnoreRatioPeersSeedStart"]=0;config["StartStopManager_iIgnoreSeedCount"]=0;config["StartStopManager_iMaxActiveTorrentsWhenSeeding"]=0;config["StartStopManager_iMaxStalledSeeding"]=0;config["StartStopManager_iMinSeedingTime"]=20000;config["StartStopManager_iMinSpeedForActiveDL"]=10240;config["StartStopManager_iMinSpeedForActiveSeeding"]=512;config["StartStopManager_iRankType"]=0;config["max active torrents"]=0;config["max downloads"]=0;config["opaque.daily"]="config";write(path+"/biglybt.config",QByteArray::fromStdString(encoded(config)));write(path+"/plugins/mlDHT/plugin.properties","plugin.id=mldht\nplugin.version=2.6.12\n");write(path+"/plugins/mlDHT/baseID.config",key.toHex().toUpper()+'\n');require(::chmod(path.toUtf8().constData(),0777)==0,"Original-compatible owned directory mode");
 }
 void releasePorts(){peerReservation.close();peerUdpReservation.close();mainlineReservation.close();peerV6Reservation.close();peerUdpV6Reservation.close();mainlineV6Reservation.close();}
 std::filesystem::path native()const{return path.toStdString();}
 QString checkpoint()const{return path+"/BiglyBTC-mainline.config";}
 lt::sha1_hash wireHash()const{return lt::sha1_hash(reinterpret_cast<const char*>(hash.bytes().data()));}
};
class Trackers final {
 QTcpServer http_;QUdpSocket udp_;QByteArray hash_;std::set<unsigned short> ports_;
 void fail(const char* why){if(failure.isEmpty()){failure=QString::fromLatin1(why);}}
public:
 int httpAnnounces=0,udpAnnounces=0;QString failure;
 explicit Trackers(const QHostAddress& address){
  require(http_.listen(address,0)&&udp_.bind(address,0,QUdpSocket::DontShareAddress),"Owned external tracker endpoints");
  QObject::connect(&http_,&QTcpServer::newConnection,&http_,[this]{while(http_.hasPendingConnections()){
   auto* socket=http_.nextPendingConnection();auto pending=std::make_shared<QByteArray>();QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
   QObject::connect(socket,&QTcpSocket::readyRead,socket,[this,socket,pending]{*pending+=socket->readAll();if(pending->size()>16384){fail("HTTP tracker request limit");socket->abort();return;}if(!pending->contains("\r\n\r\n")){return;}const auto first=pending->left(pending->indexOf("\r\n"));const bool scrape=first.startsWith("GET /scrape?");if(!first.startsWith("GET /announce?")&&!scrape){fail("HTTP tracker actual announce/scrape missing");socket->abort();return;}const auto end=first.indexOf(' ',4);const auto target=first.mid(4,end-4);QByteArray requested;for(const auto& item:target.mid(target.indexOf('?')+1).split('&')){const auto equal=item.indexOf('=');if(item.left(equal)=="info_hash"){requested=QByteArray::fromPercentEncoding(item.mid(equal+1));}}
    if(requested!=hash_){fail("HTTP announce has wrong actual hash");socket->abort();return;}QByteArray body;if(scrape){lt::entry value(lt::entry::dictionary_type{});auto& row=value["files"][hash_.toStdString()];row["complete"]=1;row["downloaded"]=0;row["incomplete"]=0;body=QByteArray::fromStdString(encoded(value));}else{++httpAnnounces;body="d8:intervali3600e8:completei1e10:incompletei0e5:peers0:e";}socket->write("HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);socket->disconnectFromHost();pending->clear();});
  }});
  QObject::connect(&udp_,&QUdpSocket::readyRead,&udp_,[this]{while(udp_.hasPendingDatagrams()){
   const auto packet=udp_.receiveDatagram(1024);const auto request=packet.data();if(!packet.senderAddress().isLoopback()||request.size()<16){fail("UDP tracker actual source/frame");continue;}const auto action=number(request,8),transaction=number(request,12);QByteArray reply;
   if(action==0&&request.size()==16){if(request.first(8)!=QByteArray::fromHex("0000041727101980")){fail("UDP tracker connect magic");continue;}append(reply,0);append(reply,transaction);reply+=QByteArray::fromHex("1122334455667788");}
   else if(action==1&&request.size()>=98&&request.size()<=512){const auto announced=static_cast<unsigned short>((static_cast<unsigned char>(request[96])<<8)|static_cast<unsigned char>(request[97]));if(request.first(8)!=QByteArray::fromHex("1122334455667788")||request.mid(16,20)!=hash_||!ports_.contains(announced)){fail("UDP announce has wrong actual token/hash/port");continue;}++udpAnnounces;append(reply,1);append(reply,transaction);append(reply,3600);append(reply,0);append(reply,1);}
   else if(action==2&&request.size()>=36){append(reply,2);append(reply,transaction);append(reply,1);append(reply,0);append(reply,0);}
   else{fail("Unexpected owned UDP tracker command");continue;}if(udp_.writeDatagram(reply,packet.senderAddress(),packet.senderPort())!=reply.size()){fail("Owned UDP reply failed");}
  }});
 }
 QString url(bool udp)const{const auto host=http_.serverAddress().protocol()==QAbstractSocket::IPv6Protocol?QStringLiteral("[::1]"):QStringLiteral("127.0.0.1");return QStringLiteral("%1://%2:%3/announce").arg(udp?"udp":"http",host).arg(udp?udp_.localPort():http_.serverPort());}
 void allow(const Profile& a,const Profile& b){hash_=QByteArray(reinterpret_cast<const char*>(a.hash.bytes().data()),20);require(a.hash==b.hash,"Actual same swarm fixture");ports_={a.peerPort,b.peerPort};}
};
struct Host {
 app::SessionBridge bridge;QSignalSpy rows,native,overall,closed,messages,sessions;QString commandFailure;
 explicit Host(const std::vector<std::pair<std::string,int>>& bootstrap):bridge(nullptr,{},{},{},{},{},{},{},{},{},{},{},bootstrap),rows(&bridge,&app::SessionBridge::snapshotReady),native(&bridge,&app::SessionBridge::nativeSessionReady),overall(&bridge,&app::SessionBridge::overallReady),closed(&bridge,&app::SessionBridge::sessionClosed),messages(&bridge,&app::SessionBridge::messageReady),sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady){}
 ui::Presentation snapshot()const{return rows.empty()?ui::Presentation{}:qvariant_cast<ui::Presentation>(rows.last().at(0));}
 ui::NativeSessionPresentation sample()const{return native.empty()?ui::NativeSessionPresentation{}:qvariant_cast<ui::NativeSessionPresentation>(native.last().at(0));}
 ui::OverallPresentation totals()const{return overall.empty()?ui::OverallPresentation{}:qvariant_cast<ui::OverallPresentation>(overall.last().at(0));}
 std::optional<ui::LibraryItem> row(const Profile& p)const{const auto view=snapshot();if(view){for(const auto& item:view->items){if(item.hash==QString::fromStdString(p.hash.hex())){return item;}}}return {};}
 bool ready(const Profile& p)const{return !bridge.busy()&&row(p).has_value()&&sample()&&sample()->sample&&totals()&&totals()->available;}
 ui::Target target(const Profile& p)const{const auto view=snapshot();require(bool(view),"Actual Host snapshot");return {QString::fromStdString(p.hash.hex()),view->revision};}
 QString diagnostic()const{QStringList out;for(const auto& message:messages){out.push_back(message[0].toString().left(256));}if(const auto value=totals();value&&!value->failure.isEmpty()){out.push_back(value->failure);}if(!commandFailure.isEmpty()){out.push_back(commandFailure);}return out.join(" | ").right(1536);}
};
inline bool noPortMapping(const Profile& p){const auto doc=document(p.path+"/biglybt.config");auto policy=engine::readNetworkPolicy(&doc,{.mode=engine::NetworkMode::Normal,.portMapping=true,.mldhtDirectory="mlDHT",.context=engine::NetworkPolicyContext::ExistingLibraryPublic});if(!policy||policy->upnp||policy->natpmp){return false;}auto settings=engine::makeNetworkSettings(*policy);return settings&&!settings->get_bool(lt::settings_pack::enable_upnp)&&!settings->get_bool(lt::settings_pack::enable_natpmp);}
inline core::LibrarySnapshot saved(const Profile& p){auto parsed=core::readLibrary(document(p.path+"/downloads.config"));require(bool(parsed),"Actual compatible library");return std::move(*parsed);}
inline bool paused(const Profile& p){return !saved(p).paused.empty();}
inline std::int64_t seedSeconds(const Profile& p){const auto doc=document(p.path+"/downloads.config");const auto* rows=doc.find("downloads");require(rows&&rows->children.size()==1,"Actual one-row duration source");const auto* seconds=rows->children[0].find("secondsOnlySeeding");require(seconds&&seconds->type==core::Node::Type::Integer,"Actual saved seeding duration");return seconds->integer;}
inline bool forced(const Profile& p){const auto view=saved(p);require(view.downloads.size()==1,"Actual one-row library");return view.downloads[0].forceStart;}
inline bool stopped(const Profile& p){const auto view=saved(p);require(view.downloads.size()==1,"Actual one-row library");return view.downloads[0].startup==core::StartupState::Stopped;}
inline bool incoming(bool v6,unsigned short listenPort){
 QFile file(v6?"/proc/net/tcp6":"/proc/net/tcp");if(!file.open(QIODevice::ReadOnly)){return false;}const auto bytes=file.read(1024*1024);for(const auto& line:bytes.split('\n')){const auto parts=line.simplified().split(' ');if(parts.size()<4||parts[3]!="01"){continue;}const auto local=parts[1].split(':'),remote=parts[2].split(':');if(local.size()!=2||remote.size()!=2){continue;}bool ok=false;const auto value=local[1].toUShort(&ok,16);if(ok&&value==listenPort&&remote[1]!="0000"){return true;}}return false;
}
inline bool unknownAndNetworksPreserved(const Profile& p){const auto state=document(p.state);const auto* attrs=state.find("attributes");return attrs&&attrs->find("opaque.daily")&&attrs->find("networks")&&raw(state,attrs->find("networks"))=="l6:Public3:I2P3:Tore"&&attrs->find("opaque.daily")->bytes==std::string_view("\0\xff" "attrs",7);}
inline bool closedAndReleased(const Host& h){return !h.bridge.busy()&&!h.closed.empty()&&h.closed.last()[1].toBool()&&h.closed.last()[2].toBool();}
} // namespace biglybtc::test::daily
