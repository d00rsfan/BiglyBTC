// SPDX-License-Identifier: GPL-2.0-only
#include "chat_notice.hpp"
#include "../services/msgsync/message_origin.hpp"
#include <QProcess>
#include <QTest>
#include <array>
#include <climits>
using namespace biglybtc;using namespace std::string_literals;
namespace policy=plugins::rating::publication;
QByteArray hex(std::u16string_view s){QByteArray out;for(auto c:s){out+=QByteArray::number(c,16).rightJustified(4,'0');}return out;}
QByteArray ask(QProcess& process,QByteArray line){process.write(line+'\n');process.waitForBytesWritten(1000);if(!process.canReadLine()){process.waitForReadyRead(3000);}return process.readLine().trimmed();}
class RatingChatNoticeTests final:public QObject{
 Q_OBJECT
private slots:
 void textAndHistoryMatchOriginalSelectedBlocks(){
  const auto java=qEnvironmentVariable("BIGLYBTC_RATING_CHAT_JAVA"),classpath=qEnvironmentVariable("BIGLYBTC_RATING_CHAT_CLASSPATH");if(java.isEmpty()){QSKIP("Original selected Java blocks not configured");}QProcess process;process.start(java,{"-cp",classpath,"RatingChatOracle"});QVERIFY(process.waitForStarted());
  std::vector<std::u16string> comments{u"",u" \t\n ",u"  Generated comment \r",u"\u00a0unchanged\u00a0",u"\u2003wide\u2003",u"\0\x1f"s,u"\u041a\u043e\u043c\u0435\u043d\u0442",std::u16string(1,char16_t(0xd800))};
  unsigned cases=0;for(auto score:std::array<std::int32_t,6>{INT32_MIN,-1,0,1,5,INT32_MAX}){for(const auto& comment:comments){const auto text=policy::noticeMessage(score,comment);for(bool written:{false,true}){for(unsigned kind=0;kind<8;++kind){const std::array<unsigned,8> sizes{0,1,49,50,51,64,1,1};std::vector<std::u16string> storage(sizes[kind],u"Generated other");if(kind==6||kind==7){storage[0]=text;}std::vector<policy::NoticeMessage> rows;QByteArray encoded;for(std::size_t i=0;i<storage.size();++i){const bool own=kind==6||(kind<6&&i==0);rows.push_back({storage[i],own});if(i){encoded+=',';}encoded+=own?'1':'0';encoded+=hex(storage[i]);}const auto result=ask(process,"N\t"+QByteArray::number(score)+'\t'+hex(comment)+'\t'+(written?"1":"0")+'\t'+encoded);const auto expected=hex(text)+'\t'+(policy::shouldSendNotice(written,rows,text)?"1":"0");QCOMPARE(result,expected);++cases;}}}}
  QCOMPARE(cases,768U);process.closeWriteChannel();QVERIFY(process.waitForFinished());QCOMPARE(process.exitCode(),0);
 }
 void originMatchesTheFullOriginalMethod(){
  const auto java=qEnvironmentVariable("BIGLYBTC_RATING_CHAT_JAVA");if(java.isEmpty()){QSKIP("Original selected Java method not configured");}QProcess process;process.start(java,{"-cp",qEnvironmentVariable("BIGLYBTC_RATING_CHAT_CLASSPATH"),"RatingChatOracle"});QVERIFY(process.waitForStarted());const std::array<std::string,6> texts{"Generated plain","Rating: 5","See http://wiki.vuze.com/w/Swarm_Merging help","See http://wiki.biglybt.com/w/Swarm_Merging[[Help]]","see http://wiki.biglybt.com/w/Swarm_Merging","Other http://wiki.biglybt.com/w/Swarm_Merging"};const std::array<std::optional<std::int64_t>,10> flags{{{},0,1,-1,2,INT32_MIN,INT32_MAX,INT64_C(4294967297),INT64_MIN,INT64_MAX}};unsigned cases=0;for(const auto& text:texts){for(const auto flag:flags){std::u16string utf16;for(char c:text){utf16+=char16_t(c);}const auto result=ask(process,"O\t"+hex(utf16)+'\t'+(flag?QByteArray::number(qlonglong(*flag)):QByteArray("-")));QCOMPARE(result,QByteArray::number(services::msgsync::messageOrigin(text,flag)));++cases;}}QCOMPARE(cases,60U);process.closeWriteChannel();QVERIFY(process.waitForFinished());QCOMPARE(process.exitCode(),0);
 }
 void originalPollAndSettlePeriodsAreExplicit(){
  using Wait=policy::NoticeWait;QCOMPARE(Wait::pollMillis,30000);QCOMPARE(Wait::settleMillis,120000);Wait immediate;QCOMPARE(immediate.poll(0,false),Wait::Stage::Delay);QCOMPARE(immediate.polls(),1U);QCOMPARE(immediate.afterDelay(false),Wait::Stage::Ready);Wait busy;for(unsigned n=1;n<10;++n){QCOMPARE(busy.poll(1,false),Wait::Stage::Sync);}QCOMPARE(busy.poll(1,false),Wait::Stage::Delay);QCOMPARE(busy.polls(),10U);QCOMPARE(busy.afterDelay(true),Wait::Stage::Cancelled);Wait destroyed;QCOMPARE(destroyed.poll(0,true),Wait::Stage::Cancelled);QCOMPARE(destroyed.afterDelay(false),Wait::Stage::Cancelled);
 }
};
QTEST_GUILESS_MAIN(RatingChatNoticeTests)
#include "rating_chat_notice_tests.moc"
