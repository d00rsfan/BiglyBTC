// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_i2p_profile_fixture.hpp"
#include "source_permit.hpp"
#include "channel.hpp"
#include "library_commands.hpp"
#include "torrent_state.hpp"
#include "sam_protocol.hpp"
#include "network_policy.hpp"
#include "persistent_session.hpp"
#include "mixed_i2p_source.hpp"
#include "mixed_i2p_transport.hpp"
#include "pending_magnet_source.hpp"
#include "magnet_intake.hpp"
#include <array>
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
fs::path mixedKey(Fixture& f){
  const auto path=f.key.parent_path()/"dest_key.dat";write(path,read(f.key));
  if(::chmod(path.c_str(),0600)!=0){throw std::runtime_error("Mixed original fixture key mode");}return path;
}
void setNetworks(Fixture& f,const core::TorrentHash& hash,const std::string& encoded){
  const auto path=f.path/"active"/(hash.hex()+".dat");auto state=core::Document::parse(read(path));
  if(!state){throw std::runtime_error("Mixed fixture metadata");}const auto* old=state->find("attributes");
  if(!old){throw std::runtime_error("Mixed fixture attributes");}
  auto attrs=core::Document::parse(state->originalBytes().substr(old->begin,old->end-old->begin));
  if(!attrs||!attrs->replaceValue("networks",encoded)||!state->replaceValue("attributes",attrs->serialize())){throw std::runtime_error("Mixed fixture network selection");}
  write(path,state->serialize());
}
std::expected<void,storage::Error> saveIntent(Fixture& f,const core::TorrentHash& hash,core::DownloadAction action){
  auto value=storage::applyLibraryCommand(*f.lease,hash,action);if(!value){return std::unexpected(value.error());}return {};
}
}
class OwnedI2pSourceTests:public QObject {
  Q_OBJECT
private slots:
// Real generated profile/source readers; no Mode or EffectAuthority setter.
  void pendingNativeBootstrapNeverBecomesAnOriginalSource_data(){
    QTest::addColumn<bool>("mixed");QTest::addColumn<int>("part");
    for(bool mixed:{false,true}){for(int part=0;part<3;++part){const auto label=QByteArray::number(part)+(mixed?"-mixed":"-pure");QTest::newRow(label.constData())<<mixed<<part;}}
  }
  void pendingNativeBootstrapNeverBecomesAnOriginalSource(){
    QFETCH(bool,mixed);QFETCH(int,part);Fixture f;if(mixed){static_cast<void>(mixedKey(f));}
    const std::array<std::string,3> paths=mixed?std::array<std::string,3>{".BiglyBTC.i2p-bootstrap",".BiglyBTC.dest_key.dat.pending",".BiglyBTC.dht.config.pending"}:
        std::array<std::string,3>{".BiglyBTC.i2p-bootstrap1",".BiglyBTC.dest_key1.dat.pending",".BiglyBTC.dht1.config.pending"};
    const auto path=f.key.parent_path()/paths[static_cast<std::size_t>(part)],key= mixed?f.key.parent_path()/"dest_key.dat":f.key;
    const auto keyBefore=read(key),configBefore=read(f.path/"biglybt.config"),inventoryBefore=read(f.path/"downloads.config");
    write(path,"preserve bootstrap prefix");QVERIFY(::chmod(path.c_str(),0600)==0);
    auto source=mixed?sam::ProfileSource::createMixedSession(*f.lease):sam::ProfileSource::createSession(*f.lease);
    QVERIFY(!source);QCOMPARE(source.error().code,storage::ErrorCode::Conflict);QVERIFY(source.error().message.find("review")!=std::string::npos);
    QCOMPARE(read(path),std::string("preserve bootstrap prefix"));QCOMPARE(read(key),keyBefore);QCOMPARE(read(f.path/"biglybt.config"),configBefore);QCOMPARE(read(f.path/"downloads.config"),inventoryBefore);
    QVERIFY(fs::remove(path));source=mixed?sam::ProfileSource::createMixedSession(*f.lease):sam::ProfileSource::createSession(*f.lease);QVERIFY(source);QVERIFY((*source)->validate());
    write(path,"later incomplete source");QVERIFY(!(*source)->validate());QCOMPARE(read(path),std::string("later incomplete source"));QCOMPARE(read(key),keyBefore);
  }
  void bootstrapRefusalIsIndexScopedAndUnknownFilesRemainInert(){
    Fixture f;static_cast<void>(mixedKey(f));const auto marker=f.key.parent_path()/".BiglyBTC.i2p-bootstrap1";write(marker,"pure prefix");
    auto mixed=sam::ProfileSource::createMixedSession(*f.lease);QVERIFY(mixed);QVERIFY((*mixed)->validate());QVERIFY(!sam::ProfileSource::createSession(*f.lease));
    QVERIFY(fs::remove(marker));const auto unknown=f.key.parent_path()/".BiglyBTC.i2p-bootstrap1.future";write(unknown,"unknown retained bytes");
    auto pure=sam::ProfileSource::createSession(*f.lease);QVERIFY(pure);QVERIFY((*pure)->validate());QVERIFY((*mixed)->validate());QCOMPARE(read(unknown),std::string("unknown retained bytes"));
  }

