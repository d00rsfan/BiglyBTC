// SPDX-License-Identifier: GPL-2.0-or-later
#include "session_bridge.hpp"
#include "subscription_metainfo_fetch.hpp"
#include "subscription_values.hpp"
#include "subscription_rss_http_fixture.hpp"
#include "library_window.hpp"
#include "biglybtc/library.hpp"
#include <QApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
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
QByteArray bytes(const QString& path){QFile file(path);w::require(file.open(QIODevice::ReadOnly),"Cannot read private recovery fixture");return file.readAll();}
QByteArray info(){return f::dictionary({{"length",f::integer(1)},{"name",w::b("Owned")},
  {"piece length",f::integer(16384)},{"pieces",w::b(QByteArray(20,'x'))}});}
QByteArray metainfo(){return f::dictionary({{"future",w::b("opaque-torrent")},{"info",info()}});}
bool within(const QString& root,const QString& path){const auto relative=QDir(root).relativeFilePath(path);return !relative.isEmpty()&&!QDir::isAbsolutePath(relative)&&relative!=".."&&!relative.startsWith("../");}
template<class Predicate>bool until(Predicate ready,int milliseconds){QElapsedTimer timer;timer.start();while(!ready()&&timer.elapsed()<milliseconds){QCoreApplication::processEvents();QTest::qWait(10);}return ready();}

// Same small private-profile preparation as the actual Bridge producer test.
// No caller profile, URL, hash or serialized intake plan enters this process.
void prepare(f::Profile& area,const std::string& url){
  auto settings=storage::ConfigFile::load(*area.lease,"biglybt.config");w::require(bool(settings),"Private settings missing");
  for(const auto* key:{"Download History Enabled","TCP.Listen.Port.Enable","peercontrol.udp.public.enable","Plugin.DHT.dht.enabled","Plugin.mldht.enable","PluginInfo.mldht.enabled","Peer Source Selection Default.DHT","Peer Source Selection Default.PeerExchange","Peer Source Selection Default.Tracker","Plugin.UPnP.upnp.enable","Plugin.UPnP.natpmp.enable","IPV6 Enable Support"}){w::require(bool(settings->document().replaceValue(key,"i0e")),"Private disabled setting failed");}
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

int producer(const QString& checkpoint){
  if(!Checkpoints.contains(checkpoint)){return 2;}
  w::Server server;server.body=metainfo();f::Profile area(server.url());prepare(area,server.url("/owned.torrent?recovery=1"));
  const auto payload=area.root.filePath("payload");w::require(QDir().mkpath(payload),"Cannot create recovery payload directory");
  const auto receiptPath=area.root.filePath("producer-receipt.json");
  const QJsonObject initial{{"body",QString::fromLatin1(bytes(area.bodyPath).toBase64())},{"configuration",QString::fromLatin1(bytes(area.path+"/subscriptions.config").toBase64())},{"results",QString::fromLatin1(bytes(area.resultsPath).toBase64())},{"inventory",QString::fromLatin1(bytes(area.path+"/downloads.config").toBase64())}};
  const QJsonObject receipt{{"schema","BiglyBTC.Acquisition.CrashProducer.V1"},{"checkpoint",checkpoint},{"profile",area.path},{"body",area.bodyPath},{"results",area.resultsPath},{"payload",payload},{"receipt",receiptPath},{"initial",initial}};
  const auto encoded=QJsonDocument(receipt).toJson(QJsonDocument::Compact);w::write(receiptPath,encoded);const auto output=ReceiptPrefix+encoded+'\n';
  if(std::fwrite(output.constData(),1,static_cast<std::size_t>(output.size()),stdout)!=static_cast<std::size_t>(output.size())||std::fflush(stdout)!=0){return 3;}
  std::atomic<int> made{0};
  app::SessionBridge::SubscriptionAcquisitionFactory factory=[&](const storage::ProfileLease& lease,const std::filesystem::path& path,quint64 session,const ui::SubscriptionKey& key,const QByteArray& result,std::stop_token stop){
    ++made;const s::SavedSubscriptionSelector selected{static_cast<std::size_t>(key.sourceIndex),key.sid.toStdString(),key.publicKey.toStdString(),key.singleton?std::optional{key.singletonKey.toStdString()}:std::nullopt,key.version};
    auto opened=s::SubscriptionMetainfoFetch::openFixture(lease,path,session,selected,result.toStdString(),area.endpoint(),stop);return opened?std::move(*opened):std::unique_ptr<s::SubscriptionMetainfoFetch>{};};
  app::SessionBridge bridge(nullptr,{},{},{},{},{},{},std::move(factory));ui::LibraryWindow window;wire(bridge,window);
  QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),catalogs(&bridge,&app::SessionBridge::subscriptionCatalogReady);
  if(!bridge.openNormal(area.path.toStdString())||!until([&]{return !bridge.busy()&&sessions.size()==1;},12000)||made.load()!=0){return 4;}
  const auto session=sessions.front()[0].toULongLong();if(!bridge.subscriptionCatalog({session,1,1})||!until([&]{return !bridge.busy()&&catalogs.size()==1;},5000)){return 5;}
  const auto catalog=qvariant_cast<ui::SubscriptionCatalogPresentation>(catalogs.front()[0]);if(!catalog||catalog->sources.size()!=1){return 6;}
  qputenv("BIGLYBTC_SUBSCRIPTION_ACQUISITION_CRASH_AT",checkpoint.toUtf8());
  ui::SubscriptionCommand command{{session,1,2,catalog->generation,catalog->sources.front().key,ui::SubscriptionAction::AcquireStopped},{Key},std::make_shared<std::stop_source>(),payload};
  if(!bridge.subscriptionCommand(std::move(command))){return 7;}
  // A successful producer never returns: the actual fixture checkpoint _exit(86)
  // bypasses destructors after the relevant real sync/publication.
  (void)until([]{return false;},28000);return 8;
}

