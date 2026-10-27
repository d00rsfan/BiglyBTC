#pragma once
#include "custom_verified_seeding_fixture.hpp"

namespace biglybtc::test {
inline VerifiedSeedCase prepareLinkedVerifiedSeedCase(const QString& root,const QString& kind,bool corrupt=false){
  auto fixture=prepareCustomVerifiedSeedCase(root,kind,corrupt);const auto old=fixture.payload+'/'+fixture.files[0].relative;
  const auto target=fixture.payload+"/linked-first.bin";require(QFile::rename(old,target),"Move owned linked input before approval");fixture.files[0].relative=QStringLiteral("linked-first.bin");
  require(::stat(target.toStdString().c_str(),&fixture.files[0].before)==0,"Pin actual linked input inode");
  auto source=core::Document::parse(read(fixture.metadata).toStdString(),storage::compatibleConfigLimits());require(source.has_value(),"Own initial linked source");
  const auto* oldProperties=source->find("azureus_private_properties");auto properties=core::Document::parse(oldProperties?customNodeBytes(*oldProperties):"de");require(properties.has_value(),"Own original linkage properties");
  const auto utf8=target.toUtf8();const auto path=std::to_string(utf8.size())+':'+utf8.toStdString();
  require(properties->replaceValue("initial_linkage","d1:0"+path+'e').has_value()&&source->replaceValue("azureus_private_properties",properties->serialize()).has_value(),"Original initial map changes no raw info");
  write(fixture.metadata,QByteArray::fromStdString(source->serialize()));return fixture;
}
inline auto importLinkedVerifiedSeedCase(storage::ProfileLease& lease,const VerifiedSeedCase& fixture,const QString& kind){
  const auto preview=storage::inspectTorrentIntake(fixture.metadata.toStdString());if(!preview){return std::expected<core::TorrentHash,storage::Error>{std::unexpected(preview.error())};}
  const auto policies=versionedFilePolicies(*preview);std::optional<std::string> name;if(preview->layout().multifile){name=customVerifiedLeaf(kind).toStdString();}
  return storage::importTorrentWithFileOptions(lease,*preview,fixture.payload.toStdString(),true,policies,nullptr,false,{}, {},false,false,false,{},std::move(name),true);
}
}