  void savedDhtOnlySourcesRespectOriginalBackupAndPrivateRules_data(){
    QTest::addColumn<int>("kind");for(int kind=0;kind<7;++kind){QTest::newRow(std::to_string(kind).c_str())<<kind;}
  }
  void savedDhtOnlySourcesRespectOriginalBackupAndPrivateRules(){
    QFETCH(int,kind);Fixture f;
    const auto tracker=std::string("http://")+std::string(52,'a')+".b32.i2p:80/announce";
    const auto announce=kind==1?"dht://"+f.hash.hex()+".dht/announce":kind==6?std::string("http://example.invalid/announce"):tracker;
    const auto info=kind==4?dict({{"length","i"+std::to_string(f.payload.size())+"e"},{"name",b("owned.bin")},{"piece length","i16384e"},{"pieces",b(sha1(f.payload))},{"private","i1e"}}):f.info;
    const auto sources=kind==1?"le":"l3:DHte"; // case-insensitive original DHT source.
    const auto bytes=dict({{"announce",b(announce)},{"attributes",dict({{"networks","l3:I2Pe"},{"peersources",sources}})},
        {"azureus_properties",dict({{"dht_backup_enable",kind==2?"i0e":"i1e"}})},{"info",info}});
    write(f.state(),bytes);write(f.path/"fixture.torrent",dict({{"announce",b(announce)},{"info",info}}));
    if(kind==3||kind==5){auto config=storage::ConfigFile::load(*f.lease,"biglybt.config");QVERIFY(config);QVERIFY(config->document().replaceValue("Plugin.azneti2phelper.azi2phelper.dht.enable",kind==3?"i0e":"3:bad"));QVERIFY(config->save(*f.lease));}
    const auto configBefore=read(f.path/"biglybt.config"),inventoryBefore=read(f.path/"downloads.config"),stateBefore=read(f.state());
    auto source=sam::ProfileSource::open(*f.lease,f.hash);
    if(kind>=2&&kind!=6){QVERIFY(!source);QCOMPARE(read(f.state()),stateBefore);QCOMPARE(read(f.path/"biglybt.config"),configBefore);QCOMPARE(read(f.path/"downloads.config"),inventoryBefore);return;}
    QVERIFY(source);auto storage=(*source)->prepareStorage();QVERIFY2(storage,storage?"":storage.error().message.c_str());
    QVERIFY((*source)->discoveryAllowed());QVERIFY(storage->parameters().trackers.empty());QVERIFY(storage->parameters().ti->trackers().empty());
    const auto savedMetadata=core::Document::parse(read(f.state()));QVERIFY(savedMetadata);const auto* savedAnnounce=savedMetadata->find("announce");QVERIFY(savedAnnounce);QCOMPARE(std::string(savedAnnounce->bytes),announce);
    const auto actualHashes=storage->parameters().ti->info_hashes();QVERIFY(actualHashes.has_v1());QCOMPARE(sha1(f.info),std::string(actualHashes.v1.data(),20));
    const auto trackerHash=sam::hashName(std::string(52,'a')+".b32.i2p");QVERIFY(trackerHash);QVERIFY(!(*source)->trackerAllowed(*trackerHash,80));
    QVERIFY(storage->parameters().flags&lt::torrent_flags::disable_dht); // Public BEP5 remains disabled.
  }
  void enabledOrdinaryTrackerKeepsItsActualRuntimeMetadata(){
    Fixture f;const auto before=core::Document::parse(read(f.state()));QVERIFY(before);const auto url=std::string(before->find("announce")->bytes);
    auto source=sam::ProfileSource::open(*f.lease,f.hash);QVERIFY(source);auto storage=(*source)->prepareStorage();QVERIFY(storage);
    const auto params=storage->parameters();QVERIFY(!(params.flags&lt::torrent_flags::override_trackers));
    QVERIFY(!params.ti->trackers().empty());QCOMPARE(params.ti->trackers().front().url,url);
    const auto hash=sam::hashName(std::string(52,'a')+".b32.i2p");QVERIFY(hash);QVERIFY((*source)->trackerAllowed(*hash,80));
  }
  void pendingSourcesHaveNoStorageGrantAndSelectedPublicationKeepsSiblingsExact(){
    Fixture f;auto policy=biglybtc::engine::readNetworkPolicy(nullptr,{biglybtc::engine::NetworkMode::Normal});QVERIFY(policy);
    const auto other=core::TorrentHash::parse(std::string(20,'q'));QVERIFY(other);
    const auto uri="magnet:?xt=urn:btih:"+other->hex()+"&net=I2P";
    auto intent=biglybtc::engine::parseMagnetIntent(uri,fs::path(f.payloadDirectory.path().toStdString())/"metadata-target",true,*policy,biglybtc::engine::MagnetNetwork::I2p);QVERIFY(intent);
    auto pending=biglybtc::engine::queuePendingMagnet(*f.lease,*intent,*policy,biglybtc::engine::MagnetNetwork::I2p);QVERIFY(pending);
    const auto state=read(f.state()),inventory=read(f.path/"downloads.config");
    auto anchor=sam::ProfileSource::createSession(*f.lease);QVERIFY(anchor);
    auto forged=*pending;forged.uri+="&dn=not-saved";QVERIFY(!(*anchor)->preparePendingMagnet(forged));
    auto child=(*anchor)->preparePendingMagnet(*pending);QVERIFY(child);
    QVERIFY(!(*child)->prepared());QVERIFY(!(*child)->discoveryAllowed());QVERIFY(!(*child)->effectAuthority());QVERIFY((*child)->validate());
    auto rejected=(*anchor)->publishPendingMagnet(pending->hash,[]{return std::expected<void,storage::Error>{std::unexpected(storage::Error{storage::ErrorCode::Conflict,"No write"})};});
    QVERIFY(!rejected);QVERIFY((*anchor)->prepared());QVERIFY((*anchor)->validate());
    QVERIFY((*anchor)->publishPendingMagnet(pending->hash,[&]{return biglybtc::engine::cancelPendingMagnet(*f.lease,*pending);}));
    QVERIFY((*anchor)->prepared());QVERIFY((*anchor)->validate());QCOMPARE(read(f.state()),state);QCOMPARE(read(f.path/"downloads.config"),inventory);
    QVERIFY((*child)->release()); // Never bound, so no actual SDK lifetime exists.
  }
  void pendingPublicationRejectsUnrelatedConfigEdits(){
    Fixture f;auto anchor=sam::ProfileSource::createSession(*f.lease);QVERIFY(anchor);
    const auto result=(*anchor)->publishPendingMagnet(f.hash,[&]()->std::expected<void,storage::Error>{
      auto config=storage::ConfigFile::load(*f.lease,"biglybt.config");if(!config){return std::unexpected(config.error());}
      if(!config->document().replaceValue("future","3:bad")){return std::unexpected(storage::Error{storage::ErrorCode::InvalidFormat,"Fixture edit"});}return config->save(*f.lease);
    });QVERIFY(!result);QVERIFY(!(*anchor)->prepared());QVERIFY(!(*anchor)->effectAuthority()->current());
  }

