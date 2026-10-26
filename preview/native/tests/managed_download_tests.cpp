#include "persistent_session.hpp"
#include "managed_payload_counters.hpp"
#include <limits>
#include "owned_engine_fixture.hpp"
#include "verified_seeding_fixture.hpp"
#include "versioned_verified_seeding_fixture.hpp"
#include "library_commands.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <QtTest>

#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <unistd.h>

namespace {
using namespace biglybtc;
using namespace std::chrono_literals;
using Phase = engine::ManagedPhase;

std::unique_ptr<engine::PersistentSession> owner() {
  auto result = engine::PersistentSession::create({101, 202, 303});
  test::require(result.has_value(), "Managed tests require the approved source-bound provider");
  return std::move(*result);
}
storage::ProfileLease held(const test::OwnedEngineFixture& fixture) {
  auto result = storage::ProfileLease::acquire(fixture.native());
  test::require(result.has_value(), "Cannot hold generated managed profile");
  return std::move(*result);
}
core::Document parse(const QByteArray& raw) {
  auto result = core::Document::parse(raw.toStdString(), storage::compatibleConfigLimits());
  test::require(result.has_value(), "Cannot parse generated managed document");
  return std::move(*result);
}
std::string encoded(const core::Node& value) {
  auto result = core::encodeValue(value, storage::compatibleConfigLimits());
  test::require(result.has_value(), "Cannot encode generated managed subtree");
  return std::move(*result);
}
std::string bytes(std::string_view text) { return std::to_string(text.size()) + ':' + std::string(text); }
std::string bytes(const QString& text) { return bytes(text.toUtf8().toStdString()); }
std::optional<engine::ManagedDownloadStatus> status(engine::PersistentSession& service, const core::TorrentHash& hash) {
  const auto all = service.managedDownloads();
  const auto found = std::find_if(all.begin(), all.end(), [&](const auto& value) { return value.hash == hash; });
  return found == all.end() ? std::nullopt : std::optional{*found};
}
bool phase(engine::PersistentSession& service, const core::TorrentHash& hash, Phase expected) {
  const auto value = status(service, hash);
  return value && value->phase == expected;
}
bool hasReceived(engine::PersistentSession& service, const core::TorrentHash& hash, std::int64_t after = 0) {
  const auto value = status(service, hash);
  return value && !value->failure && value->received > after && value->pieces > 0 && !value->complete;
}

// Same small merge used by persistent_session_tests: the second seed remains in
// its own generated root, while both client layouts use the FIRST held profile.
void appendMultifile(test::OwnedEngineFixture& first, const test::OwnedEngineFixture& second) {
  for (const auto& file : second.files) {
    const auto path = first.payload + '/' + file.relative;
    test::write(path, QByteArray(file.expected.size(), '\0'));
    auto owned = file;
    test::require(::stat(path.toStdString().c_str(), &owned.before) == 0, "Cannot stat imported generated payload");
    first.files.push_back(std::move(owned));
  }
  const auto state = first.profile + QStringLiteral("/active/")
      + QString::fromStdString(second.hash->hex()) + QStringLiteral(".dat");
  test::write(state, test::read(second.stateFile));
  auto a = parse(test::read(first.profile + QStringLiteral("/downloads.config")));
  auto b = parse(test::read(second.profile + QStringLiteral("/downloads.config")));
  auto entry = core::Document::parse(encoded(b.find("downloads")->children.at(0)), storage::compatibleConfigLimits());
  test::require(entry.has_value(), "Cannot own imported generated entry");
  test::require(entry->replaceValue("save_dir", bytes(first.payload)).has_value(), "Cannot bind imported payload root");
  test::require(entry->replaceValue("torrent", bytes(state)).has_value(), "Cannot bind imported state");
  test::require(a.replaceValue("downloads", "l" + encoded(a.find("downloads")->children.at(0))
      + entry->serialize() + 'e').has_value(), "Cannot merge generated profiles");
  test::write(first.profile + QStringLiteral("/downloads.config"), QByteArray::fromStdString(a.serialize()));
}
void editFirstEntry(storage::ConfigFile& file, std::initializer_list<std::pair<std::string_view, std::string>> changes) {
  const auto* downloads = file.document().find("downloads");
  test::require(downloads && downloads->children.size() == 1, "Expected one generated entry");
  auto entry = core::Document::parse(encoded(downloads->children.front()), storage::compatibleConfigLimits());
  test::require(entry.has_value(), "Cannot own generated entry edit");
  for (const auto& [key, value] : changes) {
    test::require(entry->replaceValue(key, value).has_value(), "Cannot edit generated intent");
  }
  test::require(file.document().replaceValue("downloads", "l" + entry->serialize() + 'e').has_value(),
      "Cannot replace generated entry");
}
const core::Node& firstEntry(const core::Document& document) { return document.find("downloads")->children.at(0); }
std::int64_t integer(const core::Node& node, std::string_view key) {
  const auto* value = node.find(key);
  test::require(value && value->type == core::Node::Type::Integer, "Expected saved generated integer");
  return value->integer;
}
} // namespace

class ManagedDownloadTests final : public QObject {
  Q_OBJECT
private slots:

  void versionedExistingSeedingChecksEveryPieceBeforeSelectedSwarmUpload_data(){
    QTest::addColumn<QString>("kind");for(const auto* kind:{"v2-aligned","v2-tail","hybrid-v1","hybrid-v2"}){QTest::newRow(kind)<<QString::fromLatin1(kind);}
  }
  void versionedExistingSeedingChecksEveryPieceBeforeSelectedSwarmUpload(){
    QFETCH(QString,kind);QTemporaryDir directory;QVERIFY(directory.isValid());
    const auto fixture=test::prepareVersionedVerifiedSeedCase(directory.path()+"/owned",kind);
    auto lease=storage::ProfileLease::acquire(fixture.profile.toStdString());QVERIFY(lease);
    const auto imported=test::importVerifiedSeedCase(*lease,fixture);QVERIFY2(imported,imported?"":imported.error().message.c_str());QCOMPARE(*imported,fixture.hash);
    const auto source=parse(test::read(fixture.metadata));const auto layout=compat::originalLayout(source);QVERIFY(layout);
    const auto archive=test::read(fixture.profile+"/torrents/"+QString::fromStdString(fixture.hash.hex())+".torrent");QCOMPARE(archive,test::read(fixture.metadata));
    auto service=owner();const auto admitted=service->startManaged(*lease,{.hash=fixture.hash,.verifyExisting=true});QVERIFY2(admitted,admitted?"":admitted.error().message.c_str());
    QVERIFY(status(*service,fixture.hash)->verificationHeld);QCOMPARE(status(*service,fixture.hash)->received,std::int64_t{0});QCOMPARE(status(*service,fixture.hash)->sent,std::int64_t{0});
    QVERIFY(!service->queueManaged(fixture.hash,true,false));QVERIFY(!service->pauseManaged(fixture.hash,false));
    QVERIFY(test::spinVerified(*service,fixture.hash));const auto accepted=status(*service,fixture.hash);QVERIFY(accepted);QVERIFY2(!accepted->failure,accepted->failure?accepted->failure->message.c_str():"");
    QCOMPARE(accepted->verifiedSeeding,engine::VerifiedSeedingPhase::Released);QVERIFY(accepted->checked&&accepted->complete&&accepted->selectedComplete&&!accepted->verificationHeld);
    const auto bits=service->verifiedSeedPieces(fixture.hash);QCOMPARE(bits.size(),static_cast<std::size_t>(accepted->totalPieces));QVERIFY(!bits.empty()&&std::ranges::all_of(bits,[](auto bit){return bit==1;}));
    QCOMPARE(accepted->bytes,layout->bytes);QCOMPARE(accepted->completed,layout->bytes);
    QVERIFY(test::versionedRealSnapshotsIntact(fixture));QVERIFY(test::versionedPadPathsAbsent(fixture));
    const auto actualReceiver=test::receiveVerifiedSeed(*service,fixture);QVERIFY(!actualReceiver.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value()&&status(*service,fixture.hash)->sent>0,5000);QCOMPARE(status(*service,fixture.hash)->received,std::int64_t{0});
    const auto close=service->close(std::chrono::steady_clock::now()+5s);QVERIFY(close.sessionJoined&&close.retirement.storageReleased&&!close.checkpointFailure);
    QVERIFY(test::versionedRealSnapshotsIntact(fixture));QVERIFY(test::versionedPadPathsAbsent(fixture));
    const auto state=parse(test::read(fixture.profile+"/active/"+QString::fromStdString(fixture.hash.hex())+".dat"));
    QCOMPARE(encoded(*state.find("info")),encoded(*source.find("info")));QCOMPARE(test::read(fixture.profile+"/torrents/"+QString::fromStdString(fixture.hash.hex())+".torrent"),archive);
    QCOMPARE(state.find("opaque.fixture")->bytes,std::string("\0\xffQ",3));QCOMPARE(state.find("attributes")->find("opaque.fixture")->bytes,std::string("\0\xffQ",3));
    const auto& downloaded=state.find("attributes")->find("filedownloaded")->find("downloaded")->children;QCOMPARE(downloaded.size(),layout->files.size());
    for(std::size_t n=0;n<layout->files.size();++n){QCOMPARE(downloaded[n].integer,layout->files[n].bytes);}
    auto reopened=owner();QVERIFY(reopened->startManaged(*lease,{.hash=fixture.hash}));QVERIFY(status(*reopened,fixture.hash)->verificationHeld);
    QVERIFY(test::spinVerified(*reopened,fixture.hash));QVERIFY(status(*reopened,fixture.hash)->verifiedSeeding==engine::VerifiedSeedingPhase::Released&&!status(*reopened,fixture.hash)->failure);
    const auto reclosed=reopened->close(std::chrono::steady_clock::now()+5s);QVERIFY(reclosed.sessionJoined&&reclosed.retirement.storageReleased&&!reclosed.checkpointFailure);
    QVERIFY(test::versionedRealSnapshotsIntact(fixture));QVERIFY(test::versionedPadPathsAbsent(fixture));
  }
  void versionedCorruptionCancellationAndExpiryNeverDownload_data(){
    QTest::addColumn<QString>("kind");QTest::addColumn<int>("fault");
    for(const auto* kind:{"v2-aligned","v2-tail","hybrid-v1","hybrid-v2"}){QTest::newRow(kind)<<QString::fromLatin1(kind)<<0;}
    QTest::newRow("tail-cancel")<<QStringLiteral("v2-tail")<<1;QTest::newRow("hybrid-v2-cancel")<<QStringLiteral("hybrid-v2")<<1;
    QTest::newRow("hybrid-v2-expiry")<<QStringLiteral("hybrid-v2")<<2;
  }
  void versionedCorruptionCancellationAndExpiryNeverDownload(){
    QFETCH(QString,kind);QFETCH(int,fault);QTemporaryDir directory;QVERIFY(directory.isValid());
    const auto fixture=test::prepareVersionedVerifiedSeedCase(directory.path()+"/owned",kind,fault==0);
    auto lease=storage::ProfileLease::acquire(fixture.profile.toStdString());QVERIFY(lease);QVERIFY(test::importVerifiedSeedCase(*lease,fixture));auto service=owner();
    engine::ManagedDownloadOptions options{.hash=fixture.hash,.verifyExisting=true};if(fault==2){options.verificationDeadline=std::chrono::steady_clock::now();}
    QVERIFY(service->startManaged(*lease,options));if(fault==1){QVERIFY(service->stopManaged(fixture.hash));}
    QVERIFY(test::spinVerified(*service,fixture.hash));const auto refused=status(*service,fixture.hash);QVERIFY(refused&&refused->verifiedSeeding!=engine::VerifiedSeedingPhase::Released);
    QCOMPARE(refused->received,std::int64_t{0});QCOMPARE(refused->sent,std::int64_t{0});
    const auto pending=storage::readVerifiedSeedIntent(*lease,fixture.hash);QVERIFY(pending&&*pending);QCOMPARE((*pending)->state,storage::VerifiedSeedState::Pending);QCOMPARE((*pending)->startup,core::StartupState::Stopped);
    const auto close=service->close(std::chrono::steady_clock::now()+5s);QVERIFY(close.sessionJoined&&close.retirement.storageReleased);
    QVERIFY(test::versionedRealSnapshotsIntact(fixture,fault==0));QVERIFY(test::versionedPadPathsAbsent(fixture));
    auto retry=owner();QVERIFY(!retry->startManaged(*lease,{.hash=fixture.hash}));QVERIFY(retry->managedDownloads().empty());
    if(fault!=0){
      QVERIFY(storage::resetVerifiedSeedForRetry(*lease,fixture.hash));QVERIFY(retry->startManaged(*lease,{.hash=fixture.hash,.verifyExisting=true}));
      QVERIFY(test::spinVerified(*retry,fixture.hash));QVERIFY(status(*retry,fixture.hash)->verifiedSeeding==engine::VerifiedSeedingPhase::Released);
    }
    const auto retried=retry->close(std::chrono::steady_clock::now()+5s);QVERIFY(retried.sessionJoined&&retried.retirement.storageReleased);
    QVERIFY(test::versionedRealSnapshotsIntact(fixture,fault==0));QVERIFY(test::versionedPadPathsAbsent(fixture));
  }
  void versionedAcceptedJournalRecoveryKeepsStoppedIdentityAndRechecks_data(){
    QTest::addColumn<QString>("kind");QTest::newRow("tail")<<QStringLiteral("v2-tail");QTest::newRow("hybrid-v2")<<QStringLiteral("hybrid-v2");
  }
  void versionedAcceptedJournalRecoveryKeepsStoppedIdentityAndRechecks(){
    QFETCH(QString,kind);QTemporaryDir directory;QVERIFY(directory.isValid());const auto fixture=test::prepareVersionedVerifiedSeedCase(directory.path()+"/owned",kind);
    auto acquired=storage::ProfileLease::acquire(fixture.profile.toStdString());QVERIFY(acquired);std::optional<storage::ProfileLease> lease{std::move(*acquired)};
    const auto metadata=storage::inspectTorrentIntake(fixture.metadata.toStdString());QVERIFY(metadata);
    const auto policies=test::versionedFilePolicies(*metadata);
    const auto interrupted=storage::importTorrentWithFileOptions(*lease,*metadata,fixture.payload.toStdString(),true,policies,
        [](storage::TorrentIntakeStage stage){if(stage==storage::TorrentIntakeStage::JournalSynced){throw std::runtime_error("Owned versioned journal interruption");}},false,{}, {},false,false,false,{}, {},true);
    QVERIFY(!interrupted&&storage::hasPendingTorrentIntake(*lease));lease.reset();acquired=storage::ProfileLease::acquire(fixture.profile.toStdString());QVERIFY(acquired);lease.emplace(std::move(*acquired));
    const auto recovered=storage::recoverTorrentIntake(*lease);QVERIFY(recovered&&*recovered);QCOMPARE(**recovered,fixture.hash);
    const auto pending=storage::readVerifiedSeedIntent(*lease,fixture.hash);QVERIFY(pending&&*pending);QCOMPARE((*pending)->startup,core::StartupState::Stopped);QCOMPARE((*pending)->state,storage::VerifiedSeedState::Pending);
    QCOMPARE(test::read(fixture.profile+"/torrents/"+QString::fromStdString(fixture.hash.hex())+".torrent"),test::read(fixture.metadata));
    auto service=owner();QVERIFY(service->startManaged(*lease,{.hash=fixture.hash,.verifyExisting=true}));QVERIFY(status(*service,fixture.hash)->verificationHeld);
    QVERIFY(test::spinVerified(*service,fixture.hash));QVERIFY(status(*service,fixture.hash)->verifiedSeeding==engine::VerifiedSeedingPhase::Released);
    const auto close=service->close(std::chrono::steady_clock::now()+5s);QVERIFY(close.sessionJoined&&close.retirement.storageReleased&&!close.checkpointFailure);
    QVERIFY(test::versionedRealSnapshotsIntact(fixture));QVERIFY(test::versionedPadPathsAbsent(fixture));
  }