struct Receipt {QString profile,body,results,payload;QByteArray initialBody,initialConfiguration,initialResults,initialInventory;};
std::expected<Receipt,QString> produce(const QString& ownedRoot,const QString& checkpoint){
  const auto temporary=ownedRoot+"/tmp";if(!QDir().mkpath(temporary)){return std::unexpected("Cannot create owned child TMPDIR");}
  auto environment=QProcessEnvironment::systemEnvironment();environment.remove("BIGLYBTC_SUBSCRIPTION_ACQUISITION_CRASH_AT");environment.insert("TMPDIR",temporary);environment.insert("QT_QPA_PLATFORM","offscreen");
  QElapsedTimer lifetime;lifetime.start();QProcess child;child.setProcessEnvironment(environment);child.setProgram(QCoreApplication::applicationFilePath());child.setArguments({"--acquisition-crash-child",checkpoint});child.start();
  if(!child.waitForStarted(3000)){return std::unexpected("Owned recovery child did not start");}
  const bool ended=until([&]{return child.state()==QProcess::NotRunning;},std::max(0,30000-static_cast<int>(lifetime.elapsed())));if(!ended){child.kill();child.waitForFinished(3000);}
  const auto output=child.readAllStandardOutput(),errors=child.readAllStandardError();
  if(!ended||child.exitStatus()!=QProcess::NormalExit||child.exitCode()!=86){return std::unexpected(QString("checkpoint=%1 ended=%2 exit=%3 stderr=%4").arg(checkpoint).arg(ended).arg(child.exitCode()).arg(QString::fromUtf8(errors.left(1024))));}
  QByteArray encoded;int receipts=0;for(const auto& line:output.split('\n')){if(line.startsWith(ReceiptPrefix)){++receipts;encoded=line.mid(ReceiptPrefix.size());}}
  if(receipts!=1||encoded.size()>32768||output.size()>65536||errors.size()>65536){return std::unexpected("Missing or oversized owned producer receipt/output");}
  QJsonParseError error;const auto document=QJsonDocument::fromJson(encoded,&error);if(error.error!=QJsonParseError::NoError||!document.isObject()){return std::unexpected("Invalid producer receipt JSON");}
  const auto object=document.object();if(object.value("schema").toString()!="BiglyBTC.Acquisition.CrashProducer.V1"||object.value("checkpoint").toString()!=checkpoint){return std::unexpected("Producer receipt identity changed");}
  for(const auto* name:{"profile","body","results","payload","receipt"}){const auto path=object.value(QLatin1String(name)).toString();if(!within(temporary,path)||QFileInfo(path).isSymLink()||QFileInfo(path).canonicalFilePath()!=path){return std::unexpected("Producer receipt escaped owned temporary root");}}
  if(bytes(object.value("receipt").toString())!=encoded){return std::unexpected("Producer path receipt changed");}
  const auto initial=object.value("initial").toObject();Receipt receipt{object.value("profile").toString(),object.value("body").toString(),object.value("results").toString(),object.value("payload").toString(),{},{},{},{}};
  auto decode=[&](const char* name){return QByteArray::fromBase64(initial.value(QLatin1String(name)).toString().toLatin1(),QByteArray::AbortOnBase64DecodingErrors);};
  receipt.initialBody=decode("body");receipt.initialConfiguration=decode("configuration");receipt.initialResults=decode("results");receipt.initialInventory=decode("inventory");
  if(receipt.initialBody.isEmpty()||receipt.initialConfiguration.isEmpty()||receipt.initialResults.isEmpty()||receipt.initialInventory.isEmpty()){return std::unexpected("Producer initial bytes missing");}return receipt;
}

