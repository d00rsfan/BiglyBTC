// SPDX-License-Identifier: GPL-2.0-or-later
#include "room.hpp"
#include "discovery.hpp"
#include "peek.hpp"
#include "chat_profile.hpp"
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QDateTime>
#include <QFile>
#include <QtEndian>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace biglybtc;namespace msg=services::msgsync;namespace crypto=plugins::msgsync;
namespace {
void need(bool value,const char* text){if(!value){throw std::runtime_error(text);}}
template<class T,class E>T take(std::expected<T,E> value){need(bool(value),"Generated Native room operation");return std::move(*value);}
class Oracle {
 QProcess process_;QByteArray incoming_;
public:
 Oracle(){
  auto environment=QProcessEnvironment::systemEnvironment();for(const auto& key:{"JAVA_TOOL_OPTIONS","JDK_JAVA_OPTIONS","_JAVA_OPTIONS","CLASSPATH","LD_PRELOAD","LD_AUDIT"}){environment.remove(QLatin1StringView(key));}process_.setProcessEnvironment(environment);
  const QString root=QStringLiteral(BIGLYBTC_ORIGINAL_CHAT_ROOT),core=QStringLiteral(BIGLYBTC_ORIGINAL_CORE_CLASSES);
  process_.setWorkingDirectory(root+QStringLiteral("/full-handler"));
  QStringList arguments{QStringLiteral("-Xmx192m"),QStringLiteral("--add-opens=java.base/java.net=ALL-UNNAMED"),QStringLiteral("-Dazureus.security.manager.install=0"),QStringLiteral("-XX:ActiveProcessorCount=1"),QStringLiteral("-Djava.security.manager=FullChatFence"),QStringLiteral("-Dbiglybtc.test.root=")+root,QStringLiteral("-Dbiglybtc.test.classes=")+core,QStringLiteral("-Duser.home=")+root+QStringLiteral("/home"),QStringLiteral("-Djava.io.tmpdir=")+root+QStringLiteral("/tmp"),QStringLiteral("-Dazureus.config.path=")+root+QStringLiteral("/full-handler/profile"),QStringLiteral("-cp"),root+QStringLiteral("/full-handler/classes:")+root+QStringLiteral("/classes:")+core,QStringLiteral("com.vuze.plugins.azmsgsync.FullHandlerOracle")};
  process_.start(QStringLiteral(BIGLYBTC_ORIGINAL_CHAT_JAVA),arguments);need(process_.waitForStarted(3000),"Start fenced original handler");
 }
 ~Oracle(){if(process_.state()!=QProcess::NotRunning){process_.kill();process_.waitForFinished(3000);}}
 void send(int type,std::string_view payload){QByteArray data(8,Qt::Uninitialized);qToBigEndian<qint32>(type,data.data());qToBigEndian<qint32>(static_cast<qint32>(payload.size()),data.data()+4);data.append(payload.data(),static_cast<qsizetype>(payload.size()));need(process_.write(data)==data.size(),"Write bounded Java command");}
 std::pair<int,std::string> read(){
  const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
  for(;;){incoming_+=process_.readAllStandardOutput();if(incoming_.size()>=8){const auto length=qFromBigEndian<qint32>(incoming_.constData()+4);need(length>=0&&length<=1024*1024,"Original reply frame bound");if(incoming_.size()>=8+length){const auto type=qFromBigEndian<qint32>(incoming_.constData());auto bytes=incoming_.mid(8,length).toStdString();incoming_.remove(0,8+length);return {type,std::move(bytes)};}}
   if(process_.state()==QProcess::NotRunning||std::chrono::steady_clock::now()>=end){const auto raw=process_.readAllStandardError();QFile log(QStringLiteral(BIGLYBTC_ORIGINAL_CHAT_ROOT)+QStringLiteral("/full-handler/runtime-stderr.log"));if(log.open(QIODevice::WriteOnly|QIODevice::Truncate)){log.write(raw);}const auto error=(raw.left(1200)+raw.right(1200)).toStdString();throw std::runtime_error("Fenced original handler stopped/timed out: "+error);}process_.waitForReadyRead(100);
  }
 }
 void stop(){send(9,{});need(process_.waitForFinished(3000)&&process_.exitCode()==0,"Original handler clean stop");}
};
void restoreOriginalProfileIdentity(Oracle& oracle){
 oracle.send(10,{});auto reply=oracle.read();need(reply.first==11,"Original generated identity frame");
 auto source=take(core::Document::parse(reply.second));const auto* publicKey=source.find("pub");const auto* uid=source.find("uid");need(publicKey&&publicKey->bytes.size()==49&&publicKey->bytes.front()==4&&uid&&uid->bytes.size()==8,"Original Java uncompressed public-key format");
 const std::string originalPublic(publicKey->bytes),originalUid(uid->bytes);const std::string name="Generated Java profile room";
 QTemporaryDir root;need(root.isValid(),"Generated profile directory");auto lease=take(storage::ProfileLease::acquire(root.path().toStdString()));auto cfg=take(storage::ConfigFile::createMissing(lease,"biglybt.config","de"));
 need(bool(cfg.document().replaceValue(app::ChatProfile::configKey(name),reply.second)),"Install generated original identity");need(bool(cfg.save(lease)),"Save generated original identity");const auto before=cfg.document().serialize();
 auto profile=take(app::ChatProfile::open(lease));auto loaded=profile->identity(name,false);
 need(bool(loaded),"Existing original Java 49-byte chat identity must be readable");
 need(loaded->signer.publicKey()==originalPublic&&std::string(reinterpret_cast<const char*>(loaded->uid.data()),loaded->uid.size())==originalUid,"Imported Java key encoding or UID rotated");
 auto current=take(storage::ConfigFile::load(lease,"biglybt.config"));need(current.document().serialize()==before,"Reading original identity rewrote profile");
 need(take(storage::chatIdentityStamp(current.document(),app::ChatProfile::configKey(name)))==loaded->sourceStamp,"Original identity source stamp disagrees");
 // A saved 49-byte identity remains usable for room signing and history.
 auto sender=take(msg::Room::open(name,loaded->uid,std::move(loaded->signer),"de"));auto peerKeys=take(crypto::generateMessageIdentity());msg::Uid peerUid{};peerUid[0]=77;auto peer=take(msg::Room::open(name,peerUid,take(crypto::MessageSigner::open(peerKeys.publicKey,peerKeys.privateKey)),"de"));
 take(sender.send("Generated restored Java identity","Generated",100000));take(peer.receive(take(sender.answer(take(peer.request(100000)),100000)),100000));auto messages=take(peer.messages(100000));need(messages.size()==1&&messages[0].publicKey==originalPublic,"Restored original public encoding lost in signed exchange");
 auto restoredIdentity=take(profile->identity(name,false));auto historyRoom=take(msg::Room::open(name,restoredIdentity.uid,std::move(restoredIdentity.signer),"de"));take(historyRoom.importHistory(take(sender.exportHistory(100000)),101000));need(take(historyRoom.messages(101000)).size()==1,"Original identity history handoff failed");
 const auto managed=name+"[pk="+app::ChatProfile::optionKey(originalPublic).substr(7)+"&ro=1]";auto policy=take(msg::roomPolicy(managed));need(policy.manager==originalPublic&&policy.readOnly,"Managed room rejected original Java owner encoding");
 auto manager=take(profile->identity(managed,true));need(manager.signer.publicKey()==originalPublic&&!manager.settings.readOnly,"Original owner identity was not inherited unchanged");
 auto authored=take(msg::Room::open(managed,manager.uid,std::move(manager.signer),"de"));auto reader=take(msg::Room::open(managed,peerUid,take(crypto::MessageSigner::open(peerKeys.publicKey,peerKeys.privateKey)),"de"));need(!authored.readOnly()&&reader.readOnly()&&!reader.send("forbidden","Generated",100000),"Original 49-byte managed-room owner enforcement");take(authored.send("Generated owner message","Generated",100000));take(reader.receive(take(authored.answer(take(reader.request(100000)),100000)),100000));need(take(reader.messages(100000)).size()==1,"Original owner post rejected");
 // Invalid/mismatched keys are still refused and never regenerated.
 auto damaged=take(storage::ConfigFile::load(lease,"biglybt.config"));auto entry=take(core::Document::parse(reply.second));need(bool(entry.replaceValue("pri","1:"+std::string(1,'\0'))),"Generated invalid private scalar");need(bool(damaged.document().replaceValue(app::ChatProfile::configKey(name),entry.serialize()))&&bool(damaged.save(lease)),"Install invalid generated pair");const auto invalidBefore=damaged.document().serialize();need(!profile->identity(name,true),"Unreadable original identity was regenerated");need(take(storage::ConfigFile::load(lease,"biglybt.config")).document().serialize()==invalidBefore,"Refused identity changed config");
}
}
int main(int argc,char** argv){
 QCoreApplication application(argc,argv);
 try{
  auto identity=take(crypto::generateMessageIdentity());msg::Uid uid{};uid[0]=99;auto room=take(msg::Room::open("Generated full Java room",uid,take(crypto::MessageSigner::open(identity.publicKey,identity.privateKey)),"d1:a4:"+std::string("\x7f\x7b\x1f\x02",4)+"1:pi45322e1:vi55ee"));Oracle oracle;need(oracle.read().first==0,"Original handler ready");restoreOriginalProfileIdentity(oracle);
  auto peek=take(msg::PeekChannel::open("Generated full Java room"));oracle.send(12,take(peek.request()));auto peekFrame=oracle.read();need(peekFrame.first==13&&!peekFrame.second.empty(),"Actual original registered peek handler answer");auto summary=take(peek.receive(std::move(peekFrame.second)));need(summary.messages==1&&summary.total==0&&summary.live==0&&summary.estimate==0&&summary.signature&&summary.publicKey,"Original encrypted peek metadata");auto wrongPeek=take(msg::PeekChannel::open("Generated different room"));oracle.send(12,take(wrongPeek.request()));peekFrame=oracle.read();need(peekFrame.first==13&&peekFrame.second.empty(),"Original handler must refuse another room's encrypted peek");

  oracle.send(1,take(room.request(QDateTime::currentMSecsSinceEpoch())));auto answer=oracle.read();need(answer.first==2,"Actual original handler answer");auto received=take(room.receive(std::move(answer.second),QDateTime::currentMSecsSinceEpoch()));need(received.added==1,"Native admits original handler message");auto messages=take(room.messages(QDateTime::currentMSecsSinceEpoch()));need(messages.back().text=="Generated Java message: Привіт 🌍"&&messages.back().nickname=="Generated Java","Java message and nickname unchanged");
  take(room.send("Generated Native reply: Дякую 🌍","Generated Native",QDateTime::currentMSecsSinceEpoch()));oracle.send(5,{});auto request=oracle.read();need(request.first==3,"Actual original handler sync request");oracle.send(4,take(room.answer(std::move(request.second),QDateTime::currentMSecsSinceEpoch())));auto result=oracle.read();need(result.first==6,"Original handler validates Native signed reply");for(unsigned offset=0;offset<10;++offset){msg::DiscoveryKey key{};for(unsigned i=0;i<20;++i){key[i]=static_cast<std::uint8_t>(i*7);}const auto native=take(msg::diversifiedDiscoveryKey(key,offset));std::string payload(reinterpret_cast<const char*>(key.data()),key.size());payload+=static_cast<char>(offset);oracle.send(7,payload);const auto derived=oracle.read();need(derived.first==8&&derived.second==std::string(reinterpret_cast<const char*>(native.data()),native.size()),"Original diversified DHT key differs");}oracle.stop();std::cout<<"PASS full unmodified original MsgSyncHandler Java-to-Native and Native-to-Java messages\n";return 0;
 }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