  void verifiedHoldSurvivesQueueReconfigureAndPositionsUntilActualCancellation(){
    QTemporaryDir directory;QVERIFY(directory.isValid());const auto fixture=test::prepareVerifiedSeedCase(directory.path()+"/owned",false);
    auto lease=storage::ProfileLease::acquire(fixture.profile.toStdString());QVERIFY(lease);QVERIFY(test::importVerifiedSeedCase(*lease,fixture));auto service=owner();
    QVERIFY(service->startManaged(*lease,{.hash=fixture.hash,.verifyExisting=true}));const auto inventory=test::read(fixture.profile+"/downloads.config");
    core::QueuePolicy policy;policy.minimumDownloads=0;QVERIFY(service->configureQueuePolicy(policy));
    const std::array<core::TorrentHash,1> order{fixture.hash};QVERIFY(service->reorderManaged(order));
    QVERIFY(!service->queueManaged(fixture.hash,true,false));QVERIFY(!service->queueManaged(fixture.hash,false,true));QVERIFY(!service->pauseManaged(fixture.hash,false));
    QVERIFY(status(*service,fixture.hash)->verificationHeld);QCOMPARE(test::read(fixture.profile+"/downloads.config"),inventory);
    QVERIFY(service->stopManaged(fixture.hash));QCOMPARE(status(*service,fixture.hash)->verifiedSeeding,engine::VerifiedSeedingPhase::Cancelled);
    const auto closed=service->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased);
    const auto intent=storage::readVerifiedSeedIntent(*lease,fixture.hash);QVERIFY(intent&&*intent);QCOMPARE((*intent)->startup,core::StartupState::Stopped);QCOMPARE((*intent)->state,storage::VerifiedSeedState::Pending);QVERIFY(test::verifiedSeedBytesIntact(fixture));
  }

  void verifiedExistingSeedingReadsOnlyThenUploadsToActualReceiver_data(){QTest::addColumn<bool>("multi");QTest::newRow("single")<<false;QTest::newRow("multi")<<true;}
  void verifiedExistingSeedingReadsOnlyThenUploadsToActualReceiver(){
    QFETCH(bool,multi);QTemporaryDir directory;QVERIFY(directory.isValid());
    const auto fixture=test::prepareVerifiedSeedCase(directory.path()+"/owned",multi);
    auto lease=storage::ProfileLease::acquire(fixture.profile.toStdString());QVERIFY(lease);
    const auto imported=test::importVerifiedSeedCase(*lease,fixture);QVERIFY(imported);QCOMPARE(*imported,fixture.hash);
    const auto pending=storage::readVerifiedSeedIntent(*lease,fixture.hash);QVERIFY(pending&&*pending);QCOMPARE((*pending)->startup,core::StartupState::Stopped);
    auto service=owner();QVERIFY(service->startManaged(*lease,{.hash=fixture.hash,.verifyExisting=true}));
    auto before=status(*service,fixture.hash);QVERIFY(before&&before->verificationHeld);QCOMPARE(before->received,std::int64_t{0});QCOMPARE(before->sent,std::int64_t{0});
    QVERIFY(!service->queueManaged(fixture.hash,true,false));QVERIFY(!service->pauseManaged(fixture.hash,false));
    QVERIFY(test::spinVerified(*service,fixture.hash));const auto accepted=status(*service,fixture.hash);QVERIFY(accepted&&!accepted->failure);
    QCOMPARE(accepted->verifiedSeeding,engine::VerifiedSeedingPhase::Released);QVERIFY(!accepted->verificationHeld&&accepted->complete&&accepted->selectedComplete);QCOMPARE(accepted->pieces,accepted->totalPieces);
    const auto released=storage::readVerifiedSeedIntent(*lease,fixture.hash);QVERIFY(released&&*released);QCOMPARE((*released)->startup,core::StartupState::Queued);QVERIFY(!(*released)->forceStart);
    QVERIFY(test::verifiedSeedBytesIntact(fixture));const auto received=test::receiveVerifiedSeed(*service,fixture);QVERIFY(!received.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value()&&status(*service,fixture.hash)->sent>0,5000);
    QCOMPARE(status(*service,fixture.hash)->received,std::int64_t{0});
    // Actual original command shape can also be written by Java after release.
    const auto forced=storage::applyLibraryCommand(*lease,fixture.hash,core::DownloadAction::ForceStart);QVERIFY(forced);QVERIFY(forced->after.forceStart);QVERIFY(service->queueManaged(fixture.hash,true,false));
    const auto closed=service->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased);QVERIFY(!closed.checkpointFailure);QVERIFY(test::verifiedSeedBytesIntact(fixture));
    const auto captured=storage::readVerifiedSeedIntent(*lease,fixture.hash);QVERIFY(captured&&*captured&&(*captured)->forceStart);
    auto reopened=owner();QVERIFY(reopened->startManaged(*lease,{.hash=fixture.hash,.forceStart=true}));QVERIFY(status(*reopened,fixture.hash)->verificationHeld);
    QVERIFY(test::spinVerified(*reopened,fixture.hash));const auto actualForce=status(*reopened,fixture.hash);QVERIFY(actualForce&&actualForce->verifiedSeeding==engine::VerifiedSeedingPhase::Released&&actualForce->forceStart&&!actualForce->failure);
    const auto preserved=storage::readVerifiedSeedIntent(*lease,fixture.hash);QVERIFY(preserved&&*preserved);QCOMPARE((*preserved)->row,(*captured)->row);
    const auto reclosed=reopened->close(std::chrono::steady_clock::now()+5s);QVERIFY(reclosed.sessionJoined&&reclosed.retirement.storageReleased);QVERIFY(test::verifiedSeedBytesIntact(fixture));
    const auto state=parse(test::read(fixture.profile+"/active/"+QString::fromStdString(fixture.hash.hex())+".dat"));
    const auto flags=core::originalDownloadFlags(state.find("attributes"));QVERIFY(flags&&(*flags&8));QVERIFY(!(*flags&1)); // Never invent ONLY_EVER_SEEDED.
    QCOMPARE(state.find("opaque.fixture")->bytes,std::string("\0\xffQ",3));QCOMPARE(state.find("attributes")->find("opaque.fixture")->bytes,std::string("\0\xffQ",3));
  }
  void verifiedExistingCorruptCancelExpiryAndChangedSnapshotNeverDownload_data(){QTest::addColumn<int>("fault");QTest::newRow("corrupt")<<0;QTest::newRow("manual-cancel")<<1;QTest::newRow("expired")<<2;QTest::newRow("changed-after-open")<<3;QTest::newRow("changed-active-mapping")<<4;}
  void verifiedExistingCorruptCancelExpiryAndChangedSnapshotNeverDownload(){
    QFETCH(int,fault);QTemporaryDir directory;QVERIFY(directory.isValid());const auto fixture=test::prepareVerifiedSeedCase(directory.path()+"/owned",false,fault==0);
    auto lease=storage::ProfileLease::acquire(fixture.profile.toStdString());QVERIFY(lease);QVERIFY(test::importVerifiedSeedCase(*lease,fixture));auto service=owner();
    engine::ManagedDownloadOptions options{.hash=fixture.hash,.verifyExisting=true};if(fault==2){options.verificationDeadline=std::chrono::steady_clock::now();}
    QVERIFY(service->startManaged(*lease,options));
    if(fault==1){QVERIFY(service->stopManaged(fixture.hash));}
    if(fault==3){auto bytes=test::read(fixture.payload+"/sample.bin");bytes[1]=static_cast<char>(bytes[1]^1);test::write(fixture.payload+"/sample.bin",bytes);}
    if(fault==4){const auto path=fixture.profile+"/active/"+QString::fromStdString(fixture.hash.hex())+".dat";auto state=parse(test::read(path));auto attrs=core::Document::parse(encoded(*state.find("attributes")));QVERIFY(attrs);QVERIFY(attrs->replaceValue("filelinks2","l5:aliene"));QVERIFY(state.replaceValue("attributes",attrs->serialize()));test::write(path,QByteArray::fromStdString(state.serialize()));}
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value()&&status(*service,fixture.hash)->verifiedSeeding!=engine::VerifiedSeedingPhase::Checking,30000);
    const auto current=status(*service,fixture.hash);QVERIFY(current&&current->verifiedSeeding!=engine::VerifiedSeedingPhase::Released);QCOMPARE(current->received,std::int64_t{0});QCOMPARE(current->sent,std::int64_t{0});
    const auto stopped=storage::readVerifiedSeedIntent(*lease,fixture.hash);QVERIFY(stopped&&*stopped);QCOMPARE((*stopped)->state,storage::VerifiedSeedState::Pending);QCOMPARE((*stopped)->startup,core::StartupState::Stopped);
    const auto closed=service->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined);
    if(fault!=3){QVERIFY(test::verifiedSeedBytesIntact(fixture,fault==0));}
    auto reopened=owner();QVERIFY(!reopened->startManaged(*lease,{.hash=fixture.hash}));QVERIFY(reopened->managedDownloads().empty());QVERIFY(reopened->close(std::chrono::steady_clock::now()+5s).sessionJoined);
  }
  void verifiedExistingMissingPartialAndSymlinkRefuseBeforePreparation_data(){QTest::addColumn<int>("fault");QTest::newRow("missing")<<0;QTest::newRow("partial")<<1;QTest::newRow("symlink")<<2;}
  void verifiedExistingMissingPartialAndSymlinkRefuseBeforePreparation(){
    QFETCH(int,fault);QTemporaryDir directory;QVERIFY(directory.isValid());const auto fixture=test::prepareVerifiedSeedCase(directory.path()+"/owned",false);
    auto lease=storage::ProfileLease::acquire(fixture.profile.toStdString());QVERIFY(lease);QVERIFY(test::importVerifiedSeedCase(*lease,fixture));
    const auto path=fixture.payload+"/sample.bin";if(fault==0){QVERIFY(QFile::remove(path));}
    else if(fault==1){test::write(path,"partial");}else{QVERIFY(QFile::rename(path,path+".kept"));QVERIFY(::symlink((path+".kept").toStdString().c_str(),path.toStdString().c_str())==0);}
    const auto inventory=test::read(fixture.profile+"/downloads.config"),state=test::read(fixture.profile+"/active/"+QString::fromStdString(fixture.hash.hex())+".dat");
    auto service=owner();QVERIFY(!service->startManaged(*lease,{.hash=fixture.hash,.verifyExisting=true}));QVERIFY(service->managedDownloads().empty());
    QCOMPARE(test::read(fixture.profile+"/downloads.config"),inventory);QCOMPARE(test::read(fixture.profile+"/active/"+QString::fromStdString(fixture.hash.hex())+".dat"),state);
    if(fault==0){QVERIFY(!QFileInfo::exists(path));}else if(fault==1){QCOMPARE(test::read(path),QByteArray("partial"));}else{QVERIFY(QFileInfo(path).isSymLink());}
    QVERIFY(service->close(std::chrono::steady_clock::now()+5s).sessionJoined);
  }
  void rawEpochContinuityKeepsUntickedBytesAndCrossingReadsRetry() {
    using Counters = engine::detail::ManagedPayloadCounters;
    Counters counters; QVERIFY(!counters.epoch()); counters.installed();
    const auto first = counters.epoch(); QVERIFY(first);
    auto value = counters.combine(*first, 65527, 31);
    QCOMPARE(value.state, Counters::Read::Accepted); QCOMPARE(value.received, std::int64_t{65527});
    counters.seal(131072, 37); // Actual pre-clear raw total, including unticked bytes.
    value = counters.combine(*first, 0, 0); QCOMPARE(value.state, Counters::Read::Retry);
    const auto second = counters.epoch(); QVERIFY(second);
    value = counters.combine(*second, 5, 7);
    QCOMPARE(value.state, Counters::Read::Accepted);
    QCOMPARE(value.received, std::int64_t{131077}); QCOMPARE(value.sent, std::int64_t{44});
    counters.seal(5, 7);
    const auto third = counters.epoch(); QVERIFY(third);
    value = counters.combine(*third, 0, 0);
    QCOMPARE(value.state, Counters::Read::Accepted);
    QCOMPARE(value.received, std::int64_t{131077}); QCOMPARE(value.sent, std::int64_t{44});
  }
  void rawEpochInvalidReadsAndOverflowFailWithoutFallback() {
    using Counters = engine::detail::ManagedPayloadCounters;
    Counters regressed; regressed.installed(); const auto first = regressed.epoch(); QVERIFY(first);
    QCOMPARE(regressed.combine(*first, 10, 20).state, Counters::Read::Accepted);
    regressed.seal(9, 20); QVERIFY(!regressed.epoch());
    QCOMPARE(regressed.combine(*first, 0, 0).state, Counters::Read::Failed);
    Counters overflow; overflow.installed(); overflow.seal(std::numeric_limits<std::int64_t>::max(), 0);
    const auto full = overflow.epoch(); QVERIFY(full);
    QCOMPARE(overflow.combine(*full, 1, 0).state, Counters::Read::Failed); QVERIFY(!overflow.epoch());
    Counters negative; negative.installed(); const auto empty = negative.epoch(); QVERIFY(empty);
    QCOMPARE(negative.combine(*empty, 0, -1).state, Counters::Read::Failed); QVERIFY(!negative.epoch());
  }
  void concurrentMixedHandlesRemainControllableUnderOneOwner() {
    test::OwnedEngineFixture single(false, true), multi(true, true);
    QTRY_VERIFY_WITH_TIMEOUT(single.ready() && multi.ready(), 30000);
    QVERIFY(*single.hash != *multi.hash);
    appendMultifile(single, multi);
    auto lease = held(single);
    auto service = owner();
    QVERIFY(service->startManaged(lease, {.hash = *single.hash, .ownedLoopbackPeerPort = single.port(), .forceStart = true}));
    QVERIFY(service->startManaged(lease, {.hash = *multi.hash, .ownedLoopbackPeerPort = multi.port(), .forceStart = true}));
    // Hold the second actual handle before pumping either admission. It can
    // progress after resumption while the first handle remains manually paused.
    QVERIFY(service->pauseManaged(*multi.hash, true));
    QCOMPARE(service->managedDownloads().size(), std::size_t{2});
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && hasReceived(*service, *single.hash), 30000);
    QVERIFY(service->pauseManaged(*single.hash, true));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && phase(*service, *single.hash, Phase::Paused), 5000);
    const auto singlePaused = status(*service, *single.hash); QVERIFY(singlePaused);
    const auto multiPaused = status(*service, *multi.hash); QVERIFY(multiPaused);
    QCOMPARE(multiPaused->phase, Phase::Paused);
    QVERIFY(singlePaused->admission != multiPaused->admission);
    QVERIFY(service->pauseManaged(*multi.hash, false));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value()
        && hasReceived(*service, *multi.hash, multiPaused->received)
        && phase(*service, *single.hash, Phase::Paused), 30000);
    QVERIFY(service->queueManaged(*single.hash, true, false));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value()
        && phase(*service, *single.hash, Phase::Downloading), 5000);
    QVERIFY(service->stopManaged(*single.hash));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && phase(*service, *single.hash, Phase::Stopped), 10000);
    const auto stopped = status(*service, *single.hash); QVERIFY(stopped && !stopped->failure);
    const auto other = status(*service, *multi.hash); QVERIFY(other && !other->failure);
    QVERIFY(!other->complete && other->received > multiPaused->received);
    QVERIFY(other->phase != Phase::Stopped && other->phase != Phase::Stopping);
    QCOMPARE(stopped->hash, *single.hash); QCOMPARE(other->hash, *multi.hash);
    QCOMPARE(stopped->totalPieces, test::OwnedEngineFixture::PayloadBytes / 16384);
    QCOMPARE(other->totalPieces, stopped->totalPieces);
    // Both peers deliberately lack the final piece: completion cannot race the
    // pause/stop assertions or silently retire the second admission.
    QVERIFY(!stopped->complete && !other->complete);
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased && closed.retirement.peerCounters);
    QCOMPARE(closed.retirement.peerCounters->identity(), (engine::AccountingIdentity{101, 202, 303}));
    QVERIFY(closed.retirement.peerCounters->counters().payloadReceived >= stopped->received + other->received);
    QCOMPARE(service->managedDownloads().size(), std::size_t{2});
    QVERIFY(phase(*service, *multi.hash, Phase::Stopped));
    QVERIFY(single.intact(false));
  }

  void partialStopAndRepeatedHashCheckpointsKeepFreshSavedIntent() {
    test::OwnedEngineFixture partial(false, true), full(false);
    QTRY_VERIFY_WITH_TIMEOUT(partial.ready() && full.ready(), 30000);
    QCOMPARE(*partial.hash, *full.hash);
    const auto initialState = parse(test::read(partial.stateFile));
    auto lease = held(partial);
    {
      auto baseline = storage::ConfigFile::load(lease, "downloads.config"); QVERIFY(baseline);
      editFirstEntry(*baseline, {{"downloaded", "i1000e"}, {"uploaded", "i200e"},
          {"opaque.managed", bytes(std::string_view("\0\xffmanaged", 9))}});
      QVERIFY(baseline->save(lease));
    }
    auto service = owner();
    QVERIFY(service->startManaged(lease, {.hash = *partial.hash, .ownedLoopbackPeerPort = partial.port(), .forceStart = true}));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && hasReceived(*service, *partial.hash), 30000);
    const auto originalAdmission = status(*service, *partial.hash); QVERIFY(originalAdmission);
    QByteArray requestedIntent;
    {
      // This models a later worker-side saved-intent command under the SAME
      // held lease. Checkpoint must read it fresh, not restore admission state.
      auto intent = storage::ConfigFile::load(lease, "downloads.config"); QVERIFY(intent);
      editFirstEntry(*intent, {{"state", "i70e"}, {"forceStart", "i1e"}, {"position", "i8e"}});
      const auto rawHash = partial.hash->bytes();
      const std::string hash(reinterpret_cast<const char*>(rawHash.data()), rawHash.size());
      QVERIFY(intent->document().replaceValue("pause_data", "ld4:hash" + bytes(hash)
          + "5:forcei1e5:owneri7eee"));
      QVERIFY(intent->save(lease));
      requestedIntent = test::read(partial.profile + QStringLiteral("/downloads.config"));
    }
    QVERIFY(service->stopManaged(*partial.hash));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && phase(*service, *partial.hash, Phase::Stopped), 10000);
    const auto stopped = status(*service, *partial.hash); QVERIFY(stopped && !stopped->failure);
    QVERIFY(stopped->received > 0 && !stopped->complete);
    auto requested = parse(requestedIntent);
    auto saved = parse(test::read(partial.profile + QStringLiteral("/downloads.config")));
    const auto& entry = firstEntry(saved);
    for (const auto key : {"state", "forceStart", "position", "file_priorities", "opaque.managed"}) {
      QVERIFY(firstEntry(requested).find(key) && entry.find(key));
      QCOMPARE(encoded(*entry.find(key)), encoded(*firstEntry(requested).find(key)));
    }
    QCOMPARE(encoded(*saved.find("pause_data")), encoded(*requested.find("pause_data")));
    QCOMPARE(integer(entry, "downloaded"), std::int64_t{1000} + stopped->received);
    QCOMPARE(integer(entry, "uploaded"), std::int64_t{200} + stopped->sent);
    auto state = parse(test::read(partial.stateFile));
    const auto* resume = state.find("resume"); QVERIFY(resume);
    const auto* data = resume->find("data"); QVERIFY(data);
    const auto* pieces = data->find("resume data"); QVERIFY(pieces);
    QCOMPARE(pieces->type, core::Node::Type::Bytes);
    QCOMPARE(pieces->bytes.size(), std::size_t{test::OwnedEngineFixture::PayloadBytes / 16384});
    // Every original piece remains RECHECK after a partial stop, including
    // payload not yet checked in this admission. Zero would hide earlier good
    // payload from the original reader. Verified accounting is independent.
    QCOMPARE(std::string(pieces->bytes), std::string(pieces->bytes.size(), '\2'));
    QVERIFY(data->find("valid"));
    QCOMPARE(data->find("valid")->type, core::Node::Type::Integer);
    QCOMPARE(data->find("valid")->integer, std::int64_t{0});
    const auto verified = static_cast<std::int64_t>(stopped->pieces) * 16384;
    QVERIFY(verified > 0 && verified < test::OwnedEngineFixture::PayloadBytes);
    QCOMPARE(integer(entry, "completedbytes"), verified);
    QVERIFY(state.find("opaque.fixture") && initialState.find("opaque.fixture"));
    QCOMPARE(encoded(*state.find("opaque.fixture")), encoded(*initialState.find("opaque.fixture")));
    const auto* attributes = state.find("attributes"); QVERIFY(attributes);
    QVERIFY(attributes->find("resumecomplete"));
    QCOMPARE(attributes->find("resumecomplete")->type, core::Node::Type::Integer);
    QCOMPARE(attributes->find("resumecomplete")->integer, std::int64_t{1});
    QVERIFY(attributes->find("opaque.fixture") && initialState.find("attributes")->find("opaque.fixture"));
    QCOMPARE(encoded(*attributes->find("opaque.fixture")), encoded(*initialState.find("attributes")->find("opaque.fixture")));
    const auto* fileDownloaded = attributes->find("filedownloaded"); QVERIFY(fileDownloaded);
    const auto* perFile = fileDownloaded->find("downloaded"); QVERIFY(perFile);
    QCOMPARE(perFile->children.size(), std::size_t{1});
    QCOMPARE(perFile->children.front().type, core::Node::Type::Integer);
    QCOMPARE(perFile->children.front().integer, verified);
    QVERIFY(partial.intact(false));

    // Resume on the same persistent session from the actual partial payload.
    QVERIFY(service->startManaged(lease, {.hash = *partial.hash, .ownedLoopbackPeerPort = full.port(), .forceStart = true}));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && phase(*service, *partial.hash, Phase::Seeding), 30000);
    const auto resumed = status(*service, *partial.hash); QVERIFY(resumed && resumed->complete && !resumed->failure);
    QVERIFY(resumed->admission > originalAdmission->admission);
    QVERIFY(partial.intact());
    QVERIFY(service->stopManaged(*partial.hash));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && phase(*service, *partial.hash, Phase::Stopped), 10000);
    const auto finished = status(*service, *partial.hash); QVERIFY(finished && !finished->failure);
    const auto complete = parse(test::read(partial.profile + QStringLiteral("/downloads.config")));
    QCOMPARE(integer(firstEntry(complete), "downloaded"), integer(entry, "downloaded") + finished->received);
    QCOMPARE(integer(firstEntry(complete), "completedbytes"), std::int64_t{test::OwnedEngineFixture::PayloadBytes});
    QCOMPARE(encoded(*complete.find("pause_data")), encoded(*requested.find("pause_data")));
    QCOMPARE(encoded(*firstEntry(complete).find("forceStart")), encoded(*firstEntry(requested).find("forceStart")));
    // A third, checked-complete admission must not charge its restored bytes
    // again as new network traffic.
    const auto beforeRepeat = integer(firstEntry(complete), "downloaded");
    QVERIFY(service->startManaged(lease, {.hash = *partial.hash, .ownedLoopbackPeerPort = full.port(), .forceStart = true}));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && phase(*service, *partial.hash, Phase::Seeding), 30000);
    QCOMPARE(status(*service, *partial.hash)->received, std::int64_t{0});
    QVERIFY(service->stopManaged(*partial.hash));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && phase(*service, *partial.hash, Phase::Stopped), 10000);
    QVERIFY(!status(*service, *partial.hash)->failure);
    const auto repeated = parse(test::read(partial.profile + QStringLiteral("/downloads.config")));
    QCOMPARE(integer(firstEntry(repeated), "downloaded"), beforeRepeat);
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased && closed.retirement.peerCounters);
    QVERIFY(partial.intact());
  }

  void failedAndForeignAdmissionsDoNotReplaceTheOtherActiveTorrent() {
    test::OwnedEngineFixture first(false, true), foreign(true);
    QTRY_VERIFY_WITH_TIMEOUT(first.ready() && foreign.ready(), 30000);
    auto lease = held(first);
    auto otherLease = held(foreign);
    auto service = owner();
    const auto foreignInventory = test::read(foreign.profile + QStringLiteral("/downloads.config"));
    const auto foreignState = test::read(foreign.stateFile);
    QVERIFY(service->startManaged(lease, {.hash = *first.hash, .ownedLoopbackPeerPort = first.port(), .forceStart = true}));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && hasReceived(*service, *first.hash), 30000);
    const auto original = status(*service, *first.hash); QVERIFY(original);
    const auto duplicate = service->startManaged(lease, {.hash = *first.hash, .ownedLoopbackPeerPort = first.port()});
    QVERIFY(!duplicate); QCOMPARE(duplicate.error().code, storage::ErrorCode::Conflict);
    const auto wrongProfile = service->startManaged(otherLease, {.hash = *foreign.hash, .ownedLoopbackPeerPort = foreign.port()});
    QVERIFY(!wrongProfile); QCOMPARE(wrongProfile.error().code, storage::ErrorCode::Conflict);
    const auto missingHash = service->startManaged(lease, {.hash = *foreign.hash, .ownedLoopbackPeerPort = foreign.port()});
    QVERIFY(!missingHash);
    QCOMPARE(service->managedDownloads().size(), std::size_t{1});
    QCOMPARE(status(*service, *first.hash)->admission, original->admission);
    QVERIFY(!status(*service, *first.hash)->failure);
    QCOMPARE(test::read(foreign.profile + QStringLiteral("/downloads.config")), foreignInventory);
    QCOMPARE(test::read(foreign.stateFile), foreignState);
    QVERIFY(foreign.intact(false));
    QVERIFY(service->stopManaged(*first.hash));
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value() && phase(*service, *first.hash, Phase::Stopped), 10000);
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased && closed.retirement.peerCounters);
    const auto rejected = service->startManaged(lease, {.hash = *first.hash, .ownedLoopbackPeerPort = first.port()});
    QVERIFY(!rejected); QCOMPARE(rejected.error().code, storage::ErrorCode::Conflict);
    QVERIFY(!service->pauseManaged(*first.hash, false));
    QVERIFY(!service->queueManaged(*first.hash, true, false));
    QVERIFY(!service->stopManaged(*first.hash));
    QVERIFY(first.intact(false));
  }

  void unsafePayloadPreparationDoesNotAdmitOrRewriteAProfile() {
    test::OwnedEngineFixture fixture(false);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(), 30000);
    const auto path = fixture.payload + '/' + fixture.files.front().relative;
    const auto saved = path + QStringLiteral(".owned-original");
    const auto outside = fixture.root.path() + QStringLiteral("/owned-sentinel");
    const QByteArray sentinel("untouched generated sentinel");
    test::write(outside, sentinel);
    QVERIFY(QFile::rename(path, saved));
    QVERIFY(::symlink(outside.toStdString().c_str(), path.toStdString().c_str()) == 0);
    const auto inventory = test::read(fixture.profile + QStringLiteral("/downloads.config"));
    const auto state = test::read(fixture.stateFile);
    auto lease = held(fixture);
    auto service = owner();
    const auto denied = service->startManaged(lease, {.hash = *fixture.hash, .ownedLoopbackPeerPort = fixture.port()});
    QVERIFY(!denied);
    QVERIFY(service->managedDownloads().empty());
    QCOMPARE(test::read(fixture.profile + QStringLiteral("/downloads.config")), inventory);
    QCOMPARE(test::read(fixture.stateFile), state);
    QCOMPARE(test::read(outside), sentinel);
    QVERIFY(QFileInfo(path).isSymLink());
    const auto closed = service->close(std::chrono::steady_clock::now() + 5s);
    QVERIFY(closed.sessionJoined && closed.retirement.storageReleased);
    QVERIFY(!closed.retirement.peerCounters);
    QVERIFY(QFile::remove(path));
    QVERIFY(QFile::rename(saved, path));
    QVERIFY(fixture.intact(false));
  }
};
namespace {
int prepareVerifiedSeedReceipt(const QString& root,bool multi,const QString& outcome,const QString& versioned={}){
  try{
    test::require(outcome=="complete"||outcome=="corrupt"||outcome=="stopped","Unknown verified-seed outcome");
    const auto fixture=versioned.isEmpty()?test::prepareVerifiedSeedCase(root,multi,outcome=="corrupt")
        :test::prepareVersionedVerifiedSeedCase(root,versioned,outcome=="corrupt");
    auto lease=storage::ProfileLease::acquire(fixture.profile.toStdString());test::require(lease.has_value(),"Own generated verification lease");
    test::require(test::importVerifiedSeedCase(*lease,fixture).has_value(),"Actual verified-intent intake failed");auto service=owner();
    test::require(service->startManaged(*lease,{.hash=fixture.hash,.verifyExisting=true}).has_value(),"Actual RO verification admission failed");
    if(outcome=="stopped"){test::require(service->stopManaged(fixture.hash).has_value(),"Actual verification cancellation failed");}
    else{test::require(test::spinVerified(*service,fixture.hash),"Actual verification deadline");}
    auto actual=test::verifiedStatus(*service,fixture.hash);test::require(actual.has_value(),"Missing actual verification observation");
    const bool released=actual->verifiedSeeding==engine::VerifiedSeedingPhase::Released;
    test::require(released==(outcome=="complete"),"Unexpected actual verification outcome");
    const auto bits=service->verifiedSeedPieces(fixture.hash);
    QByteArray receiverHash;if(released){receiverHash=test::receiveVerifiedSeed(*service,fixture);test::require(service->poll().has_value(),"Actual post-upload sample failed");actual=test::verifiedStatus(*service,fixture.hash);}
    const auto close=service->close(std::chrono::steady_clock::now()+5s);test::require(close.sessionJoined,"Actual owner did not join");
    test::require(test::verifiedSeedBytesIntact(fixture,outcome=="corrupt"),"Read-only owner changed payload");
    const auto saved=storage::readVerifiedSeedIntent(*lease,fixture.hash);test::require(saved&&saved->has_value(),"Read actual final saved intent");
    const auto state=parse(test::read(fixture.profile+"/active/"+QString::fromStdString(fixture.hash.hex())+".dat"));const auto flags=core::originalDownloadFlags(state.find("attributes"));test::require(flags.has_value(),"Actual original flags absent");
    QJsonArray files,verified;for(auto bit:bits){verified.push_back(static_cast<int>(bit));}
    for(const auto& file:fixture.files){const auto path=fixture.payload+'/'+file.relative;struct stat info{};test::require(::stat(path.toStdString().c_str(),&info)==0,"Read actual final inode");files.push_back(QJsonObject{{"path",path},{"bytes",QString::number(info.st_size)},{"sha256",QString::fromLatin1(QCryptographicHash::hash(test::read(path),QCryptographicHash::Sha256).toHex())},{"dev",QString::number(info.st_dev)},{"ino",QString::number(info.st_ino)}});}
    QJsonObject result{{"schema",1},{"status","ACTUAL_NATIVE_VERIFIED_SEED_CASE"},{"profile",fixture.profile},{"metadata",fixture.metadata},{"payloadRoot",fixture.payload},{"hash",QString::fromStdString(fixture.hash.hex())},{"admission",QString::number(actual->admission)},{"released",released},{"phase",static_cast<int>(actual->verifiedSeeding)},{"savedStartup",static_cast<int>((*saved)->startup)},{"forceStart",(*saved)->forceStart},{"moveFlag",bool(*flags&8)},{"marker",(*saved)->state==storage::VerifiedSeedState::Released?2:1},{"totalPieces",actual->totalPieces},{"verifiedPieces",verified},{"received",QString::number(actual->received)},{"sent",QString::number(actual->sent)},{"receiverCompleted",!receiverHash.isEmpty()},{"receiverSha256",QString::fromLatin1(receiverHash)},{"files",files},{"sessionJoined",close.sessionJoined},{"storageReleased",close.retirement.storageReleased},{"state",fixture.profile+"/active/"+QString::fromStdString(fixture.hash.hex())+".dat"},{"archive",fixture.profile+"/torrents/"+QString::fromStdString(fixture.hash.hex())+".torrent"}};
    if(!versioned.isEmpty()){
      const auto document=parse(test::read(fixture.metadata));const auto layout=compat::originalLayout(document);test::require(layout.has_value(),"Actual versioned receipt layout");
      lt::error_code error;const lt::torrent_info metadata(fixture.metadata.toStdString(),error);test::require(!error,"Actual versioned receipt metadata");
      const auto identity=storage::originalTorrentIdentityFromLibtorrent(document,metadata);test::require(identity.has_value(),"Actual versioned receipt full identity");
      const auto projection=engine::detail::projectOriginalLayout(*layout,metadata);test::require(projection.has_value(),"Actual versioned receipt engine projection");
      const auto full=metadata.info_hashes();const auto hex=[](const auto& hash){return QString::fromLatin1(QByteArray::fromStdString(hash.to_string()).toHex().toUpper());};
      QJsonArray originalFiles,engineFiles;std::size_t real=0;
      for(std::size_t n=0;n<layout->files.size();++n){const auto& file=layout->files[n];
        originalFiles.push_back(QJsonObject{{"index",static_cast<int>(n)},{"offset",QString::number(file.offset)},{"bytes",QString::number(file.bytes)},{"padding",file.padding},{"path",QString::fromStdString(file.path)}});
        if(!file.padding){test::require(real<static_cast<std::size_t>(files.size()),"Actual real receipt file count");auto value=files[static_cast<qsizetype>(real)].toObject();value.insert("index",static_cast<int>(n));files[static_cast<qsizetype>(real++)]=value;}
      }
      test::require(real==static_cast<std::size_t>(files.size()),"Actual receipt real files cover original slots");
      for(int n=0;n<metadata.num_files();++n){const auto index=lt::file_index_t{n};engineFiles.push_back(QJsonObject{{"index",n},{"offset",QString::number(metadata.files().file_offset(index))},{"bytes",QString::number(metadata.files().file_size(index))},{"padding",metadata.files().pad_file_at(index)},{"path",QString::fromStdString(metadata.files().file_path(index))}});}
      const auto rawInfo=storage::originalTorrentInfoSubject(document);test::require(rawInfo.has_value(),"Actual versioned receipt raw info");
      result.insert("schema",2);result.insert("format",versioned);result.insert("selectedVersion",identity->selected==storage::OriginalSelectedHash::V2?2:1);
      result.insert("fullV1",full.has_v1()?hex(full.v1):QStringLiteral("-"));result.insert("fullV2",hex(full.v2));
      result.insert("rawInfoSha256",QString::fromLatin1(QCryptographicHash::hash(QByteArray(rawInfo->data(),static_cast<qsizetype>(rawInfo->size())),QCryptographicHash::Sha256).toHex()));
      result.insert("originalFiles",originalFiles);result.insert("engineFiles",engineFiles);result.insert("files",files);result.insert("engineTrailingPad",projection->trailingPad.has_value());
      result.insert("originalTotalBytes",QString::number(layout->bytes));result.insert("engineTotalBytes",QString::number(metadata.total_size()));result.insert("pieceLength",QString::number(layout->pieceLength));
      result.insert("observedCompleted",QString::number(actual->completed));result.insert("observedBytes",QString::number(actual->bytes));result.insert("padPathsAbsent",test::versionedPadPathsAbsent(fixture));
      test::require(test::versionedRealSnapshotsIntact(fixture,outcome=="corrupt")&&test::versionedPadPathsAbsent(fixture),"Versioned RO snapshot or virtual pad changed");
    }
    test::write(root+"/native-receipt.json",QJsonDocument(result).toJson(QJsonDocument::Indented));return 0;
  }catch(const std::exception& error){qCritical("%s",error.what());return 1;}
}
}
int main(int argc,char** argv){
  QCoreApplication application(argc,argv);
  if(argc==5&&QString::fromLocal8Bit(argv[1])==QStringLiteral("--prepare-verified-seed")){
    const auto kind=QString::fromLocal8Bit(argv[3]);if(kind!="single"&&kind!="multi"){return 2;}
    return prepareVerifiedSeedReceipt(QString::fromLocal8Bit(argv[2]),kind=="multi",QString::fromLocal8Bit(argv[4]));
  }
  if(argc==5&&QString::fromLocal8Bit(argv[1])==QStringLiteral("--prepare-versioned-verified-seed")){
    const auto kind=QString::fromLocal8Bit(argv[3]);
    if(kind!="v2-aligned"&&kind!="v2-tail"&&kind!="hybrid-v1"&&kind!="hybrid-v2"){return 2;}
    return prepareVerifiedSeedReceipt(QString::fromLocal8Bit(argv[2]),false,QString::fromLocal8Bit(argv[4]),kind);
  }
  ManagedDownloadTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "managed_download_tests.moc"
