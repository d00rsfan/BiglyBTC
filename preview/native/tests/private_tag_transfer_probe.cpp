#include "tag_rate_transport_network.hpp"
#include "profile_tag_rates.hpp"
#include "manual_tag_store.hpp"
#include "tag_constraint_worker.hpp"
// Test-only shared-profile, multi-torrent I2P acceptance. No default activation.
// Both explicit private services and controllers live on their own caller workers.
#include "owned_i2p_service.hpp"
#include "../services/i2p_client/identity.hpp"
#include "../services/i2cp_session/session.hpp"
#include "profile.hpp"
#include "biglybtc/library.hpp"
#include <libtorrent/bencode.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/session.hpp>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
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

namespace {thread_local libtorrent::session** capturedPrivateSdk=nullptr;}
extern "C" void realPrivateSdk(libtorrent::session*,libtorrent::session_params&&) asm("__real__ZN10libtorrent7sessionC1EONS_14session_paramsE");
extern "C" void wrappedPrivateSdk(libtorrent::session*,libtorrent::session_params&&) asm("__wrap__ZN10libtorrent7sessionC1EONS_14session_paramsE");
extern "C" void wrappedPrivateSdk(libtorrent::session* self,libtorrent::session_params&& params){realPrivateSdk(self,std::move(params));if(capturedPrivateSdk){*capturedPrivateSdk=self;}}
namespace i2p=biglybtc::services::i2p;
namespace cp=i2p::i2cp;
namespace lt=libtorrent;
namespace {
constexpr qsizetype PayloadBytes=2097152;

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
      row["maxdl"]=0;row["maxul"]=0;
      row["file_priorities"]=lt::entry::list_type{lt::entry(1)};inventory["downloads"].list().push_back(row);
    }write(profile+"/downloads.config",encode(inventory));writeSettings(seed,2);write(profile+"/tables.config",QByteArray("de"));
  }
  void writeSettings(bool seed,int downloads)const{
    (void)seed;
    lt::entry s(lt::entry::dictionary_type{});s["PluginInfo.azneti2phelper.enabled"]=1;s["Plugin.azneti2phelper.enable"]=1;s["Plugin.azneti2phelper.azi2phelper.pure.enabled"]=1;
    s["max downloads"]=downloads;s["max active torrents"]=4;s["min downloads"]=0;s["StartStopManager_iRankType"]=0;
    // Original selectable rank=None avoids inventing unknown swarm/idle facts.
    // Scrapes still carry only real tracker observations; unavailable bits stay visible.
    s["StartStopManager_iFirstPriority_Type"]=1;s["StartStopManager_iFirstPriority_ShareRatio"]=0;
    s["StartStopManager_bFirstPriority_ignore0Peer"]=0;s["StartStopManager_bIgnore0Peers"]=0;
    s["StartStopManager_iMinSeedingTime"]=0;s["StartStopManager_iMinSeedingTimeWhenActive"]=0;
    s["Enable Light Seeding"]=0;s["Light Seed Slots Reserved"]=0;s["Flexible Seed Slots"]=0;
    s["Max Download Speed KBs"]=0;s["Max Upload Speed KBs"]=0;
    s["Max Upload Speed Seeding KBs"]=0;s["enable.seedingonly.upload.rate"]=0;
    write(profile+"/biglybt.config",encode(s));
  }
  biglybtc::core::LibrarySnapshot saved()const{
    const auto raw=read(profile+"/downloads.config",1024*1024);auto d=biglybtc::core::Document::parse(raw.toStdString());require(d.has_value(),"Saved original inventory malformed");
    auto snapshot=biglybtc::core::readLibrary(*d);require(snapshot&&snapshot->issues.empty()&&snapshot->downloads.size()==3,"Saved original inventory lost exact three rows");return std::move(*snapshot);
  }
};
cp::SessionOptions zeroHop(){return {{"inbound.length","0"},{"outbound.length","0"},{"inbound.lengthVariance","0"},{"outbound.lengthVariance","0"},{"inbound.quantity","1"},{"outbound.quantity","1"},{"inbound.backupQuantity","0"},{"outbound.backupQuantity","0"},{"inbound.allowZeroHop","true"},{"outbound.allowZeroHop","true"},{"i2cp.leaseSetType","3"},{"i2cp.leaseSetEncType","4"},{"i2cp.dontPublishLeaseSet","false"},{"i2cp.fastReceive","true"},{"i2cp.messageReliability","none"}};}
std::string base32(const biglybtc::core::TorrentHash& hash){constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";std::string out;unsigned value=0,bits=0;for(auto byte:hash.bytes()){value=(value<<8)|std::to_integer<unsigned char>(byte);bits+=8;while(bits>=5){bits-=5;out+=alphabet[(value>>bits)&31];}}return out;}
void writeTags(const Fixture& f,bool seed){lt::entry tags(lt::entry::dictionary_type{});auto& record=tags["3"]["7"];record["n"]="Generated shared private rates";record["c"]["rl.up"]=seed?65536:0;record["c"]["rl.down"]=seed?0:32768;record["o"]=lt::entry::list_type{};for(std::size_t i=0;i<2;++i){record["o"].list().push_back(lt::entry(base32(f.torrents[i].hash)));}if(!seed){auto& automatic=tags["3"]["8"];automatic["n"]="Generated private automatic rate";automatic["c"]["rl.down"]=32768;automatic["c"]["pp.constraint"]=lt::entry::list_type{lt::entry("true"),lt::entry("am=0;")};automatic["o"]=lt::entry::list_type{};}write(f.profile+"/tag.config",encode(tags));}
class TagWorker {
  QThread thread_;QObject dispatch_;
  std::shared_ptr<const biglybtc::storage::ProfileLease> lease_;
  std::optional<biglybtc::storage::TagManagerStartupMode> startup_;
  std::unique_ptr<biglybtc::engine::ProfileTagRates> rates_;
  std::unique_ptr<lt::session> ordinary_;
  std::unique_ptr<app::OwnedI2pService> service_;
  lt::session* actualPrivate_=nullptr;
  std::map<std::string,std::shared_ptr<std::atomic_bool>> drains_;
  template<class F>void call(F&& f){std::exception_ptr error;require(QMetaObject::invokeMethod(&dispatch_,[&]{try{f();}catch(...){error=std::current_exception();}},Qt::BlockingQueuedConnection),"Actual caller worker unavailable");if(error){std::rethrow_exception(error);}}
public:
  TagWorker(){require(dispatch_.moveToThread(&thread_),"Cannot move caller worker");thread_.start();require(thread_.isRunning(),"Cannot start caller worker");}
  ~TagWorker(){try{close();call([&]{require(dispatch_.moveToThread(QCoreApplication::instance()->thread()),"Cannot return caller affinity");});}catch(...){}thread_.quit();thread_.wait();}
  void start(const biglybtc::storage::ProfileLease& lease,const std::vector<Torrent>& torrents,const app::I2pServiceOptions& options){call([&]{
    auto held=lease.retain();require(bool(held),"Actual profile retention");lease_=std::move(*held);auto mode=biglybtc::storage::TagManagerStartupMode::capture(*lease_);require(bool(mode),"Actual tag startup");startup_.emplace(std::move(*mode));
    auto opened=biglybtc::engine::ProfileTagRates::open(*lease_,*startup_);require(bool(opened),"Actual common controller");rates_=std::move(*opened);
    lt::settings_pack settings;settings.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");settings.set_str(lt::settings_pack::dht_bootstrap_nodes,"");for(int option:{lt::settings_pack::enable_dht,lt::settings_pack::enable_lsd,lt::settings_pack::enable_upnp,lt::settings_pack::enable_natpmp}){settings.set_bool(option,false);}ordinary_=std::make_unique<lt::session>(settings);require(bool(rates_->prepare(torrents[0].hash,*ordinary_)),"Actual second SDK cohort owner");
    capturedPrivateSdk=&actualPrivate_;auto started=app::OwnedI2pService::startWithTagRates(*lease_,torrents[0].hash,*rates_,options,{},&*startup_);capturedPrivateSdk=nullptr;if(!started){throw std::runtime_error(started.error().message);}require(actualPrivate_!=nullptr,"Actual private SDK constructor witness");service_=std::move(*started);
    auto added=service_->addWithTagRates(torrents[1].hash,*rates_);if(!added){throw std::runtime_error(added.error().message);}
  });}
  std::vector<app::OwnedI2pService::TorrentObservation> observations(){std::vector<app::OwnedI2pService::TorrentObservation> rows;call([&]{require(bool(service_),"Actual private service missing");rows=service_->observations();});return rows;}
  struct DrainTarget {lt::session_handle session;lt::torrent_handle handle;std::string hash;std::shared_ptr<std::atomic_bool> acknowledged;};
  std::vector<DrainTarget> drainTargets(){std::vector<DrainTarget> result;call([&]{
    require(actualPrivate_&&service_,"Actual retained private SDK");
    const auto handles=actualPrivate_->get_torrents();require(handles.size()==2,"Actual installed private SDK handles");
    for(const auto& handle:handles){const auto full=handle.info_hashes();require(full.has_v1(),"Actual private v1 identity");const auto hash=QByteArray(full.v1.data(),20).toHex().toStdString();
      auto at=drains_.find(hash);if(at==drains_.end()){at=drains_.emplace(hash,shared_transport_fixture::installDrain(handle)).first;}
      result.push_back({*actualPrivate_,handle,hash,at->second});
    }
  });return result;}
  QJsonArray classes(){QJsonArray result;call([&]{
    require(actualPrivate_!=nullptr,"Actual private SDK diagnostics");const auto handles=actualPrivate_->get_torrents();const lt::session_handle session=*actualPrivate_;
    shared_transport_fixture::network(session,[&]{const auto owner=session.native_handle();
      for(const auto& h:handles){const auto torrent=h.native_handle();if(!torrent){continue;}
        for(auto* peer:*torrent){QJsonArray groups;for(int index=0;index<peer->num_classes();++index){const auto id=peer->class_at(index);if(const auto* group=owner->peer_classes().at(id)){lt::peer_class_info info;group->get_info(&info);groups.push_back(QJsonObject{{"id",static_cast<int>(id)},{"name",QString::fromStdString(info.label)},{"upload",info.upload_limit},{"download",info.download_limit},{"actual_upload",group->channel[0].throttle()},{"actual_download",group->channel[1].throttle()}});}}
          lt::peer_info actual;peer->get_peer_info(actual);result.push_back(QJsonObject{{"groups",groups},{"send_quota",actual.send_quota},{"receive_quota",actual.receive_quota},{"read_state",static_cast<int>(actual.read_state)},{"write_state",static_cast<int>(actual.write_state)},{"upload_queue",static_cast<int>(peer->upload_queue().size())},{"pending_disk_read_bytes",actual.pending_disk_read_bytes},{"send_buffer_size",actual.used_send_buffer},{ "handshake",peer->in_handshake()},{"connecting",peer->is_connecting()},{"payload_down",static_cast<qint64>(actual.total_download)},{"payload_up",static_cast<qint64>(actual.total_upload)}});
        }
      }
    });
  });return result;}
  void exerciseAutomaticPrivateRates(const std::vector<Torrent>& torrents){call([&]{
    auto store=biglybtc::storage::ManualTagStore::open(*lease_);require(bool(store),"Actual automatic source store");auto snapshot=(*store)->snapshot();require(bool(snapshot),"Actual automatic source snapshot");
    auto actor=app::TagConstraintWorker::activate(*lease_,41001,*startup_);require(bool(actor),"Actual automatic creating-worker actor");
    auto library=std::make_shared<biglybtc::ui::LibraryPresentation>();library->revision=1;
    for(const auto& t:torrents){biglybtc::ui::LibraryItem row;row.hash=t.text();row.name=t.name;row.state=QStringLiteral("Downloading");row.bytes=PayloadBytes;row.completed=0;library->items.push_back(std::move(row));}
    app::ConstraintWorkerFrame frame;frame.session=41001;frame.library=library;unsigned writes=0;
    frame.privateRateWrite=[&](const auto& hash,std::uint64_t admission,std::uint64_t generation,std::span<const biglybtc::storage::ManualTagMembershipEdit> edits,const app::ConstraintWorkerFrame::PrivateRateValidation& validate){
      auto current=(*store)->snapshot();if(!current)return std::expected<biglybtc::storage::ManualTagCommit,biglybtc::storage::Error>{std::unexpected(current.error())};
      auto changed=service_->changeRateMembership(*rates_,**store,*current,hash,edits,validate,{},std::pair{admission,generation});if(changed&&changed->changed){++writes;}return changed;
    };
    for(unsigned turn=0;turn<12&&writes<2;++turn){++frame.sequence;frame.sourceRevision=lease_->sourceRevision();frame.i2p=service_->observations();
      for(const auto& actual:frame.i2p){auto row=std::find_if(library->items.begin(),library->items.end(),[&](const auto& item){return item.hash==QString::fromStdString(actual.hash.hex());});if(row!=library->items.end()){row->providerAdmission=actual.value.admission;row->providerGeneration=actual.value.generation;}}
      auto changed=(*actor)->step(**store,*snapshot,frame);if(!changed){throw std::runtime_error(changed.error().message);}*snapshot=changed->snapshot;
    }
    (*actor)->close();require(writes==2,"Actual private constant-rule actor must publish both joined membership CAS operations");
    auto current=(*store)->snapshot();require(bool(current),"Actual automatic result source");const auto* tags=current->tags();const auto row=std::ranges::find_if(tags->tags,[](const auto& tag){return tag.id==8;});require(row!=tags->tags.end()&&row->uniqueHashes.size()==2,"Automatic private memberships match both actual subjects");
    auto cleared=service_->updateTagSource(*rates_,[&]()->std::expected<void,biglybtc::storage::Error>{auto saved=(*store)->apply(*current,biglybtc::core::SavedTagSetRateLimit{8,false,0});if(!saved)return std::unexpected(saved.error());return {};});require(bool(cleared)&&service_->sourceCurrent(),"Actual automatic rate/source retirement before measured window");
  });}
  void limit(int bytes){call([&]{
    auto store=biglybtc::storage::ManualTagStore::open(*lease_);require(bool(store),"Actual edit store");auto snapshot=(*store)->snapshot();require(bool(snapshot),"Actual edit preimage");
    auto updated=service_->updateTagSource(*rates_,[&]()->std::expected<void,biglybtc::storage::Error>{auto saved=(*store)->apply(*snapshot,biglybtc::core::SavedTagSetRateLimit{7,false,bytes});if(!saved){return std::unexpected(saved.error());}return {};});if(!updated){throw std::runtime_error(updated.error().message);}require(service_->sourceCurrent(),"Refreshed private source remains current");
  });}
  void close(){if(!thread_.isRunning()){return;}call([&]{
    if(service_){auto result=service_->close();const bool joined=service_->joined();service_.reset();actualPrivate_=nullptr;drains_.clear();require(joined,"Actual private source/SDK joins");if(!result){throw std::runtime_error(result.error().message);}}
    if(rates_){require(bool(rates_->close()),"Actual common root close");rates_.reset();}ordinary_.reset();startup_.reset();lease_.reset();
  });}
};
void orderedDrain(TagWorker& sender,TagWorker& receiver){
 const auto outgoing=sender.drainTargets();const auto incoming=receiver.drainTargets();
 for(const auto& source:outgoing){const auto at=std::find_if(incoming.begin(),incoming.end(),[&](const auto& target){return target.hash==source.hash;});require(at!=incoming.end(),"Exact private drain identity");at->acknowledged->store(false,std::memory_order_release);try{shared_transport_fixture::drainPriorPayload(source.session,source.handle,at->acknowledged);}catch(...){qCritical().noquote()<<"DRAIN_SENDER"<<QJsonDocument(sender.classes()).toJson(QJsonDocument::Compact);qCritical().noquote()<<"DRAIN_RECEIVER"<<QJsonDocument(receiver.classes()).toJson(QJsonDocument::Compact);throw;}}
}
std::int64_t downloaded(const std::vector<app::OwnedI2pService::TorrentObservation>& rows){std::int64_t total=0;require(rows.size()==2,"Exactly two retained private targets");for(const auto& row:rows){require(row.value.failure.empty()&&row.value.phase!=app::OwnedI2pService::Phase::Withdrawn,"Actual private member withdrew");if(row.value.payloadDownloaded<0){return -1;}total+=row.value.payloadDownloaded;}return total;}
}
int main(int argc,char** argv){QCoreApplication application(argc,argv);if(argc!=3){return 2;}bool ok=false;const int port=QString::fromLocal8Bit(argv[1]).toInt(&ok);if(!ok||port<1024||port>65535){return 2;}const QString work=QString::fromLocal8Bit(argv[2]);
 std::unique_ptr<TagWorker> seed,leech;std::optional<Fixture> a,b;std::optional<biglybtc::storage::ProfileLease> seedLease,leechLease;QTimer poll;QElapsedTimer elapsed;elapsed.start();int stage=0;bool done=false;std::int64_t baseline=0,disabledDelta=0,rateBytes=0; qint64 stageStarted=0;QJsonObject result;int code=1;
 const auto finish=[&](bool success,const QString& reason){if(done){return;}done=true;poll.stop();try{if(leech){leech->close();leech.reset();}if(seed){seed->close();seed.reset();}seedLease.reset();leechLease.reset();if(success){for(std::size_t i=0;i<3;++i){require(a->torrents[i].intact(true),"Seed payload/inode changed");require(b->torrents[i].intact(i<2),"Actual leech verified bytes/inode or stopped sentinel changed");}auto x=biglybtc::storage::ProfileLease::acquire(a->profile.toStdString()),y=biglybtc::storage::ProfileLease::acquire(b->profile.toStdString());require(bool(x)&&bool(y),"Actual profile locks released after joins");}}catch(std::exception const& error){success=false;result.insert("close_error",QString::fromUtf8(error.what()));}result.insert("status",success?"PASS":"FAIL");result.insert("reason",reason);result.insert("stage",stage);result.insert("elapsed_ms",elapsed.elapsed());result.insert("disabled_payload_delta",static_cast<qint64>(disabledDelta));result.insert("limited_window_payload_bytes",static_cast<qint64>(rateBytes));result.insert("verified_bytes",success?2*PayloadBytes:0);result.insert("joined_before_disk_read",success);result.insert("payload_inodes_and_stopped_sentinel_preserved",success);output(work+"/native-result.json",result);code=success?0:1;application.exit(code);};
 try{
  const QString tracker=QString::fromLatin1(read(work+"/tracker.url",256));require(tracker.startsWith("http://")&&tracker.endsWith(".b32.i2p/announce"),"Owned tracker URL");a.emplace(work,"seed",true,tracker);b.emplace(work,"leech",false,tracker);writeTags(*a,true);writeTags(*b,false);
  QByteArray hashes;for(std::size_t i=0;i<3;++i){require(a->torrents[i].hash==b->torrents[i].hash,"Actual metadata mismatch");hashes+=QByteArray::fromHex(a->torrents[i].text().toLatin1());}write(work+"/torrent.hashes",hashes);
  auto x=biglybtc::storage::ProfileLease::acquire(a->profile.toStdString()),y=biglybtc::storage::ProfileLease::acquire(b->profile.toStdString());require(bool(x)&&bool(y),"Actual generated profile ownership");seedLease.emplace(std::move(*x));leechLease.emplace(std::move(*y));app::I2pServiceOptions options;options.routerPort=static_cast<quint16>(port);options.sessionOptions=zeroHop();options.peerPort=6881;
  seed=std::make_unique<TagWorker>();leech=std::make_unique<TagWorker>();seed->start(*seedLease,a->torrents,options);leech->start(*leechLease,b->torrents,options);
  output(work+"/native-started.json",{{"status","ACTUAL_TAGGED_PRIVATE_SERVICES_STARTED"},{"seed_destination_sha256",hex(a->identity)},{"leech_destination_sha256",hex(b->identity)},{"active_hashes",2},{"saved_hashes",3}});
  QObject::connect(&poll,&QTimer::timeout,&application,[&]{try{
   if(elapsed.elapsed()>600000){finish(false,"Actual private tag transfer deadline");return;}const auto rows=leech->observations();const auto seeds=seed->observations();const auto bytes=downloaded(rows);const auto sent=downloaded(seeds);if(bytes<0||sent<0){return;}output(work+"/native-progress.json",{{"stage",stage},{"payload_downloaded",static_cast<qint64>(bytes)},{"elapsed_ms",elapsed.elapsed()}});
   if(stage==0&&QFile::exists(work+"/cache-precondition.json")&&std::all_of(rows.begin(),rows.end(),[](const auto& row){return row.value.payloadDownloaded>=32768;})){leech->exerciseAutomaticPrivateRates(b->torrents);result.insert("automatic_private_rule_joined_cas",true);leech->limit(-1);orderedDrain(*leech,*seed);orderedDrain(*seed,*leech);baseline=downloaded(leech->observations());require(baseline>=bytes,"Actual post-fence counter sample");result.insert("payload_during_disabled_transition",static_cast<qint64>(baseline-bytes));stageStarted=elapsed.elapsed();stage=1;}
   else if(stage==1&&elapsed.elapsed()-stageStarted>=2000){disabledDelta=bytes-baseline;require(disabledDelta==0,"Disabled private tag admitted new payload after ordered request/data drain");leech->limit(16384);baseline=downloaded(leech->observations());require(baseline>=bytes,"Actual resumed policy fence sample");stageStarted=elapsed.elapsed();stage=2;}
   else if(stage==2&&elapsed.elapsed()-stageStarted>=15000){rateBytes=bytes-baseline;const auto millis=elapsed.elapsed()-stageStarted;result.insert("actual_leech_peer_classes",leech->classes());result.insert("limited_window_ms",millis);result.insert("limited_window_allowed_bytes",static_cast<qint64>(16384*millis/1000+65536));result.insert("limited_window_before_bytes",static_cast<qint64>(baseline));result.insert("limited_window_after_bytes",static_cast<qint64>(bytes));require(rateBytes>0&&rateBytes<=16384*millis/1000+65536,"Actual aggregate private tag budget/recovery failed");leech->limit(0);stage=3;}
   else if(stage==3&&std::all_of(rows.begin(),rows.end(),[](const auto& row){return row.value.checked&&row.value.seeding&&row.value.verifiedBytes==PayloadBytes;})){require(bytes>=2*PayloadBytes,"Actual private transfer counters incomplete");finish(true,"Actual shared private tag changes,Disabled,recovery,last limit removal and verified payload passed");}
  }catch(std::exception const& error){finish(false,QString::fromUtf8(error.what()));}});poll.start(100);
 }catch(std::exception const& error){finish(false,QString::fromUtf8(error.what()));return code;}
 application.exec();return code;
}
