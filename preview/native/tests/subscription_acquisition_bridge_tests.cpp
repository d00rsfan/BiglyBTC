// SPDX-License-Identifier: GPL-2.0-or-later
#include "session_bridge.hpp"
#include "torrent_intake.hpp"
#include "subscription_metainfo_fetch.hpp"
#include "library_window.hpp"
#include "subscription_values.hpp"
#include "torrent_state.hpp"
#include "subscription_rss_http_fixture.hpp"
#include "biglybtc/library.hpp"
#include "biglybtc/saved_download_history.hpp"
#include <QAction>
#include <QApplication>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <cstdio>
#include <optional>
#include <map>
#include <QDateTime>
#include <QSignalSpy>
#include <QUdpSocket>
#include <QUrl>
#include <QtTest>
namespace {
using namespace biglybtc;namespace f=subscription_http_fixture;namespace w=rss_fixture;namespace s=search;
const QByteArray Key="result-key";
QByteArray bytes(const QString& path){QFile file(path);w::require(file.open(QIODevice::ReadOnly),"Cannot read private acquisition fixture");return file.readAll();}
QByteArray metainfo(int kind=0){
  const auto v1=f::dictionary({{"length",f::integer(1)},{"name",w::b("Owned")},{"piece length",f::integer(16384)},{"pieces",w::b(QByteArray(20,'x'))}});
  if(kind==1){return f::dictionary({{"info",f::dictionary({{"file tree",f::dictionary({{"Owned",f::dictionary({{"",f::dictionary({{"length",f::integer(1)},{"pieces root",w::b(QByteArray(32,'y'))}})}})}})},{"meta version",f::integer(2)},{"name",w::b("Owned")},{"piece length",f::integer(16384)}})}});}
  if(kind==2){auto info=core::Document::parse(v1.toStdString());w::require(bool(info)&&bool(info->replaceValue("meta version","i2e"))&&bool(info->replaceValue("file tree",f::dictionary({{"Owned",f::dictionary({{"",f::dictionary({{"length",f::integer(1)},{"pieces root",w::b(QByteArray(32,'y'))}})}})}}).toStdString())),"Cannot create actual hybrid metadata");return f::dictionary({{"info",QByteArray::fromStdString(info->serialize())}});}
  return f::dictionary({{"future",w::b("opaque-torrent")},{"info",v1}});
}
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

void prepare(f::Profile& area,const std::string& url,bool wrongHash=false,bool historyEnabled=false){
  const auto normalPort=isolatedNormalPort(url);
  auto settings=storage::ConfigFile::load(*area.lease,"biglybt.config");w::require(bool(settings),"Private settings missing");
  for(const auto* key:{"Download History Enabled","TCP.Listen.Port.Enable","peercontrol.udp.public.enable","Plugin.DHT.dht.enabled","Plugin.mldht.enable","PluginInfo.mldht.enabled","Peer Source Selection Default.DHT","Peer Source Selection Default.PeerExchange","Peer Source Selection Default.Tracker","Plugin.UPnP.upnp.enable","Plugin.UPnP.natpmp.enable","IPV6 Enable Support"}){w::require(bool(settings->document().replaceValue(key,"i0e")),"Private disabled setting failed");}
  w::require(bool(settings->document().replaceValue("Download History Enabled",historyEnabled?"i1e":"i0e")),"Private history policy failed");
  for(const auto* key:{"TCP.Listen.Port","UDP.Listen.Port","UDP.NonData.Listen.Port"}){w::require(bool(settings->document().replaceValue(key,f::integer(normalPort).toStdString())),"Private isolated listener port failed");}
  w::require(bool(settings->document().replaceValue("Bind IP","9:127.0.0.1"))&&bool(settings->document().replaceValue("Enforce Bind IP","i1e"))&&bool(settings->save(*area.lease)),"Private networking isolation failed");
  w::require(bool(storage::ConfigFile::createMissing(*area.lease,"downloads.config","d9:downloadsle6:future3:rawe"))&&bool(storage::ConfigFile::createMissing(*area.lease,"tables.config","de")),"Private empty inventory failed");
  auto metadata=core::Document::parse(area.metadataRow.toStdString());w::require(bool(metadata),"Private signed metadata missing");const auto* old=metadata->find("history");w::require(old,"Private history missing");auto history=core::Document::parse(*core::encodeValue(*old));w::require(bool(history)&&bool(history->replaceValue("last_scan",f::integer(QDateTime::currentMSecsSinceEpoch()).toStdString()))&&bool(history->replaceValue("num_unread","i1e"))&&bool(metadata->replaceValue("history",history->serialize())),"Private history setup failed");area.metadataRow=QByteArray::fromStdString(metadata->serialize());area.configuration=f::dictionary({{"subs","l"+area.metadataRow+"e"}});w::write(area.path+"/subscriptions.config",area.configuration);
  QJsonObject result{{"n","Owned"},{"lb","1"},{"s","4294967297"},{"p","2"},{"tf","1"},{"ts","0"},{"dl",QString::fromStdString(url)},{"future","retain-json"}};if(wrongHash){result["h"]=QString(32,'A');}
  w::write(area.resultsPath,f::dictionary({{"future_results",w::b("retain-results")},{"results","l"+f::dictionary({{"key",w::b(Key)},{"read",f::integer(0)},{"future_row",w::b("retain-row")},{"result_json",w::b(QJsonDocument(result).toJson(QJsonDocument::Compact))},{"tz",f::integer(1)}})+"e"}}));area.lease.reset();
}
void wire(app::SessionBridge& bridge,ui::LibraryWindow& window,bool preview=false){using B=app::SessionBridge;using W=ui::LibraryWindow;window.setOfflinePreview(preview);QObject::connect(&bridge,&B::snapshotReady,&window,&W::setPresentation);QObject::connect(&bridge,&B::columnLayoutSessionReady,&window,&W::beginColumnLayoutSession);QObject::connect(&bridge,&B::busyChanged,&window,&W::setBusy);QObject::connect(&bridge,&B::subscriptionCatalogReady,&window,&W::setSubscriptionCatalog);QObject::connect(&bridge,&B::subscriptionReady,&window,&W::setSubscriptionResult);QObject::connect(&window,&W::subscriptionCatalogRequested,&bridge,[&bridge](ui::SubscriptionCatalogKey key){bridge.subscriptionCatalog(key);});QObject::connect(&window,&W::subscriptionCommandRequested,&bridge,[&bridge](ui::SubscriptionCommand command){bridge.subscriptionCommand(std::move(command));});QObject::connect(&window,&W::subscriptionCancelRequested,&bridge,&B::cancelSubscription);}
app::SessionBridge::SubscriptionAcquisitionFactory factory(f::Profile& area,std::atomic<int>& made,int wrong=0){return [&area,&made,wrong](const storage::ProfileLease& lease,const std::filesystem::path& path,quint64 session,const ui::SubscriptionKey& key,const QByteArray& result,std::stop_token stop){++made;auto selected=area.selector();selected={static_cast<std::size_t>(key.sourceIndex),key.sid.toStdString(),key.publicKey.toStdString(),key.singleton?std::optional{key.singletonKey.toStdString()}:std::nullopt,key.version};auto opened=s::SubscriptionMetainfoFetch::openFixture(lease,path,session,selected,wrong==1?std::string(10,'q'):result.toStdString(),area.endpoint(),wrong==2?std::stop_token{}:stop);return opened?std::move(*opened):std::unique_ptr<s::SubscriptionMetainfoFetch>{};};}
ui::SubscriptionPresentation last(const QSignalSpy& packets){return qvariant_cast<ui::SubscriptionPresentation>(packets.last()[0]);}
bool terminal(const QSignalSpy& packets){return !packets.empty()&&last(packets)->key.action==ui::SubscriptionAction::AcquireStopped&&last(packets)->state!=ui::SubscriptionState::Started;}
QString terminalDiagnostic(const QSignalSpy& packets,int made,const w::Server& server,const QSignalSpy& notices){
  QString recent;const qsizetype first=notices.size()>3?notices.size()-3:0;
  for(qsizetype n=first;n<notices.size();++n){recent+=QString(" |messageReady=%1").arg(notices.at(n).at(0).toString().left(384));}
  if(packets.empty()){return QString("No terminal packets; factories=%1 GETs=%2").arg(made).arg(server.requests.size())+recent;}
  const auto packet=last(packets);return QString("state=%1 committed=%2 factories=%3 GETs=%4 packets=%5 message=%6").arg(static_cast<int>(packet->state)).arg(packet->committed).arg(made).arg(server.requests.size()).arg(packets.size()).arg(packet->message.left(1024))+recent;
}
ui::SubscriptionCommand command(const QSignalSpy& catalogs,const QString& destination,quint64 offset=100){const auto catalog=qvariant_cast<ui::SubscriptionCatalogPresentation>(catalogs.last()[0]);return {{catalog->key.session,catalog->key.page,catalog->key.request+offset,catalog->generation,catalog->sources.front().key,ui::SubscriptionAction::AcquireStopped},{Key},std::make_shared<std::stop_source>(),destination};}
QString destination(f::Profile& area){const auto path=area.root.filePath("payload");w::require(QDir().mkpath(path),"Cannot create private save directory");return path;}
struct ExistingFiles {core::TorrentHash hash;QString archivePath,statePath;QByteArray inventory,archive,state;};
ExistingFiles existingStopped(f::Profile& area,const QString& save,int mode){
  auto held=storage::ProfileLease::acquire(area.path.toStdString());w::require(bool(held),"Cannot own actual existing-target fixture");
  const auto input=area.root.filePath("existing-input.torrent");w::write(input,metainfo());auto hash=storage::importTorrent(*held,input.toStdString(),save.toStdString(),true);w::require(bool(hash),"Genuine prior stopped intake failed");
  const auto encoded=QByteArray(reinterpret_cast<const char*>(hash->bytes().data()),static_cast<qsizetype>(hash->bytes().size()));const auto upper=QString::fromLatin1(encoded.toHex().toUpper());const auto statePath=area.path+"/active/"+upper+".dat",archivePath=area.path+"/torrents/"+upper+".torrent";
  auto store=storage::TorrentStateStore::open(*held);w::require(bool(store),"Actual existing state store failed");auto state=store->loadAttributes(*held,*hash);if(!state){throw std::runtime_error("Actual existing attribute snapshot failed: code="+std::to_string(static_cast<int>(state.error().code))+" "+state.error().message);}const auto* attributes=state->document().find("attributes");auto encodedAttributes=core::encodeValue(*attributes);w::require(bool(encodedAttributes),"Actual existing attributes missing");auto attrs=core::Document::parse(*encodedAttributes);w::require(bool(attrs)&&bool(attrs->replaceValue("fixture_existing_unknown","li17e3:rawe")),"Cannot retain unknown existing state");
  if(mode==1){w::require(bool(attrs->replaceValue("Plugin.<internal>.azsubs.subs_info",f::dictionary({{"lc",f::integer(17)},{"s","l"+w::b("other12345")+w::b(area.shortId)+"e"},{"unknown",w::b("retained-map")}}).toStdString())),"Cannot seed actual existing membership");}
  if(mode==3){w::require(bool(attrs->replaceValue("scsrc","i0e"))&&bool(attrs->replaceValue("scrapecache","i999e")),"Cannot seed real prior raw cache");}
  auto saved=store->saveAttributes(*held,*state,attrs->serialize());if(!saved){throw std::runtime_error("Actual existing attribute publication failed: code="+std::to_string(static_cast<int>(saved.error().code))+" "+saved.error().message);}
  if(mode>=2){auto config=storage::ConfigFile::load(*held,"subscriptions.config");w::require(bool(config),"Actual subscription configuration missing");const auto* rows=config->document().find("subs");auto first=core::encodeValue(rows->children[0]);w::require(bool(first),"Actual saved subscription row missing");auto selected=core::Document::parse(*first);w::require(bool(selected)&&bool(selected->replaceValue("assoc",("l"+f::dictionary({{"h",w::b(encoded)},{"w",f::integer(123)}})+"e").toStdString()))&&bool(config->document().replaceValue("subs","l"+selected->serialize()+"e"))&&bool(config->save(*held)),"Cannot prepare genuine duplicate subscription association");}
  auto results=core::Document::parse(bytes(area.resultsPath).toStdString());w::require(bool(results),"Actual result fixture missing");const auto* rows=results->find("results");auto first=core::encodeValue(rows->children[0]);w::require(bool(first),"Actual result row missing");auto result=core::Document::parse(*first);auto object=QJsonDocument::fromJson(QByteArray::fromStdString(std::string(result->find("result_json")->bytes))).object();if(mode==3){object.remove("s");object.remove("p");}else{object["p"]="2147483648";}w::require(bool(result->replaceValue("result_json",w::b(QJsonDocument(object).toJson(QJsonDocument::Compact)).toStdString()))&&bool(results->replaceValue("results","l"+result->serialize()+"e")),"Cannot preserve actual saved raw counts");w::write(area.resultsPath,QByteArray::fromStdString(results->serialize()));
  return {*hash,archivePath,statePath,bytes(area.path+"/downloads.config"),bytes(archivePath),bytes(statePath)};
}
void absentIntent(f::Profile& area){for(const auto* name:{"BiglyBTC.subscription-acquire","BiglyBTC.torrent-import",".subscriptions-native-write"}){for(const auto* suffix:{"",".saving",".bak"}){w::require(!QFile::exists(area.path+"/"+name+suffix),"Unexpected acquisition journal retained");}}}
}
#if defined(BIGLYBTC_RSS_FETCH_TEST_FIXTURE)
// Local one-shot value handback only. This path is an output owner, never a
// caller-supplied profile/URL or production effect permission.
const QString StateHandbackParent=QStringLiteral("/home/yurii/Java/BiglyBT/build/i2p-discovery-20261101/p08-rss-fetch-20261106/next-subscriptions-20261106/acquisition-source-20261106/next-existing-download-study/original-state-handback/owned-native-fixture");
const QString StateHandbackReceipt=StateHandbackParent+"/native-state-export.json";
constexpr const char* StateHandbackEnvironment="BIGLYBTC_ORIGINAL_STATE_FIXTURE_EXPORT";
QString digest(const QByteArray& value){return QString::fromLatin1(QCryptographicHash::hash(value,QCryptographicHash::Sha256).toHex());}
QString canonicalDirectory(const QString& path){const QFileInfo file(path);w::require(file.isDir()&&!file.isSymLink()&&file.canonicalFilePath()==QDir::cleanPath(path),"Unsafe handback owner directory");return file.canonicalFilePath();}
void handbackParent(){
  canonicalDirectory(StateHandbackParent);const auto temporary=canonicalDirectory(StateHandbackParent+"/tmp");
  w::require(qEnvironmentVariable("TMPDIR")==temporary&&QDir::tempPath()==temporary,"Handback requires its exact private TMPDIR");
}
QJsonObject filePin(const QString& path){const QFileInfo file(path);w::require(file.isFile()&&!file.isSymLink()&&file.size()<=64*1024*1024,"Unsafe handback fixture file");const auto value=bytes(path);return {{"bytes",QString::number(value.size())},{"sha256",digest(value)}};}
void equalPin(const QString& path,const QJsonObject& pin){w::require(filePin(path)==pin,"Handback changed an immutable fixture file");}
void stateCandidateAbsence(const QString& profile,const QString& stem){
  for(const auto* name:{"BiglyBTC.subscription-acquire","BiglyBTC.torrent-import",".subscriptions-native-write"}){
    for(const auto* suffix:{"",".saving",".bak"}){w::require(!QFileInfo::exists(profile+"/"+name+suffix),"Pending handback owner intent");}
  }
  for(const auto* suffix:{".saving","._az"}){w::require(!QFileInfo::exists(profile+"/active/"+stem+".dat"+suffix),"Pending handback state writer");}
}
void exportOriginalStateFixture(f::Profile& area,const QString& save,const QByteArray& hash){
  if(qEnvironmentVariableIsEmpty(StateHandbackEnvironment)){return;}
  w::require(qEnvironmentVariable(StateHandbackEnvironment)=="qualified-fresh-v1","Unknown handback export mode");handbackParent();
  const auto root=canonicalDirectory(area.root.path()),profile=canonicalDirectory(area.path);w::require(root.startsWith(StateHandbackParent+"/tmp/")&&profile==root+"/profile"&&!area.lease,"Fixture escaped owned generated root");
  {auto probe=storage::ProfileLease::acquire(profile.toStdString());w::require(bool(probe),"Native did not release the export lease");}
  const auto stem=QString::fromLatin1(hash.toHex().toUpper());w::require(hash.size()==20&&area.shortId.size()==10,"Unexpected fixture identity");stateCandidateAbsence(profile,stem);
  QJsonObject pins;QDirIterator files(profile,QDir::Files|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);qsizetype count=0;quint64 total=0;
  while(files.hasNext()){const auto path=files.next();const auto info=QFileInfo(path);w::require(!info.isSymLink()&&info.isFile()&&++count<=128,"Unbounded handback fixture inventory");total+=static_cast<quint64>(info.size());w::require(total<=128ULL*1024*1024,"Handback fixture aggregate bound");pins.insert(QDir(profile).relativeFilePath(path),filePin(path));}
  const auto statePath=profile+"/active/"+stem+".dat";const auto state=bytes(statePath);w::require(state.size()<=256*1024,"Handback state receipt bound");
  const QJsonObject receipt{{"schema","BiglyBTC.OwnedOriginalStateFixture.V1"},{"fixture_root",root},{"profile",profile},{"payload",save},{"hash",stem},{"selected_sid",QString::fromLatin1(area.shortId.toHex().toUpper())},{"scsrc","1"},{"scrapecache","4294967298"},{"state_before_base64",QString::fromLatin1(state.toBase64())},{"profile_files_before_Java",pins},{"body_relative",QDir(profile).relativeFilePath(area.bodyPath)},{"results_relative",QDir(profile).relativeFilePath(area.resultsPath)},{"Native_joined_and_lease_released",true}};
  const auto encoded=QJsonDocument(receipt).toJson(QJsonDocument::Compact);w::require(encoded.size()<=1024*1024,"Handback receipt bound");QFile output(StateHandbackReceipt);w::require(output.open(QIODevice::WriteOnly|QIODevice::NewOnly)&&output.write(encoded+"\n")==encoded.size()+1&&output.flush(),"Cannot publish new handback receipt");output.close();area.root.setAutoRemove(false);
  std::puts("PASS_NATIVE_PUBLIC_STATE_FIXTURE_EXPORTED");
}
// Exact bencode values: dictionaries have unique unordered keys; lists stay ordered.
bool sameHandbackValue(const core::Node& a,const core::Node& b,unsigned depth=0){
  if(depth>128||a.type!=b.type){return false;}
  if(a.type==core::Node::Type::Integer){return a.integer==b.integer;}
  if(a.type==core::Node::Type::Bytes){return a.bytes==b.bytes;}
  if(a.children.size()!=b.children.size()){return false;}
  if(a.type==core::Node::Type::List){
    for(std::size_t n=0;n<a.children.size();++n){if(!sameHandbackValue(a.children[n],b.children[n],depth+1)){return false;}}
    return true;
  }
  if(a.type!=core::Node::Type::Dictionary||a.children.size()%2!=0){return false;}
  std::map<std::string_view,const core::Node*> left,right;
  for(std::size_t n=0;n<a.children.size();n+=2){
    if(a.children[n].type!=core::Node::Type::Bytes||b.children[n].type!=core::Node::Type::Bytes||!left.emplace(a.children[n].bytes,&a.children[n+1]).second||!right.emplace(b.children[n].bytes,&b.children[n+1]).second){return false;}
  }
  for(const auto& [key,value]:left){const auto found=right.find(key);if(found==right.end()||!sameHandbackValue(*value,*found->second,depth+1)){return false;}}
  return true;
}
std::string_view rawHandbackInfo(const core::Document& document){
  const auto* info=document.find("info");w::require(info&&info->type==core::Node::Type::Dictionary&&!info->changed&&info->begin<info->end&&info->end<=document.originalBytes().size(),"Missing actual unchanged raw info");
  return std::string_view(document.originalBytes()).substr(info->begin,info->end-info->begin);
}
int originalStateReturn(){
  w::require(qEnvironmentVariableIsEmpty(StateHandbackEnvironment),"Readback is separate from export");handbackParent();const auto encoded=bytes(StateHandbackReceipt);w::require(encoded.size()<=1024*1024,"Handback receipt bound");const auto json=QJsonDocument::fromJson(encoded);w::require(json.isObject(),"Invalid handback receipt");const auto receipt=json.object();w::require(receipt.value("schema")=="BiglyBTC.OwnedOriginalStateFixture.V1"&&receipt.value("Native_joined_and_lease_released").toBool(),"Wrong handback fixture schema");
  const auto root=canonicalDirectory(receipt.value("fixture_root").toString()),profile=canonicalDirectory(receipt.value("profile").toString());w::require(root.startsWith(StateHandbackParent+"/tmp/")&&profile==root+"/profile","Readback escaped owned generated fixture");
  const auto stem=receipt.value("hash").toString();const auto sid=QByteArray::fromHex(receipt.value("selected_sid").toString().toLatin1()),rawHash=QByteArray::fromHex(stem.toLatin1());w::require(rawHash.size()==20&&stem==QString::fromLatin1(rawHash.toHex().toUpper())&&sid.size()==10&&receipt.value("selected_sid").toString()==QString::fromLatin1(sid.toHex().toUpper()),"Invalid handback identity");stateCandidateAbsence(profile,stem);
  {auto lease=storage::ProfileLease::acquire(profile.toStdString());w::require(bool(lease),"Cannot own returned Native fixture");auto store=storage::TorrentStateStore::open(*lease);w::require(bool(store),"Cannot open returned Native state store");auto hash=core::TorrentHash::parse(rawHash.toStdString());w::require(bool(hash),"Returned hash invalid");auto actual=store->loadAttributes(*lease,*hash);w::require(bool(actual)&&!actual->recoveredBackup(),"Returned primary state was not read");
  const auto before=core::Document::parse(QByteArray::fromBase64(receipt.value("state_before_base64").toString().toLatin1()).toStdString());w::require(bool(before),"Missing genuine original Native state");const auto* oldAttrs=before->find("attributes");const auto* attrs=actual->document().find("attributes");w::require(oldAttrs&&attrs&&attrs->find("scsrc")&&attrs->find("scrapecache")&&attrs->find("scsrc")->integer==1&&attrs->find("scrapecache")->integer==4294967298LL,"Public returned cache mismatch");
  const auto* oldMap=oldAttrs->find("Plugin.<internal>.azsubs.subs_info");const auto* map=attrs->find("Plugin.<internal>.azsubs.subs_info");w::require(oldMap&&map,"Returned SID map missing");auto expected=core::Document::parse(*core::encodeValue(*oldMap));w::require(bool(expected)&&bool(expected->replaceValue("native_original_handback",w::b("public-DownloadManagerState-V1").toStdString()))&&sameHandbackValue(*map,expected->root()),"Returned SID order/opaque map changed");
  const auto* beforeResume=before->find("resume");const auto* returnedResume=actual->document().find("resume");w::require(beforeResume&&returnedResume&&beforeResume->type==core::Node::Type::Dictionary&&returnedResume->type==core::Node::Type::Dictionary,"Actual fresh resume section missing");const auto* beforeResumeData=beforeResume->find("data");w::require(beforeResumeData&&beforeResumeData->type==core::Node::Type::Dictionary&&beforeResumeData->find("resume data")&&beforeResumeData->find("resume data")->type==core::Node::Type::Bytes,"Actual Native resume piece flags missing");
  w::require(rawHandbackInfo(*before)==rawHandbackInfo(actual->document()),"Returned raw info bytes changed");
  for(const auto* key:{"resume","resume_history","tracker_cache","future"}){const auto* a=before->find(key);const auto* b=actual->document().find(key);w::require((a==nullptr)==(b==nullptr)&&(!a||sameHandbackValue(*a,*b)),"Returned resume/opaque property changed");}
  const auto pins=receipt.value("profile_files_before_Java").toObject();for(const auto& name:QStringList{"downloads.config","torrents/"+stem+".torrent","subscriptions.config",receipt.value("body_relative").toString(),receipt.value("results_relative").toString()}){w::require(!name.startsWith("/")&&!name.split('/').contains("..")&&pins.contains(name),"Invalid immutable return path");equalPin(profile+"/"+name,pins.value(name).toObject());}
  w::require(bytes(profile+"/active/"+stem+".dat.bak")==QByteArray::fromBase64(receipt.value("state_before_base64").toString().toLatin1()),"Original did not retain exact previous primary backup");const auto result=QJsonDocument::fromJson(bytes(profile+"/original-download-state-handback.json")).object();w::require(result.value("status")=="PASS_PUBLIC_STATE_PROPERTY_HANDBACK"&&result.value("hash")==stem&&result.value("selected_sid")==receipt.value("selected_sid")&&result.value("cache_after")=="4294967298"&&result.value("state_after_sha256").toString().toLower()==digest(bytes(profile+"/active/"+stem+".dat"))&&result.value("state_before_sha256").toString().toLower()==digest(QByteArray::fromBase64(receipt.value("state_before_base64").toString().toLatin1())),"Missing public Original handback proof");
  }{auto probe=storage::ProfileLease::acquire(profile.toStdString());w::require(bool(probe),"Readback retained profile aliases");}
  std::puts("PASS_NATIVE_PUBLIC_STATE_HANDBACK_RETURN");return 0;
}
#endif

