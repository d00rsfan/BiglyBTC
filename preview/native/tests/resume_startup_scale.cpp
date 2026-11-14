// SPDX-License-Identifier: GPL-2.0-or-later
// Explicit generated-profile startup diagnostic; no real profile/Internet input.
#include "persistent_session.hpp"
#include "session_bridge.hpp"
#include "network_policy.hpp"
#include <QApplication>
#include <QTcpServer>
#include <QUdpSocket>
#include <QThread>
#include <set>
#include <algorithm>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <libtorrent/entry.hpp>
#include <libtorrent/bencode.hpp>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/statvfs.h>
#include <iostream>
using namespace biglybtc;
namespace {
void need(bool v,const char* why){if(!v){throw std::runtime_error(why);}}
std::string encode(const libtorrent::entry& e){std::string out;libtorrent::bencode(std::back_inserter(out),e);return out;}
void write(const QString& name,std::string_view raw){QFile f(name);need(f.open(QIODevice::WriteOnly)&&f.write(raw.data(),qsizetype(raw.size()))==qsizetype(raw.size()),"Generated metadata write");}
struct Torrent {core::TorrentHash hash;QString state,data;std::string expected;};
bool clean(const Torrent& t,int pieces,bool active=false){QFile file(t.state);need(file.open(QIODevice::ReadOnly),"Generated state read");auto doc=core::Document::parse(file.readAll().toStdString(),storage::compatibleConfigLimits());need(bool(doc),"Generated state parse");const auto* r=doc->find("resume"),*d=r?r->find("data"):nullptr,*p=d?d->find("resume data"):nullptr,*v=d?d->find("valid"):nullptr;auto expected=t.expected.empty()?std::string(std::size_t(pieces),'\1'):t.expected;const bool incomplete=expected.find('\0')!=std::string::npos;if(active&&incomplete){for(auto& piece:expected){if(piece==0){piece=2;}}}return p&&p->type==core::Node::Type::Bytes&&p->bytes==expected&&v&&v->type==core::Node::Type::Integer&&v->integer==(active&&incomplete?0:1);}
void allocationBound(const Torrent& t){struct stat s{};need(::stat(t.data.toUtf8().constData(),&s)==0,"Generated data stat");need(s.st_blocks<8192,"Unexpected allocation of clean sparse payload");struct statvfs fs{};need(::statvfs(t.data.toUtf8().constData(),&fs)==0&&std::uint64_t(fs.f_bavail)*fs.f_frsize>6ULL*1024*1024*1024,"Diagnostic disk reserve");}
}
int main(int argc,char** argv){QApplication app(argc,argv);try{
 const int count=argc>1?std::stoi(argv[1]):800,pieces=argc>2?std::stoi(argv[2]):513;const int mode=argc>3?std::stoi(argv[3]):1;const bool external=mode!=0;const bool partial=argc>4&&std::stoi(argv[4])!=0;const int rowPadding=argc>5?std::stoi(argv[5]):0;const int idleSeconds=argc>6?std::stoi(argv[6]):0;const bool batchClose=argc>7&&std::stoi(argv[7])!=0,faultClose=argc>8&&std::stoi(argv[8])!=0;need(idleSeconds>=0&&idleSeconds<=20,"Bounded steady diagnostic interval");need(rowPadding>=0&&rowPadding<=65536&&std::uint64_t(count)*rowPadding<=16*1024*1024,"Bounded opaque inventory padding");need(count>0&&count<=1200&&pieces>0&&pieces<=1024,"Bounded generated diagnostic arguments");constexpr int pieceBytes=8*1024*1024;const auto bytes=std::int64_t(pieceBytes)*pieces;const auto parent=qEnvironmentVariable("BIGLYBTC_STARTUP_FIXTURE_PARENT","/tmp");need(QFileInfo(parent).isDir()&&!QFileInfo(parent).isSymLink()&&QFileInfo(parent).canonicalFilePath()==parent,"Canonical generated diagnostic parent");QTemporaryDir root(parent+"/biglybtc-resume-startup-XXXXXX");const auto profile=root.path()+"/profile";need(root.isValid()&&QDir().mkpath(profile+"/active")&&QDir().mkpath(root.path()+"/payload"),"Generated profile root");QElapsedTimer elapsed;elapsed.start();std::vector<Torrent> torrents;libtorrent::entry inventory(libtorrent::entry::dictionary_type{});inventory["downloads"]=libtorrent::entry::list_type{};const auto pieceHash=QCryptographicHash::hash(QByteArray(pieceBytes,'\0'),QCryptographicHash::Sha1).toStdString();std::string pieceHashes;for(int n=0;n<pieces;++n){pieceHashes+=pieceHash;}
 for(int i=0;i<count;++i){const auto name=QString("generated-%1.bin").arg(i);Torrent t;t.expected=std::string(std::size_t(pieces),'\1');if(partial){std::fill(t.expected.begin()+pieces/2,t.expected.end(),'\0');}t.data=root.path()+"/payload/"+name;QFile payload(t.data);need(payload.open(QIODevice::ReadWrite)&&payload.resize(bytes),"Generated sparse payload");payload.close();allocationBound(t);libtorrent::entry state(libtorrent::entry::dictionary_type{});auto& info=state["info"];info["name"]=name.toStdString();info["length"]=bytes;info["piece length"]=pieceBytes;info["pieces"]=pieceHashes;const auto digest=QCryptographicHash::hash(QByteArray::fromStdString(encode(info)),QCryptographicHash::Sha1);auto hash=core::TorrentHash::parse(std::string_view(digest.data(),20));need(bool(hash),"Generated hash");t.hash=*hash;t.state=profile+"/active/"+QString::fromStdString(hash->hex())+".dat";state["attributes"]["networks"]=libtorrent::entry::list_type{libtorrent::entry("Public")};state["attributes"]["storetypes"]=libtorrent::entry::list_type{libtorrent::entry("L")};state["attributes"]["resumecomplete"]=partial?1:2;state["attributes"]["flags"]=8;auto& resume=state["resume"]["data"];resume["valid"]=1;resume["resume data"]=t.expected;resume["blocks"]=libtorrent::entry::dictionary_type{};write(t.state,encode(state));libtorrent::entry row(libtorrent::entry::dictionary_type{});row["torrent_hash"]=digest.toStdString();row["torrent"]=t.state.toStdString();row["save_dir"]=(root.path()+"/payload").toStdString();row["save_file"]=name.toStdString();row["persistent"]=1;row["state"]=75;row["position"]=i+1;row["completedbytes"]=partial?std::int64_t(pieces/2)*pieceBytes:bytes;row["downloaded"]=partial?std::int64_t(pieces/2)*pieceBytes:bytes;row["uploaded"]=0;if(rowPadding){row["opaque.scale"]=std::string(std::size_t(rowPadding),'x');}inventory["downloads"].list().push_back(std::move(row));torrents.push_back(std::move(t));}
 write(profile+"/downloads.config",encode(inventory));
 if(mode==2){
  QTcpServer reservation;QUdpSocket udp;need(reservation.listen(QHostAddress::LocalHost,0),"Generated Host TCP port");const auto port=reservation.serverPort();need(udp.bind(QHostAddress::LocalHost,port),"Generated Host UDP port");
  libtorrent::entry config(libtorrent::entry::dictionary_type{});
  config["Bind IP"]="127.0.0.1";config["Enforce Bind IP"]=1;config["IPV6 Enable Support"]=0;config["TCP.Listen.Port"]=port;config["UDP.Listen.Port"]=port;config["UDP.NonData.Listen.Port"]=port;config["UDP.NonData.Listen.Port.Same"]=1;config["peercontrol.udp.public.enable"]=0;config["Network Selection Default.Public"]=1;config["PluginInfo.mldht.enabled"]=0;config["Plugin.mlDHT.enable"]=0;config["Plugin.DHT.dht.enabled"]=0;config["Plugin.UPnP.upnp.enable"]=0;config["Plugin.UPnP.natpmp.enable"]=0;config["max active torrents"]=0;config["max downloads"]=0;config["StartStopManager_iRankType"]=0;config["StartStopManager_bAutoStart0Peers"]=0;
  write(profile+"/biglybt.config",encode(config));reservation.close();udp.close();
  app::SessionBridge bridge;std::set<QString> admitted;bool closed=false,joined=false,released=false;std::size_t reported=0;
  QObject::connect(&bridge,&app::SessionBridge::snapshotReady,&bridge,[&](const ui::Presentation& view){if(!view){return;}for(const auto& item:view->items){if(item.providerAdmission){admitted.insert(item.hash);}}if(admitted.size()/100>reported){reported=admitted.size()/100;std::cout<<"HOST_STARTUP_PROGRESS admitted="<<admitted.size()<<" elapsed_ms="<<elapsed.elapsed()<<'\n'<<std::flush;}});
  QObject::connect(&bridge,&app::SessionBridge::messageReady,&bridge,[](const QString& text){std::cerr<<"HOST_MESSAGE "<<text.toStdString()<<'\n';});
  QObject::connect(&bridge,&app::SessionBridge::sessionClosed,&bridge,[&](quint64,bool j,bool r){closed=true;joined=j;released=r;});
  need(bridge.openNormal(profile.toStdString()),"Generated Host open");while(admitted.size()<std::size_t(count)&&elapsed.elapsed()<900000){QCoreApplication::processEvents();QThread::msleep(5);}need(admitted.size()==std::size_t(count),"Host failed to admit the generated library");
  if(idleSeconds){
   rusage before{},after{};need(::getrusage(RUSAGE_SELF,&before)==0,"Steady CPU start");
   const auto begin=elapsed.elapsed(),end=begin+1000*idleSeconds;
   while(elapsed.elapsed()<end){QCoreApplication::processEvents();QThread::msleep(5);}
   need(::getrusage(RUSAGE_SELF,&after)==0,"Steady CPU finish");
   const auto micros=[](const timeval& t){return std::int64_t(t.tv_sec)*1000000+t.tv_usec;};
   std::cout<<"HOST_STEADY_CPU count="<<count<<" wall_ms="<<elapsed.elapsed()-begin
       <<" user_us="<<micros(after.ru_utime)-micros(before.ru_utime)
       <<" system_us="<<micros(after.ru_stime)-micros(before.ru_stime)<<'\n'<<std::flush;
  }
  int bad=0;for(const auto& t:torrents){if(!clean(t,pieces,partial)){++bad;}}std::cout<<"HOST_STARTUP_MAPS count="<<count<<" invalid="<<bad<<" elapsed_ms="<<elapsed.elapsed()<<'\n'<<std::flush;
  need(bridge.closeSession(),"Generated Host close requested");const auto end=elapsed.elapsed()+1000000;while(!closed&&elapsed.elapsed()<end){QCoreApplication::processEvents();QThread::msleep(5);}need(closed&&joined&&released,"Generated Host close joined");
  int badClosed=0;for(const auto& t:torrents){if(!clean(t,pieces)){++badClosed;}allocationBound(t);}std::cout<<"HOST_STARTUP_SCALE_RESULT count="<<count<<" pieces="<<pieces<<" invalid="<<bad<<" invalid_after_close="<<badClosed<<" elapsed_ms="<<elapsed.elapsed()<<'\n'<<std::flush;return bad||badClosed?3:0;
 }
 write(profile+"/biglybt.config","de");auto lease=storage::ProfileLease::acquire(profile.toStdString());need(bool(lease),"Generated lease");auto owner=engine::PersistentSession::create({301,302,303});need(bool(owner),"Qualified owner");engine::NetworkPolicy policy;policy.mode=engine::NetworkMode::Normal;policy.context=engine::NetworkPolicyContext::ExistingLibraryPublic;policy.bindAddresses={"127.0.0.1"};policy.listenPort=0;policy.incomingTcp=false;policy.outgoingTcp=false;need((*owner)->configureNetwork(policy).has_value(),"Diagnostic network policy");need((*owner)->configureExternalQueue(external).has_value(),"Diagnostic queue mode");need((*owner)->startIdle(*lease).has_value(),"Diagnostic idle owner");
 bool admissionFailed=false;for(int i=0;i<count;++i){need(clean(torrents[i],pieces),"UNREQUESTED_RECHECK_BEFORE_ADMISSION");auto admitted=(*owner)->startManaged(*lease,{torrents[i].hash,0,false,false});if(!admitted){std::cerr<<"ADMISSION_FAILED index="<<i<<" reason="<<admitted.error().message<<'\n';admissionFailed=true;break;}need(clean(torrents[i],pieces,partial),"UNREQUESTED_RECHECK_AFTER_ADMISSION");allocationBound(torrents[i]);if(i%25==0){need((*owner)->poll().has_value(),"Diagnostic owner poll");QCoreApplication::processEvents();}if(i%100==0){std::cout<<"STARTUP_PROGRESS admitted="<<i+1<<" elapsed_ms="<<elapsed.elapsed()<<'\n'<<std::flush;}}
 if(!admissionFailed){for(const auto& t:torrents){need(clean(t,pieces,partial),"UNREQUESTED_RECHECK_AFTER_LIBRARY_STARTUP");}}std::cout<<"STARTUP_MAPS_CLEAN count="<<count<<" elapsed_ms="<<elapsed.elapsed()<<'\n'<<std::flush;bool injected=false;
 if(faultClose){need(batchClose,"Batch fault requires batch mode");need((*owner)->configureSourcePublication(
  [](const core::TorrentHash&,const std::function<std::expected<void,storage::Error>()>& write){return write();},
  [&](const std::function<std::expected<void,storage::Error>()>& save)->std::expected<void,storage::Error>{
   if(!injected){injected=true;QFile f(profile+"/downloads.config");need(f.open(QIODevice::ReadOnly),"Generated late source read");auto doc=core::Document::parse(f.readAll().toStdString(),storage::compatibleConfigLimits());f.close();need(bool(doc)&&bool(doc->replaceValue("generated-late-source-edit","i1e")),"Generated late source change");write(profile+"/downloads.config",doc->serialize());}
   return save();
  }).has_value(),"Generated close publication boundary");}
 const auto closeBegin=elapsed.elapsed();
 const auto closed=(*owner)->close(std::chrono::steady_clock::now()+std::chrono::seconds(60),{},batchClose?engine::PersistentSession::CloseInventoryMode::BatchPublic:engine::PersistentSession::CloseInventoryMode::Individual);
 std::cout<<"CLOSE_ELAPSED_MS "<<elapsed.elapsed()-closeBegin<<'\n';
 if(faultClose){
  need(injected&&closed.checkpointFailure&&closed.checkpointFailure->code==storage::ErrorCode::Conflict,"Actual batch CAS failure must be reported");
  const auto rows=(*owner)->managedDownloads();const auto failed=std::ranges::count_if(rows,[](const auto& v){return v.failure&&v.phase==engine::ManagedPhase::Failed;});
  need(failed>0,"All staged owners must lose checkpoint success after failed batch");
  QFile f(profile+"/downloads.config");need(f.open(QIODevice::ReadOnly),"Generated retained source read");auto doc=core::Document::parse(f.readAll().toStdString(),storage::compatibleConfigLimits());need(doc&&doc->find("generated-late-source-edit"),"Late external inventory edit must survive");
  std::cout<<"CLOSE_EXPECTED_BATCH_CONFLICT failed_owners="<<failed<<'\n';
 }else{need(!closed.checkpointFailure,"Generated batch clean checkpoint");}std::cout<<"CLOSE_RECEIPT joined="<<closed.sessionJoined<<" storage_released="<<closed.retirement.storageReleased<<" counters_certified="<<bool(closed.retirement.peerCounters)<<" accounting_failure="<<bool(closed.retirement.accountingFailure)<<'\n';need(closed.sessionJoined&&closed.retirement.storageReleased,"Diagnostic close joined");if(closed.checkpointFailure){std::cerr<<"CHECKPOINT_FAILURE "<<closed.checkpointFailure->message<<'\n';}
 int bad=0;for(int i=0;i<count;++i){if(!clean(torrents[i],pieces)){if(bad<8){std::cout<<"UNREQUESTED_RECHECK_AFTER_CLOSE index="<<i<<'\n';}++bad;}}std::cout<<"STARTUP_SCALE_RESULT count="<<count<<" pieces="<<pieces<<" external_queue="<<external<<" invalid_after_close="<<bad<<" elapsed_ms="<<elapsed.elapsed()<<'\n'<<std::flush;return bad?3:(admissionFailed?2:!closed.retirement.peerCounters?4:0);
 }catch(const std::exception& error){std::cerr<<"DIAGNOSTIC_FAILED "<<error.what()<<'\n';return 1;}}
