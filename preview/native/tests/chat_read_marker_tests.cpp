// SPDX-License-Identifier: GPL-2.0-or-later
#include "read_marker.hpp"
#include <QTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QProcessEnvironment>
using namespace biglybtc::services::msgsync;
namespace {
QByteArray encoded(QString text){QByteArray out;for(auto c:text){out+=QByteArray::number(c.unicode(),16).rightJustified(4,'0');}return out;}
QByteArray ask(QProcess& java,QByteArray request){java.write(request+'\n');java.waitForBytesWritten(1000);while(!java.canReadLine()){if(!java.waitForReadyRead(3000)){return {};}}return java.readLine().trimmed();}
}
class ChatReadMarkerTests final:public QObject{
 Q_OBJECT
private slots:
 void originalReadAndSeenMethodsMatchOrderedAndDetachedMessages(){
  if(!qEnvironmentVariableIsSet("BIGLYBTC_CHAT_READ_JAVA")){QSKIP("Selected original read methods not configured");}QTemporaryDir root;QProcess java;auto env=QProcessEnvironment::systemEnvironment();for(const auto* key:{"JAVA_TOOL_OPTIONS","JDK_JAVA_OPTIONS","_JAVA_OPTIONS","CLASSPATH","LD_PRELOAD","LD_AUDIT"}){env.remove(QLatin1StringView(key));}java.setProcessEnvironment(env);java.start(qEnvironmentVariable("BIGLYBTC_CHAT_READ_JAVA"),{"-Xmx128m","-cp",qEnvironmentVariable("BIGLYBTC_CHAT_READ_CLASSPATH"),"ChatReadMarkerOracle"});QVERIFY(java.waitForStarted());QByteArray digits;for(unsigned c=0;c<65536;++c){digits+=QByteArray::number(read_marker_detail::digit(char16_t(c))+1,16);}QCOMPARE(ask(java,"D"),digits);
  unsigned cases=0;for(unsigned sample=0;sample<64;++sample){std::vector<ChatReadMessage> messages;for(unsigned i=0;i<5;++i){std::string id(8,char('A'+i));messages.push_back({id,std::int64_t(1700000000000LL+(sample*17+i*59)*1000)});}if(sample%3==0){std::swap(messages[0].timestamp,messages[4].timestamp);}if(sample%5==0){messages[2].timestamp=INT64_MIN;messages[3].timestamp=INT64_MAX;}QByteArray ledger;for(const auto& msg:messages){if(!ledger.isEmpty()){ledger+=',';}ledger+=QByteArray::fromStdString(msg.id).toHex()+':'+QByteArray::number(qlonglong(msg.timestamp));}const auto mark=encodeChatReadMarker(1700000300000LL+sample*1000,messages[sample%5].timestamp,messages[sample%5].id);QList<QString> markers{QString::fromStdString(mark),QString::fromStdString(mark).toLower(),"0/0/!INVALID!","bad/0/AAAA","0//AAAA","0/0/","0/0//extra","9223372036854775808/0/AAAA","-9223372036854775808/-9223372036854775808/AAAA","9223372036854775807/9223372036854775807/AAAA","+0000/-0/AAAA/ignored",QString::fromUtf8("١٧٠٠٠٠٠٣٠٠/０/AAAA")};std::vector<ChatReadMessage> detached=messages;std::vector<const ChatReadMessage*> targets;for(std::size_t i=0;i<messages.size();++i){targets.push_back(&messages[i]);targets.push_back(&detached[i]);}for(const auto& marker:markers){auto parsed=parseChatReadMarker(marker.toStdU16String());QByteArray native;for(const auto* msg:targets){native+=oldOutstandingMessage(parsed,messages,*msg)?'1':'0';}native+=' ';for(bool seen:fixupChatSeen(parsed,messages,targets)){native+=seen?'1':'0';}QCOMPARE(ask(java,encoded(marker)+'\t'+ledger),native);++cases;}}
  QCOMPARE(cases,768U);java.closeWriteChannel();QVERIFY(java.waitForFinished());QCOMPARE(java.exitCode(),0);
 }
 void canonicalWriterUsesOriginalSecondsAndBase32(){QCOMPARE(encodeChatReadMarker(1700000123456LL,1700000000456LL,"ABC"),std::string("1700000123/1700000000/IFBEG"));QCOMPARE(encodeChatReadMarker(-999,-1001,""),std::string("0/-1/"));auto parsed=parseChatReadMarker(u"1/2/IFBEG");QVERIFY(parsed);QCOMPARE(parsed->id,std::string("ABC"));}
};
QTEST_GUILESS_MAIN(ChatReadMarkerTests)
#include "chat_read_marker_tests.moc"
