// SPDX-License-Identifier: GPL-2.0-or-later
#include "session_bridge.hpp"
#include "subscription_metainfo_fetch.hpp"
#include "subscription_values.hpp"
#include "subscription_rss_http_fixture.hpp"
#include "library_window.hpp"
#include "biglybtc/library.hpp"
#include "torrent_intake.hpp"
#include "torrent_state.hpp"
#include <QApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QUdpSocket>
#include <QUrl>
#include <QtTest>
#include <algorithm>
#include <cstdio>
#include <expected>
#include <sys/stat.h>
#include <unistd.h>

namespace {
using namespace biglybtc;
namespace f=subscription_http_fixture;
namespace w=rss_fixture;
namespace s=search;
const QByteArray Key="result-key";
const QByteArray ReceiptPrefix="SUBSCRIPTION_ACQUISITION_PRODUCER ";
const QStringList Checkpoints{"BeforeIntake","JournalSynced","StatePublished","ArchivePublished",
  "InventoryPublished","AssociationPublished","ReadPublished","FinalPublished","Done"};
const QStringList FreshExtraCheckpoints{"DownloadStateSavingSynced","DownloadStatePublished","TemporaryRetired"};
const QStringList ExistingCheckpoints{"ExistingCachePublished","AssociationPublished","DownloadStateSavingSynced","DownloadStatePublished","ReadPublished","FinalPublished","Done","TemporaryRetired"};
QByteArray bytes(const QString& path){QFile file(path);w::require(file.open(QIODevice::ReadOnly),"Cannot read private recovery fixture");return file.readAll();}
QByteArray info(){return f::dictionary({{"length",f::integer(1)},{"name",w::b("Owned")},
  {"piece length",f::integer(16384)},{"pieces",w::b(QByteArray(20,'x'))}});}
QByteArray metainfo(){return f::dictionary({{"future",w::b("opaque-torrent")},{"info",info()}});}
QString targetStem(){return QString::fromLatin1(QCryptographicHash::hash(info(),QCryptographicHash::Sha1).toHex().toUpper());}
QJsonValue initialFile(const QString& path){return QFile::exists(path)?QJsonValue(QString::fromLatin1(bytes(path).toBase64())):QJsonValue(QJsonValue::Null);}
QJsonObject fileIdentity(const QString& path){struct stat value{};w::require(::lstat(path.toLocal8Bit().constData(),&value)==0&&S_ISREG(value.st_mode)&&value.st_nlink==1,"Cannot identify actual regular fixture file");return {{"device",QString::number(static_cast<qulonglong>(value.st_dev))},{"inode",QString::number(static_cast<qulonglong>(value.st_ino))},{"size",QString::number(static_cast<qlonglong>(value.st_size))},{"modified_seconds",QString::number(static_cast<qlonglong>(value.st_mtim.tv_sec))},{"modified_nanos",QString::number(static_cast<qlonglong>(value.st_mtim.tv_nsec))},{"changed_seconds",QString::number(static_cast<qlonglong>(value.st_ctim.tv_sec))},{"changed_nanos",QString::number(static_cast<qlonglong>(value.st_ctim.tv_nsec))}};}
bool within(const QString& root,const QString& path){const auto relative=QDir(root).relativeFilePath(path);return !relative.isEmpty()&&!QDir::isAbsolutePath(relative)&&relative!=".."&&!relative.startsWith("../");}
template<class Predicate>bool until(Predicate ready,int milliseconds){QElapsedTimer timer;timer.start();while(!ready()&&timer.elapsed()<milliseconds){QCoreApplication::processEvents();QTest::qWait(10);}return ready();}

// Same small private-profile preparation as the actual Bridge producer test.
// No caller profile, URL, hash or serialized intake plan enters this process.
// Select a fixture-owned loopback port independently of the HTTP endpoint.
// Reservations are closed before the genuine Normal Host binds; any later
// competing bind still fails through its unchanged owner/retention guards.
quint16 isolatedNormalPort(const std::string& httpUrl){
  const int httpPort=QUrl(QString::fromStdString(httpUrl)).port(-1);w::require(httpPort>0,"Private HTTP endpoint has no explicit port");
  for(unsigned attempt=0;attempt<16;++attempt){
    w::Server reservation;const auto port=reservation.serverPort();QUdpSocket udp;
    if(port!=httpPort&&udp.bind(QHostAddress::LocalHost,port,QAbstractSocket::DontShareAddress)){
      udp.close();reservation.close();return port;
    }
  }
  throw std::runtime_error("Cannot reserve isolated Normal Host TCP/UDP port");
}

void prepare(f::Profile& area,const std::string& url){
  const auto normalPort=isolatedNormalPort(url);
  auto settings=storage::ConfigFile::load(*area.lease,"biglybt.config");w::require(bool(settings),"Private settings missing");
  for(const auto* key:{"Download History Enabled","TCP.Listen.Port.Enable","peercontrol.udp.public.enable","Plugin.DHT.dht.enabled","Plugin.mldht.enable","PluginInfo.mldht.enabled","Peer Source Selection Default.DHT","Peer Source Selection Default.PeerExchange","Peer Source Selection Default.Tracker","Plugin.UPnP.upnp.enable","Plugin.UPnP.natpmp.enable","IPV6 Enable Support"}){w::require(bool(settings->document().replaceValue(key,"i0e")),"Private disabled setting failed");}
  for(const auto* key:{"TCP.Listen.Port","UDP.Listen.Port","UDP.NonData.Listen.Port"}){w::require(bool(settings->document().replaceValue(key,f::integer(normalPort).toStdString())),"Private isolated listener port failed");}
  w::require(bool(settings->document().replaceValue("Bind IP","9:127.0.0.1"))&&bool(settings->document().replaceValue("Enforce Bind IP","i1e"))&&bool(settings->save(*area.lease)),"Private networking isolation failed");
  w::require(bool(storage::ConfigFile::createMissing(*area.lease,"downloads.config","d9:downloadsle6:future3:rawe"))&&bool(storage::ConfigFile::createMissing(*area.lease,"tables.config","de")),"Private empty inventory failed");
  auto metadata=core::Document::parse(area.metadataRow.toStdString());w::require(bool(metadata),"Private signed metadata missing");const auto* old=metadata->find("history");w::require(old,"Private history missing");auto history=core::Document::parse(*core::encodeValue(*old));
  w::require(bool(history)&&bool(history->replaceValue("last_scan",f::integer(QDateTime::currentMSecsSinceEpoch()).toStdString()))&&bool(history->replaceValue("num_unread","i1e"))&&bool(metadata->replaceValue("history",history->serialize())),"Private history setup failed");
  area.metadataRow=QByteArray::fromStdString(metadata->serialize());area.configuration=f::dictionary({{"subs","l"+area.metadataRow+"e"}});w::write(area.path+"/subscriptions.config",area.configuration);
  const QJsonObject result{{"n","Owned"},{"lb","1"},{"s","4294967297"},{"p","2"},{"tf","1"},{"ts","0"},{"dl",QString::fromStdString(url)},{"future","retain-json"}};
  w::write(area.resultsPath,f::dictionary({{"future_results",w::b("retain-results")},{"results","l"+f::dictionary({{"key",w::b(Key)},{"read",f::integer(0)},{"future_row",w::b("retain-row")},{"result_json",w::b(QJsonDocument(result).toJson(QJsonDocument::Compact))},{"tz",f::integer(1)}})+"e"}}));area.lease.reset();
}
void wire(app::SessionBridge& bridge,ui::LibraryWindow& window){using B=app::SessionBridge;using W=ui::LibraryWindow;
  window.setOfflinePreview(false);QObject::connect(&bridge,&B::snapshotReady,&window,&W::setPresentation);QObject::connect(&bridge,&B::columnLayoutSessionReady,&window,&W::beginColumnLayoutSession);QObject::connect(&bridge,&B::busyChanged,&window,&W::setBusy);
}

// Genuine stopped Public intake and public state-store writes construct B and
// its ordinary backup. The future coordinator still captures its own target;
// none of these fixture bytes is passed as a witness, receipt or postimage.
void seedExisting(f::Profile& area,const QString& payload){
  auto lease=storage::ProfileLease::acquire(area.path.toStdString());w::require(bool(lease),"Cannot acquire existing seed profile");
  const auto metadata=area.root.filePath("seed.torrent");w::write(metadata,metainfo());
  auto imported=storage::importTorrent(*lease,metadata.toStdString(),payload.toStdString(),true,nullptr,false,storage::TorrentIntakeNetwork::Public);
  w::require(bool(imported)&&QString::fromStdString(imported->hex())==targetStem(),"Cannot genuinely seed matching stopped torrent");
  auto store=storage::TorrentStateStore::open(*lease);w::require(bool(store),"Cannot open existing seed state store");auto state=store->beginUpdate(*lease,*imported);w::require(bool(state),"Cannot prepare existing seed state");
  const auto* attributes=state->document().find("attributes");w::require(attributes,"Existing seed attributes missing");auto values=core::Document::parse(*core::encodeValue(*attributes));
  const auto subscriptions=f::dictionary({{"s","l"+w::b("other12345")+"e"},{"lc",f::integer(17)},{"future",w::b("retain-map")}});
  w::require(bool(values)&&bool(values->replaceValue("scsrc","i0e"))&&bool(values->replaceValue("scrapecache","i33e"))&&bool(values->replaceValue("fixture_state",w::b("retain-attributes").toStdString()))&&bool(values->replaceValue("Plugin.<internal>.azsubs.subs_info",subscriptions.toStdString())),"Cannot prepare real seed attribute values");
  w::require(bool(state->replaceSection("attributes",values->serialize()))&&bool(state->replaceSection("resume",f::dictionary({{"fixture_resume",w::b("retain-resume")}}).toStdString()))&&bool(store->save(*lease,*state)),"Cannot save real seed state and backup");
  w::require(QFile::exists(area.path+"/active/"+targetStem()+".dat.bak"),"Genuine state write did not retain ordinary backup");
}

int producer(const QString& checkpoint,bool existing=false){
  if(existing?!ExistingCheckpoints.contains(checkpoint):!Checkpoints.contains(checkpoint)&&!FreshExtraCheckpoints.contains(checkpoint)){return 2;}
  w::Server server;server.body=metainfo();f::Profile area(server.url());prepare(area,server.url("/owned.torrent?recovery=1"));
  const auto payload=area.root.filePath("payload");w::require(QDir().mkpath(payload),"Cannot create recovery payload directory");
  if(existing){seedExisting(area,payload);}
  const auto receiptPath=area.root.filePath("producer-receipt.json");
  const auto statePath=area.path+"/active/"+targetStem()+".dat",archivePath=area.path+"/torrents/"+targetStem()+".torrent";
  const QJsonObject initial{{"body",QString::fromLatin1(bytes(area.bodyPath).toBase64())},{"configuration",QString::fromLatin1(bytes(area.path+"/subscriptions.config").toBase64())},{"results",QString::fromLatin1(bytes(area.resultsPath).toBase64())},{"inventory",QString::fromLatin1(bytes(area.path+"/downloads.config").toBase64())},{"inventory_backup",initialFile(area.path+"/downloads.config.bak")},{"state",initialFile(statePath)},{"state_backup",initialFile(statePath+".bak")},{"archive",initialFile(archivePath)},{"archive_backup",initialFile(archivePath+".bak")}};
  const QJsonObject receipt{{"schema","BiglyBTC.Acquisition.CrashProducer.V2"},{"checkpoint",checkpoint},{"existing",existing},{"profile",area.path},{"body",area.bodyPath},{"results",area.resultsPath},{"payload",payload},{"receipt",receiptPath},{"sid",QString::fromLatin1(area.shortId.toBase64())},{"state_backup_identity",existing?fileIdentity(statePath+".bak"):QJsonObject{}},{"initial",initial}};
  const auto encoded=QJsonDocument(receipt).toJson(QJsonDocument::Compact);w::write(receiptPath,encoded);const auto output=ReceiptPrefix+encoded+'\n';
  if(std::fwrite(output.constData(),1,static_cast<std::size_t>(output.size()),stdout)!=static_cast<std::size_t>(output.size())||std::fflush(stdout)!=0){return 3;}
  std::atomic<int> made{0};
  app::SessionBridge::SubscriptionAcquisitionFactory factory=[&](const storage::ProfileLease& lease,const std::filesystem::path& path,quint64 session,const ui::SubscriptionKey& key,const QByteArray& result,std::stop_token stop){
    ++made;const s::SavedSubscriptionSelector selected{static_cast<std::size_t>(key.sourceIndex),key.sid.toStdString(),key.publicKey.toStdString(),key.singleton?std::optional{key.singletonKey.toStdString()}:std::nullopt,key.version};
    auto opened=s::SubscriptionMetainfoFetch::openFixture(lease,path,session,selected,result.toStdString(),area.endpoint(),stop);return opened?std::move(*opened):std::unique_ptr<s::SubscriptionMetainfoFetch>{};};
  app::SessionBridge bridge(nullptr,{},{},{},{},{},{},std::move(factory));ui::LibraryWindow window;wire(bridge,window);
  QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),catalogs(&bridge,&app::SessionBridge::subscriptionCatalogReady),packets(&bridge,&app::SessionBridge::subscriptionReady);
  if(!bridge.openNormal(area.path.toStdString())||!until([&]{return !bridge.busy()&&sessions.size()==1;},12000)||made.load()!=0){return 4;}
  const auto session=sessions.front()[0].toULongLong();if(!bridge.subscriptionCatalog({session,1,1})||!until([&]{return !bridge.busy()&&catalogs.size()==1;},5000)){return 5;}
  const auto catalog=qvariant_cast<ui::SubscriptionCatalogPresentation>(catalogs.front()[0]);if(!catalog||catalog->sources.size()!=1){return 6;}
  qputenv("BIGLYBTC_SUBSCRIPTION_ACQUISITION_CRASH_AT",checkpoint.toUtf8());
  ui::SubscriptionCommand command{{session,1,2,catalog->generation,catalog->sources.front().key,ui::SubscriptionAction::AcquireStopped},{Key},std::make_shared<std::stop_source>(),payload};
  const auto key=command.key;
  const auto latest=[&]()->ui::SubscriptionPresentation{if(packets.empty()){return {};}auto packet=qvariant_cast<ui::SubscriptionPresentation>(packets.last()[0]);return packet&&packet->key==key?packet:ui::SubscriptionPresentation{};};
  const auto report=[&](const char* stage){
    const auto packet=latest();const auto state=packet?static_cast<int>(packet->state):-1;
    const auto prefix=QString("SUBSCRIPTION_ACQUISITION_PRODUCER_DIAGNOSTIC checkpoint=%1 mode=%2 stage=%3 state=%4 committed=%5 factories=%6 GETs=%7 packets=%8 ")
        .arg(checkpoint,existing?QStringLiteral("existing"):QStringLiteral("fresh"),QString::fromLatin1(stage)).arg(state).arg(packet&&packet->committed?1:0).arg(made.load()).arg(server.requests.size()).arg(packets.size()).toUtf8();
    const QJsonObject detail{{"message",packet?packet->message.left(384):QString{}},{"server_failure",server.failure.left(128)}};
    const auto output=prefix+QJsonDocument(detail).toJson(QJsonDocument::Compact)+'\n';
    (void)std::fwrite(output.constData(),1,static_cast<std::size_t>(output.size()),stderr);(void)std::fflush(stderr);
  };
  if(!bridge.subscriptionCommand(std::move(command))){report("command-refused");return 7;}
  // A successful producer never returns: the actual fixture checkpoint _exit(86)
  // bypasses destructors after the relevant real sync/publication. Any completed
  // terminal packet instead proves the requested checkpoint was not reached.
  const bool terminal=until([&]{const auto packet=latest();return packet&&packet->state!=ui::SubscriptionState::Started;},28000);
  report(terminal?"terminal-before-checkpoint":"checkpoint-timeout");return 8;
}

