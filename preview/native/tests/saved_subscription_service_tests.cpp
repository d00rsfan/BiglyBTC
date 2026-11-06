// SPDX-License-Identifier: GPL-2.0-or-later
#include "saved_subscription_service.hpp"
#include "subscription_rss_http_fixture.hpp"
#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>
using namespace biglybtc;
namespace {
namespace s=search;namespace f=subscription_http_fixture;namespace w=rss_fixture;
QString faultRoot;int failRename=0;bool failRollbackSync=false;
QByteArray read(const QString& path){QFile file(path);w::require(file.open(QIODevice::ReadOnly),"Private history read failed");return file.readAll();}
QByteArray row(const QByteArray& key,const char* name,bool isRead=false){
  const auto json=QJsonDocument(QJsonObject{{"n",QString::fromUtf8(name)},{"tf","10"},{"ts","0"},{"future",QJsonObject{{"x",17}}}}).toJson(QJsonDocument::Compact);
  return f::dictionary({{"future_row",w::b("opaque")},{"key",w::b(key)},{"read",f::integer(isRead?1:0)},{"result_json",w::b(json)},{"tz",f::integer(10)}});
}
void history(f::Profile& area,const QByteArray& values,qint64 reads=0,qint64 unreads=1){
  auto sub=core::Document::parse(area.metadataRow.toStdString());w::require(bool(sub),"Cannot decode actual fixture metadata");const auto* h=sub->find("history");auto raw=core::encodeValue(*h);w::require(bool(raw),"Cannot retain actual history");auto changed=core::Document::parse(*raw);w::require(bool(changed),"Cannot decode fixture history");
  for(const auto& [key,value]:std::vector<std::pair<std::string,qint64>>{{"num_read",reads},{"num_unread",unreads},{"last_scan",100000},{"last_new",90000}}){w::require(bool(changed->replaceValue(key,f::integer(value).toStdString())),"Cannot set actual history counts");}
  w::require(bool(sub->replaceValue("history",changed->serialize())),"Cannot retain actual metadata");area.metadataRow=QByteArray::fromStdString(sub->serialize());area.configuration=f::dictionary({{"future_config",w::b("opaque-config")},{"subs","l"+area.metadataRow+"e"}});w::write(area.path+"/subscriptions.config",area.configuration);w::write(area.resultsPath,f::dictionary({{"future_results",w::b("opaque-results")},{"results","l"+values+"e"}}));
}
s::SubscriptionFetchFactory factory(f::Profile& area,std::atomic<int>& made){return [&area,&made](const storage::ProfileLease& lease,const std::filesystem::path& path,std::uint64_t session,const s::SavedSubscriptionSelector& selected,std::stop_token token){++made;auto opened=s::RssHttpFetch::openSubscriptionFixture(lease,path,session,selected,area.endpoint(),token);return opened?std::move(*opened):std::unique_ptr<s::RssHttpFetch>{};};}
}
extern "C" int __real_renameat(int,const char*,int,const char*);
extern "C" int __wrap_renameat(int a,const char* oldName,int b,const char* newName){
  if(failRename>0&&(QByteArray(oldName)=="subscriptions.config"||QByteArray(oldName).endsWith(".results"))&&--failRename==0){errno=EIO;return -1;}return __real_renameat(a,oldName,b,newName);
}
extern "C" int __real_fsync(int);
extern "C" int __wrap_fsync(int fd){
  if(failRollbackSync&&!faultRoot.isEmpty()){char name[4096];const auto label=QByteArray("/proc/self/fd/")+QByteArray::number(fd);const auto size=::readlink(label.constData(),name,sizeof(name)-1);if(size>0&&QString::fromUtf8(name,static_cast<qsizetype>(size))==faultRoot+"/subs"){failRollbackSync=false;errno=EIO;return -1;}}
  return __real_fsync(fd);
}
class SavedSubscriptionServiceTests final:public QObject {
 Q_OBJECT
private slots:
 void cleanup(){failRename=0;failRollbackSync=false;faultRoot.clear();}
 void actualIdentityCollisionIndexesMalformedSiblingAndNotificationsRefuse(){
   f::Profile area("http://feeds.example.org/feed");auto malformed=core::Document::parse(area.metadataRow.toStdString());QVERIFY(malformed);QVERIFY(malformed->replaceValue("version","i0e"));
   auto index=s::parseSubscriptionIndex(f::dictionary({{"subs","l"+area.metadataRow+QByteArray::fromStdString(malformed->serialize())+"e"}}).toStdString());QVERIFY(index);QCOMPARE(index->rows.size(),std::size_t{2});QCOMPARE(index->rows[0].issue,std::optional{s::SubscriptionValueError::Ambiguous});
   auto metadata=core::Document::parse(area.metadataRow.toStdString());QVERIFY(metadata);const auto h=core::encodeValue(*metadata->find("history"));QVERIFY(h);auto policy=core::Document::parse(*h);QVERIFY(policy);QVERIFY(policy->replaceValue("post_noti","i1e"));QVERIFY(metadata->replaceValue("history",policy->serialize()));w::write(area.path+"/subscriptions.config",f::dictionary({{"subs","l"+QByteArray::fromStdString(metadata->serialize())+"e"}}));
   const auto before=area.sourceBytes();std::atomic<int> made{0};auto service=s::SavedSubscriptionService::open(*area.lease,area.path.toStdString(),1,s::SubscriptionServiceMode::Normal,factory(area,made));QVERIFY(service);auto started=(*service)->check(*area.lease,1,area.selector());QVERIFY(!started);QCOMPARE(started.error(),s::SubscriptionServiceError::Unsupported);QCOMPARE(made.load(),0);QVERIFY(area.sourceBytes()==before);QVERIFY((*service)->close());
 }
 void originalUidKnownAbsenceAndExactTombstoneThreshold_data(){QTest::addColumn<int>("aged");QTest::newRow("10000-retained")<<10000;QTest::newRow("10001-keeps-5000-stable-ties")<<10001;}
 void originalUidKnownAbsenceAndExactTombstoneThreshold(){
   QFETCH(int,aged);f::Profile area("http://feeds.example.org/feed");auto index=s::parseSubscriptionIndex(area.configuration.toStdString());QVERIFY(index);index->markLibraryRead=false;index->rows[0].lastScan=1;
   std::string json=R"({"n":"Keep","u":"oldUID","uid":{"opaque":7},"h":"OLD","dl":"old.torrent","cdp":"old.page","tf":"10","ts":"0","future":{"x":17}})";
   const auto key=QCryptographicHash::hash("-7:Keep",QCryptographicHash::Sha1).left(10);
   auto original=f::dictionary({{"key",w::b(key)},{"key2",w::b(QByteArray(10,'u'))},{"read",f::integer(0)},{"result_json",w::b(QByteArray::fromStdString(json))},{"tz",f::integer(10)}});
   auto values=s::parseSubscriptionResults(f::dictionary({{"results","l"+original+"e"}}).toStdString());QVERIFY(values);s::RssDocument incoming;incoming.results.push_back({});incoming.results[0].title="Keep";
   auto updated=s::reconcileSubscriptionResults(*index,0,*values,incoming,-7,21000LL*86400000);QVERIFY(updated);auto next=s::parseSubscriptionResults(updated->results);QVERIFY(next);const auto decoded=QJsonDocument::fromJson(QByteArray::fromStdString(next->rows[0].json)).object();QVERIFY(!decoded.contains("u")&&!decoded.contains("h")&&!decoded.contains("dl")&&!decoded.contains("cdp"));QVERIFY(decoded.value("uid").isObject()&&decoded.value("future").isObject());QVERIFY(!next->rows[0].secondaryKey);
   QByteArray encoded=row("live-key01","Keep");for(int i=0;i<aged;++i){const auto identity=QByteArray::number(i).rightJustified(10,'0');encoded+=f::dictionary({{"deleted",f::integer(1)},{"dls",f::integer(20000)},{"key",w::b(identity)},{"read",f::integer(1)}});}
   auto prior=s::parseSubscriptionResults(f::dictionary({{"results","l"+encoded+"e"}}).toStdString());QVERIFY(prior);auto changed=s::changeSubscriptionResultState(*index,0,*prior,{"live-key01"},true,false,21000LL*86400000);QVERIFY(changed);auto retained=s::parseSubscriptionResults(changed->results);QVERIFY(retained);QCOMPARE(retained->rows.size(),std::size_t{1}+static_cast<std::size_t>(aged>10000?5000:aged));QCOMPARE(retained->rows[1].key,std::string("0000000000"));
   const auto tombstone=retained->rows[1];auto repeated=s::changeSubscriptionResultState(*index,0,*retained,{tombstone.key},false,true,22000LL*86400000);QVERIFY(repeated);QCOMPARE(repeated->results,retained->source->originalBytes());
 }
 void firstScanReadThenUidRenameAndExplicitOffsetPublication(){
   f::Profile area("http://feeds.example.org/feed");auto index=s::parseSubscriptionIndex(area.configuration.toStdString());auto empty=s::parseSubscriptionResults("d7:resultslee");QVERIFY(index&&empty);s::RssDocument incoming;incoming.atom=true;s::RssResult result;result.title="First";result.uid="stable";result.publicationText="2026-11-06T10:30:00+01:00";incoming.results.push_back(result);
   auto first=s::reconcileSubscriptionResults(*index,0,*empty,incoming,-7,200000);QVERIFY(first);auto values=s::parseSubscriptionResults(first->results);QVERIFY(values);QVERIFY(values->rows[0].read);const auto json=QJsonDocument::fromJson(QByteArray::fromStdString(values->rows[0].json)).object();QCOMPARE(json.value("u").toString(),QString("stable"));QCOMPARE(json.value("ts").toString().toLongLong(),QDateTime::fromString("2026-11-06T09:30:00Z",Qt::ISODate).toMSecsSinceEpoch());
   auto after=s::parseSubscriptionIndex(first->configuration);QVERIFY(after);incoming.results[0].title="Renamed";s::RssResult fresh;fresh.title="New";incoming.results.push_back(fresh);auto second=s::reconcileSubscriptionResults(*after,0,*values,incoming,-7,300000);QVERIFY(second);auto merged=s::parseSubscriptionResults(second->results);QVERIFY(merged);QCOMPARE(merged->rows.size(),std::size_t{2});QCOMPARE(merged->rows[0].key,values->rows[0].key);QVERIFY(merged->rows[0].read&&!merged->rows[1].read);
   QCOMPARE(s::subscriptionNextScan(0,0,60,120,123),std::int64_t{123});QCOMPARE(s::subscriptionNextScan(1000,5,60,120,123),std::int64_t{301000});QCOMPARE(s::subscriptionRetryTime(1000,64),std::int64_t{28801000});
 }
 void genuineWrongSourceAndReceiverFactoriesCannotAuthorizeService_data(){QTest::addColumn<int>("kind");QTest::newRow("another-subscription-same-OFD")<<0;QTest::newRow("catalog-domain-same-URL")<<1;QTest::newRow("detached-receiver")<<2;}
 void genuineWrongSourceAndReceiverFactoriesCannotAuthorizeService(){
   QFETCH(int,kind);w::Server server;f::Profile area(server.url()),other(server.url());if(kind==0){w::write(area.path+"/subscriptions.config",f::dictionary({{"subs","l"+area.metadataRow+other.metadataRow+"e"}}));const auto stem=QString::fromLatin1(other.shortId.toHex().toUpper());w::write(area.path+"/subs/"+stem+".biglybt",other.body);w::write(area.path+"/subs/"+stem+".results",read(other.resultsPath));}if(kind==1){area.catalog(server.url());}
   const auto before=area.sourceBytes();int made=0;auto service=s::SavedSubscriptionService::open(*area.lease,area.path.toStdString(),7,s::SubscriptionServiceMode::Normal,[&](const storage::ProfileLease& lease,const std::filesystem::path& path,std::uint64_t session,const s::SavedSubscriptionSelector& selected,std::stop_token token){std::expected<std::unique_ptr<s::RssHttpFetch>,s::RssFetchError> opened=kind==1?s::RssHttpFetch::openFixture(lease,path,session,{0,-7,"catalog-rss"},area.endpoint(),token):s::RssHttpFetch::openSubscriptionFixture(lease,path,session,kind==0?other.selector(1):selected,area.endpoint(),kind==2?std::stop_token{}:token);if(opened){++made;return std::move(*opened);}return std::unique_ptr<s::RssHttpFetch>{};});QVERIFY(service);
   auto started=(*service)->check(*area.lease,7,area.selector());QVERIFY(!started);QCOMPARE(started.error(),s::SubscriptionServiceError::Source);QCOMPARE(made,1);QCOMPARE(server.connections,0);QVERIFY(area.sourceBytes()==before);QVERIFY((*service)->close());
 }
 void realOwnerPolicyFenceCancelRecoveryAndKeptAliveClose(){
   w::Server server;server.holdReply=true;f::Profile area(server.url());std::atomic<int> made{0};auto service=s::SavedSubscriptionService::open(*area.lease,area.path.toStdString(),9,s::SubscriptionServiceMode::Normal,factory(area,made));QVERIFY(service);std::stop_source stop;QVERIFY((*service)->check(*area.lease,9,area.selector(),stop.get_token()));
   std::optional<s::SubscriptionCheckOutcome> outcome;const auto poll=[&]{if(outcome){return;}auto value=(*service)->poll(*area.lease,9);w::require(bool(value),"Actual service poll failed");if(*value){outcome=**value;}};
   QTRY_VERIFY_WITH_TIMEOUT((poll(),!server.requests.empty()),10000);stop.request_stop();QTRY_VERIFY_WITH_TIMEOUT((poll(),outcome.has_value()),10000);QVERIFY(outcome->cancelled&&!outcome->stored);QVERIFY(!(*service)->active());QCOMPARE(made.load(),1);QCOMPARE(read(area.resultsPath),f::dictionary({{"results","le"}}));
   server.release();outcome.reset();const auto recovery=(*service)->check(*area.lease,9,area.selector());QVERIFY2(recovery,qPrintable(QStringLiteral("recovery_error=%1 active=%2 factories=%3 GETs=%4").arg(recovery?-1:static_cast<int>(recovery.error())).arg((*service)->active()).arg(made.load()).arg(server.requests.size())));QTRY_VERIFY_WITH_TIMEOUT((poll(),outcome.has_value()),10000);QVERIFY(outcome->stored&&!outcome->failure);QCOMPARE(made.load(),2);auto values=(*service)->results(*area.lease,9,area.selector());QVERIFY(values&&values->rows.size()==1&&values->rows[0].read);QCOMPARE(read(area.bodyPath),area.body);
   QVERIFY((*service)->close());area.lease.reset();auto acquired=storage::ProfileLease::acquire(area.path.toStdString());QVERIFY(acquired);QVERIFY(service->get()!=nullptr);QVERIFY(server.failure.isEmpty());
 }
 void sourceChangesBetweenPolicyReadAndGenuineFactoryRemainBeforeDns(){
   w::Server server;f::Profile area(server.url());int made=0;auto service=s::SavedSubscriptionService::open(*area.lease,area.path.toStdString(),4,s::SubscriptionServiceMode::Normal,[&](const storage::ProfileLease& lease,const std::filesystem::path& path,std::uint64_t session,const s::SavedSubscriptionSelector& selected,std::stop_token token){auto opened=s::RssHttpFetch::openSubscriptionFixture(lease,path,session,selected,area.endpoint(),token);if(opened){++made;w::write(area.path+"/biglybt.config","d9:unrelatedi1ee");return std::move(*opened);}return std::unique_ptr<s::RssHttpFetch>{};});QVERIFY(service);auto checked=(*service)->check(*area.lease,4,area.selector());QVERIFY(!checked);QCOMPARE(made,1);QCOMPARE(server.connections,0);QCOMPARE(read(area.resultsPath),f::dictionary({{"results","le"}}));QVERIFY((*service)->close());
 }
 void actualTwoFileRecoveryHandlesRollbackWriterAndEqualConfiguration_data(){QTest::addColumn<bool>("equalConfiguration");QTest::newRow("partial-results-rollback-interrupted")<<false;QTest::newRow("before-equals-after-config-is-not-commit")<<true;}
 void actualTwoFileRecoveryHandlesRollbackWriterAndEqualConfiguration(){
   QFETCH(bool,equalConfiguration);f::Profile area("http://feeds.example.org/feed");history(area,row("live-key01","Keep"),equalConfiguration?1:0,equalConfiguration?0:1);const auto beforeResults=read(area.resultsPath),beforeConfig=read(area.path+"/subscriptions.config");
   auto service=s::SavedSubscriptionService::open(*area.lease,area.path.toStdString(),3,s::SubscriptionServiceMode::Normal);QVERIFY(service);faultRoot=area.path;failRename=equalConfiguration?1:2;auto changed=(*service)->changeResultState(*area.lease,3,area.selector(),{"live-key01"},s::SubscriptionResultAction::MarkRead);QVERIFY(!changed);QCOMPARE(changed.error(),s::SubscriptionServiceError::PendingWrite);QVERIFY(QFile::exists(area.path+"/.subscriptions-native-write"));service->reset();failRename=0;
   w::write(area.path+"/biglybt.config","d7:changedi1ee");if(!equalConfiguration){failRollbackSync=true;auto interrupted=s::SavedSubscriptionService::open(*area.lease,area.path.toStdString(),3,s::SubscriptionServiceMode::Normal);QVERIFY(!interrupted);QVERIFY(!failRollbackSync);QVERIFY(QFile::exists(area.path+"/.subscriptions-native-write"));}
   auto recovered=s::SavedSubscriptionService::open(*area.lease,area.path.toStdString(),3,s::SubscriptionServiceMode::Normal);QVERIFY(recovered);QCOMPARE(read(area.resultsPath),beforeResults);QCOMPARE(read(area.path+"/subscriptions.config"),beforeConfig);QVERIFY(!QFile::exists(area.path+"/.subscriptions-native-write"));QVERIFY(!QFile::exists(area.resultsPath+".saving"));QVERIFY(!QFile::exists(area.path+"/subscriptions.config.saving"));QCOMPARE(read(area.bodyPath),area.body);QVERIFY((*recovered)->close());
 }
 void readOnlyPreviewCatalogHistoryAndForgedCheckNeverConstructProvider(){
   w::Server server;f::Profile area(server.url());history(area,row("live-key01","Keep"));const auto before=area.sourceBytes();std::atomic<int> made{0};auto service=s::SavedSubscriptionService::open(*area.lease,area.path.toStdString(),2,s::SubscriptionServiceMode::ReadOnlyPreview,factory(area,made));QVERIFY(service);QVERIFY((*service)->catalog(*area.lease,2));QVERIFY((*service)->results(*area.lease,2,area.selector()));auto started=(*service)->check(*area.lease,2,area.selector());QVERIFY(!started);for(int i=0;i<3;++i){QVERIFY((*service)->poll(*area.lease,2));}QCOMPARE(made.load(),0);QCOMPARE(server.connections,0);QVERIFY(area.sourceBytes()==before);QVERIFY((*service)->close());
 }
};
QTEST_GUILESS_MAIN(SavedSubscriptionServiceTests)
#include "saved_subscription_service_tests.moc"
