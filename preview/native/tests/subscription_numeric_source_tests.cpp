// SPDX-License-Identifier: GPL-2.0-or-later
#include "saved_subscription_service.hpp"
#include "subscription_rss_http_fixture.hpp"
#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
using namespace biglybtc;
namespace {
namespace f=subscription_http_fixture;namespace w=rss_fixture;namespace s=search;
QByteArray bytes(const QString& path){QFile file(path);w::require(file.open(QIODevice::ReadOnly),"Private numeric source read failed");return file.readAll();}
QJsonObject numeric(){return {{"min_size",qint64{10}},{"max_size",qint64{20}},{"min_seeds",qint64{2}},{"max_seeds",qint64{4}},{"min_peers",qint64{1}},{"future",QJsonObject{{"opaque","retain"}}}};}
QByteArray item(const QByteArray& name,const QByteArray& size,const QByteArray& seeds,const QByteArray& peers){
  return "<item>"+(name.isEmpty()?QByteArray{}:"<title>"+name+"</title>")+(size.isEmpty()?QByteArray{}:"<vuze:size>"+size+"</vuze:size>")
      +"<vuze:seeds>"+seeds+"</vuze:seeds><vuze:peers>"+peers+"</vuze:peers></item>";
}
}
class SubscriptionNumericSourceTests final:public QObject {
 Q_OBJECT
private slots:
 void actualSignedEmbeddedNumericPolicyFiltersBeforeHistoryAndKeepsDefinition(){
   w::Server server;server.body="<rss xmlns:vuze=\"urn:vuze\"><channel>"+item("Lower","10","2","1")+item("Upper","20","4","1")
     +item("TooSmall","9","3","1")+item("TooManySeeds","15","5","1")+item("NoPeers","15","3","0")
     +item("UnknownSize","","3","1")+item("","15","3","1")+"</channel></rss>";
   f::Profile area(server.url(),nullptr,false,numeric());const auto originalBody=bytes(area.bodyPath);int constructed=0;
   auto service=s::SavedSubscriptionService::open(*area.lease,area.path.toStdString(),41,s::SubscriptionServiceMode::Normal,[&](const storage::ProfileLease& lease,const std::filesystem::path& path,std::uint64_t session,const s::SavedSubscriptionSelector& selected,std::stop_token stop){auto opened=s::RssHttpFetch::openSubscriptionFixture(lease,path,session,selected,area.endpoint(),stop);if(opened){++constructed;return std::move(*opened);}return std::unique_ptr<s::RssHttpFetch>{};});QVERIFY(service);
   QVERIFY((*service)->check(*area.lease,41,area.selector()));std::optional<s::SubscriptionCheckOutcome> outcome;
   const auto poll=[&]{if(outcome){return;}auto value=(*service)->poll(*area.lease,41);w::require(bool(value),"Actual numeric service poll failed");if(*value){outcome=**value;}};
   QTRY_VERIFY_WITH_TIMEOUT((poll(),outcome.has_value()),10000);QVERIFY(outcome->stored&&!outcome->failure&&!outcome->cancelled);QCOMPARE(constructed,1);QCOMPARE(server.requests.size(),qsizetype{1});QVERIFY(server.failure.isEmpty());
   auto rows=(*service)->results(*area.lease,41,area.selector());QVERIFY(rows);QCOMPARE(rows->rows.size(),std::size_t{2});QVERIFY(rows->rows[0].read&&rows->rows[1].read);
   QCOMPARE(QJsonDocument::fromJson(QByteArray::fromStdString(rows->rows[0].json)).object().value("n").toString(),QString("Lower"));QCOMPARE(QJsonDocument::fromJson(QByteArray::fromStdString(rows->rows[1].json)).object().value("n").toString(),QString("Upper"));
   auto config=s::parseSubscriptionIndex(bytes(area.path+"/subscriptions.config").toStdString());QVERIFY(config);QCOMPARE(config->rows[0].readCount,std::int64_t{2});QCOMPARE(config->rows[0].unreadCount,std::int64_t{0});QVERIFY(config->rows[0].lastScan>0);QCOMPARE(bytes(area.bodyPath),originalBody);
   const auto metadata=core::Document::parse(area.metadataRow.toStdString());const auto* changed=config->source->find("subs");QVERIFY(metadata&&changed&&changed->children.size()==1);
   for(const auto* key:{"hash","sig","sig_data_size","public_key","fixture_future"}){const auto* old=metadata->find(key);const auto* next=changed->children[0].find(key);QVERIFY(old&&next);const auto oldValue=core::encodeValue(*old),newValue=core::encodeValue(*next);QVERIFY(oldValue&&newValue);QCOMPARE(*oldValue,*newValue);}
   QVERIFY(!QFile::exists(area.path+"/.subscriptions-native-write"));QVERIFY((*service)->close());area.lease.reset();auto lock=storage::ProfileLease::acquire(area.path.toStdString());QVERIFY(lock);QVERIFY(service->get()!=nullptr);
 }
 void unsupportedPolicyStillRefusesBeforeProviderOrHistory_data(){
   QTest::addColumn<int>("policy");QTest::newRow("regex")<<0;QTest::newRow("category")<<1;QTest::newRow("age")<<2;QTest::newRow("dependency")<<3;QTest::newRow("auto-download")<<4;
 }
 void unsupportedPolicyStillRefusesBeforeProviderOrHistory(){
   QFETCH(int,policy);w::Server server;auto filters=numeric();if(policy==0){filters.insert("text_filter","a.*");}else if(policy==1){filters.insert("category","video");}else if(policy==2){filters.insert("max_age",qint64{60});}
   f::Profile area(server.url(),nullptr,policy==4,filters);
   if(policy==3){auto metadata=core::Document::parse(area.metadataRow.toStdString());QVERIFY(metadata);QVERIFY(metadata->replaceValue("dep","l16:AAAAAAAAAAAAAAAAe"));area.metadataRow=QByteArray::fromStdString(metadata->serialize());area.configuration=f::dictionary({{"subs","l"+area.metadataRow+"e"}});w::write(area.path+"/subscriptions.config",area.configuration);}
   const auto before=area.sourceBytes();int constructed=0;auto service=s::SavedSubscriptionService::open(*area.lease,area.path.toStdString(),42,s::SubscriptionServiceMode::Normal,[&](const storage::ProfileLease& lease,const std::filesystem::path& path,std::uint64_t session,const s::SavedSubscriptionSelector& selected,std::stop_token stop){++constructed;auto opened=s::RssHttpFetch::openSubscriptionFixture(lease,path,session,selected,area.endpoint(),stop);return opened?std::move(*opened):std::unique_ptr<s::RssHttpFetch>{};});QVERIFY(service);
   const auto checked=(*service)->check(*area.lease,42,area.selector());QVERIFY(!checked);QCOMPARE(checked.error(),s::SubscriptionServiceError::Unsupported);QCOMPARE(constructed,0);QCOMPARE(server.connections,0);QVERIFY(area.sourceBytes()==before);QVERIFY((*service)->close());
 }
};
QTEST_GUILESS_MAIN(SubscriptionNumericSourceTests)
#include "subscription_numeric_source_tests.moc"
