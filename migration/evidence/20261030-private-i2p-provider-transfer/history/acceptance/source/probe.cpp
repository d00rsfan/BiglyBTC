// Test-only explicit two-owner transfer. No Host/backend/default activation.
#include "owner.hpp"
#include "profile.hpp"
#include "biglybtc/library.hpp"
#include <libtorrent/bencode.hpp>
#include <libtorrent/entry.hpp>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
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
#include <atomic>
#include <chrono>
#include <thread>

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
    row["persistent"]=1;row["state"]=75;row["position"]=1;row["completedbytes"]=seed?PayloadBytes:0;
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
  void replaceKey(){SecretBytes secret{read(key,1024)};write(key,secret.bytes);}
};
cp::SessionOptions zeroHop(){return {{"inbound.length","0"},{"outbound.length","0"},{"inbound.lengthVariance","0"},{"outbound.lengthVariance","0"},{"inbound.quantity","1"},{"outbound.quantity","1"},{"inbound.backupQuantity","0"},{"outbound.backupQuantity","0"},{"inbound.allowZeroHop","true"},{"outbound.allowZeroHop","true"},{"i2cp.leaseSetType","3"},{"i2cp.leaseSetEncType","4"},{"i2cp.dontPublishLeaseSet","false"},{"i2cp.fastReceive","true"},{"i2cp.messageReliability","none"}};}

// Only the test worker acquires this genuine same-OFD cooperative gate. The
// QObject thread keeps pumping while source permits cannot complete.
struct SourceHold {
  std::atomic<bool> held{false},failed{false};std::jthread worker;
  explicit SourceHold(std::shared_ptr<const biglybtc::storage::ProfileLease> lease)
      :worker([this,lease=std::move(lease)](std::stop_token stop){try{
    auto access=lease->sourceAccess();if(!access){failed=true;return;}
    held=true;while(!stop.stop_requested()){std::this_thread::sleep_for(std::chrono::milliseconds(1));}held=false;
  }catch(...){failed=true;}}){}
};
class BridgeControls {
  using Observation=provider::OwnedTorrent::Observation;
  enum class Stage {WaitActive,WrongCaps,WaitAccepted,WaitGate,HeldExpiry,Done};
  struct Client {QTcpSocket tcp;Observation before;bool sent=false,done=false;};
  Stage stage=Stage::WaitActive;std::array<Client,2> wrong;
  QTcpSocket idle;Observation beforeIdle;std::size_t originalSockets=0;
  QElapsedTimer clock;std::unique_ptr<SourceHold> hold;
  static void sameAuthority(const Observation& now,const Observation& before){
    require(now.phase==provider::OwnedTorrent::Phase::Active&&now.admission==before.admission&&now.generation==before.generation&&now.publicIdentity==before.publicIdentity,"Foreign client changed owner authority");
  }
public:
  // Caller closes both owners before destroying controls, releasing any Ready
  // source permit if the worker is still waiting to acquire the gate.
  bool tick(const Observation& a,const Observation& b,const biglybtc::storage::ProfileLease& lease){
    const std::array<Observation,2> actual{a,b};
    if(stage==Stage::Done){return true;}
    if(stage==Stage::WaitActive){
      if(a.phase!=provider::OwnedTorrent::Phase::Active||b.phase!=provider::OwnedTorrent::Phase::Active){return false;}
      for(std::size_t n=0;n<wrong.size();++n){
        require(actual[n].bridgePort&&actual[n].admission&&actual[n].generation,"Actual bridge tuple/authority absent");
        auto& item=wrong[n];item.before=actual[n];item.tcp.setProxy(QNetworkProxy::NoProxy);item.tcp.connectToHost(QHostAddress::LocalHost,actual[n].bridgePort);
      }clock.start();stage=Stage::WrongCaps;
    }
    if(stage==Stage::WrongCaps){
      require(clock.elapsed()<5000,"Wrong-capability clients did not close in time");
      for(std::size_t n=0;n<wrong.size();++n){auto& item=wrong[n];sameAuthority(actual[n],item.before);
        if(!item.sent&&item.tcp.state()==QAbstractSocket::ConnectedState){
          const auto line=QByteArray("HELLO VERSION MIN=3.1 MAX=3.1 BIGLYBTC_OWNER=")+QByteArray(64,'0')+" GENERATION="+QByteArray::number(static_cast<qulonglong>(item.before.generation))+'\n';
          require(item.tcp.write(line)==line.size(),"Cannot send bounded foreign-capability control");item.sent=true;
        }
        require(item.tcp.bytesAvailable()==0,"Foreign capability received protocol success bytes");
        item.done=item.sent&&item.tcp.state()==QAbstractSocket::UnconnectedState&&actual[n].rejectedClients>item.before.rejectedClients;
      }
      if(!wrong[0].done||!wrong[1].done){return false;}
      beforeIdle=a;originalSockets=a.bridgeSockets;idle.setProxy(QNetworkProxy::NoProxy);idle.connectToHost(QHostAddress::LocalHost,a.bridgePort);
      clock.restart();stage=Stage::WaitAccepted;
    }
    sameAuthority(a,beforeIdle);
    if(stage==Stage::WaitAccepted){
      require(clock.elapsed()<2000,"Extra HELLO client was not actually accepted");
      if(idle.state()!=QAbstractSocket::ConnectedState||a.bridgeSockets<=originalSockets){return false;}
      auto retained=lease.retain();require(retained.has_value(),"Cannot retain actual profile OFD for held-source control");
      hold=std::make_unique<SourceHold>(std::move(*retained));stage=Stage::WaitGate;
    }
    if(stage==Stage::WaitGate){
      require(!hold->failed&&clock.elapsed()<2500,"Actual source gate was not held before HELLO expiry");
      if(!hold->held){return false;}
      require(idle.state()==QAbstractSocket::ConnectedState&&a.rejectedClients==beforeIdle.rejectedClients,"HELLO control expired before source gate was held");stage=Stage::HeldExpiry;
    }
    if(stage==Stage::HeldExpiry){
      require(hold->held&&!hold->failed,"Source gate released before HELLO rejection");
      require(clock.elapsed()<7000,"HELLO expiry waited behind a held source gate");
      require(idle.bytesAvailable()==0,"Unauthenticated HELLO received success bytes");
      if(idle.state()!=QAbstractSocket::UnconnectedState||a.rejectedClients<=beforeIdle.rejectedClients){return false;}
      hold.reset();stage=Stage::Done;return true;
    }
    return false;
  }
};
}

