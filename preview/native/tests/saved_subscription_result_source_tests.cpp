// SPDX-License-Identifier: GPL-2.0-or-later
#include "subscription_result_source_fixture.hpp"
#include "file_io.hpp"
#include <QtTest>
#include <thread>
#include <limits>
using namespace biglybtc;
namespace f=subscription_result_fixture;namespace s=search;
class SavedSubscriptionResultSourceTests final:public QObject {
 Q_OBJECT
private slots:
 void actualDownloadLinkPrecedenceAndSavedNumericGetters_data(){
   QTest::addColumn<QByteArray>("raw");QTest::addColumn<QString>("chosen");
   QTest::newRow("button-first")<<QByteArray("{\"n\":\"A\",\"dbl\":\"https://files.example.org/button.torrent\",\"dl\":\"https://files.example.org/other.torrent\"}")<<QString("https://files.example.org/button.torrent");
   QTest::newRow("null-button-falls-back")<<QByteArray("{\"n\":\"A\",\"dbl\":null,\"dl\":\"https://files.example.org/owned.torrent\"}")<<QString("https://files.example.org/owned.torrent");
   QTest::newRow("magnet-button-prefers-any-explicit-download")<<QByteArray("{\"n\":\"A\",\"dbl\":\"MaGnEt:?xt=x\",\"dl\":\"https://files.example.org/owned.torrent\"}")<<QString("https://files.example.org/owned.torrent");
   QTest::newRow("empty-button-does-not-fall-back")<<QByteArray("{\"n\":\"A\",\"dbl\":\"\",\"dl\":\"https://files.example.org/other.torrent\"}")<<QString{};
   QTest::newRow("plugin-button-does-not-fall-back")<<QByteArray("{\"n\":\"A\",\"dbl\":\"azplug:?id=x\",\"dl\":\"https://files.example.org/other.torrent\"}")<<QString{};
   QTest::newRow("chosen-magnet-refused")<<QByteArray("{\"n\":\"A\",\"dbl\":\"magnet:?xt=x\",\"dl\":\"magnet:?xt=y\"}")<<QString{};
   QTest::newRow("play-is-not-download-fallback")<<QByteArray("{\"n\":\"A\",\"pl\":\"https://files.example.org/play.torrent\"}")<<QString{};
 }
 void actualDownloadLinkPrecedenceAndSavedNumericGetters(){
   QFETCH(QByteArray,raw);QFETCH(QString,chosen);const auto object=QJsonDocument::fromJson(raw).object();f::Profile area(object,{{"min_size",qint64{0}},{"min_seeds",qint64{0}},{"min_peers",qint64{0}}});const auto before=area.sourceBytes();auto source=s::SavedSubscriptionResultSnapshot::read(*area.lease,area.path.toStdString(),71,area.resultSelector());
   if(chosen.isEmpty()){QVERIFY(!source);QCOMPARE(source.error(),s::SavedRssSourceError::Unsupported);}else{QVERIFY(source);QCOMPARE(QString::fromStdString(source->description().literalGetUrl),chosen);QCOMPARE(source->description().result.size,std::int64_t{0});QCOMPARE(source->description().result.seeds,std::int32_t{0});QCOMPARE(source->description().result.peers,std::int32_t{0});QVERIFY(!source->description().result.initialScrapeCache);QCOMPARE(source->description().referer,std::optional{std::string("http://feeds.example.org")});QVERIFY(source->current(*area.lease,71));}QVERIFY(area.sourceBytes()==before);
 }
 void actualRefererModeReadFlagAndPreparationPolicyStayBound_data(){QTest::addColumn<int>("policy");QTest::newRow("explicit-referer-off")<<0;QTest::newRow("read-result")<<1;QTest::newRow("category")<<2;QTest::newRow("tag")<<3;QTest::newRow("explicit-network-preparation")<<4;QTest::newRow("saved-custom-browser-headers")<<5;}
 void actualRefererModeReadFlagAndPreparationPolicyStayBound(){
   QFETCH(int,policy);const QJsonObject row{{"n","A"},{"dl","https://files.example.org/owned.torrent"}};f::Profile area(row,{},policy!=0);
   if(policy==1){area.save(row,true);}if(policy>=2&&policy<=4){auto metadata=core::Document::parse(area.metadataRow.toStdString());QVERIFY(metadata);if(policy==2){QVERIFY(metadata->replaceValue("cat","5:video"));}else if(policy==3){QVERIFY(metadata->replaceValue("tag","i7e"));}else{auto encoded=core::encodeValue(*metadata->find("history"));QVERIFY(encoded);auto history=core::Document::parse(*encoded);QVERIFY(history);QVERIFY(history->replaceValue("nets","6:Public"));QVERIFY(metadata->replaceValue("history",history->serialize()));}area.metadataRow=QByteArray::fromStdString(metadata->serialize());area.configuration=subscription_http_fixture::dictionary({{"subs","l"+area.metadataRow+"e"}});rss_fixture::write(area.path+"/subscriptions.config",area.configuration);}
   if(policy==5){rss_fixture::write(area.path+"/biglybt.config",subscription_http_fixture::dictionary({{"metasearch.web.last.headers",rss_fixture::b("WC1UZXN0OiBvd25lZA==")}}));}
   const auto before=area.sourceBytes();auto source=s::SavedSubscriptionResultSnapshot::read(*area.lease,area.path.toStdString(),72,area.resultSelector());if(policy==0){QVERIFY(source);QVERIFY(!source->description().referer);}else{QVERIFY(!source);QCOMPARE(source.error(),s::SavedRssSourceError::Unsupported);}QVERIFY(area.sourceBytes()==before);
 }
 void malformedPropertyMapsCannotIssuePartialPreparation_data(){QTest::addColumn<int>("field");QTest::newRow("bad-rank")<<0;QTest::newRow("bad-category")<<1;QTest::newRow("bad-contenttype")<<2;QTest::newRow("bad-detailslink")<<3;QTest::newRow("bad-tags")<<4;}
 void malformedPropertyMapsCannotIssuePartialPreparation(){
   QFETCH(int,field);QJsonObject row{{"n","A"},{"dl","https://files.example.org/owned.torrent"},{"s","3"},{"p","4"}};if(field==0){row.insert("r","bad");}else if(field==1){row.insert("c",3);}else if(field==2){row.insert("ct",3);}else if(field==3){row.insert("cdp",3);}else{row.insert("tgs",QJsonArray{1});}f::Profile area(row);const auto before=area.sourceBytes();auto source=s::SavedSubscriptionResultSnapshot::read(*area.lease,area.path.toStdString(),76,area.resultSelector());QVERIFY(!source);QVERIFY(area.sourceBytes()==before);
 }
 void savedGetterNarrowingRetainsIndependentRawPreparation(){
   f::Profile area({{"n","A"},{"dl","https://files.example.org/owned.torrent"},{"s","4294967297"},{"p","2147483648"}});auto source=s::SavedSubscriptionResultSnapshot::read(*area.lease,area.path.toStdString(),75,area.resultSelector());QVERIFY(source);const auto& value=source->description().result;QCOMPARE(value.seeds,std::int32_t{1});QCOMPARE(value.peers,std::numeric_limits<std::int32_t>::min());QCOMPARE(value.initialScrapeCache,std::optional{std::int64_t{0x0000000100000000LL}});
 }
 void actualSourceMutationWorkerSessionAndRetainedLockStayBound(){
   const QJsonObject row{{"n","A"},{"dl","https://files.example.org/owned.torrent"},{"s","-1"},{"p",QJsonValue{}}};f::Profile area(row);auto opened=s::SavedSubscriptionResultSnapshot::read(*area.lease,area.path.toStdString(),73,area.resultSelector());QVERIFY(opened);std::optional<s::SavedSubscriptionResultSnapshot> source(std::move(*opened));QVERIFY(!source->current(*area.lease,74));
   std::optional<s::SavedRssSourceError> other;std::thread worker([&]{auto result=source->current(*area.lease,73);if(!result){other=result.error();}});worker.join();QCOMPARE(other,std::optional{s::SavedRssSourceError::WrongWorker});
   const auto before=source->current(*area.lease,73);QVERIFY(before);QVERIFY(storage::ConfigFile::createMissing(*area.lease,"downloads.config","d9:downloadslee"));const auto after=source->current(*area.lease,73);QVERIFY(after&&*after>*before);
   area.save(row,true);QVERIFY(!source->current(*area.lease,73));area.lease.reset();QVERIFY(!storage::ProfileLease::acquire(area.path.toStdString()));source.reset();auto acquired=storage::ProfileLease::acquire(area.path.toStdString());QVERIFY(acquired);
 }
};
QTEST_GUILESS_MAIN(SavedSubscriptionResultSourceTests)
#include "saved_subscription_result_source_tests.moc"
