// SPDX-License-Identifier: GPL-2.0-or-later
#include "subscription_association_values.hpp"
#include <QtTest>
#include <limits>
using namespace biglybtc;
namespace d=search::detail;
namespace {
std::string b(std::string_view value){return std::to_string(value.size())+":"+std::string(value);}
d::SubscriptionAssociationHash hash(unsigned value){d::SubscriptionAssociationHash out{};out[0]=std::byte(value>>8);out[1]=std::byte(value&255);return out;}
std::string association(unsigned value,std::int64_t when){const auto raw=hash(value);return "d6:future3:raw1:h"+b(std::string_view(reinterpret_cast<const char*>(raw.data()),raw.size()))+"1:wi"+std::to_string(when)+"ee";}
std::string metadata(unsigned rows,bool duplicate=false){std::string out="d5:assocl";for(unsigned i=0;i<rows;++i){out+=association(duplicate?0:i,-42);}return out+"e6:future3:raw3:sig6:opaquee";}
}
class SubscriptionAssociationValuesTests final:public QObject {
 Q_OBJECT
private slots:
 void duplicatesNeverRefreshTimeOrEvict_data(){QTest::addColumn<int>("rows");QTest::newRow("ordinary")<<1;QTest::newRow("at-cap")<<256;QTest::newRow("oversized-existing-duplicates")<<257;}
 void duplicatesNeverRefreshTimeOrEvict(){QFETCH(int,rows);auto values=d::parseSubscriptionAssociations(metadata(static_cast<unsigned>(rows),true));QVERIFY(values);const auto before=*values->originalList;auto plan=d::planSubscriptionAssociationValue(*values,hash(0),std::numeric_limits<std::int64_t>::max());QVERIFY(plan&&!plan->changed);QCOMPARE(plan->encodedList,before);QCOMPARE(values->rows[0].when,std::int64_t{-42});}
 void appendPreservesRawUnknownValuesAndSignedTimes_data(){QTest::addColumn<qint64>("now");QTest::newRow("zero")<<qint64{0};QTest::newRow("negative")<<qint64{-1};QTest::newRow("minimum")<<std::numeric_limits<qint64>::min();QTest::newRow("maximum")<<std::numeric_limits<qint64>::max();}
 void appendPreservesRawUnknownValuesAndSignedTimes(){QFETCH(qint64,now);const auto original=metadata(1);auto values=d::parseSubscriptionAssociations(original);QVERIFY(values);const auto oldRow=values->rows[0].encoded;auto plan=d::planSubscriptionAssociationValue(*values,hash(7),now);QVERIFY(plan&&plan->changed);auto changed=core::Document::parse(original);QVERIFY(changed);QVERIFY(changed->replaceValue("assoc",plan->encodedList));auto next=d::parseSubscriptionAssociations(changed->serialize());QVERIFY(next);QCOMPARE(next->rows.size(),std::size_t{2});QCOMPARE(next->rows[0].encoded,oldRow);QCOMPARE(next->rows[1].when,static_cast<std::int64_t>(now));QCOMPARE(next->rows[1].hash,hash(7));QCOMPARE(std::string(next->metadata->find("future")->bytes),std::string("raw"));QCOMPARE(std::string(next->metadata->find("sig")->bytes),std::string("opaque"));QCOMPARE(values->metadata->originalBytes(),original);}
 void defaultCapacityRefusesUnprovedRandomEviction_data(){QTest::addColumn<int>("rows");QTest::newRow("one-space")<<255;QTest::newRow("at-cap")<<256;QTest::newRow("over-cap")<<257;}
 void defaultCapacityRefusesUnprovedRandomEviction(){QFETCH(int,rows);auto values=d::parseSubscriptionAssociations(metadata(static_cast<unsigned>(rows)));QVERIFY(values);const auto capacity=d::subscriptionAssociationCapacity(*values);QCOMPARE(capacity,rows<256?d::SubscriptionAssociationCapacity::AppendWithoutEviction:d::SubscriptionAssociationCapacity::OriginalRandomEvictionRequired);QCOMPARE(d::subscriptionAssociationCapacity(*values,hash(0)),d::SubscriptionAssociationCapacity::AlreadyPresent);auto next=d::planSubscriptionAssociationValue(*values,hash(777),123);if(rows<256){QVERIFY(next&&next->changed);}else{QVERIFY(!next);QCOMPARE(next.error(),search::SubscriptionValueError::Unsupported);}}
 void absentAndMalformedAssociationValues(){auto absent=d::parseSubscriptionAssociations("d6:future3:rawe");QVERIFY(absent&&!absent->originalList&&absent->rows.empty());auto append=d::planSubscriptionAssociationValue(*absent,hash(0),123);QVERIFY(append&&append->changed);for(const auto& raw:{std::string("d5:associ0ee"),std::string("d5:assocld1:h1:x1:wi1eeee"),std::string("d5:assocld1:h20:000000000000000000001:w1:xeee")}){QVERIFY(!d::parseSubscriptionAssociations(raw));}}
};
QTEST_GUILESS_MAIN(SubscriptionAssociationValuesTests)
#include "subscription_association_values_tests.moc"