// Only the exact recorded, uid-owned, non-symlink directory is eligible for
// fixture cleanup. Healthy recovery must delete it itself. Conflict leaves it
// for review; this guard removes that one owned fixture after assertions.
struct ExternalTemporary {
  QString path;dev_t device=0;ino_t inode=0;
  bool capture(const Receipt& receipt){
    const auto marker=core::Document::parse(bytes(receipt.profile+"/BiglyBTC.subscription-acquire").toStdString());if(!marker){return false;}const auto* node=marker->find("temp_directory");if(!node||node->type!=core::Node::Type::Bytes){return false;}path=QString::fromStdString(std::string(node->bytes));
    const auto prefix=QStringLiteral("/tmp/biglybtc-sub-acq-");const auto suffix=path.mid(prefix.size());
    if(!path.startsWith(prefix)||suffix.size()!=6||!std::ranges::all_of(suffix,[](QChar c){return c.unicode()<128&&c.isLetterOrNumber();})||QFileInfo(path).canonicalFilePath()!=path||QFileInfo(path).isSymLink()){path.clear();return false;}
    struct stat value{};if(::lstat(path.toLocal8Bit().constData(),&value)!=0||!S_ISDIR(value.st_mode)||value.st_uid!=::geteuid()||bytes(path+"/metadata.torrent")!=metainfo()){path.clear();return false;}device=value.st_dev;inode=value.st_ino;return true;
  }
  bool remove(){if(path.isEmpty()){return false;}struct stat value{};if(::lstat(path.toLocal8Bit().constData(),&value)!=0){return !QFileInfo::exists(path);}if(!S_ISDIR(value.st_mode)||value.st_uid!=::geteuid()||value.st_dev!=device||value.st_ino!=inode){return false;}return QDir(path).removeRecursively()&&!QFileInfo::exists(path);}
  ~ExternalTemporary(){if(!path.isEmpty()){(void)remove();}}
};

bool recoverNormal(const Receipt& receipt,bool review,QString& diagnostic){
  std::atomic<int> made{0};
  app::SessionBridge::SubscriptionAcquisitionFactory reject=[&](const storage::ProfileLease&,const std::filesystem::path&,quint64,const ui::SubscriptionKey&,const QByteArray&,std::stop_token){++made;return std::unique_ptr<s::SubscriptionMetainfoFetch>{};};
  app::SessionBridge::SubscriptionFetchFactory rejectScan=[&](const storage::ProfileLease&,const std::filesystem::path&,quint64,const ui::SubscriptionKey&,std::stop_token){++made;return std::unique_ptr<s::RssHttpFetch>{};};
  app::SessionBridge bridge(nullptr,{},{},{},{},{},std::move(rejectScan),std::move(reject));ui::LibraryWindow window;wire(bridge,window);
  QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),snapshots(&bridge,&app::SessionBridge::snapshotReady),messages(&bridge,&app::SessionBridge::messageReady),closed(&bridge,&app::SessionBridge::sessionClosed);
  if(!bridge.openNormal(receipt.profile.toStdString())||!until([&]{return !bridge.busy()&&sessions.size()==1&&!snapshots.empty();},15000)){diagnostic="Normal recovery startup did not finish";return false;}
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
} // namespace

