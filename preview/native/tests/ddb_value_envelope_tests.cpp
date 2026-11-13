// SPDX-License-Identifier: GPL-2.0-or-later
#include "value_envelope.hpp"
#include <QCryptographicHash>
#include <QProcess>
#include <QtTest>
using namespace biglybtc::services::azureus_ddb;
namespace {
std::span<const std::uint8_t> bytes(const QByteArray& value){return {reinterpret_cast<const std::uint8_t*>(value.data()),std::size_t(value.size())};}
QByteArray packed(unsigned header,std::initializer_list<QByteArray> entries){QByteArray result(1,char(header));for(const auto& value:entries){result+=char(value.size()>>8);result+=char(value.size());result+=value;}return result;}
}
class DdbValueEnvelopeTests final:public QObject {
 Q_OBJECT
private slots:
 void exactNormalReaderBlockMatchesValuesErrorsAndContinuationIntent(){
  if(!qEnvironmentVariableIsSet("BIGLYBTC_DDB_VALUE_JAVA")){QSKIP("Pinned original normal block not configured");}
  struct Case{unsigned flags;QByteArray raw,key;};std::vector<Case> cases;const QByteArray application("01234567890123456789.rating"),next=QCryptographicHash::hash(application,QCryptographicHash::Sha1);for(unsigned flags=0;flags<256;++flags){cases.push_back({flags,packed(1,{"","x","x"}),application});}
  for(unsigned size=0;size<=512;++size){QByteArray raw(qsizetype(size),'x');if(size){raw[0]=char(size%3);}for(unsigned n=1;n<size;++n){raw[int(n)]=char((size*17+n*43)&255);}cases.push_back({4,std::move(raw),size%2?application:next});}
  for(unsigned header:{0,1,2,255}){for(const auto& body:{packed(header,{}),packed(header,{""}),packed(header,{"x","","abc","abc"}),packed(header,{QByteArray(509,'q')})}){cases.push_back({4,body,application});auto odd=body;odd+='\xff';if(odd.size()<=512){cases.push_back({4,odd,next});}auto oversized=body;oversized+="\xff\xff";if(oversized.size()<=512){cases.push_back({4,oversized,application});}}}
  QByteArray input;for(const auto& item:cases){input+=QByteArray::number(item.flags)+'\t'+item.raw.toHex()+'\t'+item.key.toHex()+'\n';}QProcess java;java.start(qEnvironmentVariable("BIGLYBTC_DDB_VALUE_JAVA"),{"-cp",qEnvironmentVariable("BIGLYBTC_DDB_VALUE_CLASSPATH"),"DdbNormalValueOracle"});QVERIFY(java.waitForStarted(5000));java.write(input);java.closeWriteChannel();QVERIFY(java.waitForFinished(10000));QVERIFY2(java.exitCode()==0,java.readAllStandardError().constData());auto lines=java.readAllStandardOutput().split('\n');if(!lines.empty()&&lines.back().isEmpty()){lines.pop_back();}QCOMPARE(lines.size(),qsizetype(cases.size()));for(std::size_t n=0;n<cases.size();++n){const auto& item=cases[n];auto value=decodeNormalValue(std::uint8_t(item.flags),bytes(item.raw));QVERIFY(value);QByteArray outcome=value->fault==Fault::MissingHeader||value->fault==Fault::TruncatedLength?"THROW":value->fault==Fault::LengthExceedsPayload?"LOG":"OK";QByteArray events;for(unsigned i=0;i<value->count;++i){if(i){events+=',';}events+=item.raw.mid(value->entries[i].offset,value->entries[i].length).toHex();}auto actual=outcome+'\t'+QByteArray::number(value->continuation?1:0)+'\t'+(value->continuation?QCryptographicHash::hash(item.key,QCryptographicHash::Sha1).toHex():QByteArray{})+'\t'+QByteArray::number(value->count)+'\t'+events;QCOMPARE(actual,lines[qsizetype(n)]);}
 }
 void nativeQuotaAndPackedDuplicateEventsRemainExplicit(){
  QByteArray tooLarge(513,'x');QVERIFY(!decodeNormalValue(0,bytes(tooLarge)));auto raw=packed(0,{"same","same",""});auto value=decodeNormalValue(4,bytes(raw));QVERIFY(value);QCOMPARE(value->count,std::uint16_t{3});QCOMPARE(raw.mid(value->entries[0].offset,value->entries[0].length),raw.mid(value->entries[1].offset,value->entries[1].length));auto combined=decodeNormalValue(0x14,bytes(raw));QVERIFY(combined);QVERIFY(!combined->packed);QCOMPARE(combined->count,std::uint16_t{1});QCOMPARE(combined->entries[0].length,std::uint16_t(raw.size()));QByteArray maximal(511,'\0');auto many=decodeNormalValue(4,bytes(maximal));QVERIFY(many);QCOMPARE(many->count,std::uint16_t{255});QCOMPARE(many->fault,Fault::None);
 }
};
QTEST_GUILESS_MAIN(DdbValueEnvelopeTests)
#include "ddb_value_envelope_tests.moc"
