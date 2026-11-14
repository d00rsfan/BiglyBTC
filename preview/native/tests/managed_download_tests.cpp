#include "persistent_session.hpp"
#include "recheck_fixture.hpp"
#include "allocation_fixture.hpp"
#include "managed_storage.hpp"
#include "managed_payload_counters.hpp"
#include <limits>
#include "owned_engine_fixture.hpp"
#include "verified_seeding_fixture.hpp"
#include "versioned_verified_seeding_fixture.hpp"
#include "custom_verified_seeding_fixture.hpp"
#include "linked_verified_seeding_fixture.hpp"
#include "selected_verified_seeding_fixture.hpp"
#include "original_filename_policy.hpp"
#include "library_commands.hpp"
#include "category_store.hpp"
#include "mixed_i2p_source.hpp"
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
#include <stdexcept>
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
  void cleanPartialResumeSurvivesZeroTrafficClose_data(){
    QTest::addColumn<bool>("shortFile");QTest::newRow("full-physical-size")<<false;QTest::newRow("short-missing-tail")<<true;
  }
  void cleanPartialResumeSurvivesZeroTrafficClose(){
    QFETCH(bool,shortFile);test::OwnedEngineFixture fixture(false);QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);
    const auto payload=fixture.payload+'/'+fixture.files.front().relative;auto bytes=fixture.files.front().expected.first(65536);if(!shortFile){bytes+=QByteArray(fixture.files.front().expected.size()-bytes.size(),'\0');}test::write(payload,bytes);
    const std::string bitmap=std::string(4,'\1')+std::string(4,'\0');auto state=parse(test::read(fixture.stateFile));QVERIFY(state.replaceValue("resume","d4:datad6:blocksde11:resume data8:"+bitmap+"5:validi1eee"));
    auto attrs=core::Document::parse(encoded(*state.find("attributes")));QVERIFY(attrs);QVERIFY(attrs->replaceValue("resumecomplete","i1e"));QVERIFY(state.replaceValue("attributes",attrs->serialize()));test::write(fixture.stateFile,QByteArray::fromStdString(state.serialize()));
    auto lease=held(fixture);engine::NetworkPolicy policy;policy.mode=engine::NetworkMode::Normal;policy.context=engine::NetworkPolicyContext::ExistingLibraryPublic;auto service=owner();QVERIFY(service->configureNetwork(policy));QVERIFY(service->startIdle(lease));const auto started=service->startManaged(lease,{*fixture.hash,0,true,false});QVERIFY2(started,started?"":started.error().message.c_str());
    const auto ready=[&]{if(!service->poll()){return false;}const auto value=status(*service,*fixture.hash);return value&&value->checked;};QTRY_VERIFY_WITH_TIMEOUT(ready(),10000);const auto before=status(*service,*fixture.hash);QVERIFY(before);QCOMPARE(before->received,std::int64_t{0});QCOMPARE(before->originalResumeDecision,std::string("Saved checked pieces restored"));
    std::vector<std::size_t> closeProgress;bool reentryRefused=true;
    const auto closed=service->close(std::chrono::steady_clock::now()+5s,[&](std::size_t done,std::size_t total){
      if(total!=1){reentryRefused=false;}closeProgress.push_back(done);
      const auto recursive=service->poll();reentryRefused=reentryRefused&&!recursive&&recursive.error()==engine::AccountingError::ActiveOperation;
      if(shortFile){throw std::runtime_error("Owned progress observer fault");}
    },engine::PersistentSession::CloseInventoryMode::BatchPublic);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);
    QVERIFY(reentryRefused&&!closeProgress.empty());QCOMPARE(closeProgress.front(),std::size_t{0});
    if(shortFile){QCOMPARE(closeProgress.size(),std::size_t{1});}else{QVERIFY(closeProgress.size()>=2);QCOMPARE(closeProgress.back(),std::size_t{1});QVERIFY(std::is_sorted(closeProgress.begin(),closeProgress.end()));}
    service.reset();const auto saved=parse(test::read(fixture.stateFile));const auto* data=saved.find("resume")->find("data");
    QCOMPARE(std::string(data->find("resume data")->bytes),bitmap);QCOMPARE(data->find("valid")->integer,std::int64_t{1});QCOMPARE(test::read(payload).first(65536),bytes.first(65536));
  }
  void selectedSeedWithInvalidFlagRestoresDoneAndMissingSkippedPieces_data(){
    QTest::addColumn<bool>("linked");QTest::addColumn<bool>("modified");
    QTest::newRow("linear-missing-skipped")<<false<<false;
    QTest::newRow("mapped-missing-skipped")<<true<<false;
    QTest::newRow("changed-done-extent-must-check")<<true<<true;
  }
  void selectedSeedWithInvalidFlagRestoresDoneAndMissingSkippedPieces(){
    QFETCH(bool,linked);QFETCH(bool,modified);
    test::OwnedEngineFixture fixture(true);QTRY_VERIFY_WITH_TIMEOUT(fixture.ready(),30000);
    const auto kept=linked?fixture.root.path()+QStringLiteral("/linked-kept.bin"):fixture.payload+'/'+fixture.files[0].relative;
    test::write(kept,fixture.files[0].expected);
    auto state=core::Document::parse(test::read(fixture.stateFile).toStdString());QVERIFY(state);
    const std::string bits(4,'\1');const std::string bitmap=bits+std::string(4,'\0');
    QVERIFY(state->replaceValue("resume","d4:datad6:blocksde11:resume data8:"+bitmap+"5:validi0eee"));
    if(linked){
      const auto from=(fixture.payload+'/'+fixture.files[0].relative).toStdString();const auto to=kept.toStdString();
      const auto link=std::string("0\n")+from+"\n"+to;const auto attrs=core::Document::parse(core::encodeValue(*state->find("attributes")).value());QVERIFY(attrs);
      auto changed=*attrs;QVERIFY(changed.replaceValue("filelinks2","l"+std::to_string(link.size())+":"+link+"e"));
      QVERIFY(state->replaceValue("attributes",changed.serialize()));
    }
    test::write(fixture.stateFile,QByteArray::fromStdString(state->serialize()));
    auto inventory=core::Document::parse(test::read(fixture.profile+QStringLiteral("/downloads.config")).toStdString());QVERIFY(inventory);
    const auto* rows=inventory->find("downloads");QVERIFY(rows&&rows->children.size()==1);
    auto row=core::Document::parse(core::encodeValue(rows->children[0]).value());QVERIFY(row);
    QVERIFY(row->replaceValue("file_priorities","li1ei0ee"));QVERIFY(row->replaceValue("completedbytes","i65536e"));
    QVERIFY(inventory->replaceValue("downloads","l"+row->serialize()+"e"));test::write(fixture.profile+QStringLiteral("/downloads.config"),QByteArray::fromStdString(inventory->serialize()));
    if(modified){QFile changed(kept);QVERIFY(changed.open(QIODevice::ReadWrite));QVERIFY(changed.setFileTime(QDateTime::currentDateTimeUtc().addSecs(2),QFileDevice::FileModificationTime));changed.close();}
    auto lease=held(fixture);engine::NetworkPolicy policy;policy.mode=engine::NetworkMode::Normal;policy.context=engine::NetworkPolicyContext::ExistingLibraryPublic;
    const auto initialState=test::read(fixture.stateFile),initialInventory=test::read(fixture.profile+"/downloads.config");
    auto first=owner();QVERIFY(first->configureNetwork(policy));QVERIFY(first->startIdle(lease));const auto started=first->startManaged(lease,{*fixture.hash,0,true,false});
    if(modified){QVERIFY(!started);QCOMPARE(started.error().code,storage::ErrorCode::Unsupported);QVERIFY(first->managedDownloads().empty());QCOMPARE(test::read(fixture.stateFile),initialState);QCOMPARE(test::read(fixture.profile+"/downloads.config"),initialInventory);const auto closed=first->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased);QCOMPARE(test::read(kept),fixture.files[0].expected);return;}QVERIFY(started);
    const auto checked=[&]{static_cast<void>(first->poll());const auto current=first->managedDownloads();return current.size()==1&&current[0].checked;};
    QTRY_VERIFY_WITH_TIMEOUT(checked(),15000);auto status=first->managedDownloads()[0];
    {QCOMPARE(status.originalResumeDecision,std::string("Saved checked pieces restored"));QVERIFY(status.selectedComplete);QVERIFY(!status.complete);QCOMPARE(status.storageHashOperations,std::uint64_t{0});}
    const auto closed=first->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased);first.reset();
    if(!modified){
      auto saved=core::Document::parse(test::read(fixture.stateFile).toStdString());QVERIFY(saved);const auto* resume=saved->find("resume");QVERIFY(resume&&resume->find("data"));QCOMPARE(std::string(resume->find("data")->find("resume data")->bytes),bitmap);
      auto warm=owner();QVERIFY(warm->configureNetwork(policy));QVERIFY(warm->startIdle(lease));QVERIFY(warm->startManaged(lease,{*fixture.hash,0,true,false}));
      const auto restored=[&]{static_cast<void>(warm->poll());const auto current=warm->managedDownloads();return current.size()==1&&current[0].checked;};QTRY_VERIFY_WITH_TIMEOUT(restored(),15000);
      status=warm->managedDownloads()[0];QVERIFY(status.selectedComplete);QCOMPARE(status.storageHashOperations,std::uint64_t{0});QCOMPARE(status.originalResumeDecision,std::string("Saved checked pieces restored"));
      const auto end=warm->close(std::chrono::steady_clock::now()+5s);QVERIFY(end.sessionJoined&&end.retirement.storageReleased);
    }
    QCOMPARE(test::read(kept),fixture.files[0].expected);
  }
  void savedCompactSkippedEdgesAvoidWholeTorrentRecheck_data(){
    QTest::addColumn<QString>("damage");
    for(const auto* value:{"intact","missing-control","truncated-edge","false-middle-done","changed-linear"}){QTest::newRow(value)<<QString::fromLatin1(value);}
  }
  void savedCompactSkippedEdgesAvoidWholeTorrentRecheck(){
    QFETCH(QString,damage);namespace lt=libtorrent;
    QTemporaryDir root(QStringLiteral("/tmp/biglybtc-compact-resume-XXXXXX"));QVERIFY(root.isValid());
    const auto profile=root.path()+"/profile",payload=root.path()+"/payload";QVERIFY(QDir().mkpath(profile+"/active"));
    constexpr int pieceBytes=16384;const std::array<int,4> lengths{1000,50000,30000,50000};
    QByteArray all(131000,'\0');for(qsizetype n=0;n<all.size();++n){all[n]=static_cast<char>((n*37+19)%251);}
    lt::entry info(lt::entry::dictionary_type{});info["name"]="bundle";info["piece length"]=pieceBytes;
    std::string pieceHashes;for(qsizetype n=0;n<all.size();n+=pieceBytes){pieceHashes+=QCryptographicHash::hash(all.mid(n,pieceBytes),QCryptographicHash::Sha1).toStdString();}info["pieces"]=pieceHashes;
    lt::entry::list_type files;for(std::size_t n=0;n<lengths.size();++n){lt::entry file(lt::entry::dictionary_type{});file["length"]=lengths[n];file["path"]=lt::entry::list_type{lt::entry(std::to_string(n)+".bin")};files.push_back(std::move(file));}info["files"]=std::move(files);
    const auto hash=core::TorrentHash::parse(QCryptographicHash::hash(QByteArray::fromStdString(test::encoded(info)),QCryptographicHash::Sha1).toStdString());QVERIFY(hash);
    std::array<QByteArray,4> physical;int offset=0;
    for(std::size_t n=0;n<lengths.size();++n){
      const auto full=all.mid(offset,lengths[n]);physical[n]=full;
      if(n%2){const int first=(pieceBytes-offset%pieceBytes)%pieceBytes,tail=(lengths[n]-first)%pieceBytes;physical[n]=full.first(first)+full.last(tail);
        if(!(damage=="missing-control"&&n==1)){lt::entry control(lt::entry::dictionary_type{});control["version"]=0;control["length"]=lengths[n];test::write(profile+"/active/"+QString::fromStdString(hash->hex())+"/fmfile"+QString::number(n)+".dat",QByteArray::fromStdString(test::encoded(control)));}
      }
      if(damage=="truncated-edge"&&n==1){physical[n].chop(1);}
      test::write(payload+"/bundle/"+QString::number(n)+".bin",physical[n]);offset+=lengths[n];
    }
    const std::string originalMap("\1\0\0\1\1\0\0\0",8);auto map=originalMap;if(damage=="false-middle-done"){map[1]=1;}
    lt::entry state(lt::entry::dictionary_type{});state["info"]=info;state["attributes"]["networks"]=lt::entry::list_type{lt::entry("Public")};state["attributes"]["storetypes"]=lt::entry::list_type{lt::entry("L"),lt::entry("C"),lt::entry("L"),lt::entry("C")};state["attributes"]["opaque"]="retained";
    state["resume"]["data"]["valid"]=1;state["resume"]["data"]["resume data"]=map;state["resume"]["data"]["blocks"]=lt::entry::dictionary_type{};
    const auto statePath=profile+"/active/"+QString::fromStdString(hash->hex())+".dat";test::write(statePath,QByteArray::fromStdString(test::encoded(state)));
    lt::entry row(lt::entry::dictionary_type{});row["torrent_hash"]=std::string(reinterpret_cast<const char*>(hash->bytes().data()),hash->bytes().size());row["torrent"]=statePath.toStdString();row["save_dir"]=payload.toStdString();row["save_file"]="bundle";row["state"]=70;row["position"]=1;row["downloaded"]=0;row["uploaded"]=0;row["file_priorities_c"]["-1"]="0,2";row["file_priorities_c"]["0"]="1,3";
    lt::entry inventory(lt::entry::dictionary_type{});inventory["downloads"]=lt::entry::list_type{row};test::write(profile+"/downloads.config",QByteArray::fromStdString(test::encoded(inventory)));
    if(damage=="changed-linear"){QFile changed(payload+"/bundle/2.bin");QVERIFY(changed.open(QIODevice::ReadWrite));QVERIFY(changed.setFileTime(QDateTime::currentDateTimeUtc().addSecs(2),QFileDevice::FileModificationTime));}
    auto lease=storage::ProfileLease::acquire(profile.toStdString());QVERIFY(lease);engine::NetworkPolicy policy;policy.mode=engine::NetworkMode::Normal;policy.context=engine::NetworkPolicyContext::ExistingLibraryPublic;
    const auto initialState=test::read(statePath),initialInventory=test::read(profile+"/downloads.config");
    for(int run=0;run<(damage=="intact"?2:1);++run){
      auto service=owner();QVERIFY(service->configureNetwork(policy));QVERIFY(service->startIdle(*lease));const auto started=service->startManaged(*lease,{*hash,0,true,false});
      if(damage!="intact"){QVERIFY(!started);QCOMPARE(started.error().code,storage::ErrorCode::Unsupported);QVERIFY(service->managedDownloads().empty());QCOMPARE(test::read(statePath),initialState);QCOMPARE(test::read(profile+"/downloads.config"),initialInventory);for(std::size_t n=0;n<lengths.size();++n){QCOMPARE(test::read(payload+"/bundle/"+QString::number(n)+".bin"),physical[n]);}const auto closed=service->close(std::chrono::steady_clock::now()+5s,{},engine::PersistentSession::CloseInventoryMode::BatchPublic);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased);continue;}QVERIFY(started);
      const auto ready=[&]{if(!service->poll()){return false;}return status(*service,*hash).has_value()&&status(*service,*hash)->checked;};
      QTRY_VERIFY_WITH_TIMEOUT(ready(),15000);const auto current=*status(*service,*hash);
      if(damage=="intact"){QCOMPARE(current.originalResumeDecision,std::string("Saved checked pieces restored"));QCOMPARE(current.storageHashOperations,std::uint64_t{0});QVERIFY(current.selectedComplete);QVERIFY(!current.complete);}
      const auto closed=service->close(std::chrono::steady_clock::now()+5s,{},engine::PersistentSession::CloseInventoryMode::BatchPublic);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased);
      if(damage=="intact"){const auto saved=parse(test::read(statePath));QCOMPARE(std::string(saved.find("resume")->find("data")->find("resume data")->bytes),originalMap);QCOMPARE(std::string(saved.find("attributes")->find("opaque")->bytes),std::string("retained"));for(std::size_t n=0;n<lengths.size();++n){QCOMPARE(test::read(payload+"/bundle/"+QString::number(n)+".bin"),physical[n]);QCOMPARE(std::string(saved.find("attributes")->find("storetypes")->children[n].bytes),std::string(n%2?"C":"L"));}}
    }
  }
  void pureV2SavedDoneMapRestoresAndServesVerifiedBytes_data(){
    QTest::addColumn<QString>("kind");QTest::addColumn<int>("pieceSize");QTest::addColumn<bool>("changed");QTest::addColumn<QString>("layers");
    QTest::newRow("aligned-block-pieces")<<QStringLiteral("v2-aligned")<<16384<<false<<QString{};
    QTest::newRow("short-tail-multiblock-pieces")<<QStringLiteral("v2-tail")<<65536<<false<<QString{};
    QTest::newRow("multifile-virtual-padding")<<QStringLiteral("v2-multi")<<65536<<false<<QString{};
    QTest::newRow("newer-payload-is-deferred")<<QStringLiteral("v2-tail")<<65536<<true<<QString{};
    QTest::newRow("missing-Merkle-layer-is-deferred")<<QStringLiteral("v2-tail")<<65536<<false<<QStringLiteral("missing");
    QTest::newRow("corrupt-Merkle-layer-is-deferred")<<QStringLiteral("v2-tail")<<65536<<false<<QStringLiteral("corrupt");
    QTest::newRow("hybrid-v2-aligned-blocks")<<QStringLiteral("hybrid-v2")<<16384<<false<<QString{};
    QTest::newRow("hybrid-v2-multiblock-tail")<<QStringLiteral("hybrid-v2")<<32768<<false<<QString{};
    QTest::newRow("hybrid-v2-short-files-root-only")<<QStringLiteral("hybrid-v2")<<65536<<false<<QString{};
    QTest::newRow("hybrid-v2-newer-payload-is-deferred")<<QStringLiteral("hybrid-v2")<<16384<<true<<QString{};
    QTest::newRow("hybrid-v2-missing-layer-is-deferred")<<QStringLiteral("hybrid-v2")<<16384<<false<<QStringLiteral("missing");
    QTest::newRow("hybrid-v2-corrupt-layer-is-deferred")<<QStringLiteral("hybrid-v2")<<16384<<false<<QStringLiteral("corrupt");
    QTest::newRow("hybrid-v2-missing-DONE-file-is-deferred")<<QStringLiteral("hybrid-v2")<<16384<<false<<QStringLiteral("missing-payload");
    QTest::newRow("hybrid-v2-truncated-DONE-file-is-deferred")<<QStringLiteral("hybrid-v2")<<16384<<false<<QStringLiteral("short-payload");
  }
  void pureV2SavedDoneMapRestoresAndServesVerifiedBytes(){
    QFETCH(QString,kind);QFETCH(int,pieceSize);QFETCH(bool,changed);QFETCH(QString,layers);namespace lt=libtorrent;
    QTemporaryDir directory;QVERIFY(directory.isValid());auto fixture=test::prepareVersionedVerifiedSeedCase(directory.path()+"/owned",kind,false,pieceSize);
    auto source=parse(test::read(fixture.metadata));lt::error_code error;lt::torrent_info info(fixture.metadata.toStdString(),error);QVERIFY(!error&&info.info_hashes().has_v2()&&info.info_hashes().has_v1()==kind.startsWith("hybrid-"));
    if(layers=="missing"){QVERIFY(source.replaceValue("piece layers","de"));}
    if(layers=="corrupt"){const auto* field=source.find("piece layers");QVERIFY(field&&!field->children.empty());auto layer=core::Document::parse(encoded(*field));QVERIFY(layer);const auto key=std::string(layer->root().children[0].bytes);auto value=std::string(layer->root().children[1].bytes);QVERIFY(!value.empty());value[0]^=1;QVERIFY(layer->replaceValue(key,bytes(value)));QVERIFY(source.replaceValue("piece layers",layer->serialize()));}
    const std::string map(static_cast<std::size_t>(info.num_pieces()),'\1');
    QVERIFY(source.replaceValue("resume","d4:datad6:blocksde11:resume data"+std::to_string(map.size())+":"+map+"5:validi1eee"));
    if(layers=="missing-payload"){QVERIFY(QFile::remove(fixture.payload+'/'+fixture.files.front().relative));}
    if(layers=="short-payload"){test::write(fixture.payload+'/'+fixture.files.front().relative,fixture.files.front().expected.first(1));}
    const auto statePath=fixture.profile+"/active/"+QString::fromStdString(fixture.hash.hex())+".dat";test::write(statePath,QByteArray::fromStdString(source.serialize()));
    lt::entry row(lt::entry::dictionary_type{});row["torrent_hash"]=std::string(reinterpret_cast<const char*>(fixture.hash.bytes().data()),fixture.hash.bytes().size());row["torrent"]=statePath.toStdString();row["save_dir"]=fixture.payload.toStdString();row["save_file"]=(kind=="v2-multi"||kind.startsWith("hybrid-"))?"bundle":"sample.bin";row["state"]=70;row["position"]=1;row["downloaded"]=0;row["uploaded"]=0;
    lt::entry inventory(lt::entry::dictionary_type{});inventory["downloads"]=lt::entry::list_type{row};test::write(fixture.profile+"/downloads.config",QByteArray::fromStdString(test::encoded(inventory)));
    if(changed){QFile payload(fixture.payload+'/'+fixture.files.front().relative);QVERIFY(payload.open(QIODevice::ReadWrite));QVERIFY(payload.setFileTime(QDateTime::currentDateTimeUtc().addSecs(2),QFileDevice::FileModificationTime));}
    auto lease=storage::ProfileLease::acquire(fixture.profile.toStdString());QVERIFY(lease);engine::NetworkPolicy policy;policy.mode=engine::NetworkMode::Normal;policy.context=engine::NetworkPolicyContext::ExistingLibraryPublic;
    const auto initialState=test::read(statePath),initialInventory=test::read(fixture.profile+"/downloads.config");
    for(int run=0;run<(changed?1:2);++run){
      auto service=owner();QVERIFY(service->configureNetwork(policy));QVERIFY(service->startIdle(*lease));const auto started=service->startManaged(*lease,{fixture.hash,0,true,false});
      if(changed||!layers.isEmpty()){QVERIFY(!started);QCOMPARE(started.error().code,storage::ErrorCode::Unsupported);QVERIFY(service->managedDownloads().empty());QCOMPARE(test::read(statePath),initialState);QCOMPARE(test::read(fixture.profile+"/downloads.config"),initialInventory);const auto closed=service->close(std::chrono::steady_clock::now()+5s,{},engine::PersistentSession::CloseInventoryMode::BatchPublic);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased);if(layers=="missing-payload"){QVERIFY(!QFileInfo::exists(fixture.payload+'/'+fixture.files.front().relative));}else if(layers=="short-payload"){QCOMPARE(test::read(fixture.payload+'/'+fixture.files.front().relative),fixture.files.front().expected.first(1));}else{QVERIFY(test::verifiedSeedBytesIntact(fixture));}return;}QVERIFY2(started,started?"":started.error().message.c_str());
      const auto ready=[&]{if(!service->poll()){return false;}const auto current=status(*service,fixture.hash);return current&&current->checked;};QTRY_VERIFY_WITH_TIMEOUT(ready(),15000);
      auto current=*status(*service,fixture.hash);QVERIFY2(!current.failure,current.failure?current.failure->message.c_str():"");QVERIFY(current.complete);
      {QCOMPARE(current.originalResumeDecision,std::string("Saved checked pieces restored"));QCOMPARE(current.storageHashOperations,std::uint64_t{0});QVERIFY(!test::receiveVerifiedSeed(*service,fixture).isEmpty());QVERIFY(status(*service,fixture.hash)->sent>0);}
      const auto closed=service->close(std::chrono::steady_clock::now()+5s,{},engine::PersistentSession::CloseInventoryMode::BatchPublic);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);
      const auto saved=parse(test::read(statePath));QCOMPARE(std::string(saved.find("resume")->find("data")->find("resume data")->bytes),map);QCOMPARE(encoded(*saved.find("info")),encoded(*source.find("info")));QVERIFY(test::versionedPadPathsAbsent(fixture));QVERIFY(test::verifiedSeedBytesIntact(fixture));
      const auto exported=qEnvironmentVariable("BIGLYBTC_TEST_HYBRID_RESUME_EXPORT");
      if(!exported.isEmpty()&&kind=="hybrid-v2"&&run==1){QVERIFY(QDir().mkpath(exported));test::write(exported+QString("/seed-%1.complete.dat").arg(pieceSize),test::read(statePath));}
      if(run==0&&!changed){QVERIFY(QDir(fixture.root+"/receiver").removeRecursively());}
    }
  }
  void selectedV2HybridPartialWarmResumeDownloadsOnlyMissingTail_data(){
    QTest::addColumn<bool>("shortTail");QTest::addColumn<int>("valid");
    QTest::newRow("short-tail-valid")<<true<<1;QTest::newRow("short-tail-invalid-flag")<<true<<0;
    QTest::newRow("full-size-not-done-tail")<<false<<1;
  }
  void selectedV2HybridPartialWarmResumeDownloadsOnlyMissingTail(){
    QFETCH(bool,shortTail);QFETCH(int,valid);namespace lt=libtorrent;
    QTemporaryDir directory;QVERIFY(directory.isValid());auto fixture=test::prepareVersionedVerifiedSeedCase(directory.path()+"/owned","hybrid-v2",false,16384);
    auto source=parse(test::read(fixture.metadata));lt::error_code error;auto info=std::make_shared<lt::torrent_info>(fixture.metadata.toStdString(),error);QVERIFY(!error);
    const auto identity=storage::originalTorrentIdentityFromLibtorrent(source,*info);QVERIFY(identity&&identity->full.v1&&identity->selected==storage::OriginalSelectedHash::V2);
    std::string bitmap(static_cast<std::size_t>(info->num_pieces()),'\1');QVERIFY(bitmap.size()>2);bitmap.back()='\0';
    // Keep independent verified source bytes; the target's last real piece is
    // NOT_DONE. Short files prove only the physical prefixes claimed as DONE.
    for(const auto& file:fixture.files){test::write(fixture.root+"/seed/"+file.relative,file.expected);}
    const auto target=fixture.payload+'/'+fixture.files.back().relative;
    if(shortTail){QFile file(target);QVERIFY(file.open(QIODevice::ReadWrite));QVERIFY(file.resize(32768));}
    QVERIFY(source.replaceValue("resume","d4:datad6:blocksde11:resume data"+bytes(bitmap)+"5:validi"+std::to_string(valid)+"eee"));
    const auto statePath=fixture.profile+"/active/"+QString::fromStdString(fixture.hash.hex())+".dat";test::write(statePath,QByteArray::fromStdString(source.serialize()));
    lt::entry row(lt::entry::dictionary_type{});row["torrent_hash"]=std::string(reinterpret_cast<const char*>(fixture.hash.bytes().data()),fixture.hash.bytes().size());row["torrent"]=statePath.toStdString();row["save_dir"]=fixture.payload.toStdString();row["save_file"]="bundle";row["state"]=70;row["position"]=1;row["downloaded"]=9;row["uploaded"]=11;
    lt::entry inventory(lt::entry::dictionary_type{});inventory["downloads"]=lt::entry::list_type{row};test::write(fixture.profile+"/downloads.config",QByteArray::fromStdString(test::encoded(inventory)));
    auto lease=storage::ProfileLease::acquire(fixture.profile.toStdString());QVERIFY(lease);engine::NetworkPolicy policy;policy.mode=engine::NetworkMode::Normal;policy.context=engine::NetworkPolicyContext::ExistingLibraryPublic;policy.bindAddresses={"127.0.0.1"};policy.listenPort=0;
    // First close before any peer exists, then resume the identical partial map.
    for(int run=0;run<2;++run){
      auto service=owner();QVERIFY(service->configureNetwork(policy));QVERIFY(service->startIdle(*lease));const auto started=service->startManaged(*lease,{fixture.hash,0,true,false});QVERIFY2(started,started?"":started.error().message.c_str());
      const auto ready=[&]{if(!service->poll()){return false;}const auto current=status(*service,fixture.hash);return current&&current->checked;};QTRY_VERIFY_WITH_TIMEOUT(ready(),15000);
      auto current=*status(*service,fixture.hash);QVERIFY(!current.failure&&!current.complete);QCOMPARE(current.originalResumeDecision,std::string("Saved checked pieces restored"));QCOMPARE(current.storageHashOperations,std::uint64_t{0});QCOMPARE(current.received,std::int64_t{0});
      if(run==1){
        lt::session seeder(test::localSettings());lt::add_torrent_params params;params.ti=info;params.save_path=(fixture.root+"/seed").toStdString();params.flags&=~(lt::torrent_flags::auto_managed|lt::torrent_flags::paused);params.flags|=lt::torrent_flags::disable_dht|lt::torrent_flags::disable_lsd|lt::torrent_flags::disable_pex;
        QVERIFY(engine::detail::selectManagedSwarm(params,*identity,engine::detail::managedPayloadCounterBridge()));const auto seed=seeder.add_torrent(params,error);QVERIFY(!error&&seed.is_valid());QTRY_VERIFY_WITH_TIMEOUT(seed.status().is_seeding,15000);
        QVERIFY(seeder.find_torrent(info->info_hashes().v2).is_valid());QVERIFY(!seeder.find_torrent(info->info_hashes().v1).is_valid());
        const auto port=service->tcpListenPort();QVERIFY(port&&*port);seed.connect_peer(lt::tcp::endpoint(lt::address_v4::loopback(),*port));
        const auto complete=[&]{if(!service->poll()){return false;}const auto now=status(*service,fixture.hash);return now&&now->complete&&now->received>0&&!now->failure;};QTRY_VERIFY_WITH_TIMEOUT(complete(),20000);
        for(const auto& file:fixture.files){QCOMPARE(test::read(fixture.payload+'/'+file.relative),file.expected);}QVERIFY(test::versionedPadPathsAbsent(fixture));
      }
      const auto closed=service->close(std::chrono::steady_clock::now()+10s,{},engine::PersistentSession::CloseInventoryMode::BatchPublic);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);
      const auto saved=parse(test::read(statePath));const auto map=std::string(saved.find("resume")->find("data")->find("resume data")->bytes);QCOMPARE(map,run?std::string(bitmap.size(),'\1'):bitmap);QCOMPARE(encoded(*saved.find("info")),encoded(*source.find("info")));QCOMPARE(saved.find("opaque.fixture")->bytes,source.find("opaque.fixture")->bytes);
      const auto exported=qEnvironmentVariable("BIGLYBTC_TEST_HYBRID_RESUME_EXPORT");
      if(!exported.isEmpty()){QVERIFY(QDir().mkpath(exported));test::write(exported+QString("/tail-%1-%2.%3.dat").arg(shortTail?1:0).arg(valid).arg(run?"complete":"partial"),test::read(statePath));}
    }
  }
  void standaloneReadOnlyRecheckUsesActualPieceHashes_data(){
    QTest::addColumn<QString>("mode");for(const auto* mode:{"intact","corrupt","partial","missing","compact","linked","hybrid"}){QTest::newRow(mode)<<QString::fromLatin1(mode);}
  }
  void standaloneReadOnlyRecheckUsesActualPieceHashes(){
    QFETCH(QString,mode);test::recheck::Fixture fixture(mode);auto service=owner();
    const auto beforeInventory=parse(test::recheck::read(fixture.profile+"/downloads.config"));
    const auto control=fixture.profile+"/active/"+QString::fromStdString(fixture.hash.hex())+"/fmfile0.dat";const auto controlBytes=mode=="compact"?test::recheck::read(control):QByteArray{};struct stat controlBefore{};if(mode=="compact"){QVERIFY(::stat(control.toStdString().c_str(),&controlBefore)==0);}
    struct stat before{};const bool present=::stat(fixture.file.toStdString().c_str(),&before)==0;
    const auto started=service->startManagedRecheck(*fixture.lease,fixture.hash);const auto failure=started?QString{}:QString::fromStdString(started.error().message);QVERIFY2(started,qPrintable(failure));
    const auto terminal=[&]{static_cast<void>(service->poll());const auto actual=status(*service,fixture.hash);return actual&&!actual->retained;};QTRY_VERIFY_WITH_TIMEOUT(terminal(),30000);
    const auto actual=status(*service,fixture.hash);QVERIFY(actual&&actual->recheck);const auto message=actual->failure?QString::fromStdString(actual->failure->message):QString{};
    QVERIFY2(!actual->failure,qPrintable(message));QCOMPARE(actual->recheck->phase,engine::ManagedRecheckPhase::Finished);QVERIFY(actual->recheck->pieceMapSaved&&!actual->recheck->saveUncertain);QCOMPARE(actual->received,std::int64_t{0});QCOMPARE(actual->sent,std::int64_t{0});
    const auto missing=mode=="missing"?3:mode=="partial"?2:mode=="compact"?2:0;const auto bad=mode=="corrupt"?1:0;
    QCOMPARE(actual->recheck->missingPieces,missing);QCOMPARE(actual->recheck->badPieces,bad);QCOMPARE(actual->recheck->goodPieces,3-missing-bad);QCOMPARE(actual->recheck->unknownPieces,0);
    QVERIFY(actual->recheck->scannedBytes>=0&&actual->recheck->scannedBytes<=actual->recheck->totalBytes);
    if(present){struct stat after{};QVERIFY(::stat(fixture.file.toStdString().c_str(),&after)==0);QCOMPARE(after.st_dev,before.st_dev);QCOMPARE(after.st_ino,before.st_ino);QCOMPARE(after.st_size,before.st_size);QCOMPARE(after.st_mtim.tv_sec,before.st_mtim.tv_sec);QCOMPARE(after.st_mtim.tv_nsec,before.st_mtim.tv_nsec);QCOMPARE(test::recheck::read(fixture.file),fixture.originalPayload);}else{QVERIFY(!QFileInfo::exists(fixture.file));}
    if(mode=="compact"){struct stat after{};QVERIFY(::stat(control.toStdString().c_str(),&after)==0);QCOMPARE(after.st_dev,controlBefore.st_dev);QCOMPARE(after.st_ino,controlBefore.st_ino);QCOMPARE(after.st_size,controlBefore.st_size);QCOMPARE(after.st_mtim.tv_sec,controlBefore.st_mtim.tv_sec);QCOMPARE(after.st_mtim.tv_nsec,controlBefore.st_mtim.tv_nsec);QCOMPARE(test::recheck::read(control),controlBytes);}
    const auto saved=parse(test::recheck::read(fixture.profile+"/active/"+QString::fromStdString(fixture.hash.hex())+".dat"));const auto* resume=saved.find("resume")->find("data");QCOMPARE(resume->find("valid")->integer,std::int64_t{1});const auto bits=resume->find("resume data")->bytes;QCOMPARE(bits.size(),std::size_t{3});
    for(std::size_t i=0;i<bits.size();++i){const bool good=mode=="missing"?false:mode=="partial"?i==0:mode=="compact"?i==2:mode=="corrupt"?i!=1:true;QCOMPARE(bits[i],good?'\1':'\0');}
    const auto inventory=parse(test::recheck::read(fixture.profile+"/downloads.config"));const auto& row=firstEntry(inventory);QCOMPARE(integer(row,"state"),std::int64_t{70});QCOMPARE(integer(row,"forceStart"),std::int64_t{0});QCOMPARE(integer(row,"downloaded"),std::int64_t{9});QCOMPARE(integer(row,"uploaded"),std::int64_t{11});QCOMPARE(encoded(*inventory.find("opaque")),encoded(*beforeInventory.find("opaque")));QCOMPARE(saved.find("opaque")->bytes,std::string_view("root-unknown"));
    QVERIFY(service->forgetManaged(fixture.hash));const auto closed=service->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased);fixture.lease.reset();QVERIFY(storage::ProfileLease::acquire(fixture.profile.toStdString()));
  }
  void standaloneRecheckCancellationAfterActualAdmissionJoinsWithoutMapPublication_data(){
    QTest::addColumn<bool>("ordinaryStop");QTest::newRow("receiver-and-explicit-cancel")<<false;QTest::newRow("ordinary-Stop")<<true;
  }
  void standaloneRecheckCancellationAfterActualAdmissionJoinsWithoutMapPublication(){
    QFETCH(bool,ordinaryStop);test::recheck::Fixture fixture("corrupt");auto service=owner();std::stop_source stop;QVERIFY(service->startManagedRecheck(*fixture.lease,fixture.hash,stop.get_token()));
    const auto pause=service->pauseManaged(fixture.hash,true),queue=service->queueManaged(fixture.hash,true,false);QVERIFY(!pause&&!queue);QCOMPARE(pause.error().code,storage::ErrorCode::Conflict);QCOMPARE(queue.error().code,storage::ErrorCode::Conflict);
    if(ordinaryStop){QVERIFY(service->stopManaged(fixture.hash));}else{stop.request_stop();QVERIFY(service->cancelManagedRecheck(fixture.hash));}
    const auto terminal=[&]{static_cast<void>(service->poll());const auto actual=status(*service,fixture.hash);return actual&&!actual->retained;};QTRY_VERIFY_WITH_TIMEOUT(terminal(),15000);const auto actual=status(*service,fixture.hash);QVERIFY(actual&&actual->recheck);QCOMPARE(actual->recheck->phase,engine::ManagedRecheckPhase::Cancelled);QVERIFY(!actual->recheck->pieceMapSaved&&!actual->recheck->saveUncertain);QCOMPARE(test::recheck::read(fixture.file),fixture.originalPayload);
    const auto saved=parse(test::recheck::read(fixture.profile+"/active/"+QString::fromStdString(fixture.hash.hex())+".dat"));QCOMPARE(saved.find("resume")->find("data")->find("valid")->integer,std::int64_t{0});QVERIFY(service->forgetManaged(fixture.hash));const auto closed=service->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased);fixture.lease.reset();QVERIFY(storage::ProfileLease::acquire(fixture.profile.toStdString()));
  }
  void clearManagedForceStartPreservesAnActualManualPause(){
    test::recheck::Fixture fixture("intact");auto service=owner();QVERIFY(service->startManaged(*fixture.lease,{.hash=fixture.hash,.forceStart=true,.manuallyPaused=true}));
    const auto paused=[&]{static_cast<void>(service->poll());const auto actual=status(*service,fixture.hash);return actual&&actual->checked&&actual->phase==engine::ManagedPhase::Paused;};QTRY_VERIFY_WITH_TIMEOUT(paused(),15000);QVERIFY(service->clearManagedForceStart(fixture.hash));const auto actual=status(*service,fixture.hash);QVERIFY(actual&&!actual->forceStart&&actual->manuallyPaused&&actual->phase==engine::ManagedPhase::Paused);QCOMPARE(actual->received,std::int64_t{0});QCOMPARE(actual->sent,std::int64_t{0});QVERIFY(service->stopManaged(fixture.hash));const auto retired=[&]{static_cast<void>(service->poll());const auto current=status(*service,fixture.hash);return current&&!current->retained;};QTRY_VERIFY_WITH_TIMEOUT(retired(),15000);QVERIFY(service->forgetManaged(fixture.hash));QVERIFY(service->close(std::chrono::steady_clock::now()+5s).sessionJoined);
  }
  void actualCategoryAssignmentRetainsOneOwnerThroughCheckpointAndJoinedClose_data(){
    QTest::addColumn<bool>("mixed");QTest::newRow("Public")<<false;QTest::newRow("Mixed-seal")<<true;
  }
  void actualCategoryAssignmentRetainsOneOwnerThroughCheckpointAndJoinedClose(){
    QFETCH(bool,mixed);if(mixed&&!engine::PersistentSession::mixedNetworksAvailable()){QSKIP("Qualified mixed image is unavailable");}
    test::OwnedEngineFixture fixture(false);
    if(mixed){auto current=parse(test::read(fixture.stateFile));auto attrs=core::Document::parse(encoded(*current.find("attributes")));
      QVERIFY(attrs&&attrs->replaceValue("networks","l6:Public3:I2Pe")&&current.replaceValue("attributes",attrs->serialize()));
      test::write(fixture.stateFile,QByteArray::fromStdString(current.serialize()));
    }
    const auto originalPayload=test::read(fixture.payload+"/sample.bin");
    {
      auto lease=held(fixture);auto service=owner();
      if(mixed){QVERIFY(service->configureNetwork(engine::NetworkPolicy{}));QVERIFY(service->configureMixedNetworks());}
      const auto admitted=service->startManaged(lease,{.hash=*fixture.hash,.manuallyPaused=true,.mixedNetworks=mixed});QVERIFY2(admitted,admitted?"":admitted.error().message.c_str());
      QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value()&&status(*service,*fixture.hash)&&status(*service,*fixture.hash)->checked,10000);
      const auto before=status(*service,*fixture.hash);QVERIFY(before&&before->retained&&before->admission);
      std::shared_ptr<const engine::MixedManagedSource> oldSeal;std::string oldSealBytes;
      if(mixed){auto seal=service->mixedSource(*fixture.hash);QVERIFY(seal);oldSeal=*seal;oldSealBytes=oldSeal->stateSnapshot();}
      auto categories=storage::CategoryStore::open(lease);QVERIFY(categories);auto defs=(*categories)->snapshot();QVERIFY(defs);
      auto created=(*categories)->create(*defs,"Owned category");QVERIFY(created);
      auto selected=(*categories)->readTarget(created->snapshot,*fixture.hash);QVERIFY(selected);
      auto assigned=(*categories)->assign(created->snapshot,*selected,std::string("Owned category"));QVERIFY(assigned&&assigned->changed);
      {
        auto fsOnly=lease.sourceAccess();QVERIFY(fsOnly);
        const auto refreshed=service->rebaseCategory(lease,*fixture.hash);QVERIFY(refreshed&&*refreshed);
      }
      QCOMPARE(status(*service,*fixture.hash)->admission,before->admission);QVERIFY(!status(*service,*fixture.hash)->failure);
      if(mixed){auto seal=service->mixedSource(*fixture.hash);QVERIFY(seal&&seal->get()!=oldSeal.get());
        QCOMPARE(std::string(oldSeal->stateSnapshot()),oldSealBytes);QCOMPARE((*seal)->admission(),oldSeal->admission());
        QVERIFY((*seal)->identity().full==oldSeal->identity().full&&(*seal)->lease().sharesOwnership(oldSeal->lease()));
        QCOMPARE((*seal)->claims().size(),oldSeal->claims().size());QVERIFY((*seal)->current()&&oldSeal->current());
        const auto physical=parse(test::read(fixture.stateFile));QCOMPARE(std::string((*seal)->stateSnapshot()),physical.serialize());
      }
      QVERIFY(service->stopManaged(*fixture.hash));
      QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value()&&phase(*service,*fixture.hash,Phase::Stopped),10000);
      QVERIFY(!status(*service,*fixture.hash)->failure);if(oldSeal){QVERIFY(!oldSeal->current());}
      const auto actual=parse(test::read(fixture.stateFile));QVERIFY(actual.find("attributes")->find("category"));
      QCOMPARE(actual.find("attributes")->find("category")->bytes,std::string_view("Owned category"));
      QVERIFY(actual.find("resume")&&actual.find("resume")->find("data"));
      const auto closed=service->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);
      QVERIFY((*categories)->close());
    }
    QCOMPARE(test::read(fixture.payload+"/sample.bin"),originalPayload);
    auto released=storage::ProfileLease::acquire(fixture.native());QVERIFY(released);
  }

  void allocateOnlyUsesExistingPollWithoutNetworkOrVerifiedProgress_data(){QTest::addColumn<bool>("multi");QTest::addColumn<bool>("paused");QTest::newRow("single-stop")<<false<<false;QTest::newRow("single-pause")<<false<<true;QTest::newRow("multi-stop")<<true<<false;QTest::newRow("multi-pause")<<true<<true;}
  void allocateOnlyUsesExistingPollWithoutNetworkOrVerifiedProgress(){
    QFETCH(bool,multi);QFETCH(bool,paused);test::allocation::Fixture f(multi);const auto mode=paused?core::TorrentAllocationMode::Paused:core::TorrentAllocationMode::Stopped;const auto imported=f.import(mode);QVERIFY2(imported.has_value(),imported?"":imported.error().message.c_str());
    auto service=owner();QVERIFY(!service->tcpListenPort());QVERIFY(!service->startManaged(*f.lease,{.hash=f.hash}));QVERIFY(service->startManaged(*f.lease,{.hash=f.hash,.allocationMode=mode}));
    const auto first=status(*service,f.hash);QVERIFY(first&&first->retained&&first->phase==Phase::Allocating);QVERIFY(!first->checked&&!first->complete&&first->pieces==0);QCOMPARE(first->received,std::int64_t{0});QCOMPARE(first->sent,std::int64_t{0});
    QVERIFY(!service->queueManaged(f.hash,true,false));QVERIFY(!service->pauseManaged(f.hash,false));QVERIFY(!service->tcpListenPort());
    QVERIFY(service->poll());const auto step=status(*service,f.hash);QVERIFY(step&&step->retained&&step->allocationBytes>0&&step->allocationBytes<step->allocationTotalBytes);QVERIFY(!service->tcpListenPort());
    QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value()&&phase(*service,f.hash,Phase::Stopped),10000);const auto done=status(*service,f.hash);QVERIFY(done&&!done->failure&&!done->retained&&done->allocationComplete);QCOMPARE(done->allocationBytes,f.bytes());QVERIFY(!done->checked&&!done->complete&&!done->selectedComplete&&done->pieces==0);QCOMPARE(done->received,std::int64_t{0});QCOMPARE(done->sent,std::int64_t{0});QVERIFY(!service->tcpListenPort());QVERIFY(f.zeroes());
    const auto saved=f.saved();QCOMPARE(f.row(saved).find("state")->integer,std::int64_t{70});QCOMPARE(f.row(saved).find("allocated")->integer,std::int64_t{1});const auto* pauses=saved.find("pause_data");QCOMPARE(bool(pauses&&!pauses->children.empty()),paused);
    const auto closed=service->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);QVERIFY(f.zeroes());
  }
  void allocationStopCloseAndReopenKeepSafeStoppedIntent(){
    test::allocation::Fixture f(true);QVERIFY(f.import(core::TorrentAllocationMode::Paused));auto service=owner();QVERIFY(service->startManaged(*f.lease,{.hash=f.hash,.allocationMode=core::TorrentAllocationMode::Paused}));QVERIFY(service->poll());QVERIFY(status(*service,f.hash)->allocationBytes>0);QVERIFY(service->stopManaged(f.hash));QVERIFY(service->poll());
    const auto stopped=status(*service,f.hash);QVERIFY(stopped&&!stopped->retained&&!stopped->allocationComplete&&!stopped->failure);QCOMPARE(stopped->phase,Phase::Stopped);QVERIFY(!std::filesystem::exists(f.path(1))); // untouched new second leaf is removed; completed first leaf may remain.
    const auto cancelled=storage::readAllocationIntent(*f.lease,f.hash);QVERIFY(cancelled&&*cancelled);QCOMPARE((*cancelled)->state,storage::AllocationState::Cancelled);QVERIFY(!f.saved().find("pause_data"));
    const auto closed=service->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased);service.reset();
    auto reopened=owner();QVERIFY(!reopened->startManaged(*f.lease,{.hash=f.hash}));QVERIFY(reopened->startManaged(*f.lease,{.hash=f.hash,.allocationMode=core::TorrentAllocationMode::Paused}));QTRY_VERIFY_WITH_TIMEOUT(reopened->poll().has_value()&&phase(*reopened,f.hash,Phase::Stopped),10000);QVERIFY(status(*reopened,f.hash)->allocationComplete);QVERIFY(f.zeroes());QVERIFY(!reopened->tcpListenPort());
    QVERIFY(reopened->close(std::chrono::steady_clock::now()+5s).retirement.storageReleased);
    test::allocation::Fixture closing;QVERIFY(closing.import(core::TorrentAllocationMode::Stopped));auto closeOwner=owner();QVERIFY(closeOwner->startManaged(*closing.lease,{.hash=closing.hash,.allocationMode=core::TorrentAllocationMode::Stopped}));const auto receipt=closeOwner->close(std::chrono::steady_clock::now()+5s);QVERIFY(receipt.sessionJoined&&receipt.retirement.storageReleased);QVERIFY(!std::filesystem::exists(closing.path()));const auto intent=storage::readAllocationIntent(*closing.lease,closing.hash);QVERIFY(intent&&*intent&&(*intent)->state==storage::AllocationState::Cancelled);
  }


  void allocationOwnerRetainsNonzeroReplayDataOnFailureAndEarlyCancel_data(){QTest::addColumn<bool>("cancel");QTest::newRow("read-failure")<<false;QTest::newRow("cancel-before-read")<<true;}
  void allocationOwnerRetainsNonzeroReplayDataOnFailureAndEarlyCancel(){
    QFETCH(bool,cancel);test::allocation::Fixture f;QVERIFY(f.import(core::TorrentAllocationMode::Stopped));
    {auto prepared=engine::detail::ManagedStorage::prepare(*f.lease,f.hash,{}, {}, {}, {},engine::detail::ManagedStoragePurpose::AllocateOnly);QVERIFY(prepared);QVERIFY(prepared->advanceAllocation(4096));}
    {std::fstream file(f.path(),std::ios::binary|std::ios::in|std::ios::out);file.put('X');file.close();QVERIFY(!file.fail());}
    struct stat before{},after{};QVERIFY(::stat(f.path().c_str(),&before)==0);const auto data=test::allocation::read(f.path());auto service=owner();QVERIFY(service->startManaged(*f.lease,{.hash=f.hash,.allocationMode=core::TorrentAllocationMode::Stopped}));if(cancel){QVERIFY(service->stopManaged(f.hash));}QVERIFY(service->poll());const auto failed=status(*service,f.hash);QVERIFY(failed&&failed->failure&&!failed->allocationComplete&&!failed->retained);QVERIFY(::stat(f.path().c_str(),&after)==0);QCOMPARE(before.st_ino,after.st_ino);QCOMPARE(test::allocation::read(f.path()),data);QVERIFY(!service->tcpListenPort());const auto saved=f.saved();QCOMPARE(f.row(saved).find("state")->integer,std::int64_t{70});QVERIFY(f.row(saved).find(storage::AllocationStateKey)->integer!=2);
  }
  void actualSelectedReadOnlyOwnerKeepsMissingPiecesUnverifiedAndServesOnlyRealNeededData_data(){
    QTest::addColumn<QString>("kind");QTest::addColumn<bool>("corrupt");for(const auto* kind:{"linear-missing","compact-edges","reordered-edges","reordered-compact-edges","v2-missing"}){QTest::newRow(kind)<<QString::fromLatin1(kind)<<false;}QTest::newRow("corrupt-needed-piece")<<QStringLiteral("compact-edges")<<true;
  }
  void actualSelectedReadOnlyOwnerKeepsMissingPiecesUnverifiedAndServesOnlyRealNeededData(){
    QFETCH(QString,kind);QFETCH(bool,corrupt);QTemporaryDir area;QVERIFY(area.isValid());const auto fixture=test::prepareSelectedSeedCase(area.path()+"/owned",kind,corrupt);auto lease=storage::ProfileLease::acquire(fixture.value.profile.toStdString());QVERIFY(lease);
    const auto imported=test::importSelectedSeedCase(*lease,fixture);QVERIFY2(imported,imported?"":imported.error().message.c_str());auto service=owner();const auto admitted=service->startManaged(*lease,{.hash=fixture.value.hash,.verifyExisting=true});QVERIFY2(admitted,admitted?"":admitted.error().message.c_str());QVERIFY(test::spinVerified(*service,fixture.value.hash));const auto actual=status(*service,fixture.value.hash);QVERIFY(actual);QCOMPARE(actual->received,std::int64_t{0});QVERIFY(!actual->complete);
    const auto bits=service->verifiedSeedPieces(fixture.value.hash);
    if(corrupt){QCOMPARE(actual->verifiedSeeding,engine::VerifiedSeedingPhase::Failed);QVERIFY(actual->failure);QVERIFY((!core::satisfiesExistingPieceGoal(std::array<std::uint8_t,4>{1,0,0,1},bits)));}
    else{QVERIFY2(!actual->failure,actual->failure?actual->failure->message.c_str():"");QCOMPARE(actual->verifiedSeeding,engine::VerifiedSeedingPhase::Released);QVERIFY(actual->checked&&actual->selectedComplete);QCOMPARE(bits,std::vector<std::uint8_t>({1,0,0,1}));QVERIFY(!test::receiveSelectedSeed(*service,fixture).isEmpty());QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value()&&status(*service,fixture.value.hash)->sent>0,5000);}
    const auto expectedFailure=actual->failure;
    const auto closed=service->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased);
    if(corrupt){QVERIFY(expectedFailure&&closed.checkpointFailure);QCOMPARE(closed.checkpointFailure->code,expectedFailure->code);QCOMPARE(closed.checkpointFailure->message,expectedFailure->message);}
    else{QVERIFY(!closed.checkpointFailure);}
    QVERIFY(test::selectedSnapshotsIntact(fixture));const auto intent=storage::readVerifiedSeedIntent(*lease,fixture.value.hash);QVERIFY(intent&&*intent);QCOMPARE((*intent)->goal,core::ExistingVerificationGoal::SelectedFiles);QCOMPARE((*intent)->state,corrupt?storage::VerifiedSeedState::Pending:storage::VerifiedSeedState::Released);QCOMPARE((*intent)->startup,corrupt?core::StartupState::Stopped:core::StartupState::Queued);
    const auto state=parse(test::read(fixture.value.profile+"/active/"+QString::fromStdString(fixture.value.hash.hex())+".dat"));const auto source=parse(test::read(fixture.value.metadata));QCOMPARE(encoded(*state.find("info")),encoded(*source.find("info")));QCOMPARE(state.find("opaque.fixture")->bytes,std::string_view("\0\xffQ",3));QCOMPARE(state.find("attributes")->find("opaque.fixture")->bytes,std::string_view("\0\xffQ",3));QCOMPARE(test::read(fixture.value.profile+"/torrents/"+QString::fromStdString(fixture.value.hash.hex())+".torrent"),test::read(fixture.value.metadata));
  }

  void selectedScopeRejectsReplacedControlAndNeededMissingBoundaryBeforeRelease(){
    for(const bool replaceControl:{false,true}){QTemporaryDir area;QVERIFY(area.isValid());const auto fixture=test::prepareSelectedSeedCase(area.path()+"/owned",QStringLiteral("compact-edges"));auto lease=storage::ProfileLease::acquire(fixture.value.profile.toStdString());QVERIFY(lease);QVERIFY(test::importSelectedSeedCase(*lease,fixture));const auto path=replaceControl?fixture.controls.front().first:fixture.value.payload+'/'+fixture.value.files[1].relative;const auto raw=test::read(path);QVERIFY(QFile::rename(path,path+".retained"));if(replaceControl){test::write(path,raw);}
      const auto before=test::read(fixture.value.profile+"/downloads.config");auto service=owner();QVERIFY(!service->startManaged(*lease,{.hash=fixture.value.hash,.verifyExisting=true}));QVERIFY(service->managedDownloads().empty());QCOMPARE(test::read(fixture.value.profile+"/downloads.config"),before);const auto closed=service->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased);QCOMPARE(test::read(path+".retained"),raw);if(!replaceControl){QVERIFY(!QFileInfo::exists(path));}}
  }

  void actualLinkedExistingOwnerChecksAndServesOnlyApprovedIndices_data(){
    QTest::addColumn<QString>("kind");QTest::addColumn<bool>("corrupt");for(const auto* kind:{"single","multi","one-multi","v2-tail"}){QTest::newRow(kind)<<QString::fromLatin1(kind)<<false;}QTest::newRow("linked-corrupt")<<QStringLiteral("multi")<<true;
  }
  void actualLinkedExistingOwnerChecksAndServesOnlyApprovedIndices(){
    QFETCH(QString,kind);QFETCH(bool,corrupt);QTemporaryDir directory;QVERIFY(directory.isValid());auto fixture=test::prepareLinkedVerifiedSeedCase(directory.path()+"/owned",kind,corrupt);
    auto lease=storage::ProfileLease::acquire(fixture.profile.toStdString());QVERIFY(lease);const auto imported=test::importLinkedVerifiedSeedCase(*lease,fixture,kind);QVERIFY2(imported,imported?"":imported.error().message.c_str());QCOMPARE(*imported,fixture.hash);
    const auto saved=test::customFixtureRow(fixture.profile);const auto* scope=saved.find("downloads")->children[0].find(storage::VerifiedExistingScopeKey);QVERIFY(scope);const auto proof=storage::VerifiedExistingScope::decode(*scope);QVERIFY(proof&&proof->mapped());
    auto service=owner();QVERIFY(service->startManaged(*lease,{.hash=fixture.hash,.verifyExisting=true}));QVERIFY(test::spinVerified(*service,fixture.hash));const auto checked=status(*service,fixture.hash);QVERIFY(checked);QCOMPARE(checked->received,std::int64_t{0});
    if(corrupt){QCOMPARE(checked->verifiedSeeding,engine::VerifiedSeedingPhase::Failed);QVERIFY(checked->failure);QCOMPARE(checked->sent,std::int64_t{0});QVERIFY(!checked->complete&&!checked->verificationHeld);}
    else{
      QCOMPARE(checked->verifiedSeeding,engine::VerifiedSeedingPhase::Released);QVERIFY(!checked->failure);QVERIFY(!test::receiveCustomVerifiedSeed(*service,fixture).isEmpty());
      // Receiver completion can occur after the helper's last owner poll.
      // Publish an actual accepted send sample, as the custom/versioned rows do.
      const auto sentDiagnostic=[&]{const auto value=status(*service,fixture.hash);return value?std::string("sent=")+std::to_string(value->sent)+" phase="+std::to_string(static_cast<int>(value->phase))+(value->failure?" failure="+value->failure->message:""):std::string("Managed linked status absent");};
      QTRY_VERIFY2_WITH_TIMEOUT(([&]{const auto poll=service->poll();const auto value=status(*service,fixture.hash);return poll&&value&&!value->failure&&value->sent>0;})(),sentDiagnostic().c_str(),5000);
      const auto sent=status(*service,fixture.hash);QVERIFY(sent&&sent->sent>0);
    }
    const auto closed=service->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased);
    // Close reports the already-observed failed verification as well as a new
    // checkpoint failure. Require exactly that original error for corruption.
    if(corrupt){QVERIFY(closed.checkpointFailure);QCOMPARE(closed.checkpointFailure->code,checked->failure->code);QCOMPARE(closed.checkpointFailure->message,checked->failure->message);}
    else{QVERIFY(!closed.checkpointFailure);}
    QVERIFY(test::versionedRealSnapshotsIntact(fixture,corrupt));
    const auto state=parse(test::read(fixture.profile+"/active/"+QString::fromStdString(fixture.hash.hex())+".dat"));const auto source=parse(test::read(fixture.metadata));QCOMPARE(encoded(*state.find("info")),encoded(*source.find("info")));QCOMPARE(test::read(fixture.profile+"/torrents/"+QString::fromStdString(fixture.hash.hex())+".torrent"),test::read(fixture.metadata));
    if(kind=="v2-tail"){QVERIFY(test::versionedPadPathsAbsent(fixture));}
  }

  void actualConvertedFilenameOwnerRetainsRawIdentityAndUploads_data(){QTest::addColumn<bool>("configured");QTest::newRow("original-default-quote")<<false;QTest::newRow("configured-quote")<<true;}
  void actualConvertedFilenameOwnerRetainsRawIdentityAndUploads(){
    QFETCH(bool,configured);QTemporaryDir directory;QVERIFY(directory.isValid());auto fixture=test::prepareVerifiedSeedCase(directory.path()+"/owned",true);auto source=parse(test::read(fixture.metadata));auto info=core::Document::parse(encoded(*source.find("info")));QVERIFY(info);
    auto first=core::Document::parse(encoded(info->find("files")->children[0]));QVERIFY(first);QVERIFY(first->replaceValue("path","l15:first\"quote.bine"));std::string files="l"+first->serialize();for(std::size_t n=1;n<info->find("files")->children.size();++n){files+=encoded(info->find("files")->children[n]);}QVERIFY(info->replaceValue("files",files+'e'));QVERIFY(source.replaceValue("info",info->serialize()));test::write(fixture.metadata,QByteArray::fromStdString(source.serialize()));
    const QString physical=configured?QStringLiteral("bundle/firstQquote.bin"):QStringLiteral("bundle/first'quote.bin");QVERIFY(QFile::rename(fixture.payload+'/'+fixture.files[0].relative,fixture.payload+'/'+physical));fixture.files[0].relative=physical;QVERIFY(::stat((fixture.payload+'/'+physical).toStdString().c_str(),&fixture.files[0].before)==0);
    if(configured){test::write(fixture.profile+"/biglybt.config","d26:File.Character.Conversions3:\"=Qe");}
    auto lease=storage::ProfileLease::acquire(fixture.profile.toStdString());QVERIFY(lease);const auto conversions=storage::readOriginalFilenameConversions(*lease);QVERIFY(conversions);const auto preview=storage::inspectTorrentIntake(fixture.metadata.toStdString(),{},*conversions);QVERIFY(preview);fixture.hash=preview->hash();const auto policies=test::versionedFilePolicies(*preview);
    QVERIFY(storage::importTorrentWithFileOptions(*lease,*preview,fixture.payload.toStdString(),true,policies,nullptr,false,{}, {},false,false,false,{}, {},true));auto service=owner();QVERIFY(service->startManaged(*lease,{.hash=fixture.hash,.verifyExisting=true}));QVERIFY(test::spinVerified(*service,fixture.hash));const auto actual=status(*service,fixture.hash);QVERIFY(actual&&actual->verifiedSeeding==engine::VerifiedSeedingPhase::Released&&!actual->failure);QCOMPARE(actual->received,std::int64_t{0});
    QVERIFY(!test::receiveCustomVerifiedSeed(*service,fixture).isEmpty());const auto closed=service->close(std::chrono::steady_clock::now()+5s);QVERIFY(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure);QVERIFY(test::versionedRealSnapshotsIntact(fixture));QVERIFY(!QFileInfo::exists(fixture.payload+"/bundle/first\"quote.bin"));const auto state=parse(test::read(fixture.profile+"/active/"+QString::fromStdString(fixture.hash.hex())+".dat"));QCOMPARE(encoded(*state.find("info")),encoded(*source.find("info")));QCOMPARE(test::read(fixture.profile+"/torrents/"+QString::fromStdString(fixture.hash.hex())+".torrent"),test::read(fixture.metadata));
  }

  void customExistingOwnerChecksAndServesOnlyApprovedRoot_data(){
    QTest::addColumn<QString>("kind");for(const auto* kind:{"single","multi","one-multi","v2-tail"}){QTest::newRow(kind)<<QString::fromLatin1(kind);}
  }
  void customExistingOwnerChecksAndServesOnlyApprovedRoot(){
    QFETCH(QString,kind);QTemporaryDir directory;QVERIFY(directory.isValid());const auto fixture=test::prepareCustomVerifiedSeedCase(directory.path()+"/owned",kind);
    auto lease=storage::ProfileLease::acquire(fixture.profile.toStdString());QVERIFY(lease);const auto imported=test::importCustomVerifiedSeedCase(*lease,fixture,kind);QVERIFY2(imported,imported?"":imported.error().message.c_str());QCOMPARE(*imported,fixture.hash);
    const auto archive=fixture.profile+"/torrents/"+QString::fromStdString(fixture.hash.hex())+".torrent";QCOMPARE(test::read(archive),test::read(fixture.metadata));
    auto service=owner();const auto admitted=service->startManaged(*lease,{.hash=fixture.hash,.verifyExisting=true});QVERIFY2(admitted,admitted?"":admitted.error().message.c_str());QVERIFY(status(*service,fixture.hash)->verificationHeld);
    QVERIFY(!service->queueManaged(fixture.hash,true,false)&&!service->pauseManaged(fixture.hash,false));QVERIFY(test::spinVerified(*service,fixture.hash));const auto accepted=status(*service,fixture.hash);QVERIFY(accepted);
    QVERIFY2(accepted->verifiedSeeding==engine::VerifiedSeedingPhase::Released&&!accepted->failure,accepted->failure?accepted->failure->message.c_str():"Actual custom verification did not release");
    const auto bits=service->verifiedSeedPieces(fixture.hash);QCOMPARE(bits.size(),static_cast<std::size_t>(accepted->totalPieces));QVERIFY(!bits.empty()&&std::ranges::all_of(bits,[](auto bit){return bit==1;}));
    QVERIFY(accepted->complete&&accepted->selectedComplete&&accepted->checked);QCOMPARE(accepted->received,std::int64_t{0});QVERIFY(test::versionedRealSnapshotsIntact(fixture));
    QVERIFY(!test::receiveCustomVerifiedSeed(*service,fixture).isEmpty());QTRY_VERIFY_WITH_TIMEOUT(service->poll().has_value()&&status(*service,fixture.hash)->sent>0,5000);
    QCOMPARE(status(*service,fixture.hash)->received,std::int64_t{0});const auto close=service->close(std::chrono::steady_clock::now()+5s);QVERIFY(close.sessionJoined&&close.retirement.storageReleased&&!close.checkpointFailure);QVERIFY(test::versionedRealSnapshotsIntact(fixture));
    const auto saved=test::customFixtureRow(fixture.profile);QVERIFY(firstEntry(saved).find(storage::VerifiedExistingScopeKey));const auto released=storage::readVerifiedSeedIntent(*lease,fixture.hash);QVERIFY(released&&*released);QCOMPARE((*released)->state,storage::VerifiedSeedState::Released);QCOMPARE((*released)->startup,core::StartupState::Queued);QVERIFY(!(*released)->forceStart);
    const auto source=parse(test::read(fixture.metadata));const auto state=parse(test::read(fixture.profile+"/active/"+QString::fromStdString(fixture.hash.hex())+".dat"));QCOMPARE(encoded(*state.find("info")),encoded(*source.find("info")));QCOMPARE(test::read(archive),test::read(fixture.metadata));
    if(kind!="v2-tail"){QCOMPARE(state.find("attributes")->find("displayname")->bytes,kind=="one-multi"?std::string("retained-title"):test::customVerifiedLeaf(kind).toStdString());}
    const auto layout=compat::originalLayout(source);QVERIFY(layout);QVERIFY(!QFileInfo::exists(fixture.payload+'/'+QString::fromStdString(layout->name)));
    if(kind=="v2-tail"){QVERIFY(test::versionedPadPathsAbsent(fixture));}
    // Retained Released intent still requires its exact approved scope; a name
    // alone is never permission to re-enter the read-only checking lifecycle.
    test::customRemoveRowKey(fixture.profile,storage::VerifiedExistingScopeKey);const auto before=test::read(fixture.profile+"/downloads.config");auto denied=owner();QVERIFY(!denied->startManaged(*lease,{.hash=fixture.hash}));QVERIFY(denied->managedDownloads().empty());QCOMPARE(test::read(fixture.profile+"/downloads.config"),before);const auto final=denied->close(std::chrono::steady_clock::now()+5s);QVERIFY(final.sessionJoined&&final.retirement.storageReleased);
  }
  void customCorruptionNeverRepairsAndAbsentOriginalMarkerDoesNotAdoptScope(){
    for(const bool legacy:{false,true}){
      QTemporaryDir directory;QVERIFY(directory.isValid());const auto fixture=test::prepareCustomVerifiedSeedCase(directory.path()+"/owned",QStringLiteral("single"),!legacy);
      auto lease=storage::ProfileLease::acquire(fixture.profile.toStdString());QVERIFY(lease);QVERIFY(test::importCustomVerifiedSeedCase(*lease,fixture,QStringLiteral("single")));auto service=owner();
      if(legacy){
        test::customRemoveRowKey(fixture.profile,storage::VerifiedSeedKey,true);const auto absent=storage::readVerifiedSeedIntent(*lease,fixture.hash);QVERIFY(absent&&!*absent);
        // Java's saved-row rewrite drops native-only intent. A stale malformed
        // scope is ignored by ordinary admission, never upgraded to RO intent.
        QVERIFY(service->startManaged(*lease,{.hash=fixture.hash}));QVERIFY(!status(*service,fixture.hash)->verificationHeld);QCOMPARE(status(*service,fixture.hash)->verifiedSeeding,engine::VerifiedSeedingPhase::None);QVERIFY(service->stopManaged(fixture.hash));
      }else{
        QVERIFY(service->startManaged(*lease,{.hash=fixture.hash,.verifyExisting=true}));QVERIFY(test::spinVerified(*service,fixture.hash));const auto failed=status(*service,fixture.hash);QVERIFY(failed&&failed->verifiedSeeding!=engine::VerifiedSeedingPhase::Released);QCOMPARE(failed->received,std::int64_t{0});QCOMPARE(failed->sent,std::int64_t{0});const auto pending=storage::readVerifiedSeedIntent(*lease,fixture.hash);QVERIFY(pending&&*pending);QCOMPARE((*pending)->state,storage::VerifiedSeedState::Pending);QCOMPARE((*pending)->startup,core::StartupState::Stopped);
      }
      const auto close=service->close(std::chrono::steady_clock::now()+5s);QVERIFY(close.sessionJoined&&close.retirement.storageReleased);QVERIFY(test::verifiedSeedBytesIntact(fixture,!legacy));
    }
  }

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
int prepareLinkedVerifiedSeedReceipt(const QString& root,const QString& kind,const QString& outcome){
  try{
    test::require(outcome=="complete"||outcome=="corrupt"||outcome=="stopped","Unknown linked verification outcome");const auto fixture=test::prepareLinkedVerifiedSeedCase(root,kind,outcome=="corrupt");
    auto lease=storage::ProfileLease::acquire(fixture.profile.toStdString());test::require(lease.has_value(),"Own actual linked profile");const auto imported=test::importLinkedVerifiedSeedCase(*lease,fixture,kind);test::require(imported.has_value(),imported?"":imported.error().message.c_str());
    auto service=owner();test::require(service->startManaged(*lease,{.hash=fixture.hash,.verifyExisting=true}).has_value(),"Actual mapped read-only admission");if(outcome=="stopped"){test::require(service->stopManaged(fixture.hash).has_value(),"Actual linked cancellation");}else{test::require(test::spinVerified(*service,fixture.hash),"Actual linked verification deadline");}
    auto actual=test::verifiedStatus(*service,fixture.hash);test::require(actual.has_value(),"Actual linked owner observation");const bool released=actual->verifiedSeeding==engine::VerifiedSeedingPhase::Released;test::require(released==(outcome=="complete"),"Unexpected linked actual outcome");const auto bits=service->verifiedSeedPieces(fixture.hash);
    QByteArray receiverHash;if(released){receiverHash=test::receiveCustomVerifiedSeed(*service,fixture);test::require(service->poll().has_value(),"Actual linked upload sample");actual=test::verifiedStatus(*service,fixture.hash);test::require(actual&&actual->sent>0,"Actual linked upload bytes missing");}
    const auto closed=service->close(std::chrono::steady_clock::now()+5s);test::require(closed.sessionJoined&&closed.retirement.storageReleased,"Actual linked joined storage fence failed");if(outcome=="corrupt"){test::require(actual&&actual->failure&&closed.checkpointFailure&&closed.checkpointFailure->code==actual->failure->code&&closed.checkpointFailure->message==actual->failure->message,"Actual linked close changed the observed corruption failure");}else{test::require(!closed.checkpointFailure,"Actual linked checkpoint unexpectedly failed");}test::require(test::versionedRealSnapshotsIntact(fixture,outcome=="corrupt"),"Read-only linked data changed");
    const auto saved=storage::readVerifiedSeedIntent(*lease,fixture.hash);test::require(saved&&saved->has_value(),"Actual linked saved intent");const auto state=parse(test::read(fixture.profile+"/active/"+QString::fromStdString(fixture.hash.hex())+".dat"));const auto flags=core::originalDownloadFlags(state.find("attributes"));test::require(flags.has_value(),"Actual original flags");
    const auto inventory=test::customFixtureRow(fixture.profile);const auto& row=firstEntry(inventory);test::require(row.find(storage::VerifiedExistingScopeKey),"Actual mapped scope missing");const auto scope=storage::VerifiedExistingScope::decode(*row.find(storage::VerifiedExistingScopeKey));test::require(scope&&scope->mapped(),"Actual mapped scope type");
    const auto source=parse(test::read(fixture.metadata));const auto layout=storage::readOriginalLayout(*lease,source);test::require(layout.has_value(),"Actual linked original layout");const auto preview=storage::inspectTorrentIntake(fixture.metadata.toStdString());test::require(preview.has_value(),"Actual linked sealed source");
    lt::error_code error;const lt::torrent_info metadata(fixture.metadata.toStdString(),error);test::require(!error,"Actual linked full metadata");const auto identity=storage::originalTorrentIdentityFromLibtorrent(source,metadata);test::require(identity.has_value(),"Actual linked selected/full identity");
    const auto full=metadata.info_hashes();const auto hex=[](const auto& hash){return QString::fromLatin1(QByteArray::fromStdString(hash.to_string()).toHex().toUpper());};QJsonArray files,verified,originalFiles,targets;std::size_t real=0;
    for(auto bit:bits){verified.push_back(static_cast<int>(bit));}
    for(std::size_t n=0;n<layout->files.size();++n){const auto& file=layout->files[n];originalFiles.push_back(QJsonObject{{"index",static_cast<int>(n)},{"offset",QString::number(file.offset)},{"bytes",QString::number(file.bytes)},{"padding",file.padding},{"path",QString::fromStdString(file.path)}});if(file.padding){continue;}test::require(real<fixture.files.size(),"Actual mapped real indices");const auto path=fixture.payload+'/'+fixture.files[real++].relative;struct stat info{};test::require(::stat(path.toStdString().c_str(),&info)==0,"Actual final linked inode");files.push_back(QJsonObject{{"index",static_cast<int>(n)},{"path",path},{"bytes",QString::number(info.st_size)},{"sha256",QString::fromLatin1(QCryptographicHash::hash(test::read(path),QCryptographicHash::Sha256).toHex())},{"dev",QString::number(info.st_dev)},{"ino",QString::number(info.st_ino)}});}
    for(const auto& value:preview->initialDestinations()){targets.push_back(QJsonObject{{"index",static_cast<int>(value.index)},{"path",QString::fromStdString(value.path)}});}
    const QJsonObject result{{"schema",4},{"status","ACTUAL_NATIVE_VERIFIED_SEED_CASE"},{"kind",kind},{"profile",fixture.profile},{"metadata",fixture.metadata},{"payloadRoot",fixture.payload},{"saveDirectory",QString::fromUtf8(row.find("save_dir")->bytes.data(),static_cast<qsizetype>(row.find("save_dir")->bytes.size()))},{"saveName",QString::fromUtf8(row.find("save_file")->bytes.data(),static_cast<qsizetype>(row.find("save_file")->bytes.size()))},{"hash",QString::fromStdString(fixture.hash.hex())},{"fullV1",full.has_v1()?hex(full.v1):QStringLiteral("-")},{"fullV2",full.has_v2()?hex(full.v2):QStringLiteral("-")},{"selectedVersion",identity->selected==storage::OriginalSelectedHash::V2?2:1},{"admission",QString::number(actual->admission)},{"released",released},{"phase",static_cast<int>(actual->verifiedSeeding)},{"forceStart",(*saved)->forceStart},{"moveFlag",bool(*flags&8)},{"marker",(*saved)->state==storage::VerifiedSeedState::Released?2:1},{"totalPieces",actual->totalPieces},{"verifiedPieces",verified},{"received",QString::number(actual->received)},{"sent",QString::number(actual->sent)},{"receiverCompleted",!receiverHash.isEmpty()},{"receiverSha256",QString::fromLatin1(receiverHash)},{"files",files},{"originalFiles",originalFiles},{"fileDestinations",targets},{"sessionJoined",closed.sessionJoined},{"storageReleased",closed.retirement.storageReleased},{"state",fixture.profile+"/active/"+QString::fromStdString(fixture.hash.hex())+".dat"},{"archive",fixture.profile+"/torrents/"+QString::fromStdString(fixture.hash.hex())+".torrent"}};
    test::write(root+"/native-receipt.json",QJsonDocument(result).toJson(QJsonDocument::Indented));return 0;
  }catch(const std::exception& error){qCritical("%s",error.what());return 1;}
}
int prepareSelectedVerifiedSeedReceipt(const QString& root,const QString& kind,const QString& outcome){
  try{
    test::require(outcome=="complete"||outcome=="corrupt"||outcome=="stopped","Unknown selected verification outcome");const auto fixture=test::prepareSelectedSeedCase(root,kind,outcome=="corrupt");auto lease=storage::ProfileLease::acquire(fixture.value.profile.toStdString());test::require(lease.has_value(),"Own selected profile lease");const auto imported=test::importSelectedSeedCase(*lease,fixture);test::require(imported.has_value(),imported?"":imported.error().message.c_str());auto service=owner();test::require(service->startManaged(*lease,{.hash=fixture.value.hash,.verifyExisting=true}).has_value(),"Actual selected RO admission");
    if(outcome=="stopped"){test::require(service->stopManaged(fixture.value.hash).has_value(),"Actual selected cancellation");}else{test::require(test::spinVerified(*service,fixture.value.hash),"Actual selected verification deadline");}
    auto actual=test::verifiedStatus(*service,fixture.value.hash);test::require(actual.has_value(),"Actual selected observation");const bool released=actual->verifiedSeeding==engine::VerifiedSeedingPhase::Released;test::require(released==(outcome=="complete")&&!actual->complete,"Actual selected/full outcome differs");const auto bits=service->verifiedSeedPieces(fixture.value.hash);QByteArray receiver;if(released){test::require(bits==std::vector<std::uint8_t>({1,0,0,1}),"Exact same accepted selected bitmap");receiver=test::receiveSelectedSeed(*service,fixture);test::require(service->poll().has_value(),"Actual selected upload observation");actual=test::verifiedStatus(*service,fixture.value.hash);test::require(actual&&actual->sent>0,"Actual selected upload bytes");}
    const auto closed=service->close(std::chrono::steady_clock::now()+5s);test::require(closed.sessionJoined&&closed.retirement.storageReleased,"Actual selected joined storage fence failed");if(outcome=="corrupt"){test::require(actual&&actual->failure&&closed.checkpointFailure&&closed.checkpointFailure->code==actual->failure->code&&closed.checkpointFailure->message==actual->failure->message,"Actual selected close changed the observed corruption failure");}else{test::require(!closed.checkpointFailure,"Actual selected checkpoint unexpectedly failed");}test::require(test::selectedSnapshotsIntact(fixture),"Selected RO payload/control mutated");const auto saved=storage::readVerifiedSeedIntent(*lease,fixture.value.hash);test::require(saved&&saved->has_value()&&(*saved)->goal==core::ExistingVerificationGoal::SelectedFiles,"Actual typed selected saved intent");
    const auto source=parse(test::read(fixture.value.metadata));const auto layout=storage::readOriginalLayout(*lease,source);test::require(layout.has_value(),"Actual selected layout");lt::error_code error;const lt::torrent_info metadata(fixture.value.metadata.toStdString(),error);test::require(!error,"Actual selected receiver metadata");const auto identity=storage::originalTorrentIdentityFromLibtorrent(source,metadata);test::require(identity.has_value(),"Actual selected/full identity");const auto full=metadata.info_hashes();const auto hex=[](const auto& hash){return QString::fromLatin1(QByteArray::fromStdString(hash.to_string()).toHex().toUpper());};QJsonArray files,controls,verified,required;for(auto bit:bits){verified.push_back(static_cast<int>(bit));}for(int bit:{1,0,0,1}){required.push_back(bit);}
    for(std::size_t n=0;n<fixture.value.files.size();++n){const auto& file=fixture.value.files[n];const auto path=fixture.value.payload+'/'+file.relative;QJsonObject value{{"index",static_cast<int>(n)},{"path",path},{"logicalBytes",QString::number(file.expected.size())},{"skipped",fixture.policy[n].skipped},{"kind",static_cast<int>(fixture.kinds[n])},{"present",fixture.physical[n].has_value()},{"expectedLogicalSha256",QString::fromLatin1(QCryptographicHash::hash(file.expected,QCryptographicHash::Sha256).toHex())}};if(fixture.physical[n]){struct stat info{};test::require(::lstat(path.toStdString().c_str(),&info)==0,"Actual selected final inode");value.insert("bytes",QString::number(info.st_size));value.insert("dev",QString::number(info.st_dev));value.insert("ino",QString::number(info.st_ino));value.insert("sha256",QString::fromLatin1(QCryptographicHash::hash(test::read(path),QCryptographicHash::Sha256).toHex()));}files.push_back(value);}
    for(const auto& [path,raw]:fixture.controls){controls.push_back(QJsonObject{{"path",path},{"sha256",QString::fromLatin1(QCryptographicHash::hash(raw,QCryptographicHash::Sha256).toHex())}});}
    const QJsonObject result{{"schema",5},{"status","ACTUAL_NATIVE_VERIFIED_SEED_CASE"},{"kind",kind},{"outcome",outcome},{"profile",fixture.value.profile},{"metadata",fixture.value.metadata},{"payloadRoot",fixture.value.payload},{"hash",QString::fromStdString(fixture.value.hash.hex())},{"fullV1",full.has_v1()?hex(full.v1):QStringLiteral("-")},{"fullV2",full.has_v2()?hex(full.v2):QStringLiteral("-")},{"selectedVersion",identity->selected==storage::OriginalSelectedHash::V2?2:1},{"goal",1},{"requiredPieces",required},{"verifiedPieces",verified},{"admission",QString::number(actual->admission)},{"released",released},{"phase",static_cast<int>(actual->verifiedSeeding)},{"fullComplete",actual->complete},{"selectedComplete",actual->selectedComplete},{"totalPieces",actual->totalPieces},{"received",QString::number(actual->received)},{"sent",QString::number(actual->sent)},{"receiverCompleted",!receiver.isEmpty()},{"receiverSha256",QString::fromLatin1(receiver)},{"marker",(*saved)->state==storage::VerifiedSeedState::Released?2:1},{"forceStart",(*saved)->forceStart},{"sessionJoined",closed.sessionJoined},{"storageReleased",closed.retirement.storageReleased},{"files",files},{"controls",controls},{"state",fixture.value.profile+"/active/"+QString::fromStdString(fixture.value.hash.hex())+".dat"},{"archive",fixture.value.profile+"/torrents/"+QString::fromStdString(fixture.value.hash.hex())+".torrent"}};test::write(root+"/native-receipt.json",QJsonDocument(result).toJson(QJsonDocument::Indented));return 0;
  }catch(const std::exception& error){qCritical("%s",error.what());return 1;}
}
int prepareCustomVerifiedSeedReceipt(const QString& root,const QString& kind,const QString& outcome){
  try{
    test::require(outcome=="complete"||outcome=="corrupt"||outcome=="stopped","Unknown custom verified outcome");
    const auto fixture=test::prepareCustomVerifiedSeedCase(root,kind,outcome=="corrupt");auto lease=storage::ProfileLease::acquire(fixture.profile.toStdString());test::require(lease.has_value(),"Own actual custom profile");
    const auto imported=test::importCustomVerifiedSeedCase(*lease,fixture,kind);test::require(imported.has_value(),imported?"":imported.error().message.c_str());auto service=owner();test::require(service->startManaged(*lease,{.hash=fixture.hash,.verifyExisting=true}).has_value(),"Actual approved custom RO admission");
    if(outcome=="stopped"){test::require(service->stopManaged(fixture.hash).has_value(),"Actual custom cancellation");}else{test::require(test::spinVerified(*service,fixture.hash),"Actual custom verification deadline");}
    auto actual=test::verifiedStatus(*service,fixture.hash);test::require(actual.has_value(),"Actual custom observation missing");const bool released=actual->verifiedSeeding==engine::VerifiedSeedingPhase::Released;test::require(released==(outcome=="complete"),"Unexpected custom actual outcome");
    const auto bits=service->verifiedSeedPieces(fixture.hash);QByteArray receiverHash;if(released){receiverHash=test::receiveCustomVerifiedSeed(*service,fixture);test::require(service->poll().has_value(),"Actual custom upload sample");actual=test::verifiedStatus(*service,fixture.hash);test::require(actual.has_value()&&actual->sent>0,"Actual custom upload bytes missing");}
    const auto close=service->close(std::chrono::steady_clock::now()+5s);test::require(close.sessionJoined&&close.retirement.storageReleased,"Actual custom joined storage fence failed");if(outcome=="corrupt"){test::require(actual&&actual->failure&&close.checkpointFailure&&close.checkpointFailure->code==actual->failure->code&&close.checkpointFailure->message==actual->failure->message,"Actual custom close changed the observed corruption failure");}else{test::require(!close.checkpointFailure,"Actual custom checkpoint unexpectedly failed");}test::require(test::versionedRealSnapshotsIntact(fixture,outcome=="corrupt"),"Read-only custom payload changed");
    const auto saved=storage::readVerifiedSeedIntent(*lease,fixture.hash);test::require(saved&&saved->has_value(),"Actual custom saved intent");const auto state=parse(test::read(fixture.profile+"/active/"+QString::fromStdString(fixture.hash.hex())+".dat"));const auto flags=core::originalDownloadFlags(state.find("attributes"));test::require(flags.has_value(),"Actual original custom flags");
    const auto inventory=test::customFixtureRow(fixture.profile);test::require(firstEntry(inventory).find(storage::VerifiedExistingScopeKey)!=nullptr,"Actual custom scope was lost");
    QJsonArray files,verified;for(auto bit:bits){verified.push_back(static_cast<int>(bit));}for(const auto& file:fixture.files){const auto path=fixture.payload+'/'+file.relative;struct stat info{};test::require(::stat(path.toStdString().c_str(),&info)==0,"Actual final custom inode");files.push_back(QJsonObject{{"path",path},{"bytes",QString::number(info.st_size)},{"sha256",QString::fromLatin1(QCryptographicHash::hash(test::read(path),QCryptographicHash::Sha256).toHex())},{"dev",QString::number(info.st_dev)},{"ino",QString::number(info.st_ino)}});}
    const QJsonObject result{{"schema",3},{"status","ACTUAL_NATIVE_VERIFIED_SEED_CASE"},{"kind",kind},{"saveName",test::customVerifiedLeaf(kind)},{"profile",fixture.profile},{"metadata",fixture.metadata},{"payloadRoot",fixture.payload},{"hash",QString::fromStdString(fixture.hash.hex())},{"admission",QString::number(actual->admission)},{"released",released},{"phase",static_cast<int>(actual->verifiedSeeding)},{"savedStartup",static_cast<int>((*saved)->startup)},{"forceStart",(*saved)->forceStart},{"moveFlag",bool(*flags&8)},{"marker",(*saved)->state==storage::VerifiedSeedState::Released?2:1},{"totalPieces",actual->totalPieces},{"verifiedPieces",verified},{"received",QString::number(actual->received)},{"sent",QString::number(actual->sent)},{"receiverCompleted",!receiverHash.isEmpty()},{"receiverSha256",QString::fromLatin1(receiverHash)},{"files",files},{"sessionJoined",close.sessionJoined},{"storageReleased",close.retirement.storageReleased},{"state",fixture.profile+"/active/"+QString::fromStdString(fixture.hash.hex())+".dat"},{"archive",fixture.profile+"/torrents/"+QString::fromStdString(fixture.hash.hex())+".torrent"}};
    test::write(root+"/native-receipt.json",QJsonDocument(result).toJson(QJsonDocument::Indented));return 0;
  }catch(const std::exception& error){qCritical("%s",error.what());return 1;}
}
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
namespace {
int prepareAllocatedProfile(const QString& root,const QString& kind){
  try{
    const auto mode=kind==QStringLiteral("paused")?core::TorrentAllocationMode::Paused:core::TorrentAllocationMode::Stopped;
    test::allocation::Fixture f(false,std::filesystem::path(root.toStdString()));test::require(f.import(mode).has_value(),"Actual allocation oracle import failed");auto service=owner();test::require(!service->tcpListenPort()&&service->startManaged(*f.lease,{.hash=f.hash,.allocationMode=mode}).has_value(),"Actual allocation-only admission failed");
    const auto deadline=std::chrono::steady_clock::now()+10s;
    while(std::chrono::steady_clock::now()<deadline){test::require(service->poll().has_value(),"Allocation oracle poll failed");const auto current=status(*service,f.hash);if(current&&current->phase==Phase::Stopped){break;}}
    const auto actual=status(*service,f.hash);test::require(actual&&actual->allocationComplete&&!actual->failure&&!actual->retained&&!actual->complete&&!actual->checked&&actual->received==0&&actual->sent==0&&actual->pieces==0&&!service->tcpListenPort()&&f.zeroes(),"Actual allocation-only final state differs");
    const auto closed=service->close(std::chrono::steady_clock::now()+5s);test::require(closed.sessionJoined&&closed.retirement.storageReleased&&!closed.checkpointFailure,"Allocation oracle owner did not retire");
    const auto saved=f.saved();const auto& row=f.row(saved);test::require(row.find("state")->integer==70&&row.find("allocated")->integer==1&&row.find("completedbytes")->integer==0&&bool(saved.find("pause_data"))==(mode==core::TorrentAllocationMode::Paused),"Actual allocation saved intent differs");
    QJsonArray files;for(std::size_t n=0;n<f.sizes.size();++n){const auto path=f.path(n);struct stat stat{};test::require(::lstat(path.c_str(),&stat)==0&&S_ISREG(stat.st_mode)&&stat.st_nlink==1,"Actual allocation leaf identity missing");const auto data=test::allocation::read(path);QJsonObject file;file.insert("path",QString::fromStdString(path.string()));file.insert("bytes",QString::number(static_cast<qlonglong>(stat.st_size)));file.insert("sha256",QString::fromLatin1(QCryptographicHash::hash(QByteArray::fromStdString(data),QCryptographicHash::Sha256).toHex()));file.insert("dev",QString::number(static_cast<qulonglong>(stat.st_dev)));file.insert("ino",QString::number(static_cast<qulonglong>(stat.st_ino)));files.push_back(file);}
    QJsonObject result;result.insert("schema",1);result.insert("profile",QString::fromStdString(f.profile.string()));result.insert("metadata",QString::fromStdString(f.metadata.string()));result.insert("state",QString::fromStdString((f.profile/"active"/(f.hash.hex()+".dat")).string()));result.insert("archive",QString::fromStdString((f.profile/"torrents"/(f.hash.hex()+".torrent")).string()));result.insert("hash",QString::fromStdString(f.hash.hex()));result.insert("mode",static_cast<int>(mode));result.insert("sessionJoined",closed.sessionJoined);result.insert("storageReleased",closed.retirement.storageReleased);result.insert("allocationComplete",actual->allocationComplete);result.insert("files",files);
    test::write(root+"/native-receipt.json",QJsonDocument(result).toJson(QJsonDocument::Indented));return 0;
  }catch(const std::exception& error){qCritical("%s",error.what());return 1;}
}
}