class SubscriptionAcquisitionBridgeTests final:public QObject {
 Q_OBJECT
private slots:
 void actualNormalStoppedImportAssociatesAndReadsWithoutStarting(){
  w::Server server;server.body=metainfo();f::Profile area(server.url());prepare(area,server.url("/owned.torrent?literal=1"),false,true);const auto body=bytes(area.bodyPath),beforeConfig=bytes(area.path+"/subscriptions.config");const auto save=destination(area);std::atomic<int> made{0};app::SessionBridge bridge(nullptr,{},{},{},{},{},{},factory(area,made));ui::LibraryWindow window;wire(bridge,window);QSignalSpy notices(&bridge,&app::SessionBridge::messageReady);
  QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),catalogs(&bridge,&app::SessionBridge::subscriptionCatalogReady),packets(&bridge,&app::SessionBridge::subscriptionReady),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(bridge.openNormal(area.path.toStdString()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.count()==1,15000);QCOMPARE(made.load(),0);window.findChild<QAction*>("classic-subscriptions")->trigger();QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!catalogs.empty()&&!packets.empty(),10000);QCOMPARE(made.load(),0);QVERIFY(bridge.subscriptionCommand(command(catalogs,save)));
  const auto diagnostic=[&]{return QString("factories=%1 GETs=%2 packets=%3 message=%4").arg(made.load()).arg(server.requests.size()).arg(packets.size()).arg(packets.empty()?QString{}:last(packets)->message);};QTRY_VERIFY2_WITH_TIMEOUT(terminal(packets),qPrintable(diagnostic()),15000);QVERIFY2(last(packets)->state==ui::SubscriptionState::Ready,qPrintable(terminalDiagnostic(packets,made.load(),server,notices)));QVERIFY(last(packets)->committed);QCOMPARE(made.load(),1);QCOMPARE(server.requests.size(),qsizetype{1});QVERIFY(server.requests[0].startsWith("GET /owned.torrent?literal=1 HTTP/1.1\r\n"));QVERIFY(!server.requests[0].contains("info_hash="));
  const auto inventory=core::Document::parse(bytes(area.path+"/downloads.config").toStdString());QVERIFY(inventory);auto library=core::readLibrary(*inventory);QVERIFY(library&&library->downloads.size()==1);const auto& added=library->downloads.front();QVERIFY(added.hash&&!added.forceStart&&added.originalState==70&&added.downloaded==0);QCOMPARE(QString::fromStdString(added.saveDirectory),save);QVERIFY(QDir(save).entryList(QDir::AllEntries|QDir::NoDotAndDotDot).empty());
  const auto hash=QByteArray(reinterpret_cast<const char*>(added.hash->bytes().data()),static_cast<qsizetype>(added.hash->bytes().size()));const auto state=core::Document::parse(bytes(area.path+"/active/"+QString::fromLatin1(hash.toHex().toUpper())+".dat").toStdString());QVERIFY(state);const auto* attrs=state->find("attributes");QVERIFY(attrs&&attrs->find("scsrc")&&attrs->find("scrapecache"));QCOMPARE(attrs->find("scsrc")->integer,std::int64_t{1});QCOMPARE(attrs->find("scrapecache")->integer,std::int64_t{4294967298});QCOMPARE(attrs->find("networks")->children[0].bytes,std::string("Public"));const auto* callback=attrs->find("Plugin.<internal>.azsubs.subs_info");QVERIFY(callback&&callback->find("s")&&callback->find("s")->children.size()==1);QCOMPARE(callback->find("s")->children[0].bytes,area.shortId.toStdString());
  const auto history=core::readSavedDownloadHistory(bytes(area.path+"/dlhistorya.config").toStdString());QVERIFY(history&&history->records.size()==1&&history->records[0].hash==hash.toStdString());QCOMPARE(history->records[0].completeTime,std::int64_t{0});
  auto config=core::Document::parse(bytes(area.path+"/subscriptions.config").toStdString());auto prior=core::Document::parse(beforeConfig.toStdString());QVERIFY(config&&prior);const auto& row=config->find("subs")->children[0];QCOMPARE(row.find("assoc")->children.size(),std::size_t{1});QCOMPARE(row.find("assoc")->children[0].find("h")->bytes,hash.toStdString());QCOMPARE(row.find("history")->find("last_scan")->integer,prior->find("subs")->children[0].find("history")->find("last_scan")->integer);QCOMPARE(row.find("fixture_future")->bytes,std::string("preserve"));auto results=s::parseSubscriptionResults(bytes(area.resultsPath).toStdString());QVERIFY(results&&results->rows.size()==1&&results->rows[0].read&&!results->rows[0].deleted);QCOMPARE(bytes(area.bodyPath),body);absentIntent(area);
  QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,15000);QVERIFY(storage::ProfileLease::acquire(area.path.toStdString()));QVERIFY(server.failure.isEmpty());
