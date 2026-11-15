// SPDX-License-Identifier: GPL-2.0-or-later
#include "../services/azureus_key_block/state.hpp"
#include <QTest>
#include <QFile>
#include <memory>
namespace block=biglybtc::services::azureus_key_block;namespace core=biglybtc::core;
std::string unhex(const QByteArray& bytes){return QByteArray::fromHex(bytes).toStdString();}
struct Fixture {
 std::string modulus,exponent,request,signature;
 Fixture(){QFile f(QString::fromUtf8(KEY_BLOCK_POLICY_FIXTURE));if(!f.open(QIODevice::ReadOnly)){throw std::runtime_error("Generated key policy fixture");}while(!f.atEnd()){const auto fields=f.readLine().trimmed().split('\t');if(fields[0]=="test-key"){modulus=fields[1].toStdString();exponent=fields[2].toStdString();}else if(fields[1]=="operation"){request=unhex(fields[5]);signature=unhex(fields[6]);break;}}}
 block::Verifier verifier()const{return [n=modulus,e=exponent](std::string_view req,std::string_view sig){return block::verifySignatureStatus(req,sig,{n,e});};}
};
std::string origin(char n){std::string id(20,'\0');id[0]=n;return id;}
class AzureusKeyBlockStateTests final:public QObject{Q_OBJECT
private slots:
 void actualOriginalAdmissionTransitions(){
  Fixture fixture;QFile file(QString::fromUtf8(KEY_BLOCK_POLICY_FIXTURE));QVERIFY(file.open(QIODevice::ReadOnly));std::map<QByteArray,std::unique_ptr<block::State>> states;unsigned count=0;const auto sender=origin(1);
  while(!file.atEnd()){const auto f=file.readLine().trimmed().split('\t');if(f[0]=="test-key"||f[0]=="signature"){continue;}auto& state=states[f[0]];if(!state){state=std::make_unique<block::State>(fixture.verifier());}const auto req=unhex(f[5]);auto admitted=state->admit(req,unhex(f[6]),f[2]=="1"?std::optional<std::string_view>{sender}:std::nullopt,1000000);QVERIFY(admitted);QCOMPARE(admitted->returnedBlocked,f[7]=="1");QCOMPARE(state->blocked(req.substr(8))!=nullptr,f[8]=="1");const auto records=state->records();QCOMPARE(records.empty()?QByteArray("-"):QByteArray::fromStdString(records[0].request).toHex(),f[9]);QCOMPARE(!records.empty()&&records[0].direct,f[10]=="1");QCOMPARE(state->records(true).size(),std::size_t(f[11].toUInt()));++count;
  }QCOMPARE(count,40u);
 }
 void actualOriginalSavedFilesAndOpaqueFields(){
  Fixture fixture;QFile file(QString::fromUtf8(KEY_BLOCK_SAVED_FIXTURE));QVERIFY(file.open(QIODevice::ReadOnly));unsigned count=0;
  while(!file.atEnd()){const auto f=file.readLine().trimmed().split('\t');if(f[0]=="test-key"){continue;}auto document=core::Document::parse(unhex(f[2]));QVERIFY(document);const auto wall=f[1].toLongLong();auto loaded=block::decodeSaved(document->root(),wall,fixture.verifier());QVERIFY(loaded);QCOMPARE(!loaded->records.empty(),f[3]=="1");if(!loaded->records.empty()){QCOMPARE(loaded->records[0].received,f[4].toInt());QCOMPARE(loaded->records[0].direct,f[5]=="1");}QCOMPARE(std::count_if(loaded->records.begin(),loaded->records.end(),[](const auto& r){return r.direct;}),f[6].toInt());
   block::State state(fixture.verifier());QVERIFY(state.restore(loaded->records));const auto records=state.records();QCOMPARE(records,loaded->records);auto encoded=block::encodeSaved(document->root(),records);QVERIFY(encoded);auto saved=core::Document::parse(*encoded);QVERIFY(saved);QVERIFY(saved->find("opaque-root"));QCOMPARE(saved->find("opaque-root")->bytes,std::string_view("retained"));if(!records.empty()){const auto* entries=saved->find("entries");QVERIFY(entries&&entries->children.size()==1);const auto* opaque=entries->children[0].find("opaque-entry");QVERIFY(opaque);QCOMPARE(opaque->bytes,std::string_view("retained"));}auto again=block::decodeSaved(saved->root(),wall,fixture.verifier());QVERIFY(again);QCOMPARE(again->records,records);++count;
  }QCOMPARE(count,36u);
 }
 void failureBloomUsesStrictWallClockAndExceptionRules(){
  Fixture f;const auto a=origin(1),b=origin(2);auto bad=f.signature;bad[0]^=1;QCOMPARE(f.verifier()(f.request,bad),block::Verification::Invalid);QCOMPARE(f.verifier()(f.request,std::string(1,'x')),block::Verification::Unavailable);
  block::State first(f.verifier());auto refused=first.admit(f.request,bad,a,1000);QVERIFY(refused&&!refused->returnedBlocked);const auto revision=first.revision();auto throttled=first.admit(f.request,f.signature,a,1801000);QVERIFY(throttled&&!throttled->changed);QCOMPARE(first.revision(),revision);auto expired=first.admit(f.request,f.signature,a,1801001);QVERIFY(expired&&expired->changed);
  block::State other(f.verifier());QVERIFY(other.admit(f.request,bad,a,1000));auto independent=other.admit(f.request,f.signature,b,1001);QVERIFY(independent&&independent->changed);
  block::State backward(f.verifier());QVERIFY(backward.admit(f.request,bad,a,1000));auto reset=backward.admit(f.request,f.signature,a,999);QVERIFY(reset&&reset->changed);
  bool available=false;block::State errors([&](auto req,auto sig){return available?f.verifier()(req,sig):block::Verification::Unavailable;});QVERIFY(!errors.admit(f.request,f.signature,a,1000));available=true;auto retry=errors.admit(f.request,f.signature,a,1001);QVERIFY(retry&&retry->changed);
 }
 void senderKnowledgeAndIdChangeDoNotEraseIndirectRecords(){
  Fixture f;block::State state(f.verifier());const auto a=origin(1),b=origin(2),key=f.request.substr(8);auto admitted=state.admit(f.request,f.signature,a,1000);QVERIFY(admitted&&admitted->changed);QVERIFY(state.hasSent(key,a));QVERIFY(!state.hasSent(key,b));QVERIFY(state.markSent(key,b));QVERIFY(state.hasSent(key,b));const auto revision=state.revision();QVERIFY(state.purgeDirect());QCOMPARE(state.revision(),revision+1);QVERIFY(!state.blocked(key));QVERIFY(!state.hasSent(key,a));QVERIFY(state.admit(f.request,f.signature,std::nullopt,1000));QVERIFY(!state.purgeDirect());QVERIFY(state.blocked(key));
 }
 void malformedAndOversizedSavedAreasCannotReplaceHealthyState(){
  Fixture f;block::State state(f.verifier());const auto a=origin(1),key=f.request.substr(8);QVERIFY(state.admit(f.request,f.signature,a,1000));const auto previous=state.records();auto invalid=previous;invalid[0].signature="x";QVERIFY(!state.restore(invalid));QCOMPARE(state.records(),previous);invalid.assign(block::maximumRecords+1,previous[0]);QVERIFY(!state.restore(invalid));QCOMPARE(state.records(),previous);auto malformed=core::Document::parse("d7:entriesi1ee");QVERIFY(malformed);QVERIFY(!block::decodeSaved(malformed->root(),1000,f.verifier()));QVERIFY(!block::encodeSaved(malformed->root(),previous));QVERIFY(!state.admit(std::string(8,'x'),"",a,1000));QVERIFY(!state.admit(f.request,f.signature,std::string_view("short"),1000));QCOMPARE(state.records(),previous);
 }
};
QTEST_GUILESS_MAIN(AzureusKeyBlockStateTests)
#include "azureus_key_block_state_tests.moc"
