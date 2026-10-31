// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_i2p_profile_fixture.hpp"
#include "source_permit.hpp"
#include "channel.hpp"
#include "library_commands.hpp"
#include "sam_protocol.hpp"
#include "network_policy.hpp"
#include <QScopeGuard>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_info.hpp>
#include <QSignalSpy>
#include <QTcpServer>
#include <QtTest>
#include <fcntl.h>
#include <unistd.h>
namespace sam=biglybtc::services::i2p::provider;
namespace i2cp=biglybtc::services::i2p::i2cp;
using namespace owned_i2p_test;
namespace {
void replaceNamedFileWithSameBytes(const fs::path& path){const auto bytes=read(path);fs::rename(path,path.string()+".retained-original");write(path,bytes);}
core::TorrentHash addSibling(Fixture& f,bool overlap=false){
  const auto name=overlap?"owned.bin":"second.bin";
  const auto info=dict({{"length","i"+std::to_string(f.payload.size())+"e"},{"name",b(name)},
      {"piece length",overlap?"i32768e":"i16384e"},{"pieces",b(sha1(f.payload))}});
  const auto hash=*core::TorrentHash::parse(sha1(info));const auto tracker="http://"+std::string(51,'a')+"q.b32.i2p:80/announce";
  write(f.path/"active"/(hash.hex()+".dat"),dict({{"announce",b(tracker)},{"attributes",dict({{"networks","l3:I2Pe"},{"peersources","l7:Trackere"}})},{"info",info}}));
  write(f.path/"second.torrent",dict({{"announce",b(tracker)},{"info",info}}));
  auto inventory=storage::ConfigFile::load(*f.lease,"downloads.config");if(!inventory){throw std::runtime_error(inventory.error().message);}
  const auto& d=inventory->document();const auto* downloads=d.find("downloads");
  const auto& first=downloads->children.at(0);auto firstRow=core::Document::parse(d.originalBytes().substr(first.begin,first.end-first.begin));
  if(!firstRow||!firstRow->replaceValue("position","i1e")){throw std::runtime_error("Cannot initialize source fixture positions");}
  auto rows="l"+firstRow->serialize();
  rows+=dict({{"persistent","i1e"},{"position","i2e"},{"save_dir",b(f.payloadDirectory.path().toStdString())},{"save_file",b(name)},
      {"state","i75e"},{"torrent",b((f.path/"second.torrent").string())},{"torrent_hash",b(sha1(info))}})+'e';
  if(!inventory->document().replaceValue("downloads",rows)){throw std::runtime_error("Cannot append owned sibling");}
  auto saved=inventory->save(*f.lease);if(!saved){throw std::runtime_error(saved.error().message);}return hash;
}
std::expected<void,storage::Error> saveIntent(Fixture& f,const core::TorrentHash& hash,core::DownloadAction action){
  auto value=storage::applyLibraryCommand(*f.lease,hash,action);if(!value){return std::unexpected(value.error());}return {};
}
}
class OwnedI2pSourceTests:public QObject {
  Q_OBJECT
private slots:

  void trustedPublicStorageAndPositionWritesPreserveTheCurrentI2pMember(){
    Fixture f;const auto publicHash=addSibling(f);const auto publicState=f.path/"active"/(publicHash.hex()+".dat");
    auto state=core::Document::parse(read(publicState));QVERIFY(state);const auto* old=state->find("attributes");QVERIFY(old);
    auto attributes=core::Document::parse(state->originalBytes().substr(old->begin,old->end-old->begin));QVERIFY(attributes);QVERIFY(attributes->replaceValue("networks","l6:Publice"));
    QVERIFY(state->replaceValue("attributes",attributes->serialize()));write(publicState,state->serialize());
    auto anchor=sam::ProfileSource::createSession(*f.lease);QVERIFY(anchor);auto child=(*anchor)->prepareChild(f.hash);QVERIFY(child);auto privateStorage=(*child)->prepareStorage();QVERIFY(privateStorage);
    const auto protectedState=read(f.state()),config=read(f.path/"biglybt.config"),key=read(f.key);std::optional<biglybtc::engine::detail::ManagedStorage> publicStorage;
    const std::array ids{privateStorage->identity()};
    QVERIFY((*anchor)->publish(publicHash,[&]()->std::expected<void,storage::Error>{
      auto prepared=biglybtc::engine::detail::ManagedStorage::prepare(*f.lease,publicHash,biglybtc::engine::NetworkPolicy{},privateStorage->claims(),ids);
      if(!prepared){return std::unexpected(prepared.error());}publicStorage.emplace(std::move(*prepared));return {};
    }));QVERIFY(publicStorage);QVERIFY((*anchor)->validate());QVERIFY((*child)->validate());
    const std::vector<std::uint8_t> noVerifiedPieces(static_cast<std::size_t>(publicStorage->parameters().ti->num_pieces()),0);
    QVERIFY((*anchor)->publish(publicHash,[&]{return publicStorage->checkpoint(*f.lease,noVerifiedPieces,false,0,0);}));
    const std::array pools{storage::LibraryCompletionPool{f.hash,false},storage::LibraryCompletionPool{publicHash,false}};
    QVERIFY((*anchor)->publishPositions([&]()->std::expected<void,storage::Error>{auto moved=storage::applyLibraryCommand(*f.lease,publicHash,core::DownloadAction::MoveTop,pools);return moved?std::expected<void,storage::Error>{}:std::unexpected(moved.error());}));
    QVERIFY((*anchor)->validate());QVERIFY((*child)->validate());QCOMPARE(read(f.state()),protectedState);QCOMPARE(read(f.path/"biglybt.config"),config);QCOMPARE(read(f.key),key);
    // An unrelated config write inside this narrow position transaction remains
    // fatal; there is no generic recapture of cooperative external mutations.
    QVERIFY(!(*anchor)->publishPositions([&]()->std::expected<void,storage::Error>{auto file=storage::ConfigFile::load(*f.lease,"biglybt.config");if(!file){return std::unexpected(file.error());}if(!file->document().replaceValue("future",owned_i2p_test::b("foreign change"))){return std::unexpected(storage::Error{storage::ErrorCode::InvalidFormat,"Owned config refusal"});}return file->save(*f.lease);}));
    QVERIFY((*anchor)->effectAuthority()->revoked());QVERIFY(!(*child)->validate());QCOMPARE(read(f.state()),protectedState);
  }
  void boundSourceAuthorityRefusesLegacySingleStreamPath(){
    Fixture f;auto source=sam::ProfileSource::open(*f.lease,f.hash);QVERIFY(source);auto storage=(*source)->prepareStorage();QVERIFY(storage);
    i2cp::InitialChannel channel;QVERIFY(channel.bindEffectAuthority((*source)->effectAuthority()));
    auto stream=channel.connectStream((*source)->destination().wire,{});QVERIFY(!stream);QCOMPARE(stream.error().code,biglybtc::services::i2p::ErrorCode::Unsupported);
    QCOMPARE(channel.state(),i2cp::InitialChannel::State::Idle);QVERIFY(!channel.sourceEffectsPending());
  }
  void genuinePureSourceRetainsSameLeaseAndOriginalBytes(){
    Fixture f;const auto oldConfig=read(f.path/"biglybt.config"),oldKey=read(f.key),rawInfo=f.info;
    auto source=sam::ProfileSource::open(*f.lease,f.hash);QVERIFY2(source,source?"":source.error().message.c_str());QVERIFY((*source)->lease()->sharesOwnership(*f.lease));
    f.lease.reset();auto storage=(*source)->prepareStorage();QVERIFY2(storage,storage?"":storage.error().message.c_str());QVERIFY((*source)->validate());
    QVERIFY(storage->identity().full.v1.has_value());QCOMPARE(storage->identity().originalKey,f.hash);QVERIFY(!storage->mainlinePolicy().discover);QVERIFY(!storage->mainlinePolicy().announce);
    QCOMPARE(read(f.path/"biglybt.config"),oldConfig);QCOMPARE(read(f.key),oldKey);QVERIFY(read(f.state()).find(rawInfo)!=std::string::npos);
    auto key=(*source)->takeIdentity();QVERIFY(key);QCOMPARE(key->destination()->wire,(*source)->destination().wire);QVERIFY(!(*source)->takeIdentity());
    auto params=storage->parameters();QVERIFY(bool(params.flags&libtorrent::torrent_flags::disable_dht));QVERIFY(bool(params.flags&libtorrent::torrent_flags::disable_lsd));QVERIFY(bool(params.flags&libtorrent::torrent_flags::disable_pex));
  }
  void keyReplacementAndSourceMutationWithdrawWithoutRepair(){
    Fixture f;auto source=sam::ProfileSource::open(*f.lease,f.hash);QVERIFY(source);auto storage=(*source)->prepareStorage();QVERIFY(storage);
    const auto bytes=read(f.key);fs::rename(f.key,f.key.string()+".old");write(f.key,bytes);QVERIFY(::chmod(f.key.c_str(),0600)==0);
    QVERIFY(!(*source)->validate());QVERIFY(!(*source)->effectAuthority()->current());QCOMPARE(read(f.key),bytes);QVERIFY(fs::exists(f.key.string()+".old"));
    Fixture g;auto other=sam::ProfileSource::open(*g.lease,g.hash);QVERIFY(other);auto prepared=(*other)->prepareStorage();QVERIFY(prepared);
    write(g.path/"biglybt.config",dict({{"Plugin.azneti2phelper.enable","i0e"}}));QVERIFY(!(*other)->validate());QCOMPARE(read(g.path/"biglybt.config"),dict({{"Plugin.azneti2phelper.enable","i0e"}}));
  }
  void unsupportedNetworksAndUnsafeOriginalKeysRefuse_data(){QTest::addColumn<int>("kind");for(int i=0;i<5;++i){QTest::newRow(std::to_string(i).c_str())<<i;}}
  void unsupportedNetworksAndUnsafeOriginalKeysRefuse(){
    QFETCH(int,kind);Fixture f(kind==0);const auto original=read(f.key);
    if(kind==1){fs::rename(f.key,f.key.string()+".old");fs::create_symlink(f.key.string()+".old",f.key);}
    if(kind==2){fs::create_hard_link(f.key,f.key.string()+".alias");}
    if(kind==3){fs::remove(f.key);QVERIFY(::mkfifo(f.key.c_str(),0600)==0);}
    if(kind==4){QVERIFY(::chmod(f.key.c_str(),0644)==0);}
    QVERIFY(!sam::ProfileSource::open(*f.lease,f.hash));if(kind==0||kind==2||kind==4){QCOMPARE(read(f.key),original);}
  }
  void sourcePermitIsOneShotAndBlockedIoCannotExtendChannelDeadline(){
    Fixture f;auto source=sam::ProfileSource::open(*f.lease,f.hash);QVERIFY(source);auto storage=(*source)->prepareStorage();QVERIFY(storage);
    QObject receiver;sam::SourcePermit permit(*source,&receiver);
    auto access=(*source)->lease()->sourceAccess();QVERIFY(access);QVERIFY(permit.request());
    QTRY_COMPARE_WITH_TIMEOUT(permit.phase(),sam::SourcePermit::Phase::Reading,2000);
    i2cp::InitialChannel channel;QVERIFY(channel.bindEffectAuthority((*source)->effectAuthority()));QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost,0));
    i2cp::InitialChannel::Options options{QHostAddress::LocalHost,server.serverPort(),"owned-test",false,100,false};QVERIFY(channel.begin(options));
    // No worker permit means no actual TCP connect or greeting. Existing
    // absolute channel deadline still closes without filesystem progress.
    QTRY_COMPARE_WITH_TIMEOUT(channel.state(),i2cp::InitialChannel::State::Failed,2000);QCOMPARE(channel.failure(),i2cp::InitialChannel::Failure::Deadline);QVERIFY(!server.hasPendingConnections());
    access=std::unexpected(biglybtc::storage::Error{biglybtc::storage::ErrorCode::Cancelled,"release owned test gate"});
    QTRY_COMPARE_WITH_TIMEOUT(permit.phase(),sam::SourcePermit::Phase::Ready,2000);QVERIFY(permit.take());QVERIFY((*source)->effectAuthority()->granted());QVERIFY(!channel.resumeAuthorizedEffects());QVERIFY(!permit.take());permit.acknowledge();QVERIFY(!(*source)->effectAuthority()->granted());
    QTRY_COMPARE_WITH_TIMEOUT(permit.phase(),sam::SourcePermit::Phase::Idle,2000);permit.stop();permit.join();
  }
  void sessionAnchorIsNotAStoppedTorrentGrantAndConsumesIdentityOnce(){
    Fixture f;QVERIFY(saveIntent(f,f.hash,core::DownloadAction::Stop));
    auto anchor=sam::ProfileSource::createSession(*f.lease);QVERIFY2(anchor,anchor?"":anchor.error().message.c_str());
    QVERIFY((*anchor)->prepared());QVERIFY((*anchor)->groupMode());QVERIFY((*anchor)->validate());QVERIFY((*anchor)->lease()->sharesOwnership(*f.lease));
    QVERIFY(!(*anchor)->prepareChild(f.hash));QVERIFY((*anchor)->validate());QVERIFY(!(*anchor)->prepareStorage());
    auto identity=(*anchor)->takeIdentity();QVERIFY(identity);QCOMPARE(identity->destination()->wire,(*anchor)->destination().wire);QVERIFY(!(*anchor)->takeIdentity());
  }
  void siblingPreparationAndUnverifiedCheckpointPreserveOtherSourceExactly(){
    Fixture f;const auto sibling=addSibling(f);const auto config=read(f.path/"biglybt.config"),key=read(f.key);
    auto anchor=sam::ProfileSource::createSession(*f.lease);QVERIFY(anchor);auto a=(*anchor)->prepareChild(f.hash);QVERIFY(a);
    auto storageA=(*a)->prepareStorage();QVERIFY2(storageA,storageA?"":storageA.error().message.c_str());const auto stateA=read(f.state());
    QVERIFY(!(*anchor)->prepareChild(sibling));
    const std::array ids{storageA->identity()};auto b=(*anchor)->prepareChild(sibling,storageA->claims(),ids);QVERIFY(b);
    auto storageB=(*b)->prepareStorage();QVERIFY2(storageB,storageB?"":storageB.error().message.c_str());QVERIFY((*anchor)->validate());QVERIFY((*a)->validate());
    QCOMPARE(read(f.state()),stateA);QCOMPARE(read(f.path/"biglybt.config"),config);QCOMPARE(read(f.key),key);
    // The fixture's canonical b32 names are decoded through the real SAM helper.
    const auto trackerA=sam::hashName(std::string(52,'a')+".b32.i2p"),trackerB=sam::hashName(std::string(51,'a')+"q.b32.i2p");
    QVERIFY(trackerA&&trackerB);QVERIFY((*anchor)->trackerAllowed(*trackerA,80));QVERIFY((*anchor)->trackerAllowed(*trackerB,80));
    (*b)->revoke();QVERIFY(!(*anchor)->trackerAllowed(*trackerB,80));QVERIFY((*anchor)->trackerAllowed(*trackerA,80));
    const std::vector<std::uint8_t> unverified(static_cast<std::size_t>(storageB->parameters().ti->num_pieces()),0);
    auto saved=(*anchor)->publish(sibling,[&]{return storageB->checkpoint(*f.lease,unverified,false,0,0);});
    QVERIFY2(saved,saved?"":saved.error().message.c_str());QVERIFY((*a)->validate());QVERIFY((*anchor)->validate());QCOMPARE(read(f.state()),stateA);
    const auto inventory=storage::ConfigFile::load(*f.lease,"downloads.config");QVERIFY(inventory);const auto library=core::readLibrary(inventory->document());QVERIFY(library);
    const auto row=std::find_if(library->downloads.begin(),library->downloads.end(),[&](const auto& value){return value.hash&&*value.hash==sibling;});
    QVERIFY(row!=library->downloads.end());QCOMPARE(row->downloaded.value_or(0),std::int64_t{0});QCOMPARE(row->completedBytes.value_or(-1),std::int64_t{0});
    storageB=std::unexpected(storage::Error{storage::ErrorCode::Cancelled,"release actual storage before child release"});
    QVERIFY((*anchor)->releaseChild(sibling));QVERIFY((*anchor)->validate());QVERIFY((*a)->validate());
  }
  void trustedSiblingPauseAndResumeDoNotChangeTheOtherMember(){
    Fixture f;const auto sibling=addSibling(f);auto anchor=sam::ProfileSource::createSession(*f.lease);QVERIFY(anchor);
    auto a=(*anchor)->prepareChild(f.hash);QVERIFY(a);auto storageA=(*a)->prepareStorage();QVERIFY(storageA);const auto stateA=read(f.state());
    const std::array ids{storageA->identity()};auto b=(*anchor)->prepareChild(sibling,storageA->claims(),ids);QVERIFY(b);auto storageB=(*b)->prepareStorage();QVERIFY(storageB);
    (*b)->revoke();storageB=std::unexpected(storage::Error{storage::ErrorCode::Cancelled,"storage-only fixture has no engine to retire"});
    QVERIFY((*anchor)->publish(sibling,[&]{return saveIntent(f,sibling,core::DownloadAction::Pause);}));
    QVERIFY(!(*b)->validate());QVERIFY((*anchor)->validate());QVERIFY((*a)->validate());QCOMPARE(read(f.state()),stateA);
    QVERIFY((*anchor)->releaseChild(sibling));QVERIFY((*anchor)->publish(sibling,[&]{return saveIntent(f,sibling,core::DownloadAction::Resume);}));
    auto restarted=(*anchor)->prepareChild(sibling,storageA->claims(),ids);QVERIFY(restarted);auto storageAgain=(*restarted)->prepareStorage();QVERIFY2(storageAgain,storageAgain?"":storageAgain.error().message.c_str());
    QVERIFY((*anchor)->validate());QVERIFY((*a)->validate());QVERIFY(!(*b)->effectAuthority()->current());QCOMPARE(read(f.state()),stateA);
  }
  void originalQueueMovePublicationChangesOnlyPositionsAndRejectsUnknownRowEdits(){
    for(bool invalid:{false,true}){
      Fixture f;const auto sibling=addSibling(f);auto anchor=sam::ProfileSource::createSession(*f.lease);QVERIFY(anchor);
      auto a=(*anchor)->prepareChild(f.hash);QVERIFY(a);auto storageA=(*a)->prepareStorage();QVERIFY(storageA);const std::array ids{storageA->identity()};
      auto b=(*anchor)->prepareChild(sibling,storageA->claims(),ids);QVERIFY(b);auto storageB=(*b)->prepareStorage();QVERIFY(storageB);
      const auto stateA=read(f.state()),stateB=read(f.path/"active"/(sibling.hex()+".dat"));
      const std::array pools{storage::LibraryCompletionPool{f.hash,false},storage::LibraryCompletionPool{sibling,false}};
      const auto result=(*anchor)->publishPositions([&]()->std::expected<void,storage::Error>{
        if(!invalid){auto moved=storage::applyLibraryCommand(*f.lease,sibling,core::DownloadAction::MoveTop,pools);if(!moved){return std::unexpected(moved.error());}return {};}
        auto file=storage::ConfigFile::load(*f.lease,"downloads.config");if(!file){return std::unexpected(file.error());}
        const auto& d=file->document();const auto* rows=d.find("downloads");std::string changed="l";
        for(std::size_t i=0;i<rows->children.size();++i){const auto& row=rows->children[i];auto encoded=d.originalBytes().substr(row.begin,row.end-row.begin);
          if(i==0){auto edited=core::Document::parse(encoded);if(!edited||!edited->replaceValue("future",owned_i2p_test::b("unrelated change"))){return std::unexpected(storage::Error{storage::ErrorCode::InvalidFormat,"Owned position refusal fixture"});}encoded=edited->serialize();}changed+=encoded;}
        if(!file->document().replaceValue("downloads",changed+'e')){return std::unexpected(storage::Error{storage::ErrorCode::InvalidFormat,"Owned position list"});}return file->save(*f.lease);
      });
      if(invalid){QVERIFY(!result);QVERIFY((*anchor)->effectAuthority()->revoked());}else{
        QVERIFY2(result,result?"":result.error().message.c_str());QVERIFY((*anchor)->validate());QVERIFY((*a)->validate());QVERIFY((*b)->validate());
        auto file=storage::ConfigFile::load(*f.lease,"downloads.config");QVERIFY(file);auto library=core::readLibrary(file->document());QVERIFY(library);
        QCOMPARE(library->downloads[0].position.value_or(0),std::int64_t{2});QCOMPARE(library->downloads[1].position.value_or(0),std::int64_t{1});
      }
      QCOMPARE(read(f.state()),stateA);QCOMPARE(read(f.path/"active"/(sibling.hex()+".dat")),stateB);
    }
  }
  void rejectedPublicationOnlySurvivesWhenEveryActualBeforeImageIsUnchanged(){
    for(bool partial:{false,true}){
      Fixture f;auto anchor=sam::ProfileSource::createSession(*f.lease);QVERIFY(anchor);auto child=(*anchor)->prepareChild(f.hash);QVERIFY(child);auto prepared=(*child)->prepareStorage();QVERIFY(prepared);
      const auto error=storage::Error{storage::ErrorCode::Conflict,"Owned rejected operation"};
      const auto result=(*anchor)->publish(f.hash,[&]()->std::expected<void,storage::Error>{
        // A rejected attempted writer still advances the real cooperative epoch.
        auto access=f.lease->sourceAccess(true);if(!access){return std::unexpected(access.error());}
        if(partial){replaceNamedFileWithSameBytes(f.state());}return std::unexpected(error);
      });
      QVERIFY(!result);if(partial){QVERIFY((*anchor)->effectAuthority()->revoked());}else{
        QVERIFY(!(*anchor)->effectAuthority()->revoked());QVERIFY((*anchor)->validate());QVERIFY((*child)->validate());QVERIFY((*anchor)->effectAuthority()->current());QVERIFY(!(*anchor)->effectAuthority()->granted());
      }
    }
  }
  void acceptedChildMetadataCannotBeRecapturedBeforeStoragePreparation(){
    Fixture f;auto anchor=sam::ProfileSource::createSession(*f.lease);QVERIFY(anchor);auto child=(*anchor)->prepareChild(f.hash);QVERIFY(child);
    replaceNamedFileWithSameBytes(f.state());QVERIFY(!(*child)->prepareStorage());QVERIFY((*anchor)->effectAuthority()->revoked());
  }
  void unregisteredInventoryReplacementCannotAcquirePublicationAuthority(){
    Fixture f;auto anchor=sam::ProfileSource::createSession(*f.lease);QVERIFY(anchor);auto child=(*anchor)->prepareChild(f.hash);QVERIFY(child);auto prepared=(*child)->prepareStorage();QVERIFY(prepared);
    const auto before=read(f.path/"downloads.config");auto replacement=storage::ConfigFile::load(*f.lease,"downloads.config");QVERIFY(replacement);QVERIFY(replacement->save(*f.lease));
    QCOMPARE(read(f.path/"downloads.config"),before);QVERIFY(!(*anchor)->validate());QVERIFY((*anchor)->effectAuthority()->revoked());
  }
  void wrongPublicationTargetWithdrawsGroupAndPreservesActualEdit(){
    Fixture f;const auto sibling=addSibling(f);auto anchor=sam::ProfileSource::createSession(*f.lease);QVERIFY(anchor);auto child=(*anchor)->prepareChild(f.hash);QVERIFY(child);auto prepared=(*child)->prepareStorage();QVERIFY(prepared);
    const auto result=(*anchor)->publish(sibling,[&]{return saveIntent(f,f.hash,core::DownloadAction::Stop);});QVERIFY(!result);QVERIFY((*anchor)->effectAuthority()->revoked());
    auto inventory=storage::ConfigFile::load(*f.lease,"downloads.config");QVERIFY(inventory);auto library=core::readLibrary(inventory->document());QVERIFY(library);QCOMPARE(library->downloads[0].startup,core::StartupState::Stopped);
  }
  void groupSelectedAndCommonTamperingRemainFatal_data(){QTest::addColumn<int>("kind");for(int i=0;i<4;++i){QTest::newRow(std::to_string(i).c_str())<<i;}}
  void groupSelectedAndCommonTamperingRemainFatal(){
    QFETCH(int,kind);Fixture f;auto anchor=sam::ProfileSource::createSession(*f.lease);QVERIFY(anchor);auto child=(*anchor)->prepareChild(f.hash);QVERIFY(child);auto prepared=(*child)->prepareStorage();QVERIFY(prepared);
    if(kind==0){replaceNamedFileWithSameBytes(f.state());}
    if(kind==1){const auto bytes=read(f.key);fs::rename(f.key,f.key.string()+".old");write(f.key,bytes);QVERIFY(::chmod(f.key.c_str(),0600)==0);}
    if(kind==2){replaceNamedFileWithSameBytes(f.path/"biglybt.config");}
    if(kind==3){QVERIFY(saveIntent(f,f.hash,core::DownloadAction::Pause));}
    QVERIFY(!(*anchor)->validate());QVERIFY((*anchor)->effectAuthority()->revoked());QVERIFY(!(*child)->revisionCurrent(f.lease->sourceRevision()));
  }
  void siblingPayloadOverlapIsRejectedByTheExistingStorageClaimChecks(){
    Fixture f;const auto sibling=addSibling(f,true);auto anchor=sam::ProfileSource::createSession(*f.lease);QVERIFY(anchor);auto a=(*anchor)->prepareChild(f.hash);QVERIFY(a);auto storageA=(*a)->prepareStorage();QVERIFY(storageA);
    const auto claims=storageA->claims();QVERIFY(!claims.empty());const auto inode=claims.front().inode;const std::array ids{storageA->identity()};
    const std::array paths{f.path/"downloads.config",f.path/"biglybt.config",f.key,f.state(),f.path/"active"/(sibling.hex()+".dat"),claims.front().absolutePath};
    std::vector<std::string> before;for(const auto& path:paths){before.push_back(read(path));}
    auto b=(*anchor)->prepareChild(sibling,claims,ids);QVERIFY(b);auto refused=(*b)->prepareStorage();QVERIFY(!refused);QCOMPARE(refused.error().code,storage::ErrorCode::Conflict);
    QCOMPARE(storageA->claims().front().inode,inode);QVERIFY(!(*b)->prepared());QVERIFY(!(*b)->revisionCurrent(f.lease->sourceRevision()));QVERIFY(!(*b)->validate());
    QVERIFY((*anchor)->validate());QVERIFY((*a)->validate());QVERIFY((*a)->revisionCurrent(f.lease->sourceRevision()));
    const auto authority=(*anchor)->effectAuthority();QVERIFY(authority);QVERIFY(authority->current());QVERIFY(!authority->revoked());QVERIFY(!authority->granted());
    const auto trackerA=sam::hashName(std::string(52,'a')+".b32.i2p"),trackerB=sam::hashName(std::string(51,'a')+"q.b32.i2p");
    QVERIFY(trackerA&&trackerB);QVERIFY((*anchor)->trackerAllowed(*trackerA,80));QVERIFY(!(*anchor)->trackerAllowed(*trackerB,80));
    for(std::size_t i=0;i<paths.size();++i){QCOMPARE(read(paths[i]),before[i]);}
  }
  void retainedFullIdentityIsForwardedToOriginalStorageBeforeAdmission(){
    Fixture f,other;const auto sibling=addSibling(f),otherHash=addSibling(other);QCOMPARE(sibling,otherHash);
    auto held=sam::ProfileSource::open(*other.lease,otherHash);QVERIFY(held);auto retained=(*held)->prepareStorage();QVERIFY(retained);
    auto anchor=sam::ProfileSource::createSession(*f.lease);QVERIFY(anchor);auto a=(*anchor)->prepareChild(f.hash);QVERIFY(a);auto storageA=(*a)->prepareStorage();QVERIFY(storageA);
    const std::array ids{storageA->identity(),retained->identity()};auto b=(*anchor)->prepareChild(sibling,storageA->claims(),ids);QVERIFY(b);
    auto refused=(*b)->prepareStorage();QVERIFY(!refused);QCOMPARE(refused.error().code,storage::ErrorCode::Conflict);
  }
  void dirtyReadyPermitReleasesGateAndRequiresFreshValidation(){
    Fixture f;auto anchor=sam::ProfileSource::createSession(*f.lease);QVERIFY(anchor);QObject receiver;sam::SourcePermit permit(*anchor,&receiver);
    const auto cleanup=qScopeGuard([&]{if(permit.phase()==sam::SourcePermit::Phase::Executing){permit.acknowledge();}permit.stop();permit.join();});
    QVERIFY(permit.request());QTRY_COMPARE_WITH_TIMEOUT(permit.phase(),sam::SourcePermit::Phase::Ready,2000);
    (*anchor)->invalidate();QVERIFY(!(*anchor)->effectAuthority()->current());QVERIFY(!permit.take());QVERIFY(!(*anchor)->effectAuthority()->revoked());
    QTRY_COMPARE_WITH_TIMEOUT(permit.phase(),sam::SourcePermit::Phase::Idle,2000);QVERIFY(permit.request());QTRY_COMPARE_WITH_TIMEOUT(permit.phase(),sam::SourcePermit::Phase::Ready,2000);
    QVERIFY(permit.take());QVERIFY((*anchor)->effectAuthority()->granted());permit.acknowledge();QTRY_COMPARE_WITH_TIMEOUT(permit.phase(),sam::SourcePermit::Phase::Idle,2000);
    QVERIFY((*anchor)->publish(f.hash,[&]{return saveIntent(f,f.hash,core::DownloadAction::Start);}));
    QVERIFY((*anchor)->effectAuthority()->current());QVERIFY(!(*anchor)->effectAuthority()->granted());QVERIFY(!(*anchor)->effectAuthority()->revoked());
    i2cp::InitialChannel channel;QVERIFY(channel.bindEffectAuthority((*anchor)->effectAuthority()));
    QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost,0));
    i2cp::InitialChannel::Options options{QHostAddress::LocalHost,server.serverPort(),"owned-group",false,100,false};
    QVERIFY(channel.begin(options));QVERIFY(channel.sourceEffectsPending());QCOMPARE(channel.failure(),i2cp::InitialChannel::Failure::None);QVERIFY(!server.hasPendingConnections());channel.close();
  }
};
QTEST_GUILESS_MAIN(OwnedI2pSourceTests)
#include "owned_i2p_source_tests.moc"