  void actualPendingMetadataSealsRejectCrossOwnerAndCannotActivateWithoutAuthorization(){
#if defined(BIGLYBTC_I2P_DISCOVERY_ABI_V1)
    namespace engine=biglybtc::engine;
    if(!engine::PersistentSession::mixedNetworksAvailable()){QSKIP("Explicitly qualified discovery runtime unavailable");}
    std::array<Fixture,2> fixtures;std::array<std::unique_ptr<engine::PersistentSession>,2> owners;
    std::array<std::shared_ptr<sam::ProfileSource>,2> anchors;
    std::array<std::shared_ptr<sam::PendingMagnetSource>,2> children;
    std::array<std::shared_ptr<const engine::PendingMetadataSource>,2> seals;
    const auto cleanup=qScopeGuard([&]{for(auto& owner:owners){if(owner){(void)owner->close(std::chrono::steady_clock::now()+std::chrono::seconds(2));}}});
    for(std::size_t n=0;n<fixtures.size();++n){auto& f=fixtures[n];(void)mixedKey(f);write(f.path/"downloads.config",dict({{"downloads","le"}}));
      auto network=engine::readNetworkPolicy(nullptr,{engine::NetworkMode::Normal});QVERIFY(network);
      auto intent=engine::parseMagnetIntent("magnet:?xt=urn:btih:"+f.hash.hex()+"&net=Public&net=I2P",fs::path(f.payloadDirectory.path().toStdString())/"pending",true,*network,engine::MagnetNetwork::PublicAndI2p);QVERIFY(intent);
      auto queued=engine::queuePendingMagnet(*f.lease,*intent,*network,engine::MagnetNetwork::PublicAndI2p);QVERIFY(queued);
      auto anchor=sam::ProfileSource::createMixedSession(*f.lease);QVERIFY(anchor);anchors[n]=*anchor;
      auto child=anchors[n]->preparePendingMagnet(*queued);QVERIFY(child);children[n]=*child;
      auto owner=engine::PersistentSession::create({7301+n*100,7302+n*100,7303+n*100});QVERIFY(owner);owners[n]=std::move(*owner);
      QVERIFY(owners[n]->configureNetwork(engine::NetworkPolicy{}));QVERIFY(owners[n]->configureMixedNetworks());
      QVERIFY(owners[n]->configureSourcePublication([&,n](const core::TorrentHash& hash,const auto& f){return anchors[n]->publishPendingMagnet(hash,f);},[&,n](const auto& f){return anchors[n]->publishPositions(f);}));
      QVERIFY(owners[n]->startIdle(*f.lease));const auto config=read(f.path/"biglybt.config"),inventory=read(f.path/"downloads.config");
      if(n==0){
        std::shared_ptr<const engine::PendingMetadataSource> rejectedSeal;
        const auto rejected=owners[n]->prepareMagnetMetadata(*f.lease,*queued,f.path,[&](auto source)->std::expected<void,storage::Error>{rejectedSeal=source;throw std::runtime_error("Rejected fixture binder");});
        QVERIFY(!rejected);QVERIFY(rejectedSeal&&!rejectedSeal->current());QVERIFY(anchors[n]->validate());
        QCOMPARE(read(f.path/"biglybt.config"),config);QCOMPARE(read(f.path/"downloads.config"),inventory);
      }
      auto sealed=owners[n]->prepareMagnetMetadata(*f.lease,*queued,f.path,[&,n](auto source){seals[n]=source;return anchors[n]->publishPendingMagnet(source->hash(),[&]{return children[n]->bindMetadata(source);});});QVERIFY2(sealed,sealed?"":sealed.error().message.c_str());
      QVERIFY(seals[n]&&seals[n]->current());QCOMPARE(seals[n]->hash(),f.hash);QVERIFY(seals[n]->requested()==queued->requested);QVERIFY(seals[n]->admission());
      QVERIFY(children[n]->prepared());QVERIFY(children[n]->discoveryAllowed());QVERIFY(!children[n]->effectAuthority()->granted());QVERIFY(anchors[n]->validate());
      QVERIFY(!owners[n]->activatePreparedMagnet(f.hash));QCOMPARE(read(f.path/"biglybt.config"),config);QCOMPARE(read(f.path/"downloads.config"),inventory);
      QVERIFY(owners[n]->retainedPayloads().claims.empty());QVERIFY(owners[n]->retainedPayloads().identities.empty());
    }
    auto transport=owners[0]->mixedTransport();QVERIFY(transport);
    const auto crossed=(*transport)->admitMetadata(*seals[1],1,1);QVERIFY(!crossed);QCOMPARE(crossed.error().message,std::string("Pending source belongs to another actual engine owner"));
    const auto correct=(*transport)->admitMetadata(*seals[0],1,1);QVERIFY(!correct);QVERIFY(correct.error().message!=crossed.error().message);
    QVERIFY(owners[0]->cancelMagnet(fixtures[0].hash));QVERIFY(!seals[0]->current());QVERIFY(!children[0]->discoveryAllowed());QVERIFY(anchors[0]->validate());QVERIFY(seals[1]->current());
    QVERIFY(owners[0]->releasePreparedMagnet(fixtures[0].hash));QVERIFY(children[0]->release());QVERIFY(anchors[0]->validate());
    const auto savedIntent=read(fixtures[1].path/"biglybt.config");
    const auto snapshot=read(fixtures[1].path/"downloads.config");
    QVERIFY(owners[1]->withdrawPreparedMagnet(fixtures[1].hash,{storage::ErrorCode::Conflict,"Generated source withdrawal"}));
    QVERIFY(!seals[1]->current());QVERIFY(!seals[1]->effectsCurrent());QVERIFY(!children[1]->discoveryAllowed());QVERIFY(anchors[1]->validate());
    const auto status=owners[1]->pendingMagnets();QCOMPARE(status.size(),std::size_t{1});
    QCOMPARE(status.front().phase,engine::MagnetPhase::Failed);QVERIFY(!status.front().retained&&status.front().failure);
    QCOMPARE(status.front().failure->message,std::string("Generated source withdrawal"));
    QCOMPARE(read(fixtures[1].path/"biglybt.config"),savedIntent);QCOMPARE(read(fixtures[1].path/"downloads.config"),snapshot);
    QVERIFY(owners[1]->releasePreparedMagnet(fixtures[1].hash));QVERIFY(children[1]->release());QVERIFY(anchors[1]->validate());
#else
    QSKIP("No qualified discovery header/ABI; default source-only controls remain active");
#endif
  }

