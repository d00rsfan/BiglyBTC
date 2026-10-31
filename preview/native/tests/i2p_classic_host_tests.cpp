// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_i2p_profile_fixture.hpp"
#include "session_bridge.hpp"
#include "persistent_session.hpp"
#include "i2p_saved_settings.hpp"
#include "coexist_coordinator.hpp"
#include "mixed_i2p_source.hpp"
#include <QScopeGuard>
#include "library_window.hpp"
#include "statistics_page.hpp"
#include "i2p_statistics_view.hpp"
#include "original_torrent_identity.hpp"
#include <algorithm>
#include <QTcpServer>
#include <QHostAddress>
#include <QApplication>
#include <QLabel>
#include <QSignalSpy>
#include <QtTest>
using namespace biglybtc;
class I2pClassicHostTests final:public QObject {
  Q_OBJECT
private slots:
  void coordinatorNormalizationUsesBothAnchorsAndRefusedPublicationLeavesThemCurrent(){
    namespace test=owned_i2p_test;namespace sam=services::i2p::provider;
    if(!engine::PersistentSession::mixedNetworksAvailable()){QSKIP("Qualified mixed SDK unavailable; existing source controls still run");}
    test::Fixture f;const auto mixedKey=f.key.parent_path()/"dest_key.dat";test::write(mixedKey,test::read(f.key));QVERIFY(::chmod(mixedKey.c_str(),0600)==0);
    auto state=core::Document::parse(test::read(f.state()));QVERIFY(state);QVERIFY(state->replaceValue("attributes",test::dict({{"networks","l6:Public3:I2Pe"},{"peersources","l7:Trackere"}})));test::write(f.state(),state->serialize());
    auto inventory=core::Document::parse(test::read(f.path/"downloads.config"));QVERIFY(inventory);
    const auto* rows=inventory->find("downloads");QVERIFY(rows&&rows->children.size()==1);
    auto row=core::Document::parse(inventory->originalBytes().substr(rows->children[0].begin,rows->children[0].end-rows->children[0].begin));QVERIFY(row);QVERIFY(row->replaceValue("position","i5e"));QVERIFY(inventory->replaceValue("downloads","l"+row->serialize()+"e"));test::write(f.path/"downloads.config",inventory->serialize());
    auto pure=sam::ProfileSource::createSession(*f.lease),mixed=sam::ProfileSource::createMixedSession(*f.lease);QVERIFY(pure&&mixed);
    auto child=(*mixed)->prepareMixedChild(f.hash);QVERIFY(child);
    auto made=engine::PersistentSession::create({9401,9402,9403});QVERIFY(made);auto owner=std::move(*made);
    const auto close=qScopeGuard([&]{static_cast<void>(owner->close(std::chrono::steady_clock::now()+std::chrono::seconds{2}));});
    core::QueuePolicy policy;policy.maximumDownloads=1;policy.maximumActive=1;policy.minimumDownloads=0;
    engine::NetworkPolicy network;QVERIFY(owner->configureNetwork(network));QVERIFY(owner->configureQueuePolicy(policy));QVERIFY(owner->configureExternalQueue(true));QVERIFY(owner->configureMixedNetworks());
    const auto positions=[&](const engine::SourceMutation& operation){return (*pure)->publishPositions([&]{return (*mixed)->publishPositions(operation);});};
    QVERIFY(owner->configureSourcePublication([&](const core::TorrentHash& hash,const auto& operation){return (*pure)->publish(hash,[&]{return (*mixed)->publish(hash,operation);});},positions));
    QVERIFY(owner->configureMixedSourceBinding([&](const auto& source){return (*child)->bindMixedManaged(source);}));
    QVERIFY(owner->startIdle(*f.lease));QVERIFY(owner->startManaged(*f.lease,{.hash=f.hash,.queued=true,.mixedNetworks=true}));
    const auto checked=[&]{static_cast<void>(owner->poll());const auto current=owner->managedDownloads();return current.size()==1&&current.front().checked;};QTRY_VERIFY_WITH_TIMEOUT(checked(),5000);
    const auto unchangedState=test::read(f.state()),config=test::read(f.path/"biglybt.config"),pureKey=test::read(f.key),mixedBytes=test::read(mixedKey),oldInventory=test::read(f.path/"downloads.config");
    app::CoexistCoordinator coordinator;coordinator.configure(policy);bool deny=false;int publications=0,operations=0;
    const engine::PositionsPublication publication=[&](const engine::SourceMutation& operation){++publications;return positions([&]()->std::expected<void,storage::Error>{
      if(deny){return std::unexpected(storage::Error{storage::ErrorCode::Conflict,"Controlled coordinator publication refusal"});}++operations;return operation();
    });};
    QVERIFY(coordinator.tick(*owner,nullptr,*f.lease,network,publication));QCOMPARE(publications,1);QCOMPARE(operations,1);QVERIFY(!coordinator.positionFailure());
    auto normalized=core::Document::parse(test::read(f.path/"downloads.config"));QVERIFY(normalized);auto library=core::readLibrary(*normalized);QVERIFY(library);QCOMPARE(library->downloads[0].position,std::optional<std::int64_t>{1});
    QVERIFY(test::read(f.path/"downloads.config")!=oldInventory);QVERIFY((*pure)->validate());QVERIFY((*mixed)->validate());QVERIFY((*child)->prepared());
    const auto currentInventory=test::read(f.path/"downloads.config");deny=true;coordinator.invalidatePositions();
    QVERIFY(coordinator.tick(*owner,nullptr,*f.lease,network,publication));QCOMPARE(publications,2);QCOMPARE(operations,1);QVERIFY(coordinator.positionFailure());QCOMPARE(coordinator.positionFailure()->message,std::string("Controlled coordinator publication refusal"));
    QCOMPARE(test::read(f.path/"downloads.config"),currentInventory);QVERIFY((*pure)->validate());QVERIFY((*mixed)->validate());QVERIFY((*child)->prepared());QVERIFY(!(*pure)->effectAuthority()->revoked());QVERIFY(!(*mixed)->effectAuthority()->revoked());
    QCOMPARE(test::read(f.state()),unchangedState);QCOMPARE(test::read(f.path/"biglybt.config"),config);QCOMPARE(test::read(f.key),pureKey);QCOMPARE(test::read(mixedKey),mixedBytes);
  }
  void privateImporterRefusesUnsupportedModesBeforeEffectsAndSavesI2pContext_data(){
    QTest::addColumn<bool>("paused");QTest::newRow("stopped")<<false;QTest::newRow("paused")<<true;
  }
  void privateImporterRefusesUnsupportedModesBeforeEffectsAndSavesI2pContext(){
    QFETCH(bool,paused);owned_i2p_test::Fixture f;
    const auto empty=owned_i2p_test::dict({{"downloads","le"},{"future",owned_i2p_test::b("retained inventory")}});
    owned_i2p_test::write(f.path/"downloads.config",empty);QVERIFY(std::filesystem::remove_all(f.path/"active")>0);
    const auto config=owned_i2p_test::read(f.path/"biglybt.config"),key=owned_i2p_test::read(f.key),input=owned_i2p_test::read(f.path/"fixture.torrent");
    const auto destination=f.path/"absent-import-data";f.lease.reset();
    QTcpServer router;QVERIFY(router.listen(QHostAddress::LocalHost,0));QSignalSpy connections(&router,&QTcpServer::newConnection);
    app::I2pServiceOptions options;options.routerPort=router.serverPort();app::SessionBridge bridge(nullptr,{},{},{},options);
    QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),snapshots(&bridge,&app::SessionBridge::snapshotReady),reads(&bridge,&app::SessionBridge::torrentIntakeReady),writes(&bridge,&app::SessionBridge::torrentIntakeFinished),native(&bridge,&app::SessionBridge::nativeSessionReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.openNormal(f.path));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty()&&!snapshots.empty(),10000);
    const auto initial=qvariant_cast<ui::Presentation>(snapshots.last()[0]);QVERIFY(initial);QVERIFY(initial->items.empty());
    const auto session=sessions.last()[0].toULongLong();QVERIFY(bridge.previewTorrentIntake({session,1,QString::fromStdString((f.path/"fixture.torrent").string())}));
    QTRY_COMPARE_WITH_TIMEOUT(reads.size(),1,10000);QTRY_VERIFY(!bridge.busy());const auto preview=qvariant_cast<ui::TorrentIntakePresentation>(reads.last()[0]);
    QVERIFY(preview);QVERIFY2(ui::validTorrentIntake(preview),qPrintable(preview->failure));QVERIFY(preview->i2pOnly);QCOMPARE(preview->hash,QString::fromStdString(f.hash.hex()));
    ui::TorrentIntakeSubmission accepted;accepted.session=session;accepted.request=1;accepted.revision=preview->revision;accepted.destination=QString::fromStdString(destination.string());accepted.start=false;accepted.decoderShowAll=preview->decoderShowAll;
    for(const auto& file:preview->files){accepted.files.push_back({file.padding,0});}
    int responses=0;
    for(int kind=0;kind<5;++kind){
      auto forged=accepted;
      if(kind==0){forged.start=true;forged.forceStart=true;}
      else if(kind==1){forged.verifyExisting=true;}
      else if(kind==2){forged.goal=core::ExistingVerificationGoal::SelectedFiles;}
      else{forged.allocation=kind==3?core::TorrentAllocationMode::Stopped:core::TorrentAllocationMode::Paused;}
      QVERIFY(bridge.importTorrentFiles(forged));++responses;QTRY_COMPARE_WITH_TIMEOUT(writes.size(),responses,10000);QTRY_VERIFY(!bridge.busy());
      const auto denied=qvariant_cast<ui::TorrentIntakeResultPresentation>(writes.last()[0]);QVERIFY(denied);QVERIFY(!denied->imported&&!denied->started&&!denied->verificationPending&&!denied->allocationPending);
      QVERIFY2(denied->failure.contains(QStringLiteral("private I2P importer")),qPrintable(denied->failure));
      QCOMPARE(owned_i2p_test::read(f.path/"downloads.config"),empty);QCOMPARE(owned_i2p_test::read(f.path/"biglybt.config"),config);QCOMPARE(owned_i2p_test::read(f.key),key);QCOMPARE(owned_i2p_test::read(f.path/"fixture.torrent"),input);
      QVERIFY(!std::filesystem::exists(f.path/"active"));QVERIFY(!std::filesystem::exists(f.path/"torrents"));QVERIFY(!std::filesystem::exists(f.path/"BiglyBTC.torrent-import"));QVERIFY(!std::filesystem::exists(destination));QCOMPARE(native.size(),0);QCOMPARE(connections.size(),0);QVERIFY(!router.hasPendingConnections());
    }
    accepted.paused=paused;QVERIFY(bridge.importTorrentFiles(accepted));++responses;QTRY_COMPARE_WITH_TIMEOUT(writes.size(),responses,10000);QTRY_VERIFY(!bridge.busy());
    const auto added=qvariant_cast<ui::TorrentIntakeResultPresentation>(writes.last()[0]);QVERIFY(added);QVERIFY2(added->imported,qPrintable(added->failure));QVERIFY(!added->started&&!added->verificationPending&&!added->allocationPending);QCOMPARE(added->hash,preview->hash);
    QVERIFY(!std::filesystem::exists(destination));QCOMPARE(native.size(),0);QCOMPARE(connections.size(),0);QVERIFY(!router.hasPendingConnections());
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,10000);QVERIFY(closed.front()[1].toBool()&&closed.front()[2].toBool());
    auto held=storage::ProfileLease::acquire(f.path);QVERIFY(held);
    QCOMPARE(owned_i2p_test::read(f.path/"biglybt.config"),config);QCOMPARE(owned_i2p_test::read(f.key),key);QCOMPARE(owned_i2p_test::read(f.path/"fixture.torrent"),input);QCOMPARE(owned_i2p_test::read(f.path/"torrents"/(f.hash.hex()+".torrent")),input);
    const auto inventory=core::Document::parse(owned_i2p_test::read(f.path/"downloads.config"));QVERIFY(inventory);const auto library=core::readLibrary(*inventory);QVERIFY(library);QCOMPARE(library->downloads.size(),std::size_t{1});
    QVERIFY(library->downloads[0].hash&&*library->downloads[0].hash==f.hash);QCOMPARE(library->downloads[0].startup,core::StartupState::Stopped);QVERIFY(!library->downloads[0].forceStart);QCOMPARE(library->paused.size(),paused?std::size_t{1}:std::size_t{0});
    if(paused){QVERIFY(library->paused[0].hash==f.hash&&!library->paused[0].forceStart);}
    const auto* future=inventory->find("future");QVERIFY(future);QCOMPARE(future->bytes,std::string("retained inventory"));
    const auto state=core::Document::parse(owned_i2p_test::read(f.state()));QVERIFY(state);const auto* attributes=state->find("attributes");QVERIFY(attributes);
    for(const auto& field:std::array<std::pair<const char*,const char*>,2>{{{"networks","I2P"},{"peersources","Tracker"}}}){
      const auto* values=attributes->find(field.first);QVERIFY(values);QCOMPARE(values->type,core::Node::Type::List);QCOMPARE(values->children.size(),std::size_t{1});QCOMPARE(values->children[0].bytes,std::string(field.second));
    }
    const auto raw=storage::originalTorrentInfoSubject(*state);QVERIFY(raw);QCOMPARE(std::string(*raw),f.info);QVERIFY(!std::filesystem::exists(destination));QVERIFY(!std::filesystem::exists(f.path/"BiglyBTC.torrent-import"));QCOMPARE(native.size(),0);QCOMPARE(connections.size(),0);
  }
  void savedPureTunnelSettingsPreserveDefaultsOverridesAndMalformedBytes(){
    const auto document=core::Document::parse("de");QVERIFY(document);
    auto options=app::readSavedI2pOptions(*document,{});QVERIFY(options);QCOMPARE(options->sessionOptions.size(),6);
    const auto value=[&](std::string key){for(const auto& pair:options->sessionOptions){if(pair.first==key){return pair.second;}}return std::string{};};
    QCOMPARE(value("inbound.length"),std::string("3"));QCOMPARE(value("outbound.quantity"),std::string("2"));
    app::I2pServiceOptions explicitOptions;explicitOptions.sessionOptions={{"inbound.length","0"}};
    options=app::readSavedI2pOptions(*document,explicitOptions);QVERIFY(options);QCOMPARE(value("inbound.length"),std::string("0"));
    const auto raw=owned_i2p_test::dict({{"Plugin.azneti2phelper.azi2phelper.i2p.inbound.hops",owned_i2p_test::b("invalid")}});
    const auto bad=core::Document::parse(raw);QVERIFY(bad);QVERIFY(!app::readSavedI2pOptions(*bad,{}));QCOMPARE(bad->serialize(),raw);
  }
  void currentI2pMeasurementsAreIsolatedFromOtherProfileAndPublicStatistics(){
    ui::StatisticsPage page;page.beginNativeSession(11);
    auto p=std::make_shared<ui::I2pPacket>();p->session=11;p->sequence=1;p->state="Downloading — I2P";p->hash="original-hash";p->payloadDownloaded=1234;p->verifiedBytes=1000;p->trackerReplies=2;
    page.setI2p(p);auto* view=page.findChild<ui::I2pStatisticsView*>("statistics-i2p");QVERIFY(view);QCOMPARE(view->presentation()->payloadDownloaded,1234);
    auto foreign=std::make_shared<ui::I2pPacket>(*p);foreign->session=12;foreign->sequence=2;foreign->payloadDownloaded=9999;page.setI2p(foreign);QCOMPARE(view->presentation()->payloadDownloaded,1234);
    auto stale=std::make_shared<ui::I2pPacket>(*p);stale->payloadDownloaded=2;page.setI2p(stale);QCOMPARE(view->presentation()->payloadDownloaded,1234);
    page.beginNativeSession(12);QVERIFY(!view->presentation());page.setI2p(p);QVERIFY(!view->presentation());
    foreign->sequence=1;page.setI2p(foreign);QCOMPARE(view->presentation()->payloadDownloaded,9999);
    ui::StatisticsNavigation nav;nav.section=10;QVERIFY(ui::validStatisticsNavigation(nav));
  }
  void privateModeKeepsOriginalStoppedIntentAndRefusesPublicFallback(){
    owned_i2p_test::Fixture f(true);const auto config=owned_i2p_test::read(f.path/"biglybt.config"),key=owned_i2p_test::read(f.key);f.lease.reset();
    app::I2pServiceOptions options;app::SessionBridge bridge(nullptr,{},{},{},options);
    QSignalSpy snapshots(&bridge,&app::SessionBridge::snapshotReady),closed(&bridge,&app::SessionBridge::sessionClosed),observed(&bridge,&app::SessionBridge::i2pReady);
    QVERIFY(bridge.openNormal(f.path));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!snapshots.empty(),10000);
    const auto packet=qvariant_cast<ui::Presentation>(snapshots.last()[0]);QVERIFY(packet);QCOMPARE(packet->items.size(),1);QCOMPARE(packet->items.front().actionMask,0);QCOMPARE(packet->items.front().state,QString("Error"));
    QCOMPARE(owned_i2p_test::read(f.path/"biglybt.config"),config);QCOMPARE(owned_i2p_test::read(f.key),key);QTRY_VERIFY(!observed.empty());
    auto lease=storage::ProfileLease::acquire(f.path);QVERIFY(!lease);QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,10000);QVERIFY(closed.front()[1].toBool());QVERIFY(closed.front()[2].toBool());
    lease=storage::ProfileLease::acquire(f.path);QVERIFY(lease);
  }
  void stoppedPureI2pRejectsUnadvertisedPause(){
    owned_i2p_test::Fixture f;auto inventory=owned_i2p_test::read(f.path/"downloads.config");auto pos=inventory.find("5:statei75e");QVERIFY(pos!=std::string::npos);inventory.replace(pos,11,"5:statei70e");owned_i2p_test::write(f.path/"downloads.config",inventory);f.lease.reset();
    app::SessionBridge bridge(nullptr,{},{},{},app::I2pServiceOptions{});QSignalSpy snapshots(&bridge,&app::SessionBridge::snapshotReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.openNormal(f.path));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!snapshots.empty(),10000);
    auto packet=qvariant_cast<ui::Presentation>(snapshots.last()[0]);QVERIFY(packet);QCOMPARE(packet->items.front().state,QString("Stopped"));
    const ui::Target target{packet->items.front().hash,packet->revision};QVERIFY(packet->items.front().actionMask&ui::downloadActionBit(core::DownloadAction::Start));
    QVERIFY(bridge.downloadAction({target},core::DownloadAction::Pause));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000);
    // Pause was not advertised for a stopped row: stale/forged UI intent cannot mutate disk.
    QCOMPARE(owned_i2p_test::read(f.path/"downloads.config"),inventory);
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,10000);
  }

  void mixedSavedNetworksNeverFallBackWithoutExplicitMode_data(){
    QTest::addColumn<bool>("privateMode");QTest::addColumn<bool>("reverse");
    QTest::newRow("default-public-first")<<false<<false;QTest::newRow("default-i2p-first")<<false<<true;
    QTest::newRow("legacy-private-public-first")<<true<<false;QTest::newRow("legacy-private-i2p-first")<<true<<true;
  }
  void mixedSavedNetworksNeverFallBackWithoutExplicitMode(){
    QFETCH(bool,privateMode);QFETCH(bool,reverse);owned_i2p_test::Fixture f;
    auto state=core::Document::parse(owned_i2p_test::read(f.state()));QVERIFY(state);const auto* attributes=state->find("attributes");QVERIFY(attributes);
    auto attrs=core::Document::parse(state->originalBytes().substr(attributes->begin,attributes->end-attributes->begin));QVERIFY(attrs);
    QVERIFY(attrs->replaceValue("networks",reverse?"l3:I2P6:Publice":"l6:Public3:I2Pe"));QVERIFY(state->replaceValue("attributes",attrs->serialize()));
    const auto exactState=state->serialize();owned_i2p_test::write(f.state(),exactState);
    const auto inventory=owned_i2p_test::read(f.path/"downloads.config"),config=owned_i2p_test::read(f.path/"biglybt.config"),key=owned_i2p_test::read(f.key);f.lease.reset();
    QTcpServer router;QVERIFY(router.listen(QHostAddress::LocalHost,0));QSignalSpy connections(&router,&QTcpServer::newConnection);
    std::optional<app::I2pServiceOptions> options;if(privateMode){options.emplace();options->routerPort=router.serverPort();}
    app::SessionBridge bridge(nullptr,{},{},{},options);QSignalSpy snapshots(&bridge,&app::SessionBridge::snapshotReady),closed(&bridge,&app::SessionBridge::sessionClosed),native(&bridge,&app::SessionBridge::nativeSessionReady);
    QVERIFY(bridge.open(f.path,0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!snapshots.empty(),10000);
    auto current=qvariant_cast<ui::Presentation>(snapshots.last()[0]);QVERIFY(current);QCOMPARE(current->items.size(),std::size_t{1});const auto row=current->items.front();
    QCOMPARE(row.actionMask,quint16{0});QCOMPARE(row.providerAdmission,quint64{0});QCOMPARE(row.providerGeneration,quint64{0});QVERIFY(row.networkStatus.contains(QStringLiteral("Public + I2P")));QVERIFY(row.networkStatus.contains(QStringLiteral("unavailable"),Qt::CaseInsensitive));
    // A direct forged command cannot infer Public permission from the two tokens.
    QVERIFY(bridge.downloadAction({ui::Target{row.hash,current->revision}},core::DownloadAction::Start));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy(),10000);
    QCOMPARE(owned_i2p_test::read(f.state()),exactState);QCOMPARE(owned_i2p_test::read(f.path/"downloads.config"),inventory);QCOMPARE(owned_i2p_test::read(f.path/"biglybt.config"),config);QCOMPARE(owned_i2p_test::read(f.key),key);
    QVERIFY(!std::filesystem::exists(std::filesystem::path(f.payloadDirectory.path().toStdString())/"owned.bin"));QCOMPARE(connections.size(),0);QCOMPARE(native.size(),0);QVERIFY(!router.hasPendingConnections());
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,10000);QVERIFY(closed.front()[1].toBool()&&closed.front()[2].toBool());auto lease=storage::ProfileLease::acquire(f.path);QVERIFY(lease);QCOMPARE(owned_i2p_test::read(f.state()),exactState);
  }

  void mixedIntakeCapabilityComesFromCurrentHostMode_data(){
    QTest::addColumn<int>("mode");QTest::newRow("default")<<0;QTest::newRow("legacy-private")<<1;QTest::newRow("explicit-coexistence")<<2;
  }
  void mixedIntakeCapabilityComesFromCurrentHostMode(){
    QFETCH(int,mode);owned_i2p_test::Fixture f;
    const auto empty=owned_i2p_test::dict({{"downloads","le"},{"future",owned_i2p_test::b("mixed-retained")}});owned_i2p_test::write(f.path/"downloads.config",empty);QVERIFY(std::filesystem::remove_all(f.path/"active")>0);
    auto settings=core::Document::parse(owned_i2p_test::read(f.path/"biglybt.config"));QVERIFY(settings);
    QVERIFY(settings->replaceValue("Bind IP",owned_i2p_test::b("127.0.0.1")));
    for(const auto* field:{"TCP.Listen.Port.Enable","peercontrol.udp.public.enable","Plugin.DHT.dht.enabled","Peer Source Selection Default.DHT","Peer Source Selection Default.PeerExchange"}){QVERIFY(settings->replaceValue(field,"i0e"));}
    owned_i2p_test::write(f.path/"biglybt.config",settings->serialize());const auto config=settings->serialize(),key=owned_i2p_test::read(f.key),input=owned_i2p_test::read(f.path/"fixture.torrent");const auto destination=std::filesystem::path(f.payloadDirectory.path().toStdString())/"new-import";f.lease.reset();
    QTcpServer router;QVERIFY(router.listen(QHostAddress::LocalHost,0));QSignalSpy connections(&router,&QTcpServer::newConnection);
    std::optional<app::I2pServiceOptions> options;if(mode){options.emplace();options->routerPort=router.serverPort();options->publicCoexistence=mode==2;}
    app::SessionBridge bridge(nullptr,{},{},{},options);QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),snapshots(&bridge,&app::SessionBridge::snapshotReady),reads(&bridge,&app::SessionBridge::torrentIntakeReady),writes(&bridge,&app::SessionBridge::torrentIntakeFinished),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(mode==0?bridge.open(f.path,0):bridge.openNormal(f.path));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty()&&!snapshots.empty(),10000);
    const auto session=sessions.last()[0].toULongLong();QVERIFY(bridge.previewTorrentIntake({session,1,QString::fromStdString((f.path/"fixture.torrent").string())}));QTRY_COMPARE_WITH_TIMEOUT(reads.size(),1,10000);QTRY_VERIFY(!bridge.busy());
    const auto preview=qvariant_cast<ui::TorrentIntakePresentation>(reads.last()[0]);QVERIFY(preview);QVERIFY2(ui::validTorrentIntake(preview),qPrintable(preview->failure));QCOMPARE(preview->i2pOnly,mode==1);QCOMPARE(preview->i2pAvailable,mode==2);
    const bool capable=mode==2&&engine::PersistentSession::mixedNetworksAvailable();QCOMPARE(preview->mixedAvailable,capable);
    ui::TorrentIntakeSubmission submitted;submitted.session=session;submitted.request=1;submitted.revision=preview->revision;submitted.destination=QString::fromStdString(destination.string());submitted.start=false;submitted.decoderShowAll=preview->decoderShowAll;submitted.network=ui::TorrentIntakeNetwork::Mixed;
    for(const auto& file:preview->files){submitted.files.push_back({file.padding,0});}
    // Real capability permits stopped permission persistence, never a synthetic
    // live session; the separately fenced transfer fixture supplies that proof.
    QVERIFY(bridge.importTorrentFiles(submitted));QTRY_COMPARE_WITH_TIMEOUT(writes.size(),1,10000);QTRY_VERIFY(!bridge.busy());
    const auto result=qvariant_cast<ui::TorrentIntakeResultPresentation>(writes.last()[0]);QVERIFY(result);QCOMPARE(result->imported,capable);QVERIFY(!result->started&&!result->verificationPending&&!result->allocationPending);QVERIFY(!std::filesystem::exists(destination));QCOMPARE(connections.size(),0);QVERIFY(!router.hasPendingConnections());
    if(capable){
      const auto saved=core::Document::parse(owned_i2p_test::read(f.state()));QVERIFY(saved);const auto* attrs=saved->find("attributes");QVERIFY(attrs);const auto* networks=attrs->find("networks");QVERIFY(networks);QCOMPARE(networks->children.size(),std::size_t{2});QCOMPARE(networks->children[0].bytes,std::string("Public"));QCOMPARE(networks->children[1].bytes,std::string("I2P"));
      const auto inventory=core::Document::parse(owned_i2p_test::read(f.path/"downloads.config"));QVERIFY(inventory);const auto library=core::readLibrary(*inventory);QVERIFY(library);QCOMPARE(library->downloads.size(),std::size_t{1});QCOMPARE(library->downloads[0].startup,core::StartupState::Stopped);QVERIFY(!library->downloads[0].forceStart);const auto* future=inventory->find("future");QVERIFY(future);QCOMPARE(future->bytes,std::string("mixed-retained"));QCOMPARE(owned_i2p_test::read(f.path/"torrents"/(f.hash.hex()+".torrent")),input);
    }else{QVERIFY(!result->failure.isEmpty());QCOMPARE(owned_i2p_test::read(f.path/"downloads.config"),empty);QVERIFY(!std::filesystem::exists(f.path/"active"));QVERIFY(!std::filesystem::exists(f.path/"torrents"));}
    QCOMPARE(owned_i2p_test::read(f.path/"biglybt.config"),config);QCOMPARE(owned_i2p_test::read(f.key),key);QCOMPARE(owned_i2p_test::read(f.path/"fixture.torrent"),input);QVERIFY(!std::filesystem::exists(f.path/"BiglyBTC.torrent-import"));
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,10000);QVERIFY(closed.front()[1].toBool()&&closed.front()[2].toBool());auto held=storage::ProfileLease::acquire(f.path);QVERIFY(held);
  }

  void savedMixedAndPureTunnelSettingsUseDistinctOriginalKeys(){
    const auto property=[](const app::I2pServiceOptions& options,std::string_view key){for(const auto& entry:options.sessionOptions){if(entry.first==key){return entry.second;}}return std::string{};};
    auto empty=core::Document::parse("de");QVERIFY(empty);auto pure=app::readSavedI2pOptions(*empty,{}),mixed=app::readSavedMixedI2pOptions(*empty,{});QVERIFY(pure&&mixed);
    for(const auto* key:{"inbound.length","outbound.length"}){QCOMPARE(property(*pure,key),std::string("3"));QCOMPARE(property(*mixed,key),std::string("1"));}
    for(const auto* key:{"inbound.quantity","outbound.quantity"}){QCOMPARE(property(*pure,key),std::string("2"));QCOMPARE(property(*mixed,key),std::string("2"));}
    for(const auto* key:{"inbound.lengthVariance","outbound.lengthVariance"}){QCOMPARE(property(*pure,key),std::string("0"));QCOMPARE(property(*mixed,key),std::string("0"));}
    const auto raw=owned_i2p_test::dict({{"Plugin.azneti2phelper.azi2phelper.i2p.inbound.hops","i5e"},{"Plugin.azneti2phelper.azi2phelper.mix.inbound.hops","i2e"},{"Plugin.azneti2phelper.azi2phelper.i2p.outbound.quantity","i3e"},{"Plugin.azneti2phelper.azi2phelper.mix.outbound.quantity","i4e"},{"future",owned_i2p_test::b(std::string("x\0y",3))}});
    auto configured=core::Document::parse(raw);QVERIFY(configured);pure=app::readSavedI2pOptions(*configured,{});mixed=app::readSavedMixedI2pOptions(*configured,{});QVERIFY(pure&&mixed);QCOMPARE(property(*pure,"inbound.length"),std::string("5"));QCOMPARE(property(*mixed,"inbound.length"),std::string("2"));QCOMPARE(property(*pure,"outbound.quantity"),std::string("3"));QCOMPARE(property(*mixed,"outbound.quantity"),std::string("4"));
    app::I2pServiceOptions explicitOptions;explicitOptions.routerPort=12345;explicitOptions.peerPort=4567;explicitOptions.sessionOptions={{"inbound.length","0"},{"outbound.quantity","7"},{"i2cp.leaseSetEncType","4"}};
    for(bool isMixed:{false,true}){const auto options=isMixed?app::readSavedMixedI2pOptions(*configured,explicitOptions):app::readSavedI2pOptions(*configured,explicitOptions);QVERIFY(options);QCOMPARE(options->routerPort,std::uint16_t{12345});QCOMPARE(options->peerPort,std::uint16_t{4567});QCOMPARE(property(*options,"inbound.length"),std::string("0"));QCOMPARE(property(*options,"outbound.quantity"),std::string("7"));QCOMPARE(property(*options,"i2cp.leaseSetEncType"),std::string("4"));QCOMPARE(std::ranges::count_if(options->sessionOptions,[](const auto& entry){return entry.first=="inbound.length";}),1);}
    QCOMPARE(configured->serialize(),raw);
    struct Setting{const char* key;int below,above;};const std::array<Setting,6> fields{{{"inbound.hops",-1,8},{"outbound.hops",-1,8},{"inbound.quantity",0,17},{"outbound.quantity",0,17},{"inbound.var",-4,4},{"outbound.var",-4,4}}};
    for(bool isMixed:{false,true}){for(const auto& field:fields){for(int invalid:{field.below,field.above}){
      const auto key=std::string("Plugin.azneti2phelper.azi2phelper.")+(isMixed?"mix.":"i2p.")+field.key;const auto bytes=owned_i2p_test::dict({{key,"i"+std::to_string(invalid)+"e"}});const auto bad=core::Document::parse(bytes);QVERIFY(bad);
      const auto result=isMixed?app::readSavedMixedI2pOptions(*bad,{}):app::readSavedI2pOptions(*bad,{});QVERIFY(!result);QCOMPARE(result.error().code,storage::ErrorCode::Unsupported);QCOMPARE(bad->serialize(),bytes);
    }}}
    for(bool isMixed:{false,true}){
      const auto key=std::string("Plugin.azneti2phelper.azi2phelper.")+(isMixed?"mix.":"i2p.")+"inbound.hops";const auto bytes=owned_i2p_test::dict({{key,owned_i2p_test::b("invalid")}});const auto bad=core::Document::parse(bytes);QVERIFY(bad);
      const auto denied=isMixed?app::readSavedMixedI2pOptions(*bad,explicitOptions):app::readSavedI2pOptions(*bad,explicitOptions);QVERIFY(!denied);QCOMPARE(denied.error().code,storage::ErrorCode::InvalidFormat);
      const auto other=isMixed?app::readSavedI2pOptions(*bad,{}):app::readSavedMixedI2pOptions(*bad,{});QVERIFY(other);QCOMPARE(property(*other,"inbound.length"),isMixed?std::string("3"):std::string("1"));QCOMPARE(bad->serialize(),bytes);
    }
  }
};
QTEST_MAIN(I2pClassicHostTests)
#include "i2p_classic_host_tests.moc"
