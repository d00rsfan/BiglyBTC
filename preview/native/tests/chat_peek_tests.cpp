// SPDX-License-Identifier: GPL-2.0-only
#include "peek.hpp"
#include "message.hpp"
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
using namespace biglybtc;namespace msg=services::msgsync;
namespace {
QByteArray hx(std::string_view value){return QByteArray(value.data(),qsizetype(value.size())).toHex();}
QByteArray field(const std::optional<std::string>& value){return value?hx(*value):QByteArray("-");}
QByteArray ask(QProcess& process,QByteArray value){process.write(value+'\n');process.waitForBytesWritten(1000);if(!process.canReadLine()){process.waitForReadyRead(3000);}return process.readLine().trimmed();}
void start(QProcess& process,const QString& root){auto environment=QProcessEnvironment::systemEnvironment();for(const auto* name:{"JAVA_TOOL_OPTIONS","JDK_JAVA_OPTIONS","_JAVA_OPTIONS","CLASSPATH","LD_PRELOAD","LD_AUDIT"}){environment.remove(QLatin1StringView(name));}process.setProcessEnvironment(environment);process.start(qEnvironmentVariable("BIGLYBTC_CHAT_PEEK_JAVA"),{"-cp",qEnvironmentVariable("BIGLYBTC_CHAT_PEEK_CLASSPATH"),"ChatPeekOracle",root});}
}
class ChatPeekTests final:public QObject{
 Q_OBJECT
private slots:
 void responderPayloadMatchesTheSelectedOriginalHandler(){
  if(!qEnvironmentVariableIsSet("BIGLYBTC_CHAT_PEEK_JAVA")){QSKIP("Original peek responder not configured");}QTemporaryDir root;QVERIFY(root.isValid());QProcess java;start(java,root.path());QVERIFY(java.waitForStarted());msg::PeekKey key{};for(unsigned n=0;n<key.size();++n){key[n]=std::uint8_t(n*7+1);}const auto encodedKey=hx({reinterpret_cast<const char*>(key.data()),key.size()});unsigned cases=0;
  for(const auto& random:std::vector<std::string>{"",std::string(1,'\0'),std::string(16,'x'),std::string(64,'r')}){for(int messages:{0,1,2,17}){for(int total:{0,1,30}){for(int live:{0,2}){for(int estimate:{0,100}){for(bool empty:{false,true}){auto request=msg::encodePeekRequest(key,random);QVERIFY(request);auto decoded=msg::decodePeekRequest(*request,key);QVERIFY(decoded);QCOMPARE(decoded->random,random);msg::PeekReply reply;reply.messages=messages;reply.total=total;reply.live=live;reply.estimate=estimate;const std::string signature=empty?"":"generated-signature",publicKey=empty?"":"generated-public-key";if(messages){reply.signature=signature;reply.publicKey=publicKey;}auto encoded=msg::encodePeekReply(reply);QVERIFY(encoded);const auto command="R "+encodedKey+' '+hx(*request)+' '+QByteArray::number(messages)+' '+QByteArray::number(total)+' '+QByteArray::number(live)+' '+QByteArray::number(estimate)+' '+hx(signature)+' '+hx(publicKey);QCOMPARE(ask(java,command),hx(*encoded));++cases;}}}}}}
  QCOMPARE(cases,384U);const auto valid=msg::encodePeekRequest(key,"generated");QVERIFY(valid);std::vector<std::string> refused{"de","i1e"};auto wrong=key;wrong[1]^=1;auto mismatch=msg::encodePeekRequest(wrong,"");QVERIFY(mismatch);refused.push_back(*mismatch);auto document=core::Document::parse(*valid);QVERIFY(document);QVERIFY(document->replaceValue("r","i1e"));refused.push_back(document->serialize());for(const auto& request:refused){QVERIFY(!msg::decodePeekRequest(request,key));QCOMPARE(ask(java,"R "+encodedKey+' '+hx(request)+" 1 1 1 1 73 70"),QByteArray("-"));}java.closeWriteChannel();QVERIFY(java.waitForFinished());QCOMPARE(java.exitCode(),0);
 }
 void aggregationMatchesTheOriginalListener(){
  if(!qEnvironmentVariableIsSet("BIGLYBTC_CHAT_PEEK_JAVA")){QSKIP("Original peek listener not configured");}QTemporaryDir root;QProcess java;start(java,root.path());QVERIFY(java.waitForStarted());unsigned cases=0;
  for(unsigned seed=0;seed<128;++seed){QCOMPARE(ask(java,"Z"),QByteArray("OK"));msg::PeekAggregate aggregate;for(unsigned n=0;n<3;++n){msg::PeekReply reply;reply.messages=int((seed>>(n*2))%7)-2;reply.live=int((seed+n)%13)-3;reply.estimate=int((seed+3*n)%19)-4;if((seed+n)%3){reply.signature=std::string((seed+n)%2?"generated-sig":"");}if((seed+n)%5){reply.publicKey="generated-pub";}auto encoded=msg::encodePeekReply(reply);QVERIFY(encoded);auto decoded=msg::decodePeekReply(*encoded);QVERIFY(decoded);const bool more=aggregate.add(*decoded);const auto& summary=aggregate.summary();const auto expected=QByteArray::number(more?1:0)+' '+QByteArray::number(summary.results)+' '+QByteArray::number(summary.messages)+' '+QByteArray::number(summary.nodes)+' '+field(summary.signature)+' '+field(summary.publicKey)+" 1";QCOMPARE(ask(java,"A "+hx(*encoded)),expected);++cases;}}
  QCOMPARE(cases,384U);for(const auto& raw:std::vector<std::string>{"de","d1:ei0e1:li0e1:m1:xe","d1:ei0e1:li0e1:mi0e1:si1ee"}){QVERIFY(!msg::decodePeekReply(raw));QCOMPARE(ask(java,"A "+hx(raw)),QByteArray("ERROR"));}
  QCOMPARE(ask(java,"Z"),QByteArray("OK"));const std::string raw="d1:ei4294967297e1:li-2147483649e1:mi9223372036854775807e1:t1:xe";auto reply=msg::decodePeekReply(raw);QVERIFY(reply);QCOMPARE(reply->messages,-1);QCOMPARE(reply->live,INT32_MAX);QCOMPARE(reply->estimate,1);QVERIFY(!reply->total);msg::PeekAggregate wrapped;QVERIFY(wrapped.add(*reply));QCOMPARE(ask(java,"A "+hx(raw)),QByteArray("1 1 0 2147483647 - - 1"));java.closeWriteChannel();QVERIFY(java.waitForFinished());QCOMPARE(java.exitCode(),0);
 }
 void liveEstimateMatchesOriginalBloomAndTimers(){
  if(!qEnvironmentVariableIsSet("BIGLYBTC_CHAT_PEEK_JAVA")){QSKIP("Original estimator not configured");}QTemporaryDir root;QProcess java;start(java,root.path());QVERIFY(java.waitForStarted());QCOMPARE(ask(java,"E 1000"),QByteArray("OK"));msg::LiveNodeEstimate estimate(1000);std::int64_t now=1000;unsigned cases=0;
  for(unsigned cycle=0;cycle<8;++cycle){for(unsigned n=0;n<140;++n){now+=n%7+1;const auto address=std::string(n%2?"127.123.2.":"0:0:0:0:0:0:0:")+std::to_string((n*19+cycle)%151)+":"+std::to_string(42000+n%21);estimate.alive(address,now);QCOMPARE(ask(java,"N "+QByteArray::number(qlonglong(now))+' '+hx(address)),QByteArray::number(estimate.value()));++cases;}for(auto delta:{119999,1,1,480000,600000}){now+=delta;estimate.tick(now);QCOMPARE(ask(java,"T "+QByteArray::number(qlonglong(now))),QByteArray::number(estimate.value()));++cases;}const std::string repeated="127.123.5.1:42123";for(auto delta:{1,120000,120001}){now+=delta;estimate.alive(repeated,now);QCOMPARE(ask(java,"N "+QByteArray::number(qlonglong(now))+' '+hx(repeated)),QByteArray::number(estimate.value()));++cases;}}
  QCOMPARE(cases,1184U);java.closeWriteChannel();QVERIFY(java.waitForFinished());QCOMPARE(java.exitCode(),0);
 }
 void encryptedPeekDoesNotNeedAParticipantIdentity(){
  auto requester=msg::PeekChannel::open("Generated encrypted peek");auto responder=msg::PeekChannel::open("Generated encrypted peek");auto stranger=msg::PeekChannel::open("Generated other peek");QVERIFY(requester&&responder&&stranger);msg::PeekReply reply;reply.messages=7;reply.live=3;reply.estimate=9;reply.total=11;reply.signature="generated-sig";reply.publicKey="generated-key";auto request=requester->request();QVERIFY(request);QVERIFY(!stranger->answer(*request,reply));auto answer=responder->answer(*request,reply);QVERIFY(answer);auto decoded=requester->receive(*answer);QVERIFY(decoded);QCOMPARE(decoded->messages,7);QCOMPARE(decoded->signature,reply.signature);QVERIFY(!stranger->receive(*answer));answer->back()^=1;QVERIFY(!requester->receive(*answer));QVERIFY(!requester->receive(std::string(msg::MaximumTransferBytes+1,'x')));QVERIFY(!msg::PeekChannel::open(""));QVERIFY(!msg::PeekChannel::open(std::string(4097,'x')));
 }
 void nativeLimitsAndLateResultsDoNotBecomeAuthority(){
  msg::PeekKey key{};QVERIFY(!msg::encodePeekRequest(key,std::string(msg::MaximumWireBytes,'x')));QVERIFY(!msg::decodePeekReply(std::string(msg::MaximumWireBytes+1,'x')));QVERIFY(!msg::decodePeekRequest("d1:k20:"+std::string(20,'\0')+"1:r0:1:r0:e",key));msg::PeekAggregate aggregate;msg::PeekReply reply;reply.messages=1;reply.live=2;reply.estimate=3;reply.signature="s";reply.publicKey="p";QVERIFY(aggregate.add(reply));QVERIFY(aggregate.add(reply));QVERIFY(!aggregate.add(reply));reply.messages=100;QVERIFY(!aggregate.add(reply));QCOMPARE(aggregate.summary().results,3U);QCOMPARE(aggregate.summary().messages,1);auto keys=plugins::msgsync::deriveChannelKeys(std::span(reinterpret_cast<const std::uint8_t*>("Generated peek"),14));QVERIFY(keys);auto expected=keys->listen;expected[1]^=1;QCOMPARE(keys->peek,expected);
 }
};
QTEST_GUILESS_MAIN(ChatPeekTests)
#include "chat_peek_tests.moc"
