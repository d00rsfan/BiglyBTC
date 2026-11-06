// SPDX-License-Identifier: GPL-2.0-or-later
#include "saved_subscription_service.hpp"
#include "subscription_rss_http_fixture.hpp"
#include "subscription_result_source_fixture.hpp"
#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
using namespace biglybtc;
namespace {
namespace f=subscription_http_fixture;namespace w=rss_fixture;namespace s=search;
QByteArray bytes(const QString& path){QFile file(path);w::require(file.open(QIODevice::ReadOnly),"Private literal source read failed");return file.readAll();}
QJsonObject filters(){return {{"text_filter","keep 1080p"},{"text_filter_out","cam"},{"min_seeds",qint64{2}},{"future",QJsonObject{{"opaque","retain"}}}};}
QByteArray item(const QByteArray& name,const QByteArray& seeds){return "<item><title>"+name+"</title><vuze:seeds>"+seeds+"</vuze:seeds></item>";}
s::SubscriptionFetchFactory factory(f::Profile& area,int& constructed){return [&area,&constructed](const storage::ProfileLease& lease,const std::filesystem::path& path,std::uint64_t session,const s::SavedSubscriptionSelector& selected,std::stop_token stop){auto opened=s::RssHttpFetch::openSubscriptionFixture(lease,path,session,selected,area.endpoint(),stop);if(opened){++constructed;return std::move(*opened);}return std::unique_ptr<s::RssHttpFetch>{};};}
}
class SubscriptionLiteralSourceTests final:public QObject {
 Q_OBJECT
private slots:
 void actualSignedSourceCombinesLiteralAndNumericBeforeHistory(){
  w::Server server;server.body="<rss xmlns:vuze=\"urn:vuze\"><channel>"+item("KEEP.WEBRip.1080p","2")+item("Keep.CAM.1080p","3")+item("Keep.720p","3")+item("Keep.1080p.LowSeeds","1")+"</channel></rss>";
  f::Profile area(server.url(),nullptr,false,filters());const auto originalBody=bytes(area.bodyPath),originalMetadata=area.metadataRow;int constructed=0;
  auto service=s::SavedSubscriptionService::open(*area.lease,area.path.toStdString(),61,s::SubscriptionServiceMode::Normal,factory(area,constructed));QVERIFY(service);QVERIFY((*service)->check(*area.lease,61,area.selector()));std::optional<s::SubscriptionCheckOutcome> outcome;
  const auto poll=[&]{if(outcome){return;}auto value=(*service)->poll(*area.lease,61);w::require(bool(value),"Actual literal source poll failed");if(*value){outcome=**value;}};
  QTRY_VERIFY_WITH_TIMEOUT((poll(),outcome.has_value()),10000);QVERIFY(outcome->stored&&!outcome->failure&&!outcome->cancelled);QCOMPARE(constructed,1);QCOMPARE(server.requests.size(),qsizetype{1});QVERIFY(server.failure.isEmpty());
  auto results=(*service)->results(*area.lease,61,area.selector());QVERIFY(results);QCOMPARE(results->rows.size(),std::size_t{1});QVERIFY(results->rows[0].read);QCOMPARE(QJsonDocument::fromJson(QByteArray::fromStdString(results->rows[0].json)).object().value("n").toString(),QString("KEEP.WEBRip.1080p"));
  QCOMPARE(bytes(area.bodyPath),originalBody);auto index=s::parseSubscriptionIndex(bytes(area.path+"/subscriptions.config").toStdString());QVERIFY(index);QCOMPARE(index->rows[0].readCount,std::int64_t{1});QCOMPARE(index->rows[0].unreadCount,std::int64_t{0});QVERIFY(index->rows[0].lastScan>0);
  auto before=core::Document::parse(originalMetadata.toStdString());QVERIFY(before);const auto* after=index->source->find("subs");QVERIFY(after&&after->children.size()==1);
  for(const auto* key:{"hash","sig","sig_data_size","public_key","fixture_future"}){const auto* a=before->find(key);const auto* b=after->children[0].find(key);QVERIFY(a&&b);QCOMPARE(core::encodeValue(*a).value(),core::encodeValue(*b).value());}
  QVERIFY((*service)->close());area.lease.reset();auto lock=storage::ProfileLease::acquire(area.path.toStdString());QVERIFY(lock);QVERIFY(service->get()!=nullptr);
 }
 void unsupportedPolicyRefusesBeforeProviderAndBookkeeping_data(){
  QTest::addColumn<int>("mode");QTest::newRow("regex-not-literal-subset")<<0;QTest::newRow("age")<<1;QTest::newRow("category")<<2;QTest::newRow("dependency")<<3;QTest::newRow("auto-download")<<4;
 }
 void unsupportedPolicyRefusesBeforeProviderAndBookkeeping(){
  QFETCH(int,mode);w::Server server;auto policy=filters();if(mode==0){policy.insert("text_filter","keep.*");}else if(mode==1){policy.insert("max_age",qint64{60});}else if(mode==2){policy.insert("category","video");}
  f::Profile area(server.url(),nullptr,mode==4,policy);if(mode==3){auto config=core::Document::parse(area.metadataRow.toStdString());QVERIFY(config);QVERIFY(config->replaceValue("dep","l16:AAAAAAAAAAAAAAAAe"));area.metadataRow=QByteArray::fromStdString(config->serialize());area.configuration=f::dictionary({{"subs","l"+area.metadataRow+"e"}});w::write(area.path+"/subscriptions.config",area.configuration);}
  const auto before=area.sourceBytes();int constructed=0;auto service=s::SavedSubscriptionService::open(*area.lease,area.path.toStdString(),62,s::SubscriptionServiceMode::Normal,factory(area,constructed));QVERIFY(service);auto check=(*service)->check(*area.lease,62,area.selector());QVERIFY(!check);QCOMPARE(check.error(),s::SubscriptionServiceError::Unsupported);QCOMPARE(constructed,0);QCOMPARE(server.connections,0);QVERIFY(area.sourceBytes()==before);QVERIFY((*service)->close());
 }
 void unicodeNameRefusesWholeScanWithoutHistoryMutation(){
  w::Server server;server.body="<rss xmlns:vuze=\"urn:vuze\"><channel>"+item("Keep.1080p","2")+item("Keep.1080p.&#233;","2")+"</channel></rss>";
  f::Profile area(server.url(),nullptr,false,filters());const auto before=area.sourceBytes();int constructed=0;auto service=s::SavedSubscriptionService::open(*area.lease,area.path.toStdString(),63,s::SubscriptionServiceMode::Normal,factory(area,constructed));QVERIFY(service);QVERIFY((*service)->check(*area.lease,63,area.selector()));std::optional<s::SubscriptionCheckOutcome> outcome;
  const auto poll=[&]{if(outcome){return;}auto value=(*service)->poll(*area.lease,63);w::require(bool(value),"Actual literal refusal poll failed");if(*value){outcome=**value;}};
  QTRY_VERIFY_WITH_TIMEOUT((poll(),outcome.has_value()),10000);QVERIFY(!outcome->stored&&outcome->failure&&!outcome->cancelled);QCOMPARE(constructed,1);QCOMPARE(server.requests.size(),qsizetype{1});QVERIFY(area.sourceBytes()==before);QVERIFY(!QFile::exists(area.path+"/.subscriptions-native-write"));QVERIFY((*service)->close());
 }
 void actualSavedResultTextPolicyIsReadOnly_data(){
  QTest::addColumn<bool>("supported");QTest::newRow("matching-saved-name")<<true;QTest::newRow("unsupported-unicode-saved-name")<<false;
 }
 void actualSavedResultTextPolicyIsReadOnly(){
  QFETCH(bool,supported);const auto name=supported?QString("Keep.1080p"):QString::fromUtf8("Keep.1080p.\xc3\xa9");
  subscription_result_fixture::Profile area({{"n",name},{"lb","10"},{"s","2"},{"p","2"},{"dl","http://files.example.org/owned.torrent"}},filters());const auto before=area.sourceBytes();
  {
   auto source=s::SavedSubscriptionResultSnapshot::read(*area.lease,area.path.toStdString(),64,area.resultSelector());
   if(supported){QVERIFY(source);QCOMPARE(source->description().result.name,name);QCOMPARE(source->description().result.seeds,std::int32_t{2});QVERIFY(source->current(*area.lease,64));}
   else{QVERIFY(!source);QCOMPARE(source.error(),s::SavedRssSourceError::Unsupported);}
   QVERIFY(area.sourceBytes()==before);QVERIFY(!QFile::exists(area.path+"/.subscriptions-native-write"));QVERIFY(!QFile::exists(area.path+"/BiglyBTC.subscription-acquire"));
  }
  area.lease.reset();auto lock=storage::ProfileLease::acquire(area.path.toStdString());QVERIFY(lock);
 }
};
QTEST_GUILESS_MAIN(SubscriptionLiteralSourceTests)
#include "subscription_literal_source_tests.moc"
