#pragma once
#include "verified_seeding_fixture.hpp"
#include "selected_existing_scope.hpp"
#include "original_file_io.hpp"
#include "file_io.hpp"
#include <libtorrent/bencode.hpp>
#include <fcntl.h>
#include <array>
#include <cerrno>

namespace biglybtc::test {
struct SelectedSeedCase {
  VerifiedSeedCase value;
  std::vector<compat::FilePolicy> policy;
  std::vector<storage::OriginalStorageKind> kinds;
  std::vector<std::optional<QByteArray>> physical;
  std::vector<std::optional<struct stat>> stamps;
  std::vector<std::pair<QString,QByteArray>> controls;
};
inline std::string selectedEncode(const lt::entry& value){std::string raw;lt::bencode(std::back_inserter(raw),value);return raw;}
inline SelectedSeedCase prepareSelectedSeedCase(const QString& root,const QString& kind,bool corrupt=false){
  require(!QFileInfo::exists(root),"Selected case requires absent owned root");
  const bool v2=kind=="v2-missing",missing=kind=="linear-missing"||v2;
  const auto middleKind=kind=="compact-edges"?storage::OriginalStorageKind::Compact:kind=="reordered-edges"?storage::OriginalStorageKind::Reordered:kind=="reordered-compact-edges"?storage::OriginalStorageKind::ReorderedCompact:storage::OriginalStorageKind::Linear;
  require(missing||middleKind!=storage::OriginalStorageKind::Linear,"Unknown selected existing fixture");
  const QByteArray first(missing?16384:8192,'A'),middle(missing?32768:49152,'d'),last(missing?16384:8192,'B');const std::array<QByteArray,3> data{first,middle,last};
  lt::entry info;info["name"]="selected";info["piece length"]=16384;
  std::string layers;lt::entry::list_type files;const std::array<const char*,3> names{"a.bin","m.bin","z.bin"};
  if(v2){
    info["meta version"]=2;
    for(std::size_t n=0;n<data.size();++n){const auto& file=data[n];QByteArray leaves;for(qsizetype offset=0;offset<file.size();offset+=16384){leaves+=QCryptographicHash::hash(file.mid(offset,16384),QCryptographicHash::Sha256);}
      const auto hash=file.size()==16384?leaves:QCryptographicHash::hash(leaves,QCryptographicHash::Sha256);auto& leaf=info["file tree"][names[n]][""];leaf["length"]=file.size();leaf["pieces root"]=hash.toStdString();if(file.size()>16384){layers=std::to_string(hash.size())+':'+hash.toStdString()+std::to_string(leaves.size())+':'+leaves.toStdString();}
    }
  }else{
    const auto body=first+middle+last;std::string pieces;for(qsizetype offset=0;offset<body.size();offset+=16384){pieces+=QCryptographicHash::hash(body.mid(offset,16384),QCryptographicHash::Sha1).toStdString();}info["pieces"]=pieces;
    for(std::size_t n=0;n<data.size();++n){lt::entry row;row["length"]=data[n].size();row["path"]=lt::entry::list_type{lt::entry(names[n])};files.push_back(std::move(row));}info["files"]=std::move(files);
  }
  lt::entry torrent;torrent["info"]=info;torrent["opaque.fixture"]=std::string("\0\xffQ",3);torrent["attributes"]["opaque.fixture"]=std::string("\0\xffQ",3);torrent["attributes"]["displayname"]="selected-title";
  const auto code=middleKind==storage::OriginalStorageKind::Compact?"C":middleKind==storage::OriginalStorageKind::Reordered?"R":middleKind==storage::OriginalStorageKind::ReorderedCompact?"X":"L";
  torrent["attributes"]["storetypes"]=lt::entry::list_type{lt::entry("L"),lt::entry(code),lt::entry("L")};
  auto raw=core::Document::parse(selectedEncode(torrent),storage::compatibleConfigLimits());require(raw.has_value(),"Own selected metadata bytes");if(v2){require(raw->replaceValue("piece layers","d"+layers+'e').has_value(),"Own actual v2 piece layers");}
  require(QDir().mkpath(root+"/profile"),"Own selected profile");write(root+"/profile/downloads.config","d9:downloadsle14:unknown.futureli7e3:rawee");write(root+"/profile/tables.config","de");write(root+"/source.torrent",QByteArray::fromStdString(raw->serialize()));
  const auto preview=storage::inspectTorrentIntake((root+"/source.torrent").toStdString());require(preview.has_value(),"Seal actual selected fixture metadata");
  SelectedSeedCase fixture{{root,root+"/profile",root+"/payload",root+"/source.torrent",preview->hash(),{}},{},{},{},{},{}};
  require(preview->layout().files.size()==3,"Selected fixture has three original real slots");
  for(std::size_t n=0;n<data.size();++n){const auto& original=preview->layout().files[n];require(!original.padding&&original.path==names[n],"Exact original selected index order");fixture.value.files.push_back({QStringLiteral("selected/")+QString::fromLatin1(names[n]),data[n],{}});fixture.policy.push_back({n==1,0});fixture.kinds.push_back(n==1?middleKind:storage::OriginalStorageKind::Linear);fixture.physical.emplace_back();fixture.stamps.emplace_back();
    const auto path=fixture.value.payload+'/'+fixture.value.files.back().relative;if(n==1&&missing){continue;}
    if(n!=1){auto bytes=data[n];if(corrupt&&n==0){bytes[0]=static_cast<char>(bytes[0]^1);}write(path,bytes);}
    else{
      require(QDir().mkpath(QFileInfo(path).absolutePath()),"Own packed source parent");storage::detail::Descriptor fd(::open(path.toStdString().c_str(),O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));require(fd.get()>=0,"Own nonlinear selected source");
      const auto control=fixture.value.profile+"/active/"+QString::fromStdString(fixture.value.hash.hex())+'/'+QString::fromStdString(storage::originalControlLeaf(n,middleKind));std::optional<std::string> expected;
      auto io=storage::OriginalFileIo::open(fd.get(),{static_cast<std::uint64_t>(original.offset),static_cast<std::uint64_t>(original.bytes),16384},middleKind,{},[&](const auto& before,std::string_view after)->std::expected<void,storage::Error>{if(before!=expected){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Test control generation CAS"});}write(control,QByteArray(after.data(),static_cast<qsizetype>(after.size())));expected=std::string(after);return {};});require(io.has_value(),"Own packed codec fixture");
      // Only authentic boundary bytes are written. The interior remains absent
      // in C/R/X; an arbitrary reordered tail occupies the next available slot.
      const auto tail=data[n].last(8192),head=data[n].first(8192);require(io->write(static_cast<std::uint64_t>(original.bytes-8192),std::as_bytes(std::span(tail.constData(),static_cast<std::size_t>(tail.size())))).has_value(),"Write actual packed tail");require(io->write(0,std::as_bytes(std::span(head.constData(),static_cast<std::size_t>(head.size())))).has_value(),"Write actual packed prefix");require(io->setLength(static_cast<std::uint64_t>(original.bytes)).has_value()&&io->flush().has_value(),"Persist exact original sparse map");fixture.controls.push_back({control,read(control)});
    }
    struct stat stamp{};require(::lstat(path.toStdString().c_str(),&stamp)==0,"Pin selected input");fixture.stamps[n]=stamp;fixture.value.files[n].before=stamp;fixture.physical[n]=read(path);
  }
  return fixture;
}
inline auto importSelectedSeedCase(storage::ProfileLease& lease,const SelectedSeedCase& fixture,void(*observer)(storage::TorrentIntakeStage)=nullptr){
  auto preview=storage::inspectTorrentIntake(fixture.value.metadata.toStdString());if(!preview){return std::expected<core::TorrentHash,storage::Error>{std::unexpected(preview.error())};}
  return storage::importTorrentWithFileOptions(lease,*preview,fixture.value.payload.toStdString(),true,fixture.policy,observer,false,{}, {},false,false,false,{}, {},true,std::nullopt,core::ExistingVerificationGoal::SelectedFiles);
}
inline bool selectedSnapshotsIntact(const SelectedSeedCase& fixture){
  for(std::size_t n=0;n<fixture.value.files.size();++n){const auto path=fixture.value.payload+'/'+fixture.value.files[n].relative;struct stat now{};
    if(!fixture.stamps[n]){if(::lstat(path.toStdString().c_str(),&now)==0||errno!=ENOENT){return false;}continue;}
    const auto& before=*fixture.stamps[n];if(::lstat(path.toStdString().c_str(),&now)!=0||now.st_dev!=before.st_dev||now.st_ino!=before.st_ino||now.st_size!=before.st_size||now.st_mtim.tv_sec!=before.st_mtim.tv_sec||now.st_mtim.tv_nsec!=before.st_mtim.tv_nsec||now.st_ctim.tv_sec!=before.st_ctim.tv_sec||now.st_ctim.tv_nsec!=before.st_ctim.tv_nsec||read(path)!=*fixture.physical[n]){return false;}
  }for(const auto& [path,bytes]:fixture.controls){if(read(path)!=bytes||QFileInfo::exists(path+".saving")){return false;}}return true;
}
inline QByteArray receiveSelectedSeed(engine::PersistentSession& owner,const SelectedSeedCase& fixture){
  const auto port=owner.tcpListenPort();require(port&&*port,"Actual selected seed listener");lt::session receiver(localSettings());lt::error_code error;lt::add_torrent_params params;params.ti=std::make_shared<lt::torrent_info>(fixture.value.metadata.toStdString(),error);require(!error,"Selected receiver metainfo");
  params.save_path=(fixture.value.root+"/receiver").toStdString();params.flags&=~(lt::torrent_flags::auto_managed|lt::torrent_flags::paused);params.flags|=lt::torrent_flags::disable_dht|lt::torrent_flags::disable_lsd|lt::torrent_flags::disable_pex;params.file_priorities={lt::download_priority_t{4},lt::download_priority_t{0},lt::download_priority_t{4}};
  if(params.ti->info_hashes().has_v2()){const auto source=core::Document::parse(read(fixture.value.metadata).toStdString(),storage::compatibleConfigLimits());require(source.has_value(),"Own v2 selected receiver source");const auto identity=storage::originalTorrentIdentityFromLibtorrent(*source,*params.ti);require(identity&&engine::detail::selectManagedSwarm(params,*identity,engine::detail::managedPayloadCounterBridge()),"Exact v2 selected receiver admission");}
  auto handle=receiver.add_torrent(params,error);require(!error&&handle.is_valid(),"Actual selected receiver");handle.connect_peer(lt::tcp::endpoint(lt::address_v4::loopback(),*port));const auto until=std::chrono::steady_clock::now()+std::chrono::seconds{30};
  while(std::chrono::steady_clock::now()<until&&!handle.status().is_finished){require(owner.poll().has_value(),"Actual selected owner upload poll");std::vector<lt::alert*> alerts;receiver.pop_alerts(&alerts);QTest::qWait(10);}const auto actual=handle.status();require(actual.is_finished&&!actual.is_seeding,"Selected receiver must finish selected data while full torrent stays incomplete");
  QByteArray body;for(std::size_t n:{0U,2U}){const auto bytes=read(fixture.value.root+"/receiver/"+fixture.value.files[n].relative);require(bytes==fixture.value.files[n].expected,"Actual selected receiver payload differs");body+=bytes;}waitVerifiedUploadSample(owner,fixture.value.hash);return QCryptographicHash::hash(body,QCryptographicHash::Sha256).toHex();
}
}