#if defined(BIGLYBTC_RSS_FETCH_TEST_FIXTURE)
  exportOriginalStateFixture(area,save,hash);
#endif
 }
 void cancelBeforeBodyCommitAndFreshAttemptRecovers(){
  w::Server server;server.body=metainfo();server.holdBody=true;f::Profile area(server.url());prepare(area,server.url("/owned.torrent"));const auto config=bytes(area.path+"/subscriptions.config"),results=bytes(area.resultsPath),inventory=bytes(area.path+"/downloads.config");const auto save=destination(area);std::atomic<int> made{0};app::SessionBridge bridge(nullptr,{},{},{},{},{},{},factory(area,made));ui::LibraryWindow window;wire(bridge,window);QSignalSpy notices(&bridge,&app::SessionBridge::messageReady);QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),catalogs(&bridge,&app::SessionBridge::subscriptionCatalogReady),packets(&bridge,&app::SessionBridge::subscriptionReady),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(bridge.openNormal(area.path.toStdString()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.size()==1,15000);window.findChild<QAction*>("classic-subscriptions")->trigger();QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!catalogs.empty()&&!packets.empty(),10000);auto first=command(catalogs,save);QVERIFY(bridge.subscriptionCommand(first));QTRY_COMPARE_WITH_TIMEOUT(server.bodyPrefixBytes,1,10000);first.receiver->request_stop();bridge.cancelSubscription(first.key);QTRY_VERIFY_WITH_TIMEOUT(terminal(packets),10000);QCOMPARE(last(packets)->state,ui::SubscriptionState::Cancelled);QVERIFY(!last(packets)->committed);QCOMPARE(bytes(area.path+"/downloads.config"),inventory);QCOMPARE(bytes(area.path+"/subscriptions.config"),config);QCOMPARE(bytes(area.resultsPath),results);absentIntent(area);
  server.release();server.holdBody=false;QVERIFY(bridge.subscriptionCommand(command(catalogs,save,101)));QTRY_VERIFY_WITH_TIMEOUT(terminal(packets)&&last(packets)->key.request!=first.key.request,15000);QVERIFY2(last(packets)->state==ui::SubscriptionState::Ready,qPrintable(terminalDiagnostic(packets,made.load(),server,notices)));QVERIFY(last(packets)->committed);QCOMPARE(made.load(),2);QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,15000);QVERIFY(storage::ProfileLease::acquire(area.path.toStdString()));QVERIFY(server.failure.isEmpty());
 }
 void unsupportedMetainfoLeavesSourceAndInventoryUntouched_data(){QTest::addColumn<int>("mode");QTest::newRow("v2-only")<<1;QTest::newRow("hybrid-not-pure-v1")<<2;QTest::newRow("saved-hash-mismatch")<<3;}
 void unsupportedMetainfoLeavesSourceAndInventoryUntouched(){QFETCH(int,mode);w::Server server;server.body=metainfo(mode==3?0:mode);f::Profile area(server.url());prepare(area,server.url("/owned.torrent"),mode==3);const auto config=bytes(area.path+"/subscriptions.config"),results=bytes(area.resultsPath),inventory=bytes(area.path+"/downloads.config");const auto save=destination(area);std::atomic<int> made{0};app::SessionBridge bridge(nullptr,{},{},{},{},{},{},factory(area,made));ui::LibraryWindow window;wire(bridge,window);QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),catalogs(&bridge,&app::SessionBridge::subscriptionCatalogReady),packets(&bridge,&app::SessionBridge::subscriptionReady),closed(&bridge,&app::SessionBridge::sessionClosed);QVERIFY(bridge.openNormal(area.path.toStdString()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.size()==1,15000);window.findChild<QAction*>("classic-subscriptions")->trigger();QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!catalogs.empty()&&!packets.empty(),10000);QVERIFY(bridge.subscriptionCommand(command(catalogs,save)));QTRY_VERIFY_WITH_TIMEOUT(terminal(packets),15000);QCOMPARE(last(packets)->state,ui::SubscriptionState::Rejected);QVERIFY(!last(packets)->committed);QCOMPARE(bytes(area.path+"/downloads.config"),inventory);QCOMPARE(bytes(area.path+"/subscriptions.config"),config);QCOMPARE(bytes(area.resultsPath),results);absentIntent(area);QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,15000);QVERIFY(storage::ProfileLease::acquire(area.path.toStdString()));}
 void previewAndDetachedReceiverDoNotAcquire_data(){QTest::addColumn<int>("mode");QTest::newRow("preview-zero-factory")<<0;QTest::newRow("genuine-owner-other-result")<<1;QTest::newRow("genuine-owner-detached-receiver")<<2;}
 void previewAndDetachedReceiverDoNotAcquire(){QFETCH(int,mode);const bool preview=mode==0;w::Server server;server.body=metainfo();f::Profile area(server.url());prepare(area,server.url("/owned.torrent"));if(mode==1){auto values=core::Document::parse(bytes(area.resultsPath).toStdString());w::require(bool(values),"Private results missing");const auto* rows=values->find("results");auto original=core::encodeValue(rows->children[0]);w::require(bool(original),"Private result missing");auto second=core::Document::parse(*original);w::require(bool(second)&&bool(second->replaceValue("key",w::b(QByteArray(10,'q')).toStdString()))&&bool(values->replaceValue("results","l"+*original+second->serialize()+"e")),"Cannot save a second genuine unread result");w::write(area.resultsPath,QByteArray::fromStdString(values->serialize()));}const auto config=bytes(area.path+"/subscriptions.config"),results=bytes(area.resultsPath),inventory=bytes(area.path+"/downloads.config");const auto save=destination(area);std::atomic<int> made{0};app::SessionBridge bridge(nullptr,{},{},{},{},{},{},factory(area,made,mode==1?1:2));ui::LibraryWindow window;wire(bridge,window,preview);QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),catalogs(&bridge,&app::SessionBridge::subscriptionCatalogReady),packets(&bridge,&app::SessionBridge::subscriptionReady),closed(&bridge,&app::SessionBridge::sessionClosed);QVERIFY(preview?bridge.open(area.path.toStdString(),0):bridge.openNormal(area.path.toStdString()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.size()==1,15000);window.findChild<QAction*>("classic-subscriptions")->trigger();QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!catalogs.empty()&&!packets.empty(),10000);QVERIFY(bridge.subscriptionCommand(command(catalogs,save)));QTRY_VERIFY_WITH_TIMEOUT(terminal(packets),10000);QCOMPARE(last(packets)->state,ui::SubscriptionState::Rejected);QCOMPARE(made.load(),preview?0:1);QCOMPARE(server.connections,0);QCOMPARE(bytes(area.path+"/downloads.config"),inventory);QCOMPARE(bytes(area.path+"/subscriptions.config"),config);QCOMPARE(bytes(area.resultsPath),results);absentIntent(area);QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,15000);QVERIFY(storage::ProfileLease::acquire(area.path.toStdString()));}
 void actualExistingStoppedTargetLinksWithoutImportOrNewHistory_data(){QTest::addColumn<int>("mode");QTest::newRow("new-association-new-download-sid")<<0;QTest::newRow("new-association-existing-download-sid")<<1;QTest::newRow("duplicate-sub-association-no-sid-repair")<<2;QTest::newRow("duplicate-sub-association-missing-counts-keeps-cache")<<3;}
 void actualExistingStoppedTargetLinksWithoutImportOrNewHistory(){
  QFETCH(int,mode);w::Server server;server.body=metainfo();f::Profile area(server.url());prepare(area,server.url("/owned.torrent"),false,true);const auto save=destination(area);const auto existing=existingStopped(area,save,mode);const auto body=bytes(area.bodyPath);std::atomic<int> made{0};app::SessionBridge bridge(nullptr,{},{},{},{},{},{},factory(area,made));ui::LibraryWindow window;wire(bridge,window);QSignalSpy notices(&bridge,&app::SessionBridge::messageReady);QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),catalogs(&bridge,&app::SessionBridge::subscriptionCatalogReady),packets(&bridge,&app::SessionBridge::subscriptionReady),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(bridge.openNormal(area.path.toStdString()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.size()==1,15000);window.findChild<QAction*>("classic-subscriptions")->trigger();QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!catalogs.empty()&&!packets.empty(),10000);QVERIFY(bridge.subscriptionCommand(command(catalogs,save)));QTRY_VERIFY_WITH_TIMEOUT(terminal(packets),15000);QVERIFY2(last(packets)->state==ui::SubscriptionState::Ready,qPrintable(terminalDiagnostic(packets,made.load(),server,notices)));QVERIFY(last(packets)->committed);QVERIFY(last(packets)->message.startsWith("Already present; linked and marked read."));QCOMPARE(made.load(),1);QCOMPARE(server.requests.size(),qsizetype{1});QCOMPARE(bytes(area.path+"/downloads.config"),existing.inventory);QCOMPARE(bytes(existing.archivePath),existing.archive);QCOMPARE(bytes(area.bodyPath),body);QVERIFY(!QFile::exists(area.path+"/dlhistorya.config"));QVERIFY(QDir(save).entryList(QDir::AllEntries|QDir::NoDotAndDotDot).empty());
  auto state=storage::EncodedDocument::parse(bytes(existing.statePath).toStdString());QVERIFY(state);const auto* attrs=state->document().find("attributes");QVERIFY(attrs&&attrs->find("fixture_existing_unknown"));QCOMPARE(core::encodeValue(*attrs->find("fixture_existing_unknown")).value(),std::string("li17e3:rawe"));const auto* map=attrs->find("Plugin.<internal>.azsubs.subs_info");
  if(mode>=2){QVERIFY(!map);}else{QVERIFY(map&&map->find("s"));QCOMPARE(map->find("s")->children.size(),mode==1?std::size_t{2}:std::size_t{1});QCOMPARE(map->find("s")->children.back().bytes,area.shortId.toStdString());if(mode==1){QCOMPARE(map->find("lc")->integer,std::int64_t{17});QCOMPARE(map->find("unknown")->bytes,std::string("retained-map"));}}
  QVERIFY(attrs->find("scsrc")&&attrs->find("scrapecache"));QCOMPARE(attrs->find("scsrc")->integer,mode==3?std::int64_t{0}:std::int64_t{1});QCOMPARE(attrs->find("scrapecache")->integer,mode==3?std::int64_t{999}:std::int64_t{4294967296LL});auto results=s::parseSubscriptionResults(bytes(area.resultsPath).toStdString());QVERIFY(results&&results->rows.size()==1&&results->rows[0].read);auto config=core::Document::parse(bytes(area.path+"/subscriptions.config").toStdString());QVERIFY(config);const auto* associations=config->find("subs")->children[0].find("assoc");QVERIFY(associations&&associations->children.size()==1);if(mode>=2){QCOMPARE(associations->children[0].find("w")->integer,std::int64_t{123});}absentIntent(area);
  const auto finalState=bytes(existing.statePath),finalConfig=bytes(area.path+"/subscriptions.config");const auto firstRequest=last(packets)->key.request;QVERIFY(bridge.subscriptionCommand(command(catalogs,save,101)));QTRY_VERIFY_WITH_TIMEOUT(terminal(packets)&&last(packets)->key.request!=firstRequest,10000);QCOMPARE(last(packets)->state,ui::SubscriptionState::Rejected);QVERIFY(!last(packets)->committed);QCOMPARE(made.load(),1);QCOMPARE(bytes(existing.statePath),finalState);QCOMPARE(bytes(area.path+"/subscriptions.config"),finalConfig);QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,15000);QVERIFY(storage::ProfileLease::acquire(area.path.toStdString()));
 }
 void existingTargetArtifactsAndDestinationCannotBecomeHashPermission_data(){QTest::addColumn<int>("mode");QTest::newRow("missing-real-archive")<<0;QTest::newRow("corrupt-real-state")<<1;QTest::newRow("different-chosen-destination")<<2;}
 void existingTargetArtifactsAndDestinationCannotBecomeHashPermission(){
  QFETCH(int,mode);w::Server server;server.body=metainfo();f::Profile area(server.url());prepare(area,server.url("/owned.torrent"));const auto save=destination(area);const auto existing=existingStopped(area,save,0);QString selectedDestination=save;
  if(mode==2){selectedDestination=area.root.filePath("different-payload");QVERIFY(QDir().mkpath(selectedDestination));}
  std::atomic<int> made{0};app::SessionBridge bridge(nullptr,{},{},{},{},{},{},factory(area,made));ui::LibraryWindow window;wire(bridge,window);QSignalSpy sessions(&bridge,&app::SessionBridge::columnLayoutSessionReady),catalogs(&bridge,&app::SessionBridge::subscriptionCatalogReady),packets(&bridge,&app::SessionBridge::subscriptionReady),closed(&bridge,&app::SessionBridge::sessionClosed);
  QVERIFY(bridge.openNormal(area.path.toStdString()));QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&sessions.size()==1,15000);window.findChild<QAction*>("classic-subscriptions")->trigger();QTRY_VERIFY_WITH_TIMEOUT(!bridge.busy()&&!catalogs.empty()&&!packets.empty(),10000);
  // Mutate after normal startup, so this tests the actual acquisition capture
  // path rather than a separate startup/review refusal.
  if(mode==0){QVERIFY(QFile::remove(existing.archivePath));}else if(mode==1){w::write(existing.statePath,existing.state+"x");}
  const auto stateBefore=bytes(existing.statePath),configBefore=bytes(area.path+"/subscriptions.config"),resultsBefore=bytes(area.resultsPath);
  QVERIFY(bridge.subscriptionCommand(command(catalogs,selectedDestination)));QTRY_VERIFY_WITH_TIMEOUT(terminal(packets),15000);QCOMPARE(last(packets)->state,ui::SubscriptionState::Rejected);QVERIFY(!last(packets)->committed);QCOMPARE(made.load(),1);QCOMPARE(bytes(area.path+"/downloads.config"),existing.inventory);QCOMPARE(bytes(existing.statePath),stateBefore);QCOMPARE(bytes(area.path+"/subscriptions.config"),configBefore);QCOMPARE(bytes(area.resultsPath),resultsBefore);
  if(mode==0){QVERIFY(!QFile::exists(existing.archivePath));}else{QCOMPARE(bytes(existing.archivePath),existing.archive);}absentIntent(area);QVERIFY(bridge.closeSession());QTRY_COMPARE_WITH_TIMEOUT(closed.size(),1,15000);QVERIFY(storage::ProfileLease::acquire(area.path.toStdString()));

 }

};
#if defined(BIGLYBTC_RSS_FETCH_TEST_FIXTURE)
std::optional<int> dispatchOriginalStateHandback(const QStringList& arguments){
  if(arguments.size()==2&&arguments[1]=="--original-state-handback-return"){try{return originalStateReturn();}catch(const std::exception& error){std::fprintf(stderr,"Owned state return failed: %.256s\n",error.what());return 12;}}
  if(arguments.contains("--original-state-handback-return")){return 2;}
  if(!qEnvironmentVariableIsEmpty(StateHandbackEnvironment)){
    if(qEnvironmentVariable(StateHandbackEnvironment)!="qualified-fresh-v1"||arguments.size()!=2||arguments[1]!="actualNormalStoppedImportAssociatesAndReadsWithoutStarting"){return 2;}
    try{handbackParent();w::require(!QFileInfo::exists(StateHandbackReceipt)&&!QFileInfo(StateHandbackReceipt).isSymLink(),"Owned export already exists");}catch(const std::exception& error){std::fprintf(stderr,"Owned export setup failed: %.256s\n",error.what());return 13;}
  }
  return std::nullopt;
}
// Preserve installed Qt6.12 initMain, AA_Use96Dpi, source-path and qExec setup.
QTEST_MAIN_WRAPPER(SubscriptionAcquisitionBridgeTests,
  QTEST_MAIN_SETUP()
  const auto dispatched=dispatchOriginalStateHandback(QCoreApplication::arguments());
  if(dispatched){return *dispatched;}
)
#else
QTEST_MAIN(SubscriptionAcquisitionBridgeTests)
#endif
#include "subscription_acquisition_bridge_tests.moc"
