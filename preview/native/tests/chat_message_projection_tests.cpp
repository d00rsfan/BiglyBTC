// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_message_projection.hpp"
#include "participant_name.hpp"
#include <QProcess>
#include <QTimeZone>
#include <QTemporaryDir>
#include <QtTest>
#include <array>
using namespace biglybtc;
namespace {
services::msgsync::RoomMessage message(char id,const std::string& key,std::string nickname){services::msgsync::RoomMessage out;out.id.fill(static_cast<std::uint8_t>(id));out.publicKey=key;out.nickname=std::move(nickname);out.text="Generated text";out.ageSeconds=5;return out;}
}
class ChatMessageProjectionTests final:public QObject {
 Q_OBJECT
private slots:
 void originalMethodMatchesGeneratedKeysAndRejectsShortKeys(){
  if(!qEnvironmentVariableIsSet("BIGLYBTC_CHAT_NAME_JAVA")){QSKIP("Pinned original Java not configured");}
  std::vector<std::optional<std::string>> keys{std::nullopt,std::string{}};for(std::size_t n=1;n<11;++n){keys.emplace_back(std::string(n,'x'));}
  for(unsigned sample=0;sample<256;++sample){std::string key(11+sample%86,'\0');for(std::size_t i=0;i<key.size();++i){key[i]=static_cast<char>((sample*37+i*53)&255);}keys.emplace_back(std::move(key));}
  for(unsigned sample=0;sample<32;++sample){std::string key(33,'x');key[8]=static_cast<char>(sample);key[9]=static_cast<char>(sample*3);key[10]=static_cast<char>(255-sample);keys.emplace_back(std::move(key));}
  QByteArray input;for(const auto& key:keys){input+=key?QByteArray(key->data(),key->size()).toHex():QByteArray("-");input+='\n';}
  QTemporaryDir root;QVERIFY(root.isValid());QProcess java;java.start(qEnvironmentVariable("BIGLYBTC_CHAT_NAME_JAVA"),{"-cp",qEnvironmentVariable("BIGLYBTC_CHAT_NAME_CLASSPATH"),"ChatParticipantNameOracle",root.path()});QVERIFY(java.waitForStarted(5000));java.write(input);java.closeWriteChannel();QVERIFY(java.waitForFinished(10000));QVERIFY2(java.exitCode()==0,java.readAllStandardError().constData());auto rows=java.readAllStandardOutput().split('\n');if(!rows.empty()&&rows.back().isEmpty()){rows.pop_back();}QCOMPARE(rows.size(),qsizetype(keys.size()));
  for(std::size_t i=0;i<keys.size();++i){auto actual=services::msgsync::participantShortName(keys[i]?std::optional<std::string_view>{*keys[i]}:std::nullopt);QCOMPARE(actual?QByteArray::fromStdString(*actual):QByteArray("ERROR"),rows[qsizetype(i)]);}
 }
 void oneIdentityIsUsedInMessagesAndRosterWhileLatestNicknameWins(){
  std::string key(33,'x');key[8]=char(0x12);key[9]=char(0xAB);key[10]=char(0x80);std::map<QByteArray,QDateTime> times;ui::ChatViewSnapshot out;const auto now=QDateTime::fromMSecsSinceEpoch(1700000000000LL,QTimeZone::utc());const std::array first{message('1',key,""),message('2',key,"Named participant")};app::projectChatMessages(out,first,times,now);QCOMPARE(out.messages[0].nickname,QStringLiteral("12AB80"));QCOMPARE(out.messages[1].nickname,QStringLiteral("Named participant"));QCOMPARE(out.participants.size(),1);QCOMPARE(out.participants[0].nickname,QStringLiteral("Named participant"));QCOMPARE(out.participants[0].publicKey,QByteArray::fromStdString(key));
  const std::array renamed{message('1',key,"Named participant"),message('2',key,"")};app::projectChatMessages(out,renamed,times,now.addSecs(30));QCOMPARE(out.participants[0].nickname,QStringLiteral("12AB80"));QCOMPARE(out.messages[0].time,now.addSecs(-5));QCOMPARE(times.size(),std::size_t{2});
 }
 void displayCollisionsDoNotMergeFullParticipantIdentities(){
  std::string a(33,'a'),b(33,'b');for(unsigned i=8;i<11;++i){a[i]=b[i]=char(0xEE);}const std::array rows{message('1',a,""),message('2',b,""),message('3',a," ")};std::map<QByteArray,QDateTime> times;ui::ChatViewSnapshot out;app::projectChatMessages(out,rows,times,QDateTime::currentDateTimeUtc());QCOMPARE(out.participants.size(),2);QCOMPARE(out.messages[0].nickname,QStringLiteral("EEEEEE"));QCOMPARE(out.messages[1].nickname,QStringLiteral("EEEEEE"));QCOMPARE(out.participants[0].nickname,QStringLiteral(" "));QVERIFY(out.participants[0].publicKey!=out.participants[1].publicKey);app::projectChatMessages(out,{},times,QDateTime::currentDateTimeUtc());QVERIFY(out.messages.isEmpty()&&out.participants.isEmpty()&&times.empty());
 }
};
QTEST_GUILESS_MAIN(ChatMessageProjectionTests)
#include "chat_message_projection_tests.moc"