int main(int argc,char** argv){
  QCoreApplication app(argc,argv);const auto args=app.arguments();if(args.size()!=3){std::cerr<<"probe RANDOM_I2CP_PORT /work\n";return 2;}
  bool valid=false;const auto port=args[1].toUInt(&valid);if(!valid||port<1024||port>65535||args[2]!="/work"){return 2;}
  const auto work=args[2];QJsonObject result{{"status","FAIL"},{"scope","two-actual-owned-provider-I2P-torrents"}};
  std::unique_ptr<provider::OwnedTorrent> seed,leech;std::unique_ptr<BridgeControls> controls;QTimer poll;QElapsedTimer elapsed;elapsed.start();bool done=false,stopped=false;int code=1;
  const auto shutdown=[&]{if(stopped){return;}stopped=true;if(seed){seed->close();}if(leech){leech->close();}controls.reset();if(seed){seed->join();}if(leech){leech->join();}};
  const auto finish=[&](bool ok,const QString& reason){
    if(done){return;}done=true;poll.stop();shutdown();result.insert("status",ok?"PASS":"FAIL");result.insert("reason",reason);result.insert("elapsed_ms",elapsed.elapsed());
    try{output(work+"/native-result.json",result);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';ok=false;}code=ok?0:1;app.quit();
  };
  try{
    const auto all=expectedPayload();const auto tracker=QString::fromLatin1(read(work+"/tracker.url",256));
    require(tracker.startsWith("http://")&&tracker.endsWith(".b32.i2p/announce")&&!tracker.contains('\n'),"Exact generated I2P tracker URL required");
    Fixture a(work,"seed",true,tracker,all),b(work,"leech",false,tracker,all);require(a.hash==b.hash&&a.identity!=b.identity,"Distinct original identities/same torrent required");
    write(work+"/torrent.infohash",QByteArray::fromHex(QByteArray::fromStdString(a.hash.hex())));
    auto aLease=biglybtc::storage::ProfileLease::acquire(a.profile.toStdString()),bLease=biglybtc::storage::ProfileLease::acquire(b.profile.toStdString());
    if(!aLease){throw std::runtime_error(aLease.error().message);}if(!bLease){throw std::runtime_error(bLease.error().message);}
    auto aOwner=provider::OwnedTorrent::prepare(*aLease,a.hash),bOwner=provider::OwnedTorrent::prepare(*bLease,b.hash);
    if(!aOwner){throw std::runtime_error(aOwner.error().message);}if(!bOwner){throw std::runtime_error(bOwner.error().message);}seed=std::move(*aOwner);leech=std::move(*bOwner);
    provider::OwnedTorrent::Options options;options.router={QHostAddress::LocalHost,static_cast<quint16>(port),"2.13.0",false,30000,false};options.session=zeroHop();options.ports={6881,6881};
    require(seed->begin(options)&&leech->begin(options),"Explicit actual owner startup refused");
    controls=std::make_unique<BridgeControls>();
    output(work+"/native-started.json",{{"status","ACTUAL_TWO_PROVIDER_OWNERS_STARTED"},{"seed_destination_sha256",hex(a.identity)},{"leech_destination_sha256",hex(b.identity)},{"torrent_infohash",QString::fromStdString(a.hash.hex())}});
    bool replaced=false;std::uint64_t admission=0,generation=0;int afterWithdrawal=0;
    QObject::connect(&poll,&QTimer::timeout,&app,[&]{try{
      if(elapsed.elapsed()>150000){finish(false,"Absolute transfer/source-withdrawal budget exceeded");return;}
      const auto sa=seed->observation(),sb=leech->observation();
      require(sa.publicIdentity==a.identity&&sb.publicIdentity==b.identity,"Owner changed actual public identity");
      if(sa.phase==provider::OwnedTorrent::Phase::Withdrawn){finish(false,"Seed authority withdrew before completion");return;}
      if(!replaced){
        if(sb.phase==provider::OwnedTorrent::Phase::Withdrawn){finish(false,"Leech authority withdrew before completion");return;}
        const bool controlsDone=controls->tick(sa,sb,*aLease);
        if(controlsDone&&sa.phase==provider::OwnedTorrent::Phase::Active&&sb.phase==provider::OwnedTorrent::Phase::Active&&sa.seeding&&sb.seeding&&sa.verifiedBytes==PayloadBytes&&sb.verifiedBytes==PayloadBytes){
          require(sa.admission!=0&&sb.admission!=0&&sa.generation!=0&&sb.generation!=0,"Actual admissions/generations required");
          admission=sb.admission;generation=sb.generation;result.insert("seed_admission",QString::number(sa.admission));result.insert("leech_admission",QString::number(admission));result.insert("leech_generation",QString::number(generation));
          result.insert("verified_bytes",static_cast<int>(sb.verifiedBytes));result.insert("torrent_infohash",QString::fromStdString(a.hash.hex()));
          // Real named-inode source loss, with the same secret bytes. This is
          // not a sourceRevision DTO, cancellation injection, or key rotation.
          output(work+"/source-replacement-started.json",{{"status","ACTUAL_TRANSFER_COMPLETE_SOURCE_REPLACEMENT_STARTING"}});b.replaceKey();replaced=true;
        }
      }else if(sb.phase==provider::OwnedTorrent::Phase::Withdrawn){
        require(sb.admission==admission&&sb.generation==generation,"Source loss minted/rebound authority");
        // Process queued withdrawal callbacks, then join before raw disk IO.
        // This is a reentry/control check, not proof of a late HTTP reply.
        if(++afterWithdrawal<2){return;}shutdown();
        require(a.intact(all)&&b.intact(all),"Actual payload bytes/inode differ after provider and disk joins");
        result.insert("payload_sha256",QString::fromLatin1(QCryptographicHash::hash(all,QCryptographicHash::Sha256).toHex()));
        result.insert("seed_destination_sha256",hex(a.identity));result.insert("leech_destination_sha256",hex(b.identity));
        result.insert("both_payload_inodes_preserved",true);result.insert("source_replacement_withdrew",true);result.insert("no_new_admission_on_source_loss",true);
        result.insert("wrong_capability_rejected_both_owners",true);result.insert("held_source_hello_expired",true);result.insert("negative_controls_preserved_authority",true);
        result.insert("direct_peer_injection",false);result.insert("late_tracker_response_qualified",false);result.insert("joined_before_disk_read",true);
        finish(true,"Real tracker-discovered two-provider transfer and exact joined disk bytes");
      }
    }catch(const std::exception& e){finish(false,QString::fromUtf8(e.what()));}catch(...){finish(false,"Unexpected bounded acceptance resource failure");}});
    poll.start(25);app.exec();shutdown();return code;
  }catch(const std::exception& e){shutdown();result.insert("reason",QString::fromUtf8(e.what()));try{output(work+"/native-result.json",result);}catch(...){}std::cerr<<e.what()<<'\n';return 1;}
}