struct Receipt {QString profile,body,results,payload;QByteArray initialBody,initialConfiguration,initialResults,initialInventory,sid;bool existing=false;std::optional<QByteArray> initialState,initialStateBackup,initialArchive,initialInventoryBackup,initialArchiveBackup;QJsonObject initialStateBackupIdentity;};
std::expected<Receipt,QString> produce(const QString& ownedRoot,const QString& checkpoint,bool existing=false){
  const auto temporary=ownedRoot+"/tmp";if(!QDir().mkpath(temporary)){return std::unexpected("Cannot create owned child TMPDIR");}
  auto environment=QProcessEnvironment::systemEnvironment();environment.remove("BIGLYBTC_SUBSCRIPTION_ACQUISITION_CRASH_AT");environment.insert("TMPDIR",temporary);environment.insert("QT_QPA_PLATFORM","offscreen");
  QElapsedTimer lifetime;lifetime.start();QProcess child;child.setProcessEnvironment(environment);child.setProgram(QCoreApplication::applicationFilePath());child.setArguments({"--acquisition-crash-child",existing?"existing":"fresh",checkpoint});child.start();
  if(!child.waitForStarted(3000)){return std::unexpected("Owned recovery child did not start");}
  const bool ended=until([&]{return child.state()==QProcess::NotRunning;},std::max(0,30000-static_cast<int>(lifetime.elapsed())));if(!ended){child.kill();child.waitForFinished(3000);}
  const auto output=child.readAllStandardOutput(),errors=child.readAllStandardError();
  if(!ended||child.exitStatus()!=QProcess::NormalExit||child.exitCode()!=86){return std::unexpected(QString("checkpoint=%1 ended=%2 exit=%3 stderr=%4").arg(checkpoint).arg(ended).arg(child.exitCode()).arg(QString::fromUtf8(errors.left(1024))));}
  QByteArray encoded;int receipts=0;for(const auto& line:output.split('\n')){if(line.startsWith(ReceiptPrefix)){++receipts;encoded=line.mid(ReceiptPrefix.size());}}
  if(receipts!=1||encoded.size()>32768||output.size()>65536||errors.size()>65536){return std::unexpected("Missing or oversized owned producer receipt/output");}
  QJsonParseError error;const auto document=QJsonDocument::fromJson(encoded,&error);if(error.error!=QJsonParseError::NoError||!document.isObject()){return std::unexpected("Invalid producer receipt JSON");}
  const auto object=document.object();if(object.value("schema").toString()!="BiglyBTC.Acquisition.CrashProducer.V2"||object.value("checkpoint").toString()!=checkpoint||!object.value("existing").isBool()||object.value("existing").toBool()!=existing){return std::unexpected("Producer receipt identity changed");}
  for(const auto* name:{"profile","body","results","payload","receipt"}){const auto path=object.value(QLatin1String(name)).toString();if(!within(temporary,path)||QFileInfo(path).isSymLink()||QFileInfo(path).canonicalFilePath()!=path){return std::unexpected("Producer receipt escaped owned temporary root");}}
  if(bytes(object.value("receipt").toString())!=encoded){return std::unexpected("Producer path receipt changed");}
  const auto initial=object.value("initial").toObject();Receipt receipt;receipt.profile=object.value("profile").toString();receipt.body=object.value("body").toString();receipt.results=object.value("results").toString();receipt.payload=object.value("payload").toString();receipt.existing=existing;receipt.sid=QByteArray::fromBase64(object.value("sid").toString().toLatin1(),QByteArray::AbortOnBase64DecodingErrors);receipt.initialStateBackupIdentity=object.value("state_backup_identity").toObject();
  auto decode=[&](const char* name){return QByteArray::fromBase64(initial.value(QLatin1String(name)).toString().toLatin1(),QByteArray::AbortOnBase64DecodingErrors);};
  receipt.initialBody=decode("body");receipt.initialConfiguration=decode("configuration");receipt.initialResults=decode("results");receipt.initialInventory=decode("inventory");
  for(auto [name,destination]:{std::pair{"state",&receipt.initialState},std::pair{"state_backup",&receipt.initialStateBackup},std::pair{"archive",&receipt.initialArchive},std::pair{"inventory_backup",&receipt.initialInventoryBackup},std::pair{"archive_backup",&receipt.initialArchiveBackup}}){const auto value=initial.value(QLatin1String(name));if(value.isString()){*destination=decode(name);if((*destination)->isEmpty()){return std::unexpected("Empty existing-target initial bytes");}}else if(!value.isNull()){return std::unexpected("Invalid existing-target initial shape");}}
  if(receipt.initialBody.isEmpty()||receipt.initialConfiguration.isEmpty()||receipt.initialResults.isEmpty()||receipt.initialInventory.isEmpty()||receipt.sid.size()!=10||existing!=receipt.initialState.has_value()||existing!=receipt.initialArchive.has_value()||existing!=receipt.initialStateBackup.has_value()){return std::unexpected("Producer initial bytes missing or wrong target domain");}return receipt;
}