int main(int argc,char** argv){
  QCoreApplication application(argc,argv);
  if(argc==4&&QString::fromLocal8Bit(argv[1])==QStringLiteral("--prepare-allocated-profile")){
    const auto mode=QString::fromLocal8Bit(argv[3]);if(mode!="stopped"&&mode!="paused"){return 2;}return prepareAllocatedProfile(QString::fromLocal8Bit(argv[2]),mode);
  }
  if(argc==5&&QString::fromLocal8Bit(argv[1])==QStringLiteral("--prepare-verified-seed")){
    const auto kind=QString::fromLocal8Bit(argv[3]);if(kind!="single"&&kind!="multi"){return 2;}
    return prepareVerifiedSeedReceipt(QString::fromLocal8Bit(argv[2]),kind=="multi",QString::fromLocal8Bit(argv[4]));
  }
  if(argc==5&&QString::fromLocal8Bit(argv[1])==QStringLiteral("--prepare-versioned-verified-seed")){
    const auto kind=QString::fromLocal8Bit(argv[3]);
    if(kind!="v2-aligned"&&kind!="v2-tail"&&kind!="hybrid-v1"&&kind!="hybrid-v2"){return 2;}
    return prepareVerifiedSeedReceipt(QString::fromLocal8Bit(argv[2]),false,QString::fromLocal8Bit(argv[4]),kind);
  }
  if(argc==5&&QString::fromLocal8Bit(argv[1])==QStringLiteral("--prepare-custom-verified-seed")){
    const auto kind=QString::fromLocal8Bit(argv[3]);if(kind!="single"&&kind!="multi"&&kind!="one-multi"){return 2;}
    return prepareCustomVerifiedSeedReceipt(QString::fromLocal8Bit(argv[2]),kind,QString::fromLocal8Bit(argv[4]));
  }
  if(argc==5&&QString::fromLocal8Bit(argv[1])==QStringLiteral("--prepare-linked-verified-seed")){
    const auto kind=QString::fromLocal8Bit(argv[3]);if(kind!="single"&&kind!="multi"&&kind!="one-multi"&&kind!="v2-tail"){return 2;}return prepareLinkedVerifiedSeedReceipt(QString::fromLocal8Bit(argv[2]),kind,QString::fromLocal8Bit(argv[4]));
  }
  if(argc==5&&QString::fromLocal8Bit(argv[1])==QStringLiteral("--prepare-selected-verified-seed")){
    return prepareSelectedVerifiedSeedReceipt(QString::fromLocal8Bit(argv[2]),QString::fromLocal8Bit(argv[3]),QString::fromLocal8Bit(argv[4]));
  }
  ManagedDownloadTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "managed_download_tests.moc"
