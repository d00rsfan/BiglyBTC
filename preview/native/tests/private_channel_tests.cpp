// SPDX-License-Identifier: GPL-2.0-or-later
#include "private_channel.hpp"
#include "room.hpp"
#include "private_policy.hpp"
#include <QtTest>
#include <QProcess>
#include <QTemporaryDir>
#include <openssl/crypto.h>
using namespace biglybtc;
namespace chat=services::msgsync;namespace crypto=plugins::msgsync;
namespace {
template<class T,class E>T take(std::expected<T,E> v){if(!v){throw std::runtime_error("Generated private protocol rejected");}return std::move(*v);}
crypto::LegacySts sts(const crypto::GeneratedMessageIdentity& i){return take(crypto::LegacySts::create(i.publicKey,i.privateKey));}
std::span<const std::uint8_t> span(std::string_view s){return {reinterpret_cast<const std::uint8_t*>(s.data()),s.size()};}
std::string_view view(const auto& a){return {reinterpret_cast<const char*>(a.data()),a.size()};}
chat::PrivateRequest wire(const chat::PrivateRequest& r){return take(chat::decodePrivateRequest(take(chat::encodePrivateRequest(r))));}
chat::PrivateReply wire(const chat::PrivateReply& r){return take(chat::decodePrivateReply(take(chat::encodePrivateReply(r))));}
struct Pair {chat::PrivateInitiator initiator;chat::PrivateResponder responder;chat::PrivateRequest auth;};
Pair pair(const crypto::GeneratedMessageIdentity& a,const crypto::GeneratedMessageIdentity& b,std::string expected={}){
 auto initiator=take(chat::PrivateInitiator::create(sts(a),expected.empty()?b.publicKey:expected));auto first=wire(take(initiator.first()));
 auto responder=take(chat::PrivateResponder::create(sts(b),first));auto auth=wire(take(initiator.keysReply(wire(take(responder.first())))));return {std::move(initiator),std::move(responder),std::move(auth)};
}
class Oracle {
public:
 QTemporaryDir root;QProcess process;
 explicit Oracle(std::string_view general){
  auto env=QProcessEnvironment::systemEnvironment();env.insert("HOME",root.path());process.setProcessEnvironment(env);
  process.start(qEnvironmentVariable("BIGLYBTC_CHAT_PRIVATE_JAVA"),{"-Xmx128m","-cp",qEnvironmentVariable("BIGLYBTC_CHAT_PRIVATE_CLASSPATH"),"PrivateChatOracle",root.path()});
  if(!process.waitForStarted()){throw std::runtime_error("Oracle failed to start");}
  identity=raw(command("INIT "+hex(general)),"IDENTITY ");
 }
 ~Oracle(){process.closeWriteChannel();if(!process.waitForFinished(3000)){process.kill();process.waitForFinished();}}
 std::string identity;
 static QByteArray hex(std::string_view s){return QByteArray(s.data(),s.size()).toHex();}
 static std::string raw(const QByteArray& s,const QByteArray& prefix){if(!s.startsWith(prefix)){throw std::runtime_error("Unexpected oracle response: "+s.toStdString());}return QByteArray::fromHex(s.mid(prefix.size())).toStdString();}
 QByteArray command(const QByteArray& s){if(process.write(s+'\n')<0||!process.waitForBytesWritten(3000)){throw std::runtime_error("Oracle write failed");}return line();}
 QByteArray line(){while(!process.canReadLine()){if(!process.waitForReadyRead(10000)){throw std::runtime_error("Oracle response timed out: "+process.readAllStandardError().toStdString());}}return process.readLine().trimmed();}
};
}
class PrivateChannelTests final:public QObject {
 Q_OBJECT
private slots:
 void mutualAuthenticationAndPrivateCipher(){
  for(int round=0;round<12;++round){auto a=take(crypto::generateMessageIdentity()),b=take(crypto::generateMessageIdentity());auto p=pair(a,b);
   auto recipient=take(p.responder.finish(p.auth));auto sender=take(p.initiator.finish(wire(chat::PrivateReply{})));QCOMPARE(sender.peerKey(),std::string_view(b.publicKey));QCOMPARE(recipient.peerKey(),std::string_view(a.publicKey));QCOMPARE(sender.handler(),recipient.handler());QCOMPARE(sender.handler().size(),std::size_t(20));
   const std::string payload="d3:msg18:Generated private!e";auto cipher=take(sender.encrypt(payload));QCOMPARE(take(recipient.decrypt(cipher)),payload);QVERIFY(cipher!=take(sender.encrypt(payload)));
   auto moved=std::move(sender);QVERIFY(sender.handler().empty());QVERIFY(!sender.encrypt(payload));QCOMPARE(take(recipient.decrypt(take(moved.encrypt(payload)))),payload);
   QVERIFY(!p.initiator.finish({}));QVERIFY(!p.responder.finish(p.auth));
  }
 }
 void privateAcceptanceMatchesTheActualOriginalChatRequestedMethod(){
  auto keys=take(crypto::deriveChannelKeys(span("Generated policy parent")));Oracle oracle(view(keys.general));unsigned count=0;
  for(int state:{1,2,3,0,-1}){for(bool statistics:{false,true}){for(bool known:{false,true}){for(bool pinned:{false,true}){for(bool ui:{false,true}){
   const auto refusal=chat::privateChatRefusal(state,statistics,known,pinned,ui);const auto expected=QByteArray("POLICY ")+(refusal?QByteArray(refusal->data(),refusal->size()):QByteArray("accepted"));QCOMPARE(oracle.command("POLICY "+QByteArray::number(state)+' '+QByteArray::number(statistics)+' '+QByteArray::number(known)+' '+QByteArray::number(pinned)+' '+QByteArray::number(ui)),expected);++count;
  }}}}}QCOMPARE(count,80U);
 }
 void importedParentIdentityCanOutliveItsSourceOwnerWithoutKeyExport(){
  auto a=take(crypto::generateMessageIdentity()),b=take(crypto::generateMessageIdentity());auto original=take(crypto::MessageSigner::open(a.publicKey,a.privateKey));auto clone=take(original.clone());auto first=take(original.newSts()),second=take(original.newSts());QVERIFY(take(first.keysPacket())!=take(second.keysPacket()));original.clear();QVERIFY(!original.clone());QVERIFY(!original.newSts());QCOMPARE(clone.publicKey(),std::string_view(a.publicKey));auto signature=take(clone.signMessage("Generated inherited identity"));QVERIFY(take(crypto::verifyIdentitySignature(a.publicKey,"Generated inherited identity",signature)));
  auto responder=take(chat::PrivateResponder::create(sts(b),take(chat::PrivateInitiator::create(std::move(second),b.publicKey)).first().value()));QVERIFY(responder.first());
  auto make=[&](const auto& id,std::uint8_t u){chat::Uid uid{};uid[0]=u;return take(chat::Room::open("Generated inherited parent",uid,take(crypto::MessageSigner::open(id.publicKey,id.privateKey)),"de"));};
  auto left=make(a,101),right=make(b,102);QVERIFY(!left.initiatePrivate(b.publicKey));take(left.send("Generated initial signed participant","Generated",1700000000000));take(right.receive(take(left.answer(take(right.request(1700000000000)),1700000000000)),1700000000000));take(right.send("Generated response","Generated",1700000000000));take(left.receive(take(right.answer(take(left.request(1700000000000)),1700000000000)),1700000000000));
  auto outgoing=take(left.initiatePrivate(b.publicKey));auto request=take(outgoing.first());auto incoming=take(right.respondPrivate(take(right.privateRequest(take(left.privateRequestEnvelope(request))))));auto auth=take(outgoing.keysReply(take(left.privateResponse(take(right.privateReplyEnvelope(take(incoming.first())))))));auto cb=take(incoming.finish(auth)),ca=take(outgoing.finish({}));auto childA=take(left.privateChild(std::move(ca))),childB=take(right.privateChild(std::move(cb)));QCOMPARE(childA.publicKey(),left.publicKey());QCOMPARE(childB.publicKey(),right.publicKey());QVERIFY(childA.uid()!=left.uid());QVERIFY(!childA.respondPrivate(request));take(childA.send("Generated inherited private message","Generated",1700000000001));take(childB.receive(take(childA.answer(take(childB.request(1700000000001)),1700000000001)),1700000000001));QCOMPARE(childB.retainedMessages(),std::size_t(1));QCOMPARE(left.retainedMessages(),std::size_t(2));
 }
 void privateChildrenCannotBecomePublicRoomsOrPersistHistory(){
  auto a=take(crypto::generateMessageIdentity()),b=take(crypto::generateMessageIdentity()),c=take(crypto::generateMessageIdentity());auto p=pair(a,b);auto cb=take(p.responder.finish(p.auth)),ca=take(p.initiator.finish({}));
  const auto user=ca.userKey();auto pub=take(chat::Room::open(std::string(view(user)),{},take(crypto::MessageSigner::open(c.publicKey,c.privateKey)),"de"));
  auto third=take(chat::signMessage(take(crypto::MessageSigner::open(c.publicKey,c.privateKey)),chat::Uid{},"d3:msg9:Generatede"));third.publicKey=c.publicKey;third.encodedContact="de";chat::SyncReply thirdParty;thirdParty.messages.push_back(std::move(third));auto forged=take(ca.encrypt(take(chat::encodeSyncReply(thirdParty))));
  chat::Uid ua{},ub{};ua[0]=91;ub[0]=92;auto left=take(chat::Room::openPrivate(std::move(ca),ua,take(crypto::MessageSigner::open(a.publicKey,a.privateKey)),"de")),right=take(chat::Room::openPrivate(std::move(cb),ub,take(crypto::MessageSigner::open(b.publicKey,b.privateKey)),"de"));
  QVERIFY(left.isPrivate());QVERIFY(right.isPrivate());QVERIFY(!pub.isPrivate());QVERIFY(!left.exportHistory(1700000000000));QVERIFY(!left.importHistory("de",1700000000000));QVERIFY(!left.peekReply(1,1,1,1700000000000));QVERIFY(!left.privateReplyEnvelope({}));
  auto refused=take(right.receive(forged,1700000000000));QCOMPARE(refused.added,std::size_t(0));QCOMPARE(refused.rejected,std::size_t(1));
  take(left.send("Generated confidential text","Generated",1700000000000));auto request=take(right.request(1700000000000));QVERIFY(!pub.answer(request,1700000000000));auto reply=take(left.answer(request,1700000000000));QVERIFY(!pub.receive(reply,1700000000000));QCOMPARE(take(right.receive(reply,1700000000000)).added,std::size_t(1));QCOMPARE(take(right.messages(1700000000000))[0].text,"Generated confidential text");QVERIFY(right.participantContact(a.publicKey));QVERIFY(!right.participantContact(c.publicKey));QCOMPARE(pub.retainedMessages(),std::size_t(0));
 }
 void phasesKeysAndCorrelationAreBound(){
  auto a=take(crypto::generateMessageIdentity()),b=take(crypto::generateMessageIdentity()),c=take(crypto::generateMessageIdentity());
  auto wrong=take(chat::PrivateInitiator::create(sts(a),c.publicKey));auto server=take(chat::PrivateResponder::create(sts(b),take(wrong.first())));QVERIFY(!wrong.keysReply(take(server.first())));QVERIFY(!wrong.finish({}));
  auto p=pair(a,b);auto changed=p.auth;changed.activity[0]^=1;QVERIFY(!p.responder.finish(changed));QVERIFY(!p.responder.finish(p.auth));
  p=pair(a,b);changed=p.auth;changed.userKey[0]^=1;QVERIFY(!p.responder.finish(changed));
  p=pair(a,b);changed=p.auth;changed.packet.back()^=1;QVERIFY(!p.responder.finish(changed));
  p=pair(a,b);QVERIFY(!p.initiator.finish({{}, {},std::string("Private chat disabled by recipient")}));QVERIFY(!p.initiator.finish({}));
  auto early=take(chat::PrivateInitiator::create(sts(a),b.publicKey));QVERIFY(!early.finish({}));QVERIFY(!early.first());
  auto first=take(chat::PrivateInitiator::create(sts(a),b.publicKey));auto sr=take(chat::PrivateResponder::create(sts(b),take(first.first())));auto reply=take(sr.first());reply.auth->push_back('x');QVERIFY(!first.keysReply(reply));
 }
 void malformedAndAmbiguousFramesAreRefused(){
  auto a=take(crypto::generateMessageIdentity()),b=take(crypto::generateMessageIdentity());auto i=take(chat::PrivateInitiator::create(sts(a),b.publicKey));auto request=take(i.first());auto encoded=take(chat::encodePrivateRequest(request));
  auto doc=take(core::Document::parse(encoded));QCOMPARE(doc.find("t")->integer,2);QCOMPARE(doc.find("v")->integer,5);QVERIFY(doc.find("k"));QVERIFY(!doc.find("a"));
  QVERIFY(!chat::decodePrivateRequest(encoded+'e'));QVERIFY(!chat::decodePrivateRequest("d1:ai0e1:i8:123456781:k1:x1:ti2e1:u16:12345678901234561:vi5ee"));
  QVERIFY(!chat::decodePrivateRequest("d1:i8:123456781:k1:x1:k1:y1:ti2e1:u16:12345678901234561:vi5ee"));QVERIFY(!chat::decodePrivateRequest(std::string(chat::MaximumTransferBytes,'x')));
  QVERIFY(!chat::decodePrivateReply("d1:ai0e1:ti3ee"));QVERIFY(!chat::decodePrivateReply("d1:k1:x1:ti3ee"));QVERIFY(!chat::decodePrivateReply("d5:error1:x1:a1:y1:k1:z1:ti3ee"));
  auto reply=wire({{}, {},std::string("Private chat disabled by recipient")});QCOMPARE(*reply.error,"Private chat disabled by recipient");QVERIFY(!i.keysReply(reply));
 }
 void originalJavaAcceptsNativeInitiatorAndPrivatePayload(){
  auto keys=take(crypto::deriveChannelKeys(span("Generated parent")));Oracle oracle(view(keys.general));auto identity=take(crypto::generateMessageIdentity());
  auto start=[&]{return take(chat::PrivateInitiator::create(sts(identity),oracle.identity));};
  auto envelope=[&](const chat::PrivateRequest& r){return take(crypto::encrypt(crypto::Kind::General,keys.general,span(take(chat::encodePrivateRequest(r)))));};
  auto reply=[&](std::string data){return take(chat::decodePrivateReply(take(crypto::decrypt(crypto::Kind::General,keys.general,span(data)))));};
  auto i=start();auto one=take(i.first());auto oneReply=reply(Oracle::raw(oracle.command("RESPOND "+Oracle::hex(envelope(one))),"REPLY "));auto two=take(i.keysReply(oneReply));auto twoReply=reply(Oracle::raw(oracle.command("RESPOND "+Oracle::hex(envelope(two))),"REPLY "));auto channel=take(i.finish(twoReply));
  QCOMPARE(oracle.command("CHILD"),"CHILD "+Oracle::hex(identity.publicKey)+' '+Oracle::hex(view(one.userKey)));
  const std::string message="Generated signed private bytes; Unicode: Привет";QCOMPARE(Oracle::raw(oracle.command("DECRYPT "+Oracle::hex(take(channel.encrypt(message)))),"PLAIN "),message);QCOMPARE(take(channel.decrypt(Oracle::raw(oracle.command("ENCRYPT "+Oracle::hex(message)),"CIPHER "))),message);
  QCOMPARE(oracle.command("ACCEPT 0"),QByteArray("OK"));auto denied=start();auto req=take(denied.first());auto res=reply(Oracle::raw(oracle.command("RESPOND "+Oracle::hex(envelope(req))),"REPLY "));auto auth=take(denied.keysReply(res));auto error=reply(Oracle::raw(oracle.command("RESPOND "+Oracle::hex(envelope(auth))),"REPLY "));QVERIFY(error.error);QVERIFY(!denied.finish(error));QCOMPARE(oracle.command("CHILD"),QByteArray("DESTROYED"));
 }
 void originalJavaInitiatorAcceptsNativeResponder(){
  auto keys=take(crypto::deriveChannelKeys(span("Generated other parent")));Oracle oracle(view(keys.general));auto identity=take(crypto::generateMessageIdentity());std::array<std::uint8_t,16> user{};user[0]=73;
  auto receive=[&](QByteArray data){return take(chat::decodePrivateRequest(take(crypto::decrypt(crypto::Kind::General,keys.general,span(Oracle::raw(data,"CALL "))))));};
  auto envelope=[&](const chat::PrivateReply& r){return take(crypto::encrypt(crypto::Kind::General,keys.general,span(take(chat::encodePrivateReply(r)))));};
  auto req=receive(oracle.command("BEGIN "+Oracle::hex(identity.publicKey)+' '+Oracle::hex(view(user))));auto server=take(chat::PrivateResponder::create(sts(identity),req));auto auth=receive(oracle.command(Oracle::hex(envelope(take(server.first())))));auto channel=take(server.finish(auth));QCOMPARE(channel.peerKey(),std::string_view(oracle.identity));QCOMPARE(oracle.command(Oracle::hex(envelope({}))),QByteArray("CONNECTED"));
  const std::string message="Generated reverse private payload";QCOMPARE(Oracle::raw(oracle.command("DECRYPT "+Oracle::hex(take(channel.encrypt(message)))),"PLAIN "),message);QCOMPARE(take(channel.decrypt(Oracle::raw(oracle.command("ENCRYPT "+Oracle::hex(message)),"CIPHER "))),message);
 }
};
QTEST_GUILESS_MAIN(PrivateChannelTests)
#include "private_channel_tests.moc"
