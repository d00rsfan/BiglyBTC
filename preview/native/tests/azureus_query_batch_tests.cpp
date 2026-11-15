// SPDX-License-Identifier: GPL-2.0-or-later
#include "../services/azureus_query_storage/query_batch.hpp"
#include <QTest>
namespace q=biglybtc::services::azureus_query_storage;
namespace {
std::vector<std::string> flatten(const q::RequestBody& body){std::vector<std::string> result;for(const auto& group:body.groups){for(const auto& suffix:group.suffixes){result.push_back(group.prefix+suffix);}}return result;}
}
class AzureusQueryBatchTests final:public QObject {
 Q_OBJECT
private slots:
 void finalPartialPrefixAndExactMtuArePreserved_data(){QTest::addColumn<bool>("ipv6");QTest::newRow("IPv4")<<false;QTest::newRow("IPv6")<<true;}
 void finalPartialPrefixAndExactMtuArePreserved(){
  QFETCH(bool,ipv6);q::udp::EncodeFields h;h.action=1038;h.connection=INT64_MIN;h.originator=q::udp::Address{};h.originator->length=ipv6?16:4;h.originator->port=4444;q::RequestBody body;body.groups.push_back({"ab",{"cdef"}});body.groups.push_back({"",{}});for(unsigned n=0;n<1700;++n){std::string suffix(6,'Q');suffix[0]=char(n>>8);suffix[1]=char(n);body.groups.back().suffixes.push_back(suffix);}const auto expected=flatten(body);auto pages=q::splitRequest(body,h);QVERIFY(pages);QVERIFY(pages->size()>4);std::vector<std::string> actual;
  for(const auto& page:*pages){auto wire=q::encodeRequest({h,page});QVERIFY(wire);QVERIFY(wire->size()<=1400);auto decoded=q::decode(q::udp::Kind::Request,*wire,{});QVERIFY(decoded);auto rows=flatten(std::get<q::RequestBody>(decoded->body()));QVERIFY(rows.size()<=384);actual.insert(actual.end(),rows.begin(),rows.end());}QCOMPARE(actual,expected);
 }
 void zeroWidthRowsAndEmptyLivenessGroupsStayBounded(){
  q::udp::EncodeFields h;h.action=1038;h.connection=INT64_MIN;q::RequestBody body;body.groups={{"abcdef",std::vector<std::string>(1000)}};auto pages=q::splitRequest(body,h);QVERIFY(pages);QCOMPARE(pages->size(),std::size_t{3});std::size_t rows=0;for(const auto& page:*pages){rows+=flatten(page).size();QVERIFY(flatten(page).size()<=384);}QCOMPARE(rows,std::size_t{1000});body.groups={{"ab",{}},{"abc",{}}};pages=q::splitRequest(body,h);QVERIFY(pages);QCOMPARE(pages->size(),std::size_t{1});QCOMPARE(pages->front().groups,body.groups);body.groups.clear();pages=q::splitRequest(body,h);QVERIFY(pages&&pages->size()==1);QVERIFY(pages->front().groups.empty());
 }
 void badWidthsCountsAndSizingHeadersDoNotYieldPackets(){
  q::udp::EncodeFields h;h.action=1038;h.connection=INT64_MIN;q::RequestBody body;body.groups={{"abc",{"bad"}}};QVERIFY(!q::splitRequest(body,h,63));QVERIFY(!q::splitRequest(body,h,1401));body.groups={{"abcdefg",{}}};QVERIFY(!q::splitRequest(body,h));body.groups={{"abc",{"wrong"}}};QVERIFY(!q::splitRequest(body,h));body.groups={{"abcdef",std::vector<std::string>(8193)}};QVERIFY(!q::splitRequest(body,h));body={};h.action=1024;QVERIFY(!q::splitRequest(body,h));
 }
};
QTEST_GUILESS_MAIN(AzureusQueryBatchTests)
#include "azureus_query_batch_tests.moc"
