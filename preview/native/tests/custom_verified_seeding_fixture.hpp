#pragma once
#include "versioned_verified_seeding_fixture.hpp"
#include "verified_existing_scope.hpp"

namespace biglybtc::test {
inline std::string customNodeBytes(const core::Node& node){auto value=core::encodeValue(node,storage::compatibleConfigLimits());require(value.has_value(),"Own custom fixture node");return *value;}
inline QString customVerifiedLeaf(const QString& kind){return kind=="single"||kind=="v2-tail"?QStringLiteral("chosen.bin"):QStringLiteral("chosen-root");}
inline VerifiedSeedCase prepareCustomVerifiedSeedCase(const QString& root,const QString& kind,bool corrupt=false){
  require(kind=="single"||kind=="multi"||kind=="one-multi"||kind=="v2-tail","Unknown custom existing fixture");
  const auto leaf=customVerifiedLeaf(kind);
  if(kind=="v2-tail"){
    auto result=prepareVersionedVerifiedSeedCase(root,kind,corrupt);auto& file=result.files.front();
    require(QFile::rename(result.payload+'/'+file.relative,result.payload+'/'+leaf),"Move generated v2 fixture before approval");file.relative=leaf;
    require(::stat((result.payload+'/'+file.relative).toStdString().c_str(),&file.before)==0,"Pin renamed versioned fixture");return result;
  }
  require(!QFileInfo::exists(root),"Fresh custom fixture root required");OwnedEngineFixture original(kind!="single");
  VerifiedSeedCase result{root,root+"/profile",root+"/payload",root+"/source.torrent",{},original.files};
  require(QDir().mkpath(result.profile),"Create custom profile");write(result.profile+"/downloads.config","d9:downloadsle14:unknown.futureli7e3:rawee");write(result.profile+"/tables.config","de");
  auto source=core::Document::parse(read(original.stateFile).toStdString(),storage::compatibleConfigLimits());require(source.has_value(),"Actual original fixture metainfo");
  if(kind=="one-multi"){
    auto info=core::Document::parse(customNodeBytes(*source->find("info")));require(info.has_value(),"Own one-entry multifile info");
    QByteArray all;for(const auto& file:result.files){all+=file.expected;}
    const auto list="ld6:lengthi"+std::to_string(all.size())+"e4:pathl7:one.bineee";
    require(info->replaceValue("files",list).has_value()&&source->replaceValue("info",info->serialize()).has_value(),"One-entry multifile changes neither body nor piece hashes");
    result.files={{leaf+"/one.bin",all,{}}};
  }else{for(auto& file:result.files){file.relative=kind=="single"?leaf:leaf+file.relative.mid(QStringLiteral("bundle").size());}}
  auto attrs=core::Document::parse(customNodeBytes(*source->find("attributes")));require(attrs.has_value(),"Own source attributes");
  require(attrs->replaceValue("displayname","14:retained-title").has_value()&&source->replaceValue("attributes",attrs->serialize()).has_value(),"Retain independent original title");
  write(result.metadata,QByteArray::fromStdString(source->serialize()));
  const auto sealed=storage::inspectTorrentIntake(result.metadata.toStdString());require(sealed.has_value(),"Seal actual custom fixture metadata");result.hash=sealed->hash();
  for(auto& file:result.files){auto bytes=file.expected;if(corrupt && &file==&result.files.front()){bytes[0]=static_cast<char>(bytes[0]^1);}const auto path=result.payload+'/'+file.relative;write(path,bytes);require(::stat(path.toStdString().c_str(),&file.before)==0,"Pin custom existing leaf");}
  return result;
}
inline auto importCustomVerifiedSeedCase(storage::ProfileLease& lease,const VerifiedSeedCase& fixture,const QString& kind){
  const auto sealed=storage::inspectTorrentIntake(fixture.metadata.toStdString());
  if(!sealed){return std::expected<core::TorrentHash,storage::Error>{std::unexpected(sealed.error())};}
  const auto policy=versionedFilePolicies(*sealed);
  return storage::importTorrentWithFileOptions(lease,*sealed,fixture.payload.toStdString(),true,policy,nullptr,false,{}, {},false,false,false,{},customVerifiedLeaf(kind).toStdString(),true);
}
inline QByteArray receiveCustomVerifiedSeed(engine::PersistentSession& owner,const VerifiedSeedCase& fixture){
  // Receiver uses unchanged canonical metainfo paths. Only the serving owner's
  // approved mapping is renamed; actual wire identities/piece hashes stay exact.
  auto receiver=fixture;lt::error_code error;const lt::torrent_info info(fixture.metadata.toStdString(),error);require(!error,"Own custom receiver metainfo");std::size_t real=0;
  for(int n=0;n<info.num_files();++n){const auto index=lt::file_index_t{n};if(info.files().pad_file_at(index)){continue;}require(real<receiver.files.size(),"Actual custom receiver file count");receiver.files[real++].relative=QString::fromStdString(info.files().file_path(index));}
  require(real==receiver.files.size(),"All actual custom receiver files mapped");return receiveVerifiedSeed(owner,receiver);
}
inline core::Document customFixtureRow(const QString& profile){auto doc=core::Document::parse(read(profile+"/downloads.config").toStdString(),storage::compatibleConfigLimits());require(doc&&doc->find("downloads")&&doc->find("downloads")->children.size()==1,"Own saved custom row");return std::move(*doc);}
inline void customRemoveRowKey(const QString& profile,std::string_view key,bool malformedScope=false){
  auto doc=customFixtureRow(profile);const auto& row=doc.find("downloads")->children[0];std::string encoded="d";
  for(std::size_t n=0;n<row.children.size();n+=2){const auto& name=row.children[n];if(name.bytes==key){continue;}encoded+=customNodeBytes(name)+customNodeBytes(row.children[n+1]);}encoded+='e';
  auto selected=core::Document::parse(std::move(encoded));require(selected.has_value(),"Own filtered row");
  if(malformedScope){require(selected->replaceValue(storage::VerifiedExistingScopeKey,"i-1e").has_value(),"Stale malformed legacy scope");}
  require(doc.replaceValue("downloads","l"+selected->serialize()+'e').has_value(),"Keep all other saved row bytes");write(profile+"/downloads.config",QByteArray::fromStdString(doc.serialize()));
}
}