// Only the exact recorded, uid-owned, non-symlink directory is eligible for
// fixture cleanup. Healthy recovery must delete it itself. Conflict leaves it
// for review; this guard removes that one owned fixture after assertions.
struct ExternalTemporary {
  QString path;dev_t device=0;ino_t inode=0;bool alreadyGone=false,cleanupOnDestruction=true;
  bool capture(const Receipt& receipt){
    const auto marker=core::Document::parse(bytes(receipt.profile+"/BiglyBTC.subscription-acquire").toStdString());if(!marker){return false;}const auto* schema=marker->find("schema");const bool v2=schema&&schema->bytes=="BiglyBTC.Subscription.Acquisition.V2";const auto* done=marker->find("done"),*retired=marker->find("temp_retired");
    if(v2){const auto* mode=marker->find("mode");if(!mode||mode->bytes!=(receipt.existing?"existing":"fresh")||!retired||retired->type!=core::Node::Type::Integer||(retired->integer!=0&&retired->integer!=1)){return false;}}
    const bool doneV2=v2&&done&&done->type==core::Node::Type::Integer&&done->integer==1,retiredV2=v2&&retired->integer==1;const auto* node=marker->find("temp_directory");
    if(!node||(node->type==core::Node::Type::Bytes&&node->bytes.empty())){alreadyGone=retiredV2;return alreadyGone;}
    if(node->type!=core::Node::Type::Bytes){return false;}path=QString::fromStdString(std::string(node->bytes));
    const auto prefix=QStringLiteral("/tmp/biglybtc-sub-acq-");const auto suffix=path.mid(prefix.size());
    if(!path.startsWith(prefix)||suffix.size()!=6||!std::ranges::all_of(suffix,[](QChar c){return c.unicode()<128&&c.isLetterOrNumber();})||QFileInfo(path).isSymLink()){path.clear();return false;}
    if(!QFileInfo::exists(path)){alreadyGone=retiredV2||doneV2;return alreadyGone;}if(retiredV2||QFileInfo(path).canonicalFilePath()!=path){path.clear();return false;}
    struct stat value{};if(::lstat(path.toLocal8Bit().constData(),&value)!=0||!S_ISDIR(value.st_mode)||value.st_uid!=::geteuid()||bytes(path+"/metadata.torrent")!=metainfo()){path.clear();return false;}device=value.st_dev;inode=value.st_ino;return true;
  }
  bool remove(){if(alreadyGone){return path.isEmpty()||!QFileInfo::exists(path);}if(path.isEmpty()){return false;}struct stat value{};if(::lstat(path.toLocal8Bit().constData(),&value)!=0){return !QFileInfo::exists(path);}if(!S_ISDIR(value.st_mode)||value.st_uid!=::geteuid()||value.st_dev!=device||value.st_ino!=inode){return false;}return QDir(path).removeRecursively()&&!QFileInfo::exists(path);}
  ~ExternalTemporary(){if(cleanupOnDestruction&&!path.isEmpty()){(void)remove();}}
};

