#pragma once
#include "owned_engine_fixture.hpp"
#include "torrent_intake.hpp"
#include "verified_seeding.hpp"
#include "persistent_session.hpp"
#include "managed_payload_counters.hpp"
#include "original_torrent_identity_lt.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>
#include <chrono>

namespace biglybtc::test {
struct VerifiedSeedCase {
  QString root,profile,payload,metadata;
  core::TorrentHash hash;
  std::vector<OwnedEngineFixture::File> files;
};
inline VerifiedSeedCase prepareVerifiedSeedCase(const QString& root,bool multi,bool corrupt=false) {
  require(!QFileInfo::exists(root),"Verified-seed case must be a fresh owned path");
  OwnedEngineFixture source(multi);
  VerifiedSeedCase result{root,root+"/profile",root+"/payload",root+"/source.torrent",*source.hash,source.files};
  require(QDir().mkpath(result.profile),"Create owned verification profile");
  write(result.profile+"/downloads.config","d9:downloadsle14:unknown.futureli7e3:rawee");
  write(result.profile+"/tables.config","de");
  write(result.metadata,read(source.stateFile));
  for(auto& file:result.files){
    auto data=file.expected;if(corrupt && &file==&result.files.front()){data[0]=static_cast<char>(data[0]^1);}
    const auto path=result.payload+'/'+file.relative;write(path,data);
    require(::stat(path.toStdString().c_str(),&file.before)==0,"Pin owned existing payload");
  }
  return result;
}
inline bool verifiedSeedBytesIntact(const VerifiedSeedCase& fixture,bool corrupt=false) {
  for(const auto& file:fixture.files){struct stat now{};const auto path=fixture.payload+'/'+file.relative;
    if(::lstat(path.toStdString().c_str(),&now)!=0||now.st_dev!=file.before.st_dev||now.st_ino!=file.before.st_ino
        ||now.st_size!=file.before.st_size){return false;}
    auto expected=file.expected;if(corrupt && &file==&fixture.files.front()){expected[0]=static_cast<char>(expected[0]^1);}
    if(read(path)!=expected){return false;}
  }return true;
}
inline std::expected<core::TorrentHash,storage::Error> importVerifiedSeedCase(storage::ProfileLease& lease,const VerifiedSeedCase& fixture) {
  const auto metadata=storage::inspectTorrentIntake(fixture.metadata.toStdString());if(!metadata){return std::unexpected(metadata.error());}
  std::vector<compat::FilePolicy> selected;
  for(const auto& file:metadata->layout().files){selected.push_back({file.padding,0});}
  return storage::importTorrentWithFileOptions(lease,*metadata,fixture.payload.toStdString(),true,selected,nullptr,false,{}, {},false,false,false,{}, {},true);
}
inline std::optional<engine::ManagedDownloadStatus> verifiedStatus(engine::PersistentSession& owner,const core::TorrentHash& hash) {
  for(auto& row:owner.managedDownloads()){if(row.hash==hash){return row;}}return {};
}
inline bool spinVerified(engine::PersistentSession& owner,const core::TorrentHash& hash) {
  const auto until=std::chrono::steady_clock::now()+std::chrono::seconds{30};
  while(std::chrono::steady_clock::now()<until){
    require(owner.poll().has_value(),"Actual verification owner poll failed");
    if(const auto row=verifiedStatus(owner,hash);row&&row->verifiedSeeding!=engine::VerifiedSeedingPhase::Checking){return true;}
    QTest::qWait(10);
  }return false;
}
// Real owned receiver; only one fixture alert consumer, no tracker/discovery.
inline QByteArray receiveVerifiedSeed(engine::PersistentSession& owner,const VerifiedSeedCase& fixture) {
  const auto port=owner.tcpListenPort();require(port.has_value()&&*port!=0,"Actual seed listener unavailable");
  lt::session receiver(localSettings());lt::error_code error;
  lt::add_torrent_params parameters;parameters.ti=std::make_shared<lt::torrent_info>(fixture.metadata.toStdString(),error);
  require(!error,"Owned receiver metadata invalid");parameters.save_path=(fixture.root+"/receiver").toStdString();
  parameters.flags&=~(lt::torrent_flags::auto_managed|lt::torrent_flags::paused);
  parameters.flags|=lt::torrent_flags::disable_dht|lt::torrent_flags::disable_lsd|lt::torrent_flags::disable_pex;
  std::optional<storage::OriginalTorrentIdentity> selected;
  if(parameters.ti->info_hashes().has_v2()){
    const auto source=core::Document::parse(read(fixture.metadata).toStdString(),storage::compatibleConfigLimits());
    require(source.has_value(),"Owned receiver original metadata invalid");
    const auto identity=storage::originalTorrentIdentityFromLibtorrent(*source,*parameters.ti);
    require(identity.has_value(),"Owned receiver full raw identity differs");selected=*identity;
    require(engine::detail::selectManagedSwarm(parameters,*selected,engine::detail::managedPayloadCounterBridge()),
        "Owned receiver requires the same qualified selected-swarm module");
  }
  auto handle=receiver.add_torrent(parameters,error);require(!error&&handle.is_valid(),"Admit owned receiver");
  if(selected){
    const auto full=parameters.ti->info_hashes();const bool v2=selected->selected==storage::OriginalSelectedHash::V2;
    require(handle.info_hashes()==full&&receiver.find_torrent(full.v2).is_valid()==v2
        &&(!full.has_v1()||receiver.find_torrent(full.v1).is_valid()==!v2),"Actual receiver is not exclusively in the selected full swarm");
  }
  handle.connect_peer(lt::tcp::endpoint(lt::address_v4::loopback(),*port));
  const auto until=std::chrono::steady_clock::now()+std::chrono::seconds{30};
  while(std::chrono::steady_clock::now()<until&&!handle.status().is_seeding){
    require(owner.poll().has_value(),"Actual seed owner failed while uploading");
    std::vector<lt::alert*> alerts;receiver.pop_alerts(&alerts);QTest::qWait(10);
  }
  require(handle.status().is_seeding,"Owned receiver did not obtain all actual pieces");
  QByteArray all;for(const auto& file:fixture.files){const auto actual=read(fixture.root+"/receiver/"+file.relative);require(actual==file.expected,"Owned receiver bytes differ");all+=actual;}
  return QCryptographicHash::hash(all,QCryptographicHash::Sha256).toHex();
}
}
