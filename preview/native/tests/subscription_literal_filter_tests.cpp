// SPDX-License-Identifier: GPL-2.0-or-later
#include "subscription_literal_filter.hpp"
#include <QtTest>
using namespace biglybtc::search;
class SubscriptionLiteralFilterTests final:public QObject {
 Q_OBJECT
private slots:
 void originalAsciiContainsAndTokenization_data(){
  QTest::addColumn<QByteArray>("json");QTest::addColumn<QString>("name");QTest::addColumn<bool>("filtered");
  QTest::newRow("include-all-case-folded-substrings")<<QByteArray("{\"text_filter\":\"  WEBRip   1080P \"}")<<QString("Title.1080p.WEBrip")<<false;
  QTest::newRow("include-any-is-insufficient")<<QByteArray("{\"text_filter\":\"webrip 1080p\"}")<<QString("Title.1080p.BluRay")<<true;
  QTest::newRow("no-word-boundary")<<QByteArray("{\"text_filter\":\"rip\"}")<<QString("Tripod")<<false;
  QTest::newRow("exclude-any")<<QByteArray("{\"text_filter_out\":\"cam ts\"}")<<QString("SCAM.release")<<true;
  QTest::newRow("exclude-no-hit")<<QByteArray("{\"text_filter\":\"1080p\",\"text_filter_out\":\"cam ts\"}")<<QString("Movie.1080p")<<false;
  QTest::newRow("include-and-exclude")<<QByteArray("{\"text_filter\":\"1080p\",\"text_filter_out\":\"web\"}")<<QString("WEB.1080p")<<true;
  QTest::newRow("hyphen-and-underscore-literal")<<QByteArray("{\"text_filter\":\"abc-def _x\"}")<<QString("ABC-DEF_x")<<false;
  QTest::newRow("non-string-JSON-text-ignored")<<QByteArray("{\"text_filter\":17,\"text_filter_out\":[\"x\"]}")<<QString("x")<<false;
  QTest::newRow("space-only-no-tokens")<<QByteArray("{\"text_filter\":\"   \"}")<<QString("")<<false;
 }
 void originalAsciiContainsAndTokenization(){
  QFETCH(QByteArray,json);QFETCH(QString,name);QFETCH(bool,filtered);auto policy=parseSubscriptionScanFilter(json.toStdString());QVERIFY(policy);auto result=isSubscriptionLiteralFiltered(policy->literal,name);QVERIFY(result);QCOMPARE(*result,filtered);QCOMPARE(policy->literal.originalJson,std::optional{json.toStdString()});
 }
 void regexUnicodeAndOtherPoliciesRefuse_data(){
  QTest::addColumn<QByteArray>("json");
  QTest::newRow("regex-dot")<<QByteArray("{\"text_filter\":\"a.b\"}");
  QTest::newRow("regex-alternation")<<QByteArray("{\"text_filter_out\":\"a|b\"}");
  QTest::newRow("regex-escape")<<QByteArray("{\"text_filter\":\"\\\\Qx\\\\E\"}");
  QTest::newRow("tab-not-token-delimiter")<<QByteArray("{\"text_filter\":\"x\\ty\"}");
  QTest::newRow("trimmed-empty-regex-would-hit-all")<<QByteArray("{\"text_filter_out\":\"\\t\"}");
  QTest::newRow("nbsp-not-space")<<QByteArray("{\"text_filter\":\"a\\u00a0b\"}");
  QTest::newRow("unicode-token")<<QByteArray("{\"text_filter\":\"caf\\u00e9\"}");
  QTest::newRow("category-prefix")<<QByteArray("{\"text_filter\":\"category:video\"}");
  QTest::newRow("tag-prefix")<<QByteArray("{\"text_filter\":\"tag:keep\"}");
  QTest::newRow("category-policy-still-refused")<<QByteArray("{\"text_filter\":\"x\",\"category\":\"\"}");
  QTest::newRow("age-still-refused")<<QByteArray("{\"text_filter\":\"x\",\"max_age\":0}");
 }
 void regexUnicodeAndOtherPoliciesRefuse(){
  QFETCH(QByteArray,json);auto policy=parseSubscriptionScanFilter(json.toStdString());QVERIFY(!policy);QCOMPARE(policy.error(),SubscriptionValueError::Unsupported);
 }
 void numericLexemesRawBytesAndNameDomain(){
  const std::string raw="{ \"future\":{\"opaque\":[true,\"x\"]}, \"text_filter\":\"Keep\",\"min_size\":3.0,\"max_size\":1e0,\"min_seeds\":\"+2\" }";
  auto policy=parseSubscriptionScanFilter(raw);QVERIFY(policy);QCOMPARE(policy->numeric.minSize,std::int64_t{-1});QCOMPARE(policy->numeric.maxSize,std::int64_t{-1});QCOMPARE(policy->numeric.minSeeds,std::int64_t{2});QCOMPARE(policy->numeric.originalJson,std::optional{raw});QCOMPARE(policy->literal.originalJson,std::optional{raw});
  auto missing=isSubscriptionLiteralFiltered(policy->literal,std::nullopt);QVERIFY(missing&&*missing);
  auto nonAscii=isSubscriptionLiteralFiltered(policy->literal,QString::fromUtf8("Keep\xc3\xa9"));QVERIFY(!nonAscii);QCOMPARE(nonAscii.error(),SubscriptionValueError::Unsupported);
  auto inactive=parseSubscriptionScanFilter("{}");QVERIFY(inactive);auto unchanged=isSubscriptionLiteralFiltered(inactive->literal,QString::fromUtf8("\xc3\xa9"));QVERIFY(unchanged&&!*unchanged);
  // The old numeric-only public loader keeps refusing active text unchanged.
  QVERIFY(!parseSubscriptionNumericFilter(raw));
  for(const auto& bad:{std::string("{\"text_filter\":\"x\",\"text_filter\":\"y\"}"),std::string("{\"future\":{\"x\":1,\"x\":2}}"),std::string(65537,' ')}){QVERIFY(!parseSubscriptionScanFilter(bad));}
 }
};
QTEST_GUILESS_MAIN(SubscriptionLiteralFilterTests)
#include "subscription_literal_filter_tests.moc"