bool recoverNormal(const Receipt& receipt,bool review,QString& diagnostic,bool preview=false){
  std::atomic<int> made{0};
  app::SessionBridge::SubscriptionAcquisitionFactory reject=[&](const storage::ProfileLease&,const std::filesystem::path&,quint64,const ui::SubscriptionKey&,const QByteArray&,std::stop_token){++made;return std::unique_ptr<s::SubscriptionMetainfoFetch>{};};
  app::SessionBridge::SubscriptionFetchFactory rejectScan=[&](const storage::ProfileLease&,const std::filesystem::path&,quint64,const ui::SubscriptionKey&,std::stop_token){++made;return std::unique_ptr<s::RssHttpFetch>{};};
  app::SessionBridge bridge(nullptr,{},{},{},{},{},std::move(rejectScan),std::move(reject));ui::LibraryWindow window;wire(bridge,window);
  QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),snapshots(&bridge,&app::SessionBridge::snapshotReady),messages(&bridge,&app::SessionBridge::messageReady),closed(&bridge,&app::SessionBridge::sessionClosed);
  const bool opened=preview?bridge.open(receipt.profile.toStdString(),0):bridge.openNormal(receipt.profile.toStdString());
  if(!opened||!until([&]{return !bridge.busy()&&sessions.size()==1&&!snapshots.empty();},15000)){diagnostic="Recovery startup did not finish";return false;}
  const auto snapshot=qvariant_cast<ui::Presentation>(snapshots.last()[0]);bool reviewed=!review;
  if(review){for(const auto& message:messages){reviewed=reviewed||message[0].toString().contains("needs review",Qt::CaseInsensitive);}if(!snapshot||snapshot->items.size()!=1||snapshot->items[0].actionMask!=0){diagnostic="Conflict startup was not a read-only imported-row review";return false;}}
  if(!bridge.closeSession()||!until([&]{return closed.size()==1;},15000)){diagnostic="Recovery close did not join";return false;}
  if(!closed.front()[1].toBool()||!closed.front()[2].toBool()||made.load()!=0||!reviewed){diagnostic=QString("joined=%1 released=%2 factories=%3 review=%4").arg(closed.front()[1].toBool()).arg(closed.front()[2].toBool()).arg(made.load()).arg(reviewed);return false;}
  if(!storage::ProfileLease::acquire(receipt.profile.toStdString())){diagnostic="Recovery close retained the actual profile lock";return false;}return true;
}
std::string excluding(const core::Node& dictionary,std::initializer_list<std::string_view> omitted){
  std::string encoded="d";for(std::size_t i=0;i+1<dictionary.children.size();i+=2){const auto& key=dictionary.children[i];if(std::ranges::find(omitted,key.bytes)!=omitted.end()){continue;}encoded+=*core::encodeValue(key);encoded+=*core::encodeValue(dictionary.children[i+1]);}return encoded+'e';
}
bool absentIntent(const Receipt& receipt){for(const auto* name:{"BiglyBTC.subscription-acquire","BiglyBTC.torrent-import",".subscriptions-native-write"}){for(const auto* suffix:{"",".saving",".bak"}){if(QFile::exists(receipt.profile+"/"+name+suffix)){return false;}}}return true;}
std::optional<QByteArray> optionalBytes(const QString& path){if(!QFile::exists(path)){return {};}return bytes(path);}
QString statePath(const Receipt& receipt){return receipt.profile+"/active/"+targetStem()+".dat";}
bool hasSourceSid(const QByteArray& encoded,const QByteArray& sid){
  auto state=core::Document::parse(encoded.toStdString());w::require(bool(state),"Cannot inspect actual fixed state");const auto* attributes=state->find("attributes");if(!attributes){return false;}const auto* map=attributes->find("Plugin.<internal>.azsubs.subs_info");if(!map){return false;}const auto* members=map->find("s");if(!members){return false;}return std::ranges::any_of(members->children,[&](const core::Node& member){return member.bytes==std::string_view(sid.constData(),static_cast<std::size_t>(sid.size()));});
}
bool frozenExistingFiles(const Receipt& receipt){
  if(!receipt.existing){return true;}const auto state=statePath(receipt);return bytes(receipt.profile+"/downloads.config")==receipt.initialInventory&&optionalBytes(receipt.profile+"/downloads.config.bak")==receipt.initialInventoryBackup&&optionalBytes(receipt.profile+"/torrents/"+targetStem()+".torrent")==receipt.initialArchive&&optionalBytes(receipt.profile+"/torrents/"+targetStem()+".torrent.bak")==receipt.initialArchiveBackup&&optionalBytes(state+".bak")==receipt.initialStateBackup&&fileIdentity(state+".bak")==receipt.initialStateBackupIdentity;
}