  void realMixedStorageSealsBindPendingAndRejectCrossedActualPublicOwners(){
    namespace engine=biglybtc::engine;
    if(!engine::PersistentSession::mixedNetworksAvailable()){QSKIP("Explicitly qualified mixed SDK is unavailable; metadata-only controls still run");}
    std::array<Fixture,2> fixtures;QCOMPARE(fixtures[0].hash,fixtures[1].hash);
    std::array<std::shared_ptr<sam::ProfileSource>,2> anchors,children;
    std::array<std::shared_ptr<const engine::MixedManagedSource>,2> seals;
    std::array<std::unique_ptr<engine::PersistentSession>,2> owners;
    std::array<bool,2> boundPending{false,false};
    const auto cleanup=qScopeGuard([&]{for(auto& owner:owners){if(owner){static_cast<void>(owner->close(std::chrono::steady_clock::now()+std::chrono::seconds{2}));}}});
    for(std::size_t i=0;i<fixtures.size();++i){
      auto& fixture=fixtures[i];const auto key=mixedKey(fixture);QVERIFY(fs::exists(key));
      setNetworks(fixture,fixture.hash,i?"l3:I2P6:Publice":"l6:Public3:I2Pe");
      auto anchor=sam::ProfileSource::createMixedSession(*fixture.lease);QVERIFY(anchor);anchors[i]=std::move(*anchor);
      auto child=anchors[i]->prepareMixedChild(fixture.hash);QVERIFY(child);children[i]=std::move(*child);
      auto made=engine::PersistentSession::create({3301+i*100,3302+i*100,3303+i*100});
      QVERIFY2(made,made?"":"Qualified mixed runtime must create the actual Public owner");owners[i]=std::move(*made);
      QVERIFY(owners[i]->configureNetwork(engine::NetworkPolicy{})); // Isolated 127.0.0.1:0, no tracker/DHT/bootstrap.
      QVERIFY(owners[i]->configureExternalQueue(true));QVERIFY(owners[i]->configureMixedNetworks());
      QVERIFY(owners[i]->configureSourcePublication(
          [&,i](const core::TorrentHash& hash,const auto& operation){return anchors[i]->publish(hash,operation);},
          [&,i](const auto& operation){return anchors[i]->publishPositions(operation);}));
      QVERIFY(owners[i]->configureMixedSourceBinding([&,i](const auto& source)->std::expected<void,storage::Error>{
        // This callback has real prepared storage but no attached handle yet.
        // No SDK/Qt calls may be made from inside its source publication.
        seals[i]=source;boundPending[i]=source->current();return children[i]->bindMixedManaged(source);
      }));
      QVERIFY(owners[i]->startIdle(*fixture.lease));
      QVERIFY2(owners[i]->startManaged(*fixture.lease,{.hash=fixture.hash,.queued=true,.mixedNetworks=true}),"Real mixed managed storage preparation failed");
      QVERIFY(boundPending[i]);QVERIFY(seals[i]);QVERIFY(seals[i]->current());QVERIFY(children[i]->prepared());QVERIFY(children[i]->effectAuthority());
      QVERIFY(!children[i]->effectAuthority()->granted());QVERIFY(anchors[i]->validate());QVERIFY(children[i]->validate());
      auto attached=owners[i]->mixedSource(fixture.hash);QVERIFY(attached);QCOMPARE(attached->get(),seals[i].get());
      QCOMPARE(seals[i]->identity().originalKey,fixture.hash);QVERIFY(seals[i]->identity().full.v1);QVERIFY(seals[i]->admission());
      auto loaded=storage::TorrentStateStore::open(*fixture.lease);QVERIFY(loaded);auto actual=loaded->load(*fixture.lease,fixture.hash);QVERIFY(actual);
      QCOMPARE(std::string(seals[i]->stateSnapshot()),actual->document().serialize());
      QCOMPARE(owners[i]->retainedPayloads().identities.size(),std::size_t{1});QVERIFY(!children[i]->prepareStorage());
    }
    QVERIFY(seals[0]->lease().sharesOwnership(*fixtures[0].lease));QVERIFY(!seals[0]->lease().sharesOwnership(*fixtures[1].lease));
    auto transport=owners[0]->mixedTransport();QVERIFY(transport);
    const auto crossAdmit=(*transport)->admit(*seals[1],1,1),crossRevoke=(*transport)->revoke(*seals[1],1,1);
    QVERIFY(!crossAdmit);QVERIFY(!crossRevoke);
    for(const auto* result:{&crossAdmit,&crossRevoke}){
      QCOMPARE(result->error().code,storage::ErrorCode::Unsupported);
      QCOMPARE(result->error().message,std::string("Mixed source belongs to another actual engine owner"));
    }
    // Correct owner reaches the real unconfigured SDK route check (no I2CP,
    // SAM or policy configuration is fabricated just to make it successful).
    const auto sameOwner=(*transport)->admit(*seals[0],1,2);QVERIFY(!sameOwner);
    QCOMPARE(sameOwner.error().code,storage::ErrorCode::Conflict);
    QCOMPARE(sameOwner.error().message,std::string("Mixed I2P provider rejected a current route operation"));
    QVERIFY(owners[0]->stopManaged(fixtures[0].hash));QVERIFY(!seals[0]->current());QVERIFY(!children[0]->prepared());
    QVERIFY(!children[0]->effectAuthority()->current());QVERIFY(!owners[0]->mixedSource(fixtures[0].hash));
    const auto expired=(*transport)->admit(*seals[0],1,3);QVERIFY(!expired);
    QCOMPARE(expired.error().code,storage::ErrorCode::Unsupported);
    QCOMPARE(expired.error().message,std::string("Mixed I2P admission is not attached to its actual engine handle"));
    QVERIFY(seals[1]->current());QVERIFY(children[1]->prepared());QVERIFY(anchors[1]->validate());
    const auto closed=owners[0]->close(std::chrono::steady_clock::now()+std::chrono::seconds{2});QVERIFY(closed.sessionJoined);
    QVERIFY(!seals[0]->current());QVERIFY(seals[1]->current());
  }

