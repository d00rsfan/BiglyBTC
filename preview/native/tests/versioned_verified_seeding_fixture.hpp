#pragma once
#include "verified_seeding_fixture.hpp"
#include "torrent_creator.hpp"
#include "original_layout_projection.hpp"

namespace biglybtc::test {
// Actual production creator, actual file bytes and actual owner/receiver. No
// empty-file original-client boundary is hidden in these nonempty positives.
inline VerifiedSeedCase prepareVersionedVerifiedSeedCase(const QString& root,const QString& kind,bool corrupt=false) {
  require(kind=="v2-aligned"||kind=="v2-tail"||kind=="hybrid-v1"||kind=="hybrid-v2","Unknown versioned verification fixture");
  require(!QFileInfo::exists(root),"Versioned verification requires a fresh owned path");
  const bool hybrid=kind.startsWith(QStringLiteral("hybrid-"));
  VerifiedSeedCase result{root,root+"/profile",root+"/payload",root+"/source.torrent",{}, {}};
  require(QDir().mkpath(result.profile),"Create owned versioned profile");
  write(result.profile+"/downloads.config","d9:downloadsle14:unknown.futureli7e3:rawee");
  write(result.profile+"/tables.config","de");
  const auto append=[&](QString path,int count,int salt){
    QByteArray bytes(count,'\0');for(int n=0;n<count;++n){bytes[n]=static_cast<char>((n*31+salt)%251);}
    result.files.push_back({std::move(path),bytes,{}});write(result.payload+'/'+result.files.back().relative,bytes);
  };
  if(hybrid){append(QStringLiteral("bundle/a.bin"),20000,17);append(QStringLiteral("bundle/b.bin"),40003,29);}
  else{append(QStringLiteral("sample.bin"),kind=="v2-aligned"?65536:65537,17);}
  engine::CreateOptions options;options.source=(result.payload+(hybrid?"/bundle":"/sample.bin")).toStdString();
  options.output=result.metadata.toStdString();options.pieceSize=16384;
  options.format=hybrid?engine::CreateFormat::Hybrid:engine::CreateFormat::V2;
  const auto created=engine::createTorrent(options);
  require(created.has_value(),created?"":created.error().message.c_str());
  auto source=core::Document::parse(read(result.metadata).toStdString(),storage::compatibleConfigLimits());
  require(source.has_value(),"Own actual versioned creator metadata");
  const auto opaque=std::string("3:")+std::string("\0\xffQ",3);
  auto attrs=core::Document::parse("de");require(attrs.has_value(),"Own versioned fixture attributes");
  require(attrs->replaceValue("opaque.fixture",opaque).has_value()&&attrs->replaceValue("networks","l6:Publice").has_value()
      &&source->replaceValue("attributes",attrs->serialize()).has_value()&&source->replaceValue("opaque.fixture",opaque).has_value(),"Preserve owned opaque source values");
  if(kind=="hybrid-v2"){
    require(source->replaceValue("azureus_private_properties","d14:hybrid_hash_v2i1ee").has_value(),"Explicit original hybrid-v2 selector");
  }
  write(result.metadata,QByteArray::fromStdString(source->serialize()));
  lt::error_code error;const lt::torrent_info metadata(result.metadata.toStdString(),error);require(!error,"Actual versioned creator metadata parses");
  const auto identity=storage::originalTorrentIdentityFromLibtorrent(*source,metadata);require(identity.has_value(),"Exact versioned full raw identity");result.hash=identity->originalKey;
  const auto layout=compat::originalLayout(*source);require(layout.has_value(),"Actual versioned original layout");
  const auto projection=engine::detail::projectOriginalLayout(*layout,metadata);require(projection.has_value(),"Exact versioned original/engine projection");
  require(metadata.info_hashes().has_v2()&&metadata.info_hashes().has_v1()==hybrid,"Actual fixture format differs");
  require(projection->trailingPad.has_value()==(kind=="v2-tail"),"Actual pure-v2 tail must be the sole engine-only slot");
  require(std::ranges::any_of(layout->files,[](const auto& file){return file.padding;})==hybrid,"Hybrid fixture must contain declared virtual padding");
  require((identity->selected==storage::OriginalSelectedHash::V2)==(kind!="hybrid-v1"),"Actual original selected identity differs");
  for(auto& file:result.files){
    const auto path=result.payload+'/'+file.relative;
    if(corrupt && &file==&result.files.front()){auto bytes=file.expected;bytes[0]=static_cast<char>(bytes[0]^1);write(path,bytes);}
    require(::stat(path.toStdString().c_str(),&file.before)==0,"Pin actual complete versioned payload inode");
  }
  return result;
}
inline bool versionedPadPathsAbsent(const VerifiedSeedCase& fixture) {
  lt::error_code error;const lt::torrent_info metadata(fixture.metadata.toStdString(),error);require(!error,"Inspect actual virtual pad paths");
  for(int n=0;n<metadata.num_files();++n){const auto index=lt::file_index_t{n};if(!metadata.files().pad_file_at(index)){continue;}
    const auto relative=QString::fromStdString(metadata.files().file_path(index));
    for(const auto& base:{fixture.payload,fixture.root+"/receiver"}){const QFileInfo path(base+'/'+relative);if(path.exists()||path.isSymLink()){return false;}}
  }return true;
}
inline bool versionedRealSnapshotsIntact(const VerifiedSeedCase& fixture,bool corrupt=false) {
  if(!verifiedSeedBytesIntact(fixture,corrupt)){return false;}
  for(const auto& file:fixture.files){struct stat now{};
    if(::lstat((fixture.payload+'/'+file.relative).toStdString().c_str(),&now)!=0
        ||now.st_mtim.tv_sec!=file.before.st_mtim.tv_sec||now.st_mtim.tv_nsec!=file.before.st_mtim.tv_nsec
        ||now.st_ctim.tv_sec!=file.before.st_ctim.tv_sec||now.st_ctim.tv_nsec!=file.before.st_ctim.tv_nsec){return false;}
  }return true;
}
inline std::vector<compat::FilePolicy> versionedFilePolicies(const storage::TorrentIntakeMetadata& metadata) {
  std::vector<compat::FilePolicy> result;for(const auto& file:metadata.layout().files){result.push_back({file.padding,0});}return result;
}
} // namespace biglybtc::test
