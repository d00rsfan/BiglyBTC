// SPDX-License-Identifier: GPL-2.0-or-later
#include "attention_policy.hpp"
#include <QProcess>
#include <QProcessEnvironment>
#include <QTest>
#include <array>
#include <climits>
using namespace biglybtc;namespace msg=services::msgsync;
namespace {
QByteArray number(std::optional<std::int64_t> value){return value?QByteArray::number(qlonglong(*value)):QByteArray("-");}
QByteArray text(std::string_view value){const auto decoded=QString::fromUtf8(value.data(),qsizetype(value.size()));QByteArray out;for(auto c:decoded){out+=QByteArray::number(c.unicode(),16).rightJustified(4,'0');}return out;}
QByteArray ask(QProcess& process,const QByteArray& command){process.write(command+'\n');process.waitForBytesWritten(1000);if(!process.canReadLine()){process.waitForReadyRead(3000);}return process.readLine().trimmed();}
}
class ChatAttentionTests final:public QObject {
 Q_OBJECT
private slots:
 void exactOriginalFlagsAttentionAndDisplayBranches(){
  if(!qEnvironmentVariableIsSet("BIGLYBTC_CHAT_ATTENTION_JAVA")){QSKIP("Original Java attention source not configured");}QProcess java;auto environment=QProcessEnvironment::systemEnvironment();for(const auto* name:{"JAVA_TOOL_OPTIONS","JDK_JAVA_OPTIONS","_JAVA_OPTIONS","CLASSPATH","LD_PRELOAD","LD_AUDIT"}){environment.remove(QLatin1StringView(name));}java.setProcessEnvironment(environment);java.start(qEnvironmentVariable("BIGLYBTC_CHAT_ATTENTION_JAVA"),{"-Xmx128m","-cp",qEnvironmentVariable("BIGLYBTC_CHAT_ATTENTION_CLASSPATH"),"ChatAttentionOracle"});QVERIFY(java.waitForStarted());
  const std::array<std::optional<std::int64_t>,14> values{{{},0,1,-1,2,3,4,INT32_MIN,INT32_MAX,INT64_C(4294967296),INT64_C(4294967297),-INT64_C(4294967296),INT64_MIN,INT64_MAX}};const std::array<std::optional<std::int64_t>,8> origins{{{},0,1,2,3,4,-1,INT64_C(4294967297)}};const std::array<std::string,4> texts{"","Generated ordinary","See http://wiki.biglybt.com/w/Swarm_Merging[[Generated]]","Generated Unicode: Привіт 🌍"};unsigned count=0;
  for(const auto flash:values){for(const auto origin:origins){for(unsigned bits=0;bits<16;++bits){for(const auto& value:texts){const auto type=values[(count/4)%values.size()];const bool local=bits&1,ignored=bits&2;const msg::ChatDisplayPolicy policy{bool(bits&4),bool(bits&8)};const auto resolved=msg::messageOrigin(value,origin);const auto command=QByteArray::number(local?1:0)+'\t'+QByteArray::number(ignored?1:0)+'\t'+QByteArray::number(policy.hideRatings?1:0)+'\t'+QByteArray::number(policy.hideSearchSubscriptions?1:0)+'\t'+number(flash)+'\t'+number(type)+'\t'+number(origin)+'\t'+text(value);const auto expected=QByteArray::number(msg::messageFlashOverride(flash)?1:0)+' '+QByteArray::number(msg::messageFlagType(type))+' '+QByteArray::number(resolved)+' '+QByteArray::number(msg::requiresChatAttention(local,ignored,msg::messageFlashOverride(flash),resolved,policy)?1:0)+' '+QByteArray::number(msg::visibleChatMessage(ignored,!value.empty(),resolved,policy)?1:0)+' '+QByteArray::number(local?0:1);QCOMPARE(ask(java,command),expected);++count;}}}}
  QCOMPARE(count,7168U);java.closeWriteChannel();QVERIFY(java.waitForFinished());QCOMPARE(java.exitCode(),0);
 }
};
QTEST_GUILESS_MAIN(ChatAttentionTests)
#include "chat_attention_tests.moc"