  void mixedSourceAcceptsBothOriginalOrdersWithoutPreparingSecondStorage(){
    for(bool reversed:{false,true}){
      Fixture f;const auto key=mixedKey(f);setNetworks(f,f.hash,reversed?"l3:I2P6:Publice":"l6:Public3:I2Pe");
      const auto state=read(f.state()),config=read(f.path/"biglybt.config"),inventory=read(f.path/"downloads.config"),keyBytes=read(key);
      auto anchor=sam::ProfileSource::createMixedSession(*f.lease);QVERIFY2(anchor,anchor?"":anchor.error().message.c_str());
      QVERIFY((*anchor)->mixedMode());QVERIFY((*anchor)->groupMode());QVERIFY((*anchor)->prepared());QVERIFY((*anchor)->validate());
      QVERIFY(!(*anchor)->prepareChild(f.hash));QVERIFY(!(*anchor)->policy().matches(*f.lease,f.hash));
      auto child=(*anchor)->prepareMixedChild(f.hash);QVERIFY(child);QVERIFY((*child)->mixedMode());QVERIFY(!(*child)->prepared());
      QVERIFY(!(*child)->effectAuthority());QVERIFY(!(*child)->revisionCurrent(f.lease->sourceRevision()));
      QVERIFY(!(*child)->prepareStorage());QVERIFY((*child)->validate());QVERIFY((*anchor)->validate());
      const auto tracker=sam::hashName(std::string(52,'a')+".b32.i2p");QVERIFY(tracker);QVERIFY(!(*anchor)->trackerAllowed(*tracker,80));
      auto identity=(*anchor)->takeIdentity();QVERIFY(identity);QVERIFY(!(*anchor)->takeIdentity());
      QVERIFY((*anchor)->releaseMixedChild(f.hash));QVERIFY((*anchor)->validate());QVERIFY(!(*child)->validate());
      QCOMPARE(read(f.state()),state);QCOMPARE(read(f.path/"biglybt.config"),config);QCOMPARE(read(f.path/"downloads.config"),inventory);QCOMPARE(read(key),keyBytes);
    }
  }
  void mixedIdentityUsesOnlyItsOriginalKeyAndEnableSetting(){
    Fixture f;const auto key=mixedKey(f);fs::remove(f.key);
    auto mixed=sam::ProfileSource::createMixedSession(*f.lease);QVERIFY(mixed);QVERIFY((*mixed)->validate());
    QVERIFY(!sam::ProfileSource::createSession(*f.lease));
    const auto bytes=read(key);replaceNamedFileWithSameBytes(key);QVERIFY(::chmod(key.c_str(),0600)==0);
    QVERIFY(!(*mixed)->validateTransportSnapshot());QVERIFY(!(*mixed)->validate());QCOMPARE(read(key),bytes);
    Fixture disabledPure;const auto independentKey=mixedKey(disabledPure);
    write(disabledPure.path/"biglybt.config",dict({{"PluginInfo.azneti2phelper.enabled","i1e"},{"Plugin.azneti2phelper.enable","i1e"},
        {"Plugin.azneti2phelper.azi2phelper.pure.enabled","i0e"},{"Plugin.azneti2phelper.azi2phelper.mix.enabled","i1e"}}));
    QVERIFY(sam::ProfileSource::createMixedSession(*disabledPure.lease));QVERIFY(!sam::ProfileSource::createSession(*disabledPure.lease));QVERIFY(fs::exists(independentKey));
    for(bool malformed:{false,true}){
      Fixture other;const auto otherKey=mixedKey(other);const auto unchanged=read(other.key),unchangedMixed=read(otherKey);
      write(other.path/"biglybt.config",dict({{"PluginInfo.azneti2phelper.enabled","i1e"},{"Plugin.azneti2phelper.enable","i1e"},
          {"Plugin.azneti2phelper.azi2phelper.pure.enabled","i1e"},{"Plugin.azneti2phelper.azi2phelper.mix.enabled",malformed?owned_i2p_test::b("1"):"i0e"}}));
      QVERIFY(!sam::ProfileSource::createMixedSession(*other.lease));QVERIFY(sam::ProfileSource::createSession(*other.lease));
      QCOMPARE(read(other.key),unchanged);QCOMPARE(read(otherKey),unchangedMixed);
    }
  }
  void mixedSourceRejectsOtherNetworkListsAndAnUnmintedBinding(){
    for(const std::string networks:{"le","l6:Publice","l3:I2Pe","l6:Public3:I2P3:I2Pe","l6:Public3:I2P3:Tore","l6:Public7:unknowne"}){
      Fixture f;const auto key=mixedKey(f);setNetworks(f,f.hash,networks);const auto bytes=read(f.state()),keyBytes=read(key);
      auto anchor=sam::ProfileSource::createMixedSession(*f.lease);QVERIFY(anchor);QVERIFY(!(*anchor)->prepareMixedChild(f.hash));QVERIFY((*anchor)->validate());
      QCOMPARE(read(f.state()),bytes);QCOMPARE(read(key),keyBytes);
    }
    Fixture f;const auto key=mixedKey(f);setNetworks(f,f.hash,"l6:Public3:I2Pe");auto anchor=sam::ProfileSource::createMixedSession(*f.lease);QVERIFY(anchor);
    auto child=(*anchor)->prepareMixedChild(f.hash);QVERIFY(child);const auto bytes=read(f.state()),inventory=read(f.path/"downloads.config");
    QVERIFY(!(*child)->bindMixedManaged({}));QVERIFY(!(*child)->prepared());QVERIFY(!(*child)->effectAuthority());
    QVERIFY((*anchor)->validate());QCOMPARE(read(f.state()),bytes);QCOMPARE(read(f.path/"downloads.config"),inventory);QVERIFY(fs::exists(key));
  }
  void transportSnapshotReadNeverRestoresRevokedEffectsAndDetectsActualTampering(){
    for(bool mixed:{false,true}){
      Fixture f;const auto key=mixed?mixedKey(f):f.key;if(mixed){setNetworks(f,f.hash,"l3:I2P6:Publice");}
      auto anchor=mixed?sam::ProfileSource::createMixedSession(*f.lease):sam::ProfileSource::createSession(*f.lease);QVERIFY(anchor);
      auto child=mixed?(*anchor)->prepareMixedChild(f.hash):(*anchor)->prepareChild(f.hash);QVERIFY(child);
      (*anchor)->revoke();QVERIFY((*anchor)->effectAuthority()->revoked());QVERIFY((*anchor)->validateTransportSnapshot());QVERIFY((*child)->validateTransportSnapshot());
      QVERIFY((*anchor)->effectAuthority()->revoked());QVERIFY(!(*anchor)->effectAuthority()->current());QVERIFY(!(*anchor)->prepared());
      replaceNamedFileWithSameBytes(key);QVERIFY(::chmod(key.c_str(),0600)==0);QVERIFY(!(*anchor)->validateTransportSnapshot());QVERIFY(!(*child)->validateTransportSnapshot());
    }
  }
  void pureThenMixedPositionPublicationPreservesBothOriginalIdentityAnchors(){
    Fixture f;const auto key=mixedKey(f);const auto sibling=addSibling(f);setNetworks(f,sibling,"l3:I2P6:Publice");
    auto pure=sam::ProfileSource::createSession(*f.lease);QVERIFY(pure);auto child=(*pure)->prepareChild(f.hash);QVERIFY(child);auto prepared=(*child)->prepareStorage();QVERIFY(prepared);
    auto mixed=sam::ProfileSource::createMixedSession(*f.lease);QVERIFY(mixed);auto accepted=(*mixed)->prepareMixedChild(sibling);QVERIFY(accepted);
    const auto state=read(f.state()),mixedState=read(f.path/"active"/(sibling.hex()+".dat")),pureKey=read(f.key),mixedBytes=read(key);
    const std::array pools{storage::LibraryCompletionPool{f.hash,false},storage::LibraryCompletionPool{sibling,false}};
    auto moved=(*pure)->publishPositions([&]{return (*mixed)->publishPositions([&]()->std::expected<void,storage::Error>{auto value=storage::applyLibraryCommand(*f.lease,sibling,core::DownloadAction::MoveTop,pools);if(!value){return std::unexpected(value.error());}return {};});});
    QVERIFY2(moved,moved?"":moved.error().message.c_str());QVERIFY((*pure)->validate());QVERIFY((*mixed)->validate());QVERIFY((*child)->validate());QVERIFY((*accepted)->validate());
    QCOMPARE(read(f.state()),state);QCOMPARE(read(f.path/"active"/(sibling.hex()+".dat")),mixedState);QCOMPARE(read(f.key),pureKey);QCOMPARE(read(key),mixedBytes);
  }

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
