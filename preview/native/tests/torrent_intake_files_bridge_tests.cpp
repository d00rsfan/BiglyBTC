#include "session_bridge.hpp"
#include "allocation_fixture.hpp"
#include <QTcpServer>
#include <QHostAddress>
#include "owned_engine_fixture.hpp"
#include "selected_swarm_fixture.hpp"
#include "persistent_session.hpp"
#include "torrent_intake.hpp"
#include "verified_seeding_fixture.hpp"
#include "biglybtc/file_policy.hpp"
#include "biglybtc/saved_dnd.hpp"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QtTest>
#include <chrono>

using namespace biglybtc;
namespace {
core::Document document(const QString& path) {
  const auto bytes=test::read(path);auto result=core::Document::parse(bytes.toStdString(),storage::compatibleConfigLimits());
  test::require(result.has_value(),"Generated actual saved document failed to parse");return std::move(*result);
}
ui::TorrentIntakePresentation preview(const QSignalSpy& spy){return qvariant_cast<ui::TorrentIntakePresentation>(spy.last()[0]);}
ui::TorrentIntakeResultPresentation result(const QSignalSpy& spy){return qvariant_cast<ui::TorrentIntakeResultPresentation>(spy.last()[0]);}
void emptyProfile(const QString& profile) {
  test::require(QDir().mkpath(profile),"Create owned separate import profile");
  test::write(profile+QStringLiteral("/downloads.config"),"d9:downloadsle7:unknownli7e3:rawee");
  test::write(profile+QStringLiteral("/tables.config"),"de");
}
}
// Actual source-qualified owner and actual numeric-loopback peer. Its two files
// have disjoint pieces; the skipped file must remain byte-for-byte unmodified.
class TorrentIntakeFilesBridgeTests final:public QObject {
  Q_OBJECT
private slots:

  void actualAllocationIntakeRoutesModesWithoutConnectingOrInventingCompletion_data(){QTest::addColumn<bool>("paused");QTest::newRow("allocate-stop")<<false;QTest::newRow("allocate-pause")<<true;}
  void actualAllocationIntakeRoutesModesWithoutConnectingOrInventingCompletion(){
    QFETCH(bool,paused);test::allocation::Fixture f;f.lease.reset();QTcpServer outsider;QVERIFY(outsider.listen(QHostAddress::LocalHost,0));
    app::SessionBridge bridge;QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),reads(&bridge,&app::SessionBridge::torrentIntakeReady),writes(&bridge,&app::SessionBridge::torrentIntakeFinished),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(f.profile,outsider.serverPort()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty(),10000);const auto session=sessions[0][0].toULongLong();
    QVERIFY(bridge.previewTorrentIntake({session,1,QString::fromStdString(f.metadata.string())}));QTRY_COMPARE_WITH_TIMEOUT(reads.size(),1,10000);QTRY_VERIFY(!bridge.busy());const auto frame=preview(reads);QVERIFY2(ui::validTorrentIntake(frame),qPrintable(frame->failure));
    ui::TorrentIntakeSubmission submitted;submitted.session=session;submitted.request=1;submitted.revision=frame->revision;submitted.destination=QString::fromStdString(f.payload.string());submitted.start=false;submitted.files={{false,0}};submitted.decoderShowAll=frame->decoderShowAll;submitted.allocation=paused?core::TorrentAllocationMode::Paused:core::TorrentAllocationMode::Stopped;
    QVERIFY(bridge.importTorrentFiles(submitted));QTRY_COMPARE_WITH_TIMEOUT(writes.size(),1,10000);QTRY_VERIFY(!bridge.busy());const auto added=result(writes);QVERIFY2(added&&added->imported&&added->allocationPending&&!added->started&&!added->verificationPending,qPrintable(added->failure));
    QTRY_VERIFY_WITH_TIMEOUT([&]{QFile raw(QString::fromStdString((f.profile/"downloads.config").string()));if(!raw.open(QIODevice::ReadOnly)){return false;}const auto value=core::Document::parse(raw.readAll().toStdString(),storage::compatibleConfigLimits());if(!value){return false;}const auto* rows=value->find("downloads");if(!rows||rows->children.size()!=1){return false;}const auto* marker=rows->children[0].find(storage::AllocationStateKey);return marker&&marker->type==core::Node::Type::Integer&&marker->integer==2;}(),10000);
    QVERIFY(!outsider.hasPendingConnections());QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,10000);QVERIFY(closed[0][1].toBool()&&closed[0][2].toBool());
    auto held=storage::ProfileLease::acquire(f.profile);QVERIFY(held);f.lease.emplace(std::move(*held));const auto saved=f.saved();const auto& row=f.row(saved);QCOMPARE(row.find("state")->integer,std::int64_t{70});QCOMPARE(row.find("allocated")->integer,std::int64_t{1});QCOMPARE(row.find("downloaded")->integer,std::int64_t{0});QCOMPARE(row.find("uploaded")->integer,std::int64_t{0});QCOMPARE(row.find("completedbytes")->integer,std::int64_t{0});QCOMPARE(bool(saved.find("pause_data")),paused);QVERIFY(f.zeroes());QVERIFY(!outsider.hasPendingConnections());
  }

  void offlineDecoderCapabilityIsSealedByHeldProfileAndComparedBeforeCommit(){
    QTemporaryDir directory;QVERIFY(directory.isValid());const auto profile=directory.path()+QStringLiteral("/decoder-profile");emptyProfile(profile);const auto input=directory.path()+QStringLiteral("/source.torrent"),destination=directory.path()+QStringLiteral("/absent-data");
    // Structurally valid stopped v1 metadata only; no fabricated seed/progress.
    QByteArray metadata("d8:encoding6:IBM8664:infod6:lengthi1e4:name1:");metadata+=char(0x80);metadata+="12:piece lengthi16384e6:pieces20:";metadata+=QByteArray(20,'h');metadata+="ee";test::write(input,metadata);
    app::SessionBridge bridge;QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),reads(&bridge,&app::SessionBridge::torrentIntakeReady),writes(&bridge,&app::SessionBridge::torrentIntakeFinished),native(&bridge,&app::SessionBridge::nativeSessionReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(profile.toStdString(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty(),10000);const auto session=sessions[0][0].toULongLong();
    QVERIFY(bridge.previewTorrentIntake({session,1,input}));QTRY_COMPARE_WITH_TIMEOUT(reads.size(),1,10000);QTRY_VERIFY(!bridge.busy());QVERIFY(!ui::validTorrentIntake(preview(reads)));QVERIFY(!QFile::exists(profile+QStringLiteral("/biglybt.config")));
    test::write(profile+QStringLiteral("/biglybt.config"),"d20:File.Decoder.ShowAlli1e7:unknown3:rawe");QVERIFY(bridge.previewTorrentIntake({session,2,input}));QTRY_COMPARE_WITH_TIMEOUT(reads.size(),2,10000);QTRY_VERIFY(!bridge.busy());const auto frame=preview(reads);QVERIFY2(ui::validTorrentIntake(frame),qPrintable(frame->failure));QVERIFY(frame->decoderShowAll);QCOMPARE(frame->name,QString::fromUtf8("А"));
    ui::TorrentIntakeSubmission accepted{session,frame->source.request,frame->revision,destination,false,{{false,0}}};
    QVERIFY(bridge.importTorrentFiles(accepted));QTRY_COMPARE_WITH_TIMEOUT(writes.size(),1,10000);QTRY_VERIFY(!bridge.busy());QVERIFY(!result(writes)->imported); // A client cannot omit/change the sealed worker fact.
    const auto inventory=test::read(profile+QStringLiteral("/downloads.config"));accepted.decoderShowAll=frame->decoderShowAll;test::write(profile+QStringLiteral("/biglybt.config"),"d20:File.Decoder.ShowAlli0e7:unknown3:rawe");
    QVERIFY(bridge.importTorrentFiles(accepted));QTRY_COMPARE_WITH_TIMEOUT(writes.size(),2,10000);QTRY_VERIFY(!bridge.busy());QVERIFY(!result(writes)->imported);QCOMPARE(test::read(profile+QStringLiteral("/downloads.config")),inventory);QVERIFY(!QFile::exists(profile+QStringLiteral("/active")));QVERIFY(!QFile::exists(profile+QStringLiteral("/BiglyBTC.torrent-import")));QVERIFY(!QFile::exists(destination));
    test::write(profile+QStringLiteral("/biglybt.config"),"d20:File.Decoder.ShowAlli1e7:unknown3:rawe");QVERIFY(bridge.importTorrentFiles(accepted));QTRY_COMPARE_WITH_TIMEOUT(writes.size(),3,10000);QTRY_VERIFY(!bridge.busy());QVERIFY2(result(writes)->imported,qPrintable(result(writes)->failure));QVERIFY(!result(writes)->started);QCOMPARE(native.size(),0);QVERIFY(!QFile::exists(destination));QCOMPARE(test::read(input),metadata);QCOMPARE(test::read(profile+QStringLiteral("/torrents/")+frame->hash+QStringLiteral(".torrent")),metadata);
    const auto source=document(input),saved=document(profile+QStringLiteral("/active/")+frame->hash+QStringLiteral(".dat"));const auto rawInfo=storage::originalTorrentInfoSubject(source),savedInfo=storage::originalTorrentInfoSubject(saved);QVERIFY(rawInfo&&savedInfo);QCOMPARE(std::string(*rawInfo),std::string(*savedInfo));QVERIFY(document(profile+QStringLiteral("/biglybt.config")).find("unknown"));
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,10000);QVERIFY(closed[0][1].toBool()&&closed[0][2].toBool());
  }

  void actualVerifiedIntakeCommitsStoppedBeforeOwnerChecksAndNeverDownloads_data(){QTest::addColumn<bool>("corrupt");QTest::newRow("complete")<<false;QTest::newRow("corrupt")<<true;}
  void actualVerifiedIntakeCommitsStoppedBeforeOwnerChecksAndNeverDownloads(){
    QFETCH(bool,corrupt);QTemporaryDir directory;QVERIFY(directory.isValid());const auto fixture=test::prepareVerifiedSeedCase(directory.path()+"/owned",false,corrupt);
    // This independent seed supplies only a real owned numeric startup endpoint;
    // verified admission deliberately does NOT inherit/connect that shortcut.
    test::OwnedEngineFixture endpoint(false);QTRY_VERIFY_WITH_TIMEOUT(endpoint.ready(),30000);
    app::SessionBridge bridge;QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),reads(&bridge,&app::SessionBridge::torrentIntakeReady),writes(&bridge,&app::SessionBridge::torrentIntakeFinished),closed(&bridge,&app::SessionBridge::sessionClosed),rows(&bridge,&app::SessionBridge::snapshotReady);
    QVERIFY(bridge.open(fixture.profile.toStdString(),endpoint.port()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty(),10000);
    const auto session=sessions[0][0].toULongLong();QVERIFY(bridge.previewTorrentIntake({session,1,fixture.metadata}));QTRY_COMPARE_WITH_TIMEOUT(reads.size(),1,10000);QTRY_VERIFY(!bridge.busy());
    const auto frame=preview(reads);QVERIFY(ui::validTorrentIntake(frame));ui::TorrentIntakeSubmission submission;submission.session=session;submission.request=1;submission.revision=frame->revision;submission.destination=fixture.payload;submission.start=false;submission.files={{false,0}};submission.verifyExisting=true;
    QVERIFY(bridge.importTorrentFiles(submission));QTRY_COMPARE_WITH_TIMEOUT(writes.size(),1,10000);QTRY_VERIFY(!bridge.busy());const auto imported=result(writes);QVERIFY(imported&&imported->imported&&imported->verificationPending&&!imported->started);
    const auto statePath=fixture.profile+"/active/"+QString::fromStdString(fixture.hash.hex())+".dat";
    QTRY_VERIFY_WITH_TIMEOUT([&]{
      // ConfigFile rotates the primary to .bak before publishing .saving. This
      // external observer retries that gap; the leased post-close checks below
      // still require the exact final saved intent and completion flags.
      QFile file(fixture.profile+"/downloads.config");if(!file.open(QIODevice::ReadOnly)){return false;}
      const auto inventory=core::Document::parse(file.readAll().toStdString(),storage::compatibleConfigLimits());if(!inventory){return false;}
      const auto* downloads=inventory->find("downloads");if(!downloads||downloads->type!=core::Node::Type::List||downloads->children.size()!=1){return false;}
      const auto* marker=downloads->children[0].find(storage::VerifiedSeedKey);
      return marker&&marker->type==core::Node::Type::Integer&&marker->integer==(corrupt?1:2);
    }(),30000);
    if(corrupt){QTRY_VERIFY_WITH_TIMEOUT([&]{if(rows.empty()){return false;}const auto packet=qvariant_cast<ui::Presentation>(rows.last()[0]);return packet&&!packet->items.empty()&&packet->items[0].state==QStringLiteral("Error");}(),30000);}
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,10000);QTRY_VERIFY(!bridge.busy());QVERIFY(test::verifiedSeedBytesIntact(fixture,corrupt));
    auto lease=storage::ProfileLease::acquire(fixture.profile.toStdString());QVERIFY(lease);const auto saved=storage::readVerifiedSeedIntent(*lease,fixture.hash);QVERIFY(saved&&*saved);QCOMPARE((*saved)->startup,corrupt?core::StartupState::Stopped:core::StartupState::Queued);QVERIFY(!(*saved)->forceStart);
    const auto state=document(statePath);const auto flags=core::originalDownloadFlags(state.find("attributes"));QVERIFY(flags&&(*flags&8)&&!(*flags&1));
    QVERIFY(!rows.empty());
  }
  void initTestCase() {
    QCOMPARE(QGuiApplication::platformName(),QStringLiteral("offscreen"));QApplication::setQuitOnLastWindowClosed(false);
    auto owner=engine::PersistentSession::create({1,2,3});QVERIFY2(owner.has_value(),"This actual-owner cohort requires an approved source-bound runtime; no skip");
    const auto closed=(*owner)->close(std::chrono::steady_clock::now()+std::chrono::seconds{5});QVERIFY(closed.sessionJoined);
  }

  void actualOwnerUsesChosenSingleOrMultifileRootAndRetainsIdentity_data(){QTest::addColumn<bool>("multi");QTest::newRow("single-chosen-name")<<false;QTest::newRow("multi-chosen-root")<<true;}
  void actualOwnerUsesChosenSingleOrMultifileRootAndRetainsIdentity(){
    QFETCH(bool,multi);test::OwnedEngineFixture fixture(multi);QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);
    const auto profile=fixture.root.path()+QStringLiteral("/save-name-profile");emptyProfile(profile);const auto destination=fixture.root.path()+QStringLiteral("/chosen-parent");const auto leaf=multi?QStringLiteral("chosen-root"):QStringLiteral("chosen.bin");const auto metadata=test::read(fixture.stateFile);
    app::SessionBridge bridge;QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),reads(&bridge,&app::SessionBridge::torrentIntakeReady),writes(&bridge,&app::SessionBridge::torrentIntakeFinished),closed(&bridge,&app::SessionBridge::sessionClosed),rows(&bridge,&app::SessionBridge::snapshotReady);
    QVERIFY(bridge.open(profile.toStdString(),fixture.port()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty(),10000);const auto session=sessions[0][0].toULongLong();QVERIFY(bridge.previewTorrentIntake({session,1,fixture.stateFile}));QTRY_COMPARE_WITH_TIMEOUT(reads.size(),1,10000);QTRY_VERIFY(!bridge.busy());const auto files=preview(reads);QVERIFY(ui::validTorrentIntake(files));
    std::vector<compat::FilePolicy> selected(files->files.size(),{false,0});ui::TorrentIntakeSubmission accepted{session,files->source.request,files->revision,destination,true,selected,{65536,32768},true};accepted.saveName=leaf;
    auto invalid=accepted;invalid.saveName=QStringLiteral("../unsafe");QVERIFY(bridge.importTorrentFiles(invalid));QTRY_COMPARE_WITH_TIMEOUT(writes.size(),1,10000);QVERIFY(!result(writes)->imported);QVERIFY(!QFile::exists(destination));QVERIFY(document(profile+QStringLiteral("/downloads.config")).find("downloads")->children.empty());QTRY_VERIFY(!bridge.busy());
    bool committedAtResult=false;connect(&bridge,&app::SessionBridge::torrentIntakeFinished,this,[&](const ui::TorrentIntakeResultPresentation& packet){if(!packet->imported){return;}const auto inventory=document(profile+QStringLiteral("/downloads.config"));const auto state=document(profile+QStringLiteral("/active/")+files->hash+QStringLiteral(".dat"));committedAtResult=inventory.find("downloads")->children[0].find("save_file")->bytes==leaf.toStdString()&&state.find("attributes")->find("displayname")->bytes==leaf.toStdString()&&test::read(profile+QStringLiteral("/torrents/")+files->hash+QStringLiteral(".torrent"))==metadata;});
    QVERIFY(bridge.importTorrentFiles(accepted));QTRY_COMPARE_WITH_TIMEOUT(writes.size(),2,10000);QVERIFY2(result(writes)->imported&&result(writes)->started,qPrintable(result(writes)->failure));QVERIFY(committedAtResult);
    for(const auto& file:fixture.files){const auto relative=multi?file.relative.mid(file.relative.indexOf(u'/')+1):QString{};const auto path=destination+'/'+leaf+(multi?'/'+relative:QString{});QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path)&&test::read(path)==file.expected,30000);QCOMPARE(test::read(fixture.payload+'/'+file.relative),QByteArray(file.expected.size(),'\0'));struct stat original{};QVERIFY(::stat((fixture.payload+'/'+file.relative).toStdString().c_str(),&original)==0);QCOMPARE(original.st_dev,file.before.st_dev);QCOMPARE(original.st_ino,file.before.st_ino);}
    QVERIFY(!QFile::exists(destination+'/'+files->name));QCOMPARE(test::read(fixture.stateFile),metadata);QTRY_VERIFY(!bridge.busy());QVERIFY(bridge.refresh());QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!rows.empty(),10000);const auto library=qvariant_cast<ui::Presentation>(rows.last()[0]);QVERIFY(library&&library->items.size()==1);QCOMPARE(library->items[0].name,leaf);
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,15000);QVERIFY(closed[0][1].toBool()&&closed[0][2].toBool());auto lease=storage::ProfileLease::acquire(profile.toStdString());QVERIFY(lease);const auto inventory=document(profile+QStringLiteral("/downloads.config"));QCOMPARE(inventory.find("downloads")->children[0].find("save_file")->bytes,std::string_view(leaf.toStdString()));QVERIFY(inventory.find("unknown"));
  }
  void offlineTrackerOverrideCommitsBeforeResultWithoutChangingOriginalMetadata_data(){QTest::addColumn<bool>("clear");QTest::newRow("explicit-tiers")<<false;QTest::newRow("explicit-clear")<<true;}
  void offlineTrackerOverrideCommitsBeforeResultWithoutChangingOriginalMetadata(){
    QFETCH(bool,clear);test::selected_swarm::Fixture fixture(false);const auto profile=QString::fromStdString((fixture.root/"tracker-intake-profile").string());emptyProfile(profile);
    const auto source=test::selected_swarm::read(fixture.input);const auto destination=QString::fromStdString((fixture.root/"absent-trackers-payload").string());app::SessionBridge bridge;
    QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),reads(&bridge,&app::SessionBridge::torrentIntakeReady),writes(&bridge,&app::SessionBridge::torrentIntakeFinished),native(&bridge,&app::SessionBridge::nativeSessionReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(profile.toStdString(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty(),10000);const auto session=sessions[0][0].toULongLong();
    QVERIFY(bridge.previewTorrentIntake({session,1,QString::fromStdString(fixture.input.string())}));QTRY_COMPARE_WITH_TIMEOUT(reads.size(),1,10000);QTRY_VERIFY(!bridge.busy());const auto files=preview(reads);QVERIFY2(ui::validTorrentIntake(files),qPrintable(files->failure));
    std::vector<compat::FilePolicy> selected;for(const auto& file:files->files){selected.push_back({file.padding,0});}
    ui::TorrentIntakeSubmission accepted{session,files->source.request,files->revision,destination,false,selected};accepted.trackers=clear?core::TorrentIntakeTrackerTiers{}:core::TorrentIntakeTrackerTiers{{"https://first.invalid/a"},{"udp://127.0.0.1:9/a","https://last.invalid/a"}};
    auto stale=accepted;++stale.revision;QVERIFY(bridge.importTorrentFiles(stale));QTRY_COMPARE_WITH_TIMEOUT(writes.size(),1,10000);QVERIFY(!result(writes)->imported);QVERIFY(document(profile+QStringLiteral("/downloads.config")).find("downloads")->children.empty());QTRY_VERIFY(!bridge.busy());
    const auto expected=storage::rewriteTorrentIntakeTrackers(source,accepted.trackers);QVERIFY(expected);bool committedAtResult=false;
    connect(&bridge,&app::SessionBridge::torrentIntakeFinished,this,[&](const ui::TorrentIntakeResultPresentation& result){if(!result->imported){return;}const auto saved=document(profile+QStringLiteral("/downloads.config"));committedAtResult=saved.find("downloads")->children.size()==1&&test::read(profile+QStringLiteral("/torrents/")+files->hash+QStringLiteral(".torrent")).toStdString()==*expected;});
    QVERIFY(bridge.importTorrentFiles(accepted));QTRY_COMPARE_WITH_TIMEOUT(writes.size(),2,10000);QVERIFY2(result(writes)->imported,qPrintable(result(writes)->failure));QVERIFY(!result(writes)->started);QVERIFY(committedAtResult);QCOMPARE(native.size(),0);QVERIFY(!QFile::exists(destination));QCOMPARE(test::selected_swarm::read(fixture.input),source);
    const auto archive=document(profile+QStringLiteral("/torrents/")+files->hash+QStringLiteral(".torrent"));const auto state=document(profile+QStringLiteral("/active/")+files->hash+QStringLiteral(".dat"));
    const auto rawInfo=storage::originalTorrentInfoSubject(archive);const auto stateInfo=storage::originalTorrentInfoSubject(state);QVERIFY(rawInfo&&stateInfo);QCOMPARE(std::string(*rawInfo),fixture.rawInfo);QCOMPARE(std::string(*stateInfo),fixture.rawInfo);
    QTRY_VERIFY(!bridge.busy());QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,10000);QVERIFY(closed[0][1].toBool()&&closed[0][2].toBool());auto lease=storage::ProfileLease::acquire(profile.toStdString());QVERIFY(lease);QVERIFY(fixture.untouched());
  }
  void actualHostCreatesOnlyStoppedCompanionFromFreshSourceTarget() {
    test::selected_swarm::Fixture source(false);const auto original=source.initialState;
    source.lease.reset();
    // Only the existing owned-loopback policy token is needed: neither stopped
    // source nor companion is admitted to a transfer or offered to this peer.
    test::OwnedEngineFixture ownedPeer(false);QTRY_VERIFY_WITH_TIMEOUT(ownedPeer.ready(),30000);
    app::SessionBridge bridge;QSignalSpy rows(&bridge,&app::SessionBridge::snapshotReady),messages(&bridge,&app::SessionBridge::messageReady);
    QSignalSpy closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(source.profile,ownedPeer.port()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!rows.empty(),10000);
    auto current=qvariant_cast<ui::Presentation>(rows.last()[0]);QVERIFY(current&&current->items.size()==1);
    const ui::Target target{QString::fromStdString(source.key->hex()),current->revision};
    const auto beforeStale=messages.size();auto stale=target;++stale.generation;QVERIFY(bridge.createHybridCompanion(stale,true));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&messages.size()>beforeStale,10000);
    QVERIFY(messages.last()[0].toString().contains(QStringLiteral("selection is stale")));
    QCOMPARE(test::selected_swarm::read(source.profile/"downloads.config"),source.initialInventory);
    QVERIFY(!std::filesystem::exists(source.destination));
    current=qvariant_cast<ui::Presentation>(rows.last()[0]);QVERIFY(current&&current->items.size()==1);
    const ui::Target refreshed{target.hash,current->revision};
    QVERIFY(bridge.createHybridCompanion(refreshed,true));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&qvariant_cast<ui::Presentation>(rows.last()[0])
        &&qvariant_cast<ui::Presentation>(rows.last()[0])->items.size()==2,10000);
    current=qvariant_cast<ui::Presentation>(rows.last()[0]);
    QCOMPARE(test::selected_swarm::read(source.state),original);QVERIFY(!std::filesystem::exists(source.destination));
    const auto inventory=document(QString::fromStdString((source.profile/"downloads.config").string()));
    const auto saved=core::readLibrary(inventory);QVERIFY(saved&&saved->issues.empty()&&saved->downloads.size()==2);
    QVERIFY(saved->downloads[0].hash==source.key&&saved->downloads[1].hash!=source.key);
    QCOMPARE(saved->downloads[1].startup,core::StartupState::Stopped);
    const auto failureCount=messages.size();const ui::Target fresh{target.hash,current->revision};
    const auto beforeDuplicate=test::selected_swarm::read(source.profile/"downloads.config");
    QVERIFY(bridge.createHybridCompanion(fresh,true));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&messages.size()>failureCount,10000);
    QCOMPARE(test::selected_swarm::read(source.profile/"downloads.config"),beforeDuplicate);
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,15000);
    QVERIFY(closed[0][1].toBool()&&closed[0][2].toBool());
    auto reacquired=storage::ProfileLease::acquire(source.profile);QVERIFY(reacquired);QVERIFY(!std::filesystem::exists(source.destination));
  }
  void selectedFileTransfersOnlyAfterOriginalChoicesHaveCommitted_data(){QTest::addColumn<bool>("force");QTest::newRow("ordinary-queued")<<false;QTest::newRow("force-started")<<true;}
  void selectedFileTransfersOnlyAfterOriginalChoicesHaveCommitted() {
    QFETCH(bool,force);
    test::OwnedEngineFixture fixture(true);QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);
    const auto profile=fixture.root.path()+QStringLiteral("/intake-profile");emptyProfile(profile);
    const auto sourceBefore=test::read(fixture.stateFile);const auto skippedPath=fixture.payload+'/'+fixture.files[1].relative;
    const auto skippedBefore=test::read(skippedPath);
    app::SessionBridge bridge;QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),reads(&bridge,&app::SessionBridge::torrentIntakeReady);
    QSignalSpy writes(&bridge,&app::SessionBridge::torrentIntakeFinished),closed(&bridge,&app::SessionBridge::sessionClosed),rows(&bridge,&app::SessionBridge::snapshotReady);
    QVERIFY(bridge.open(profile.toStdString(),fixture.port()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty(),10000);
    const auto session=sessions[0][0].toULongLong();
    QVERIFY(bridge.previewTorrentIntake({session,1,fixture.stateFile}));QTRY_COMPARE_WITH_TIMEOUT(reads.size(),1,10000);QTRY_VERIFY(!bridge.busy());
    const auto files=preview(reads);QVERIFY2(ui::validTorrentIntake(files),qPrintable(files->failure));QCOMPARE(files->files.size(),std::size_t{2});
    QCOMPARE(files->hash,QString::fromStdString(fixture.hash->hex()));
    const std::vector<compat::FilePolicy> policy{{false,1},{true,0}};
    bool committedAtResult=false;
    connect(&bridge,&app::SessionBridge::torrentIntakeFinished,this,[&](const ui::TorrentIntakeResultPresentation& packet){
      if(!packet->imported){return;}
      const auto inventory=document(profile+QStringLiteral("/downloads.config"));const auto& row=inventory.find("downloads")->children.at(0);
      const auto saved=compat::originalFilePolicy(row,2);const auto state=document(profile+QStringLiteral("/active/")+files->hash+QStringLiteral(".dat"));
      const auto* attrs=state.find("attributes");const auto facts=core::readSavedDndFacts(attrs);
      committedAtResult=saved&&*saved==policy&&attrs->find("storetypes")->children[0].bytes=="L"
          &&attrs->find("storetypes")->children[1].bytes=="L"&&facts.skippedBytes==65536
          &&row.find("maxdl")&&row.find("maxdl")->integer==65536&&row.find("maxul")&&row.find("maxul")->integer==32768
          &&attrs->find("flags")&&(attrs->find("flags")->integer&core::OriginalSequentialDownloadFlag)
          &&row.find("forceStart")&&row.find("forceStart")->integer==(force?1:0)&&row.find("state")&&row.find("state")->integer==(force?0:75);
    });
    QVERIFY(bridge.importTorrentFiles({session,files->source.request,files->revision,fixture.payload,true,policy,{65536,32768},true,force}));
    QTRY_COMPARE_WITH_TIMEOUT(writes.size(),1,10000);QVERIFY2(result(writes)->imported,qPrintable(result(writes)->failure));
    QVERIFY2(result(writes)->started,qPrintable(result(writes)->failure));QVERIFY(committedAtResult);
    QTRY_VERIFY_WITH_TIMEOUT(test::read(fixture.payload+'/'+fixture.files[0].relative)==fixture.files[0].expected,30000);
    QCOMPARE(test::read(skippedPath),skippedBefore);QCOMPARE(test::read(fixture.stateFile),sourceBefore);
    QTRY_VERIFY(!bridge.busy());QVERIFY(bridge.refresh());QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!rows.empty(),10000);
    const auto library=qvariant_cast<ui::Presentation>(rows.last()[0]);QVERIFY(library&&library->items.size()==1);
    QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,15000);QVERIFY(closed[0][1].toBool());QVERIFY(closed[0][2].toBool());
    auto reacquired=storage::ProfileLease::acquire(profile.toStdString());QVERIFY(reacquired.has_value());
    const auto config=document(profile+QStringLiteral("/downloads.config"));const auto& row=config.find("downloads")->children[0];
    const auto persisted=compat::originalFilePolicy(row,2);QVERIFY(persisted&&*persisted==policy);
    QVERIFY(config.find("unknown")&&config.find("unknown")->children[0].integer==7);
    const auto state=document(profile+QStringLiteral("/active/")+files->hash+QStringLiteral(".dat"));
    // This fresh profile has no original cache.dat. Check the selected size
    // from its persisted state; the library projection uses that optional cache.
    QVERIFY(row.find("completedbytes"));
    QCOMPARE(row.find("maxdl")->integer,std::int64_t{65536});QCOMPARE(row.find("maxul")->integer,std::int64_t{32768});
    QCOMPARE(row.find("forceStart")->integer,std::int64_t{force?1:0});QCOMPARE(row.find("state")->integer,std::int64_t{force?0:75});
    QVERIFY(state.find("attributes")->find("flags")&&(state.find("attributes")->find("flags")->integer&core::OriginalSequentialDownloadFlag));
    const auto selected=core::projectSavedDndProgress(131072,row.find("completedbytes")->integer,core::readSavedDndFacts(state.find("attributes")));
    QCOMPARE(selected.selectedBytes,std::optional<std::int64_t>{65536});
    QVERIFY(state.find("attributes")->find("opaque.fixture"));QCOMPARE(state.find("attributes")->find("opaque.fixture")->bytes,std::string_view("\0\xffQ",3));
    QCOMPARE(test::read(skippedPath),skippedBefore);
    for(const auto& file:fixture.files){struct stat current{};QVERIFY(::stat((fixture.payload+'/'+file.relative).toStdString().c_str(),&current)==0);QCOMPARE(current.st_dev,file.before.st_dev);QCOMPARE(current.st_ino,file.before.st_ino);}
  }
  void explicitStoppedSubmissionSuppressesForceWithoutAnOwner(){
    test::OwnedEngineFixture fixture(true);const auto profile=fixture.root.path()+QStringLiteral("/explicit-stopped-profile");emptyProfile(profile);
    const auto destination=fixture.root.path()+QStringLiteral("/absent-stopped-payload");app::SessionBridge bridge;
    QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),reads(&bridge,&app::SessionBridge::torrentIntakeReady),writes(&bridge,&app::SessionBridge::torrentIntakeFinished),native(&bridge,&app::SessionBridge::nativeSessionReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(profile.toStdString(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty(),10000);const auto session=sessions[0][0].toULongLong();
    QVERIFY(bridge.previewTorrentIntake({session,1,fixture.stateFile}));QTRY_COMPARE_WITH_TIMEOUT(reads.size(),1,10000);QTRY_VERIFY(!bridge.busy());const auto files=preview(reads);QVERIFY(ui::validTorrentIntake(files));
    const std::vector<compat::FilePolicy> policy(2,{false,0});QVERIFY(bridge.importTorrentFiles({session,files->source.request,files->revision,destination,false,policy,{},false,true}));
    QTRY_COMPARE_WITH_TIMEOUT(writes.size(),1,10000);QVERIFY2(result(writes)->imported,qPrintable(result(writes)->failure));QVERIFY(!result(writes)->started);QCOMPARE(native.size(),0);QVERIFY(!QFile::exists(destination));
    const auto inventory=document(profile+QStringLiteral("/downloads.config"));const auto& row=inventory.find("downloads")->children.at(0);
    QCOMPARE(row.find("state")->integer,std::int64_t{70});QCOMPARE(row.find("forceStart")->integer,std::int64_t{0});QVERIFY(inventory.find("unknown"));
    QTRY_VERIFY(!bridge.busy());QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,10000);QVERIFY(closed[0][1].toBool()&&closed[0][2].toBool());QVERIFY(!QFile::exists(destination));
  }
  void pausedSubmissionCommitsOriginalPauseAtomicallyWithoutAnOwner(){
    // Metadata/identity fixture only: no seed session, tracker or peer is started.
    test::selected_swarm::Fixture fixture(false);const auto profile=QString::fromStdString((fixture.root/"paused-intake-profile").string());emptyProfile(profile);
    const auto destination=QString::fromStdString((fixture.root/"absent-paused-payload").string());app::SessionBridge bridge;
    QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),reads(&bridge,&app::SessionBridge::torrentIntakeReady),writes(&bridge,&app::SessionBridge::torrentIntakeFinished),native(&bridge,&app::SessionBridge::nativeSessionReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(profile.toStdString(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty(),10000);const auto session=sessions[0][0].toULongLong();
    QVERIFY(bridge.previewTorrentIntake({session,1,QString::fromStdString(fixture.input.string())}));QTRY_COMPARE_WITH_TIMEOUT(reads.size(),1,10000);QTRY_VERIFY(!bridge.busy());const auto files=preview(reads);QVERIFY(ui::validTorrentIntake(files));
    const std::vector<compat::FilePolicy> policy{{false,0}};ui::TorrentIntakeSubmission accepted{session,files->source.request,files->revision,destination,false,policy,{},false,false,true};
    const auto beforeInventory=test::read(profile+QStringLiteral("/downloads.config"));
    auto conflict=accepted;conflict.start=true;QVERIFY(bridge.importTorrentFiles(conflict));QTRY_COMPARE_WITH_TIMEOUT(writes.size(),1,10000);QVERIFY(!result(writes)->imported);QTRY_VERIFY(!bridge.busy());
    const auto unchanged=document(profile+QStringLiteral("/downloads.config"));QVERIFY(unchanged.find("downloads")->children.empty());QVERIFY(!unchanged.find("pause_data"));QVERIFY(!QFile::exists(destination));QCOMPARE(test::read(profile+QStringLiteral("/downloads.config")),beforeInventory);QVERIFY(!QFile::exists(profile+QStringLiteral("/active/")+files->hash+QStringLiteral(".dat")));
    bool committedAtResult=false;
    connect(&bridge,&app::SessionBridge::torrentIntakeFinished,this,[&](const ui::TorrentIntakeResultPresentation& packet){
      if(!packet->imported){return;}const auto config=document(profile+QStringLiteral("/downloads.config"));const auto saved=core::readLibrary(config);
      committedAtResult=saved&&saved->issues.empty()&&saved->downloads.size()==1&&saved->downloads[0].originalState==70&&!saved->downloads[0].forceStart
          &&saved->paused.size()==1&&saved->paused[0].hash==*fixture.key&&!saved->paused[0].forceStart&&saved->paused[0].owner==1;
    });
    QVERIFY(bridge.importTorrentFiles(accepted));QTRY_COMPARE_WITH_TIMEOUT(writes.size(),2,10000);QVERIFY2(result(writes)->imported,qPrintable(result(writes)->failure));QVERIFY(!result(writes)->started);QVERIFY(committedAtResult);QCOMPARE(native.size(),0);QVERIFY(!QFile::exists(destination));
    const auto config=document(profile+QStringLiteral("/downloads.config"));QVERIFY(config.find("unknown"));QCOMPARE(config.find("pause_data")->children.size(),std::size_t{1});
    QTRY_VERIFY(!bridge.busy());QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,10000);QVERIFY(closed[0][1].toBool()&&closed[0][2].toBool());
    auto reacquired=storage::ProfileLease::acquire(profile.toStdString());QVERIFY(reacquired);QVERIFY(!QFile::exists(destination));QVERIFY(fixture.untouched());
  }
  void allSkippedRequestedStartPublishesStoppedWithoutPayloadOrAdmission() {
    // The metadata generator's separate seed is owned; the offline target Host
    // has port0 and cannot create an owner or request any peer data.
    test::OwnedEngineFixture fixture(true);const auto profile=fixture.root.path()+QStringLiteral("/stopped-intake-profile");emptyProfile(profile);
    const auto destination=fixture.root.path()+QStringLiteral("/absent-destination");
    app::SessionBridge bridge;QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),reads(&bridge,&app::SessionBridge::torrentIntakeReady);
    QSignalSpy writes(&bridge,&app::SessionBridge::torrentIntakeFinished),native(&bridge,&app::SessionBridge::nativeSessionReady),closed(&bridge,&app::SessionBridge::sessionClosed);
    QVERIFY(bridge.open(profile.toStdString(),0));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!sessions.empty(),10000);
    const auto session=sessions[0][0].toULongLong();QVERIFY(bridge.previewTorrentIntake({session,1,fixture.stateFile}));QTRY_COMPARE_WITH_TIMEOUT(reads.size(),1,10000);QTRY_VERIFY(!bridge.busy());
    const auto files=preview(reads);QVERIFY(ui::validTorrentIntake(files));const std::vector<compat::FilePolicy> policy(2,{true,0});
    QVERIFY(bridge.importTorrentFiles({session,files->source.request,files->revision,destination,true,policy,{},false,true}));QTRY_COMPARE_WITH_TIMEOUT(writes.size(),1,10000);
    QVERIFY2(result(writes)->imported,qPrintable(result(writes)->failure));QVERIFY(!result(writes)->started);QVERIFY(!QFile::exists(destination));QCOMPARE(native.size(),0);
    const auto config=document(profile+QStringLiteral("/downloads.config"));const auto& row=config.find("downloads")->children.at(0);
    QCOMPARE(row.find("state")->integer,std::int64_t{70});QCOMPARE(row.find("forceStart")->integer,std::int64_t{0});QCOMPARE(row.find("completedbytes")->integer,std::int64_t{0});
    const auto state=document(profile+QStringLiteral("/active/")+files->hash+QStringLiteral(".dat"));const auto* attrs=state.find("attributes");
    QCOMPARE(attrs->find("storetypes")->children[0].bytes,std::string_view("C"));QCOMPARE(attrs->find("storetypes")->children[1].bytes,std::string_view("C"));
    const auto selected=core::projectSavedDndProgress(131072,0,core::readSavedDndFacts(attrs));QCOMPARE(selected.selectedBytes,std::optional<std::int64_t>{0});QCOMPARE(selected.percentThousandths,std::optional<std::int32_t>{1000});
    QTRY_VERIFY(!bridge.busy());QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,10000);
    QVERIFY(closed[0][1].toBool());QVERIFY(closed[0][2].toBool());QVERIFY(!QFile::exists(destination));
  }
};
QTEST_MAIN(TorrentIntakeFilesBridgeTests)
#include "torrent_intake_files_bridge_tests.moc"