// One separately selected LOCAL mode. No CI skip or profile argument: the
// immutable digest binds this exact genuinely captured, inode-bound V1 fixture.
// It must be consumed in place once; it cannot be copied/rebased/reconstructed.
int legacyUpgrade(){
  constexpr auto digest="3317036021021fe8836a0401051464eada64e0ee686e512d010b8dd8e00d6978";
  w::require(qgetenv("BIGLYBTC_SUBSCRIPTION_ACQUISITION_CRASH_AT").isEmpty(),"Legacy upgrade must not arm another crash checkpoint");
  const auto manifestPath=qEnvironmentVariable("BIGLYBTC_SUBSCRIPTION_ACQUISITION_LEGACY_V1_MANIFEST");
  w::require(!manifestPath.isEmpty()&&QFileInfo(manifestPath).canonicalFilePath()==manifestPath,"Exact local V1 manifest environment is required");const auto encoded=bytes(manifestPath);
  w::require(encoded.size()<=65536&&QCryptographicHash::hash(encoded,QCryptographicHash::Sha256).toHex()==digest,"Local V1 manifest does not match the sealed old producer");
  const auto manifest=QJsonDocument::fromJson(encoded);w::require(manifest.isObject(),"Local V1 manifest is malformed");const auto object=manifest.object(),description=object.value("description").toObject();
  w::require(object.value("schema").toString()=="BiglyBTC.LocalQualifiedLegacyV1AcquisitionFixture.V1"&&object.value("observed_schema").toString()=="BiglyBTC.Subscription.Acquisition.V1"&&object.value("observed_checkpoint").toString()=="AssociationPublished","Wrong local legacy capture domain");
  const auto ownerRoot=object.value("owner_root").toString();w::require(ownerRoot==QFileInfo(manifestPath).absolutePath()&&QFileInfo(ownerRoot).canonicalFilePath()==ownerRoot,"Local capture root changed");
  Receipt receipt;receipt.profile=description.value("profile").toString();receipt.body=description.value("body").toString();receipt.results=description.value("results").toString();receipt.payload=description.value("payload").toString();
  const auto profileRoot=QFileInfo(receipt.profile).absolutePath();w::require(within(ownerRoot+"/tmp",profileRoot)&&QFileInfo(profileRoot).canonicalFilePath()==profileRoot&&QFileInfo(receipt.profile).canonicalFilePath()==receipt.profile,"Legacy fixture escaped the owned local capture");
  const auto files=object.value("profile_files_before_consumption").toObject();w::require(files.size()==14,"Legacy pre-consumption file inventory changed");
  for(auto it=files.begin();it!=files.end();++it){const auto path=it.key();const auto entry=it.value().toObject();w::require(within(receipt.profile,path)&&QFileInfo(path).canonicalFilePath()==path&&!QFileInfo(path).isSymLink(),"Legacy source file alias changed");const auto data=bytes(path);w::require(data.size()==entry.value("bytes").toInteger(-1)&&QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex())==entry.value("sha256").toString(),"Legacy source bytes changed before upgrade");}
  const auto ownerPath=receipt.profile+"/BiglyBTC.subscription-acquire";const auto intent=core::Document::parse(bytes(ownerPath).toStdString());
  w::require(bool(intent)&&intent->find("schema")&&intent->find("schema")->bytes=="BiglyBTC.Subscription.Acquisition.V1"&&intent->find("F")&&intent->find("R"),"Genuine V1 owner marker is unavailable");
  const auto value=[&](const char* name){const auto* node=intent->find(name);w::require(node&&node->type==core::Node::Type::Bytes,"Legacy frozen value missing");return QByteArray(node->bytes.data(),static_cast<qsizetype>(node->bytes.size()));};
  const auto finalConfiguration=value("F"),finalResults=value("R"),beforeInventory=bytes(receipt.profile+"/downloads.config"),beforeState=bytes(statePath(receipt)),beforeArchive=bytes(receipt.profile+"/torrents/"+targetStem()+".torrent"),beforeBody=bytes(receipt.body);
  w::require(!beforeState.contains("Plugin.<internal>.azsubs.subs_info"),"Old V1 fixture unexpectedly contains a download SID callback");
  ExternalTemporary temporary;temporary.cleanupOnDestruction=false;w::require(temporary.capture(receipt),"Legacy owned metadata temporary changed");
  QString diagnostic;w::require(recoverNormal(receipt,false,diagnostic,true),"First preview legacy recovery failed");
  w::require(bytes(receipt.profile+"/subscriptions.config")==finalConfiguration&&bytes(receipt.results)==finalResults&&bytes(receipt.profile+"/downloads.config")==beforeInventory&&bytes(statePath(receipt))==beforeState&&bytes(receipt.profile+"/torrents/"+targetStem()+".torrent")==beforeArchive&&bytes(receipt.body)==beforeBody&&absentIntent(receipt)&&!QFile::exists(temporary.path),"Legacy upgrade did not preserve exact V1 effects");
  const auto inventory=core::Document::parse(beforeInventory.toStdString());w::require(bool(inventory),"Legacy inventory malformed");const auto library=core::readLibrary(*inventory);w::require(bool(library)&&library->downloads.size()==1&&library->paused.empty()&&library->downloads[0].originalState==70&&!library->downloads[0].forceStart&&library->downloads[0].downloaded==0&&QString::fromStdString(library->downloads[0].saveDirectory)==receipt.payload,"Legacy stopped target was reimported or changed");
  w::require(recoverNormal(receipt,false,diagnostic,true),"Second preview legacy startup failed");
  w::require(bytes(receipt.profile+"/subscriptions.config")==finalConfiguration&&bytes(receipt.results)==finalResults&&bytes(receipt.profile+"/downloads.config")==beforeInventory&&bytes(statePath(receipt))==beforeState&&bytes(receipt.profile+"/torrents/"+targetStem()+".torrent")==beforeArchive&&bytes(receipt.body)==beforeBody&&absentIntent(receipt),"Second legacy startup replayed or changed effects");
  w::require(!QFile::exists(receipt.profile+"/dlhistorya.config")&&!QFile::exists(receipt.profile+"/dlhistoryd.config")&&QDir(receipt.payload).entryList(QDir::AllEntries|QDir::NoDotAndDotDot).empty(),"Disabled-history legacy recovery created history or payload");
  w::require(temporary.remove()&&QDir(profileRoot).removeRecursively()&&!QFileInfo::exists(profileRoot),"Local legacy fixture cleanup failed");
  const QJsonObject report{{"schema","BiglyBTC.LocalQualifiedLegacyV1Upgrade.V1"},{"status","PASS_LOCAL_QUALIFIED_V1_UPGRADE"},{"old_manifest_sha256",digest},{"preview_startups",2},{"factory_calls",0},{"profile_files_verified_before_consumption",14},{"state_sha256",QString::fromLatin1(QCryptographicHash::hash(beforeState,QCryptographicHash::Sha256).toHex())},{"no_reimport",true},{"no_retroactive_download_sid_callback",true},{"history_was_disabled",true},{"profile_cleanup",true},{"external_temporary_cleanup",true}};
  const auto output=QJsonDocument(report).toJson(QJsonDocument::Compact)+'\n';if(std::fwrite(output.constData(),1,static_cast<std::size_t>(output.size()),stdout)!=static_cast<std::size_t>(output.size())||std::fflush(stdout)!=0){return 13;}return 0;
}
} // namespace

