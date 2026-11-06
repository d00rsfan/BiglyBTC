// SPDX-License-Identifier: GPL-2.0-or-later
#include "subscription_numeric_filter.hpp"
#include <QtTest>
#include <limits>
using namespace biglybtc::search;
class SubscriptionNumericFilterTests final:public QObject {
 Q_OBJECT
private slots:
 void originalMapLongTypesAndStrings_data(){
   QTest::addColumn<QByteArray>("json");QTest::addColumn<qint64>("threshold");
   QTest::newRow("missing")<<QByteArray("{}")<<qint64{-1};
   QTest::newRow("imported-zero-stays-active")<<QByteArray(R"({"min_size":0})")<<qint64{0};
   QTest::newRow("negative-threshold-inactive")<<QByteArray(R"({"min_size":-2})")<<qint64{-2};
   QTest::newRow("long-min")<<QByteArray(R"({"min_size":-9223372036854775808})")<<std::numeric_limits<qint64>::min();
   QTest::newRow("long-max")<<QByteArray(R"({"min_size":9223372036854775807})")<<std::numeric_limits<qint64>::max();
   QTest::newRow("decimal-point-is-Double-not-Long")<<QByteArray(R"({"min_size":3.0})")<<qint64{-1};
   QTest::newRow("exponent-is-Double-not-Long")<<QByteArray(R"({"min_size":1e0})")<<qint64{-1};
   QTest::newRow("boolean-ignored")<<QByteArray(R"({"min_size":true})")<<qint64{-1};
   QTest::newRow("null-ignored")<<QByteArray(R"({"min_size":null})")<<qint64{-1};
   QTest::newRow("object-ignored")<<QByteArray(R"({"min_size":{"future":1}})")<<qint64{-1};
   QTest::newRow("plus-signed-string")<<QByteArray(R"({"min_size":"+12"})")<<qint64{12};
   QTest::newRow("negative-zero-string")<<QByteArray(R"({"min_size":"-0"})")<<qint64{0};
   QTest::newRow("unicode-BMP-decimal-string")<<QByteArray(R"({"min_size":"\u0661\uff12"})")<<qint64{12};
   QTest::newRow("unknown-filter-retained")<<QByteArray(R"({ "future" : {"opaque":[true,"raw"]}, "min_size":"0007", "max_peers":0 })")<<qint64{7};
 }
 void originalMapLongTypesAndStrings(){
   QFETCH(QByteArray,json);QFETCH(qint64,threshold);const auto raw=json.toStdString();auto value=parseSubscriptionNumericFilter(raw);QVERIFY(value);QCOMPARE(value->minSize,static_cast<std::int64_t>(threshold));QCOMPARE(value->active(),threshold>=0);QCOMPARE(value->originalJson,std::optional{raw});
 }
 void inclusiveThresholdsAndUnknownGetterValues_data(){
   QTest::addColumn<QByteArray>("json");QTest::addColumn<bool>("name");QTest::addColumn<qint64>("size");QTest::addColumn<int>("seeds");QTest::addColumn<int>("peers");QTest::addColumn<bool>("filtered");
   QTest::newRow("null-name-always-invalid")<<QByteArray("{}")<<false<<qint64{0}<<0<<0<<true;
   QTest::newRow("all-unknown-no-limits")<<QByteArray("{}")<<true<<qint64{-1}<<-1<<-1<<false;
   QTest::newRow("min-size-inclusive")<<QByteArray(R"({"min_size":10})")<<true<<qint64{10}<<0<<0<<false;
   QTest::newRow("below-min-size")<<QByteArray(R"({"min_size":10})")<<true<<qint64{9}<<0<<0<<true;
   QTest::newRow("max-size-inclusive")<<QByteArray(R"({"max_size":10})")<<true<<qint64{10}<<0<<0<<false;
   QTest::newRow("above-max-size")<<QByteArray(R"({"max_size":10})")<<true<<qint64{11}<<0<<0<<true;
   QTest::newRow("unknown-size-passes-max0")<<QByteArray(R"({"max_size":0})")<<true<<qint64{-1}<<0<<0<<false;
   QTest::newRow("unknown-size-fails-min0")<<QByteArray(R"({"min_size":0})")<<true<<qint64{-1}<<0<<0<<true;
   QTest::newRow("unknown-seeds-fail-min0")<<QByteArray(R"({"min_seeds":0})")<<true<<qint64{0}<<-1<<0<<true;
   QTest::newRow("saved-normalized-zero-seeds-pass-min0")<<QByteArray(R"({"min_seeds":0})")<<true<<qint64{0}<<0<<0<<false;
   QTest::newRow("max-seeds-inclusive")<<QByteArray(R"({"max_seeds":2})")<<true<<qint64{0}<<2<<0<<false;
   QTest::newRow("above-max-seeds")<<QByteArray(R"({"max_seeds":2})")<<true<<qint64{0}<<3<<0<<true;
   QTest::newRow("min-peers-inclusive")<<QByteArray(R"({"min_peers":3})")<<true<<qint64{0}<<0<<3<<false;
   QTest::newRow("unknown-peers-fail-min0")<<QByteArray(R"({"min_peers":0})")<<true<<qint64{0}<<0<<-1<<true;
   QTest::newRow("contradictory-range-stays-contradictory")<<QByteArray(R"({"min_size":11,"max_size":10})")<<true<<qint64{10}<<0<<0<<true;
   QTest::newRow("long-max-threshold-not-int-truncated")<<QByteArray(R"({"min_seeds":9223372036854775807})")<<true<<qint64{0}<<std::numeric_limits<int>::max()<<0<<true;
 }
 void inclusiveThresholdsAndUnknownGetterValues(){
   QFETCH(QByteArray,json);QFETCH(bool,name);QFETCH(qint64,size);QFETCH(int,seeds);QFETCH(int,peers);QFETCH(bool,filtered);auto value=parseSubscriptionNumericFilter(json.toStdString());QVERIFY(value);QCOMPARE(isSubscriptionNumericFiltered(*value,{name,size,seeds,peers}),filtered);
 }
 void unsupportedAndMalformedFiltersRefuse_data(){
   QTest::addColumn<QByteArray>("json");QTest::addColumn<bool>("unsupported");
   QTest::newRow("regex-text")<<QByteArray(R"({"text_filter":"a.*"})")<<true;
   QTest::newRow("exclusion-text")<<QByteArray(R"({"text_filter_out":"x"})")<<true;
   QTest::newRow("tab-is-token-not-space-delimiter")<<QByteArray(R"({"text_filter":"\t"})")<<true;
   QTest::newRow("category-even-empty-is-active")<<QByteArray(R"({"category":""})")<<true;
   QTest::newRow("zero-age-is-active")<<QByteArray(R"({"max_age":0})")<<true;
   QTest::newRow("numeric-string-overflow")<<QByteArray(R"({"min_size":"9223372036854775808"})")<<false;
   QTest::newRow("numeric-string-underflow")<<QByteArray(R"({"min_size":"-9223372036854775809"})")<<false;
   QTest::newRow("no-whitespace-trimming")<<QByteArray(R"({"min_size":" 1"})")<<false;
   QTest::newRow("fractional-string-invalid")<<QByteArray(R"({"min_size":"1.0"})")<<false;
   QTest::newRow("supplementary-decimal-surrogates-invalid")<<QByteArray(R"({"min_size":"\ud835\udfcf"})")<<false;
 }
 void unsupportedAndMalformedFiltersRefuse(){
   QFETCH(QByteArray,json);QFETCH(bool,unsupported);const auto raw=json;auto value=parseSubscriptionNumericFilter(json.toStdString());QVERIFY(!value);QCOMPARE(value.error()==SubscriptionValueError::Unsupported,unsupported);QCOMPARE(json,raw);
 }
 void absentInactiveAndBoundsRetainRaw(){
   auto missing=parseSubscriptionNumericFilter(std::nullopt);QVERIFY(missing&&!missing->active()&&!missing->originalJson);auto nullValue=parseSubscriptionNumericFilter(" \t null\r\n");QVERIFY(nullValue&&!nullValue->active());
   auto inactive=parseSubscriptionNumericFilter(R"({"text_filter":"   ","text_filter_out":null,"category":17,"max_age":-1,"min_size":-5})");QVERIFY(inactive&&!inactive->active());
   for(const auto& raw:{std::string(R"({"min_size":1,"min_size":2})"),std::string(R"({"future":{"x":1,"x":2}})"),std::string("[]"),std::string("\xc2\xa0null"),std::string(65537,' ')}){QVERIFY(!parseSubscriptionNumericFilter(raw));}
   auto all=parseSubscriptionNumericFilter(R"({"min_size":1,"max_size":2,"min_seeds":3,"max_seeds":4,"min_peers":5})");QVERIFY(all);QCOMPARE(all->minSize,std::int64_t{1});QCOMPARE(all->maxSize,std::int64_t{2});QCOMPARE(all->minSeeds,std::int64_t{3});QCOMPARE(all->maxSeeds,std::int64_t{4});QCOMPARE(all->minPeers,std::int64_t{5});
 }
};
QTEST_GUILESS_MAIN(SubscriptionNumericFilterTests)
#include "subscription_numeric_filter_tests.moc"