class SubscriptionAcquisitionRecoveryTests final:public QObject {
  Q_OBJECT
private slots:
  void actualNormalStartupRecoversOwnedProcessCheckpoints_data(){
    QTest::addColumn<QString>("checkpoint");QTest::addColumn<bool>("conflict");for(const auto& phase:Checkpoints){QTest::newRow(qPrintable(phase))<<phase<<false;}
    QTest::newRow("FinalPublished-impossible-F-beforeResults")<<QString("FinalPublished")<<true;
  }
  void actualNormalStartupRecoversOwnedProcessCheckpoints(){
    QFETCH(QString,checkpoint);QFETCH(bool,conflict);QTemporaryDir owned;QVERIFY(owned.isValid());
    const auto produced=produce(owned.path(),checkpoint);QVERIFY2(produced.has_value(),qPrintable(produced?QString{}:produced.error()));const auto& receipt=*produced;
    ExternalTemporary temporary;QVERIFY(temporary.capture(receipt));const auto owner=bytes(receipt.profile+"/BiglyBTC.subscription-acquire");
    if(conflict){w::write(receipt.results,receipt.initialResults);}
    const auto beforeRecoveryConfig=bytes(receipt.profile+"/subscriptions.config"),beforeRecoveryResults=bytes(receipt.results);QString diagnostic;
    QVERIFY2(recoverNormal(receipt,conflict,diagnostic),qPrintable(diagnostic));QCOMPARE(bytes(receipt.body),receipt.initialBody);
    if(conflict){
      QCOMPARE(bytes(receipt.results),receipt.initialResults);QCOMPARE(bytes(receipt.profile+"/subscriptions.config"),beforeRecoveryConfig);QCOMPARE(bytes(receipt.profile+"/BiglyBTC.subscription-acquire"),owner);QVERIFY(QFile::exists(temporary.path));
      const auto result=s::parseSubscriptionResults(bytes(receipt.results).toStdString());QVERIFY(result&&result->rows.size()==1&&!result->rows[0].read);diagnostic.clear();QVERIFY2(recoverNormal(receipt,true,diagnostic),qPrintable(diagnostic));QCOMPARE(bytes(receipt.results),beforeRecoveryResults);QCOMPARE(bytes(receipt.profile+"/subscriptions.config"),beforeRecoveryConfig);QCOMPARE(bytes(receipt.profile+"/BiglyBTC.subscription-acquire"),owner);QVERIFY(temporary.remove());return;
    }
    QVERIFY(!QFile::exists(temporary.path));QVERIFY(absentIntent(receipt));
    const auto inventory=core::Document::parse(bytes(receipt.profile+"/downloads.config").toStdString());QVERIFY(inventory);const auto library=core::readLibrary(*inventory);QVERIFY(library);QVERIFY(library->paused.empty());
    if(checkpoint=="BeforeIntake"){
      QVERIFY(library->downloads.empty());QCOMPARE(bytes(receipt.profile+"/downloads.config"),receipt.initialInventory);QCOMPARE(bytes(receipt.profile+"/subscriptions.config"),receipt.initialConfiguration);QCOMPARE(bytes(receipt.results),receipt.initialResults);
    }else{
      QCOMPARE(library->downloads.size(),std::size_t{1});const auto& added=library->downloads.front();QVERIFY(added.hash&&added.hash->bytes().size()==20&&added.persistent&&!added.forceStart&&added.originalState==70&&added.downloaded==0);QCOMPARE(QString::fromStdString(added.saveDirectory),receipt.payload);
      const auto expected=QCryptographicHash::hash(info(),QCryptographicHash::Sha1);const auto hash=QByteArray(reinterpret_cast<const char*>(added.hash->bytes().data()),20);QCOMPARE(hash,expected);
      const auto statePath=receipt.profile+"/active/"+QString::fromLatin1(hash.toHex().toUpper())+".dat";const auto state=core::Document::parse(bytes(statePath).toStdString());QVERIFY(state);const auto* attributes=state->find("attributes");QVERIFY(attributes&&attributes->find("scrapecache")&&attributes->find("scsrc")&&attributes->find("networks"));QCOMPARE(attributes->find("scrapecache")->integer,std::int64_t{4294967298});QCOMPARE(attributes->find("scsrc")->integer,std::int64_t{1});QCOMPARE(attributes->find("networks")->children.size(),std::size_t{1});QCOMPARE(attributes->find("networks")->children[0].bytes,std::string("Public"));
      const auto archivePath=QString::fromStdString(added.torrentFile);QVERIFY(within(receipt.profile,archivePath));const auto archive=core::Document::parse(bytes(archivePath).toStdString());QVERIFY(archive&&archive->find("info"));const auto* archivedInfo=archive->find("info");QVERIFY(!archivedInfo->find("meta version")&&!archivedInfo->find("file tree"));QCOMPARE(QByteArray::fromStdString(*core::encodeValue(*archivedInfo)),info());
      const auto config=core::Document::parse(bytes(receipt.profile+"/subscriptions.config").toStdString()),before=core::Document::parse(receipt.initialConfiguration.toStdString());QVERIFY(config&&before);const auto* subscriptions=config->find("subs"),*beforeSubscriptions=before->find("subs");QVERIFY(subscriptions&&beforeSubscriptions&&subscriptions->children.size()==1&&beforeSubscriptions->children.size()==1);const auto& row=subscriptions->children[0];const auto& old=beforeSubscriptions->children[0];const auto* associations=row.find("assoc");QVERIFY(associations&&associations->children.size()==1&&associations->children[0].find("h")&&associations->children[0].find("w"));QCOMPARE(associations->children[0].find("h")->bytes,hash.toStdString());QVERIFY(associations->children[0].find("w")->integer>0);QCOMPARE(excluding(row,{"assoc","history"}),excluding(old,{"assoc","history"}));QVERIFY(row.find("history")&&old.find("history"));QCOMPARE(excluding(*row.find("history"),{"num_read","num_unread"}),excluding(*old.find("history"),{"num_read","num_unread"}));QCOMPARE(row.find("history")->find("num_read")->integer,std::int64_t{1});QCOMPARE(row.find("history")->find("num_unread")->integer,std::int64_t{0});
      const auto results=s::parseSubscriptionResults(bytes(receipt.results).toStdString());QVERIFY(results&&results->rows.size()==1&&results->rows[0].key==Key.toStdString()&&results->rows[0].read&&!results->rows[0].deleted);const auto raw=core::Document::parse(bytes(receipt.results).toStdString()),oldResults=core::Document::parse(receipt.initialResults.toStdString());QVERIFY(raw&&oldResults);QCOMPARE(excluding(raw->root(),{"results"}),excluding(oldResults->root(),{"results"}));QCOMPARE(excluding(raw->find("results")->children[0],{"read"}),excluding(oldResults->find("results")->children[0],{"read"}));
    }
    QVERIFY(QDir(receipt.payload).entryList(QDir::AllEntries|QDir::NoDotAndDotDot).empty());QVERIFY(!QFile::exists(receipt.profile+"/dlhistorya.config"));QVERIFY(!QFile::exists(receipt.profile+"/dlhistoryd.config"));
    const auto settledConfig=bytes(receipt.profile+"/subscriptions.config"),settledResults=bytes(receipt.results),settledInventory=bytes(receipt.profile+"/downloads.config");diagnostic.clear();QVERIFY2(recoverNormal(receipt,false,diagnostic),qPrintable(diagnostic));
    QCOMPARE(bytes(receipt.profile+"/subscriptions.config"),settledConfig);QCOMPARE(bytes(receipt.results),settledResults);QCOMPARE(bytes(receipt.profile+"/downloads.config"),settledInventory);QCOMPARE(bytes(receipt.body),receipt.initialBody);QVERIFY(absentIntent(receipt));
  }
};
int main(int argc,char** argv){
  QApplication application(argc,argv);
  const auto arguments=application.arguments();if(arguments.size()==3&&arguments[1]=="--acquisition-crash-child"){try{return producer(arguments[2]);}catch(const std::exception& error){std::fprintf(stderr,"Owned acquisition producer failed: %.256s\n",error.what());return 9;}}
  if(arguments.contains("--acquisition-crash-child")){return 2;}SubscriptionAcquisitionRecoveryTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "subscription_acquisition_recovery_tests.moc"