class SubscriptionAcquisitionRecoveryTests final:public QObject {
  Q_OBJECT
private slots:
  void actualNormalStartupRecoversOwnedProcessCheckpoints_data(){
    QTest::addColumn<QString>("checkpoint");QTest::addColumn<bool>("existing");QTest::addColumn<QString>("fault");for(const auto& phase:Checkpoints){QTest::newRow(qPrintable(phase))<<phase<<false<<QString("none");}
    QTest::newRow("FinalPublished-impossible-F-beforeResults")<<QString("FinalPublished")<<false<<QString("sub-pair");
    for(const auto& phase:FreshExtraCheckpoints){QTest::newRow(qPrintable("fresh-"+phase))<<phase<<false<<QString("none");}
    for(const auto& phase:ExistingCheckpoints){QTest::newRow(qPrintable("existing-"+phase))<<phase<<true<<QString("none");}
    QTest::newRow("fresh-proxy-revoked-after-S1")<<QString("DownloadStatePublished")<<false<<QString("proxy");
    QTest::newRow("existing-proxy-revoked-after-S1")<<QString("DownloadStatePublished")<<true<<QString("proxy");
    QTest::newRow("existing-foreign-state-after-S1")<<QString("DownloadStatePublished")<<true<<QString("foreign-state");
  }
  void actualNormalStartupRecoversOwnedProcessCheckpoints(){
    QFETCH(QString,checkpoint);QFETCH(bool,existing);QFETCH(QString,fault);const bool conflict=fault=="sub-pair"||fault=="foreign-state";QTemporaryDir owned;QVERIFY(owned.isValid());
    const auto produced=produce(owned.path(),checkpoint,existing);QVERIFY2(produced.has_value(),qPrintable(produced?QString{}:produced.error()));const auto& receipt=*produced;
    ExternalTemporary temporary;QVERIFY(temporary.capture(receipt));const auto owner=bytes(receipt.profile+"/BiglyBTC.subscription-acquire");
    if(checkpoint=="DownloadStateSavingSynced"){
      QVERIFY(QFile::exists(statePath(receipt)+".saving"));QVERIFY(!hasSourceSid(bytes(statePath(receipt)),receipt.sid));QVERIFY(hasSourceSid(bytes(statePath(receipt)+".saving"),receipt.sid));QVERIFY(frozenExistingFiles(receipt));QCOMPARE(bytes(receipt.results),receipt.initialResults);
      const auto prepared=core::Document::parse(bytes(statePath(receipt)).toStdString()),configuration=core::Document::parse(bytes(receipt.profile+"/subscriptions.config").toStdString());QVERIFY(prepared&&configuration&&prepared->find("attributes")&&configuration->find("subs"));const auto* attributes=prepared->find("attributes");QVERIFY(attributes->find("scsrc")&&attributes->find("scrapecache"));QCOMPARE(attributes->find("scsrc")->integer,std::int64_t{1});QCOMPARE(attributes->find("scrapecache")->integer,std::int64_t{4294967298});const auto& row=configuration->find("subs")->children.front();QVERIFY(row.find("assoc")&&row.find("assoc")->children.size()==1&&row.find("history"));QCOMPARE(row.find("history")->find("num_read")->integer,std::int64_t{0});
    }
    if(fault=="sub-pair"){w::write(receipt.results,receipt.initialResults);}
    QByteArray foreignState,proxySettings;
    if(fault=="proxy"){auto settings=core::Document::parse(bytes(receipt.profile+"/biglybt.config").toStdString());QVERIFY(settings&&settings->replaceValue("metasearch.config.proxy.type","i1e"));proxySettings=QByteArray::fromStdString(settings->serialize());w::write(receipt.profile+"/biglybt.config",proxySettings);}
    if(fault=="foreign-state"){auto state=core::Document::parse(bytes(statePath(receipt)).toStdString());QVERIFY(state&&state->find("attributes"));auto attributes=core::Document::parse(*core::encodeValue(*state->find("attributes")));QVERIFY(attributes&&attributes->replaceValue("fixture_foreign",w::b("preserve-foreign-state").toStdString())&&state->replaceValue("attributes",attributes->serialize()));foreignState=QByteArray::fromStdString(state->serialize());w::write(statePath(receipt),foreignState);}
    const auto beforeRecoveryConfig=bytes(receipt.profile+"/subscriptions.config"),beforeRecoveryResults=bytes(receipt.results);QString diagnostic;
    QVERIFY2(recoverNormal(receipt,conflict,diagnostic),qPrintable(diagnostic));QCOMPARE(bytes(receipt.body),receipt.initialBody);QVERIFY(frozenExistingFiles(receipt));
    if(conflict){
      QCOMPARE(bytes(receipt.results),receipt.initialResults);QCOMPARE(bytes(receipt.profile+"/subscriptions.config"),beforeRecoveryConfig);QCOMPARE(bytes(receipt.profile+"/BiglyBTC.subscription-acquire"),owner);QVERIFY(temporary.alreadyGone||QFile::exists(temporary.path));if(fault=="foreign-state"){QCOMPARE(bytes(statePath(receipt)),foreignState);}
      const auto result=s::parseSubscriptionResults(bytes(receipt.results).toStdString());QVERIFY(result&&result->rows.size()==1&&!result->rows[0].read);diagnostic.clear();QVERIFY2(recoverNormal(receipt,true,diagnostic),qPrintable(diagnostic));QCOMPARE(bytes(receipt.results),beforeRecoveryResults);QCOMPARE(bytes(receipt.profile+"/subscriptions.config"),beforeRecoveryConfig);QCOMPARE(bytes(receipt.profile+"/BiglyBTC.subscription-acquire"),owner);if(fault=="foreign-state"){QCOMPARE(bytes(statePath(receipt)),foreignState);}QVERIFY(frozenExistingFiles(receipt));QVERIFY(temporary.remove());return;
    }
    QVERIFY(!QFile::exists(temporary.path));QVERIFY(absentIntent(receipt));QVERIFY(!QFile::exists(statePath(receipt)+".saving"));
    const auto inventory=core::Document::parse(bytes(receipt.profile+"/downloads.config").toStdString());QVERIFY(inventory);const auto library=core::readLibrary(*inventory);QVERIFY(library);QVERIFY(library->paused.empty());
    if(fault=="proxy"){
      QCOMPARE(bytes(receipt.profile+"/subscriptions.config"),receipt.initialConfiguration);QCOMPARE(bytes(receipt.results),receipt.initialResults);QCOMPARE(bytes(receipt.profile+"/biglybt.config"),proxySettings);QCOMPARE(library->downloads.size(),std::size_t{1});const auto& target=library->downloads.front();QVERIFY(target.hash&&target.hash->bytes().size()==20&&target.originalState==70&&!target.forceStart&&target.downloaded==0);QCOMPARE(QString::fromStdString(target.saveDirectory),receipt.payload);
      const auto actual=bytes(statePath(receipt));if(existing){QCOMPARE(actual,*receipt.initialState);}else{const auto intent=core::Document::parse(owner.toStdString());QVERIFY(intent&&intent->find("state_base_sha256"));QCOMPARE(QCryptographicHash::hash(actual,QCryptographicHash::Sha256).toHex().toStdString(),intent->find("state_base_sha256")->bytes);}
      QVERIFY(!hasSourceSid(actual,receipt.sid));if(const auto backup=optionalBytes(statePath(receipt)+".bak")){QVERIFY(!hasSourceSid(*backup,receipt.sid));}QVERIFY(!QFile::exists(statePath(receipt)+".saving"));QVERIFY(frozenExistingFiles(receipt));
      const auto settledInventory=bytes(receipt.profile+"/downloads.config"),settledArchive=bytes(receipt.profile+"/torrents/"+targetStem()+".torrent");diagnostic.clear();QVERIFY2(recoverNormal(receipt,false,diagnostic),qPrintable(diagnostic));QCOMPARE(bytes(statePath(receipt)),actual);QCOMPARE(bytes(receipt.profile+"/subscriptions.config"),receipt.initialConfiguration);QCOMPARE(bytes(receipt.results),receipt.initialResults);QCOMPARE(bytes(receipt.profile+"/biglybt.config"),proxySettings);QCOMPARE(bytes(receipt.profile+"/downloads.config"),settledInventory);QCOMPARE(bytes(receipt.profile+"/torrents/"+targetStem()+".torrent"),settledArchive);QVERIFY(frozenExistingFiles(receipt));QVERIFY(absentIntent(receipt));QVERIFY(QDir(receipt.payload).entryList(QDir::AllEntries|QDir::NoDotAndDotDot).empty());QVERIFY(!QFile::exists(receipt.profile+"/dlhistorya.config"));QVERIFY(!QFile::exists(receipt.profile+"/dlhistoryd.config"));return;
    }
    if(checkpoint=="BeforeIntake"){
      QVERIFY(library->downloads.empty());QCOMPARE(bytes(receipt.profile+"/downloads.config"),receipt.initialInventory);QCOMPARE(bytes(receipt.profile+"/subscriptions.config"),receipt.initialConfiguration);QCOMPARE(bytes(receipt.results),receipt.initialResults);
    }else{
      QCOMPARE(library->downloads.size(),std::size_t{1});const auto& added=library->downloads.front();QVERIFY(added.hash&&added.hash->bytes().size()==20&&added.persistent&&!added.forceStart&&added.originalState==70&&added.downloaded==0);QCOMPARE(QString::fromStdString(added.saveDirectory),receipt.payload);
      const auto expected=QCryptographicHash::hash(info(),QCryptographicHash::Sha1);const auto hash=QByteArray(reinterpret_cast<const char*>(added.hash->bytes().data()),20);QCOMPARE(hash,expected);
      const auto statePath=receipt.profile+"/active/"+QString::fromLatin1(hash.toHex().toUpper())+".dat";const auto state=core::Document::parse(bytes(statePath).toStdString());QVERIFY(state);const auto* attributes=state->find("attributes");QVERIFY(attributes&&attributes->find("scrapecache")&&attributes->find("scsrc")&&attributes->find("networks"));QCOMPARE(attributes->find("scrapecache")->integer,std::int64_t{4294967298});QCOMPARE(attributes->find("scsrc")->integer,std::int64_t{1});QCOMPARE(attributes->find("networks")->children.size(),std::size_t{1});QCOMPARE(attributes->find("networks")->children[0].bytes,std::string("Public"));
      QVERIFY(hasSourceSid(bytes(statePath),receipt.sid));QVERIFY(optionalBytes(statePath+".bak")==receipt.initialStateBackup);if(receipt.initialStateBackup){QVERIFY(!hasSourceSid(*receipt.initialStateBackup,receipt.sid));}
      const auto* plugin=attributes->find("Plugin.<internal>.azsubs.subs_info");QVERIFY(plugin&&plugin->find("s"));const auto& members=plugin->find("s")->children;
      if(existing){
        const auto original=core::Document::parse(receipt.initialState->toStdString());QVERIFY(original&&original->find("attributes"));QCOMPARE(excluding(state->root(),{"attributes"}),excluding(original->root(),{"attributes"}));const auto* oldAttributes=original->find("attributes");QCOMPARE(excluding(*attributes,{"scsrc","scrapecache","Plugin.<internal>.azsubs.subs_info"}),excluding(*oldAttributes,{"scsrc","scrapecache","Plugin.<internal>.azsubs.subs_info"}));const auto* oldPlugin=oldAttributes->find("Plugin.<internal>.azsubs.subs_info");QVERIFY(oldPlugin&&oldPlugin->find("s"));QCOMPARE(excluding(*plugin,{"s"}),excluding(*oldPlugin,{"s"}));const auto& beforeMembers=oldPlugin->find("s")->children;QCOMPARE(members.size(),beforeMembers.size()+1);for(std::size_t i=0;i<beforeMembers.size();++i){QCOMPARE(members[i].bytes,beforeMembers[i].bytes);}QCOMPARE(members.back().bytes,receipt.sid.toStdString());
      }else{QCOMPARE(members.size(),std::size_t{1});QVERIFY(!plugin->find("lc"));}
      const auto archivePath=QString::fromStdString(added.torrentFile);QVERIFY(within(receipt.profile,archivePath));const auto archive=core::Document::parse(bytes(archivePath).toStdString());QVERIFY(archive&&archive->find("info"));const auto* archivedInfo=archive->find("info");QVERIFY(!archivedInfo->find("meta version")&&!archivedInfo->find("file tree"));QCOMPARE(QByteArray::fromStdString(*core::encodeValue(*archivedInfo)),info());
      const auto config=core::Document::parse(bytes(receipt.profile+"/subscriptions.config").toStdString()),before=core::Document::parse(receipt.initialConfiguration.toStdString());QVERIFY(config&&before);const auto* subscriptions=config->find("subs"),*beforeSubscriptions=before->find("subs");QVERIFY(subscriptions&&beforeSubscriptions&&subscriptions->children.size()==1&&beforeSubscriptions->children.size()==1);const auto& row=subscriptions->children[0];const auto& old=beforeSubscriptions->children[0];const auto* associations=row.find("assoc");QVERIFY(associations&&associations->children.size()==1&&associations->children[0].find("h")&&associations->children[0].find("w"));QCOMPARE(associations->children[0].find("h")->bytes,hash.toStdString());QVERIFY(associations->children[0].find("w")->integer>0);QCOMPARE(excluding(row,{"assoc","history"}),excluding(old,{"assoc","history"}));QVERIFY(row.find("history")&&old.find("history"));QCOMPARE(excluding(*row.find("history"),{"num_read","num_unread"}),excluding(*old.find("history"),{"num_read","num_unread"}));QCOMPARE(row.find("history")->find("num_read")->integer,std::int64_t{1});QCOMPARE(row.find("history")->find("num_unread")->integer,std::int64_t{0});
      const auto results=s::parseSubscriptionResults(bytes(receipt.results).toStdString());QVERIFY(results&&results->rows.size()==1&&results->rows[0].key==Key.toStdString()&&results->rows[0].read&&!results->rows[0].deleted);const auto raw=core::Document::parse(bytes(receipt.results).toStdString()),oldResults=core::Document::parse(receipt.initialResults.toStdString());QVERIFY(raw&&oldResults);QCOMPARE(excluding(raw->root(),{"results"}),excluding(oldResults->root(),{"results"}));QCOMPARE(excluding(raw->find("results")->children[0],{"read"}),excluding(oldResults->find("results")->children[0],{"read"}));
    }
    QVERIFY(QDir(receipt.payload).entryList(QDir::AllEntries|QDir::NoDotAndDotDot).empty());QVERIFY(!QFile::exists(receipt.profile+"/dlhistorya.config"));QVERIFY(!QFile::exists(receipt.profile+"/dlhistoryd.config"));
    const auto settledConfig=bytes(receipt.profile+"/subscriptions.config"),settledResults=bytes(receipt.results),settledInventory=bytes(receipt.profile+"/downloads.config");const auto settledState=optionalBytes(statePath(receipt)),settledArchive=optionalBytes(receipt.profile+"/torrents/"+targetStem()+".torrent");diagnostic.clear();QVERIFY2(recoverNormal(receipt,false,diagnostic),qPrintable(diagnostic));
    QCOMPARE(bytes(receipt.profile+"/subscriptions.config"),settledConfig);QCOMPARE(bytes(receipt.results),settledResults);QCOMPARE(bytes(receipt.profile+"/downloads.config"),settledInventory);QCOMPARE(bytes(receipt.body),receipt.initialBody);QVERIFY(optionalBytes(statePath(receipt))==settledState);QVERIFY(optionalBytes(receipt.profile+"/torrents/"+targetStem()+".torrent")==settledArchive);QVERIFY(frozenExistingFiles(receipt));QVERIFY(absentIntent(receipt));
  }
};
int main(int argc,char** argv){
  QApplication application(argc,argv);
  if(application.arguments().size()==2&&application.arguments()[1]=="--legacy-upgrade"){try{return legacyUpgrade();}catch(const std::exception& error){std::fprintf(stderr,"Local legacy upgrade failed: %.256s\n",error.what());return 12;}}
  if(application.arguments().contains("--legacy-upgrade")){return 2;}
  const auto arguments=application.arguments();if(arguments.size()==3&&arguments[1]=="--acquisition-crash-child"){try{return producer(arguments[2]);}catch(const std::exception& error){std::fprintf(stderr,"Owned acquisition producer failed: %.256s\n",error.what());return 9;}}
  if(arguments.size()==4&&arguments[1]=="--acquisition-crash-child"&&(arguments[2]=="fresh"||arguments[2]=="existing")){try{return producer(arguments[3],arguments[2]=="existing");}catch(const std::exception& error){std::fprintf(stderr,"Owned acquisition producer failed: %.256s\n",error.what());return 9;}}
  if(arguments.contains("--acquisition-crash-child")){return 2;}SubscriptionAcquisitionRecoveryTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "subscription_acquisition_recovery_tests.moc"
