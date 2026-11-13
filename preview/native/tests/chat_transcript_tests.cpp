// SPDX-License-Identifier: GPL-2.0-or-later
#include "../services/msgsync/transcript_policy.hpp"
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>
using namespace biglybtc::services::msgsync;
namespace {
QByteArray hex(QStringView text){QByteArray result;for(auto c:text){result+=QByteArray::number(c.unicode(),16).rightJustified(4,'0');}return result;}
QByteArray ask(QProcess& process,QByteArray request){process.write(request+'\n');process.waitForBytesWritten(1000);while(!process.canReadLine()){if(!process.waitForReadyRead(3000)){return {};}}return process.readLine().trimmed();}
}
class ChatTranscriptTests final:public QObject{
 Q_OBJECT
private slots:
 void selectedOriginalEmphasisSearchAndUrlRulesMatch(){
  if(!qEnvironmentVariableIsSet("BIGLYBTC_CHAT_TRANSCRIPT_JAVA")){QSKIP("Original transcript bodies not configured");}QTemporaryDir root;QProcess java;auto env=QProcessEnvironment::systemEnvironment();for(const auto* key:{"JAVA_TOOL_OPTIONS","JDK_JAVA_OPTIONS","_JAVA_OPTIONS","CLASSPATH","LD_PRELOAD","LD_AUDIT"}){env.remove(QLatin1StringView(key));}java.setProcessEnvironment(env);java.start(qEnvironmentVariable("BIGLYBTC_CHAT_TRANSCRIPT_JAVA"),{"-Xmx128m","-cp",qEnvironmentVariable("BIGLYBTC_CHAT_TRANSCRIPT_CLASSPATH"),"ChatTranscriptOracle",root.path()});QVERIFY(java.waitForStarted());QByteArray whitespace;for(unsigned c=0;c<65536;++c){whitespace+=transcriptWhitespace(QChar(ushort(c)))?'1':'0';}QCOMPARE(ask(java,"W"),whitespace);
  const QStringList bodies{"plain","a*b*c","**word**","__word__","<b>x y</b> [i]one two[/i]","<N>normal</n>","a**b**c**d**e","**x****y**","a\n*b*","*x~* [] % + &","[[prefix]]**bold**",QString(QChar(0xa0))+"**word**",QString(QChar(0x2007))+"*word*","Generated Привіт 🌍","<img src=file:///literal>","[[bad","*unterminated","_one_ two **three**"};
  unsigned count=0;for(const auto& body:bodies){for(int repeat:{1,2,7}){const auto text=body.repeated(repeat);QCOMPARE(ask(java,"E\t"+hex(text)),hex(expandTranscriptEmphasis(text)));QCOMPARE(ask(java,"P\t"+hex(text)),hex(transcriptEncode(text)));++count;}}
  for(const auto& value:QStringList{"a+b%20c","%E2%82%AC","end%","end%2","bad%xy","bad%xy%","%ff","a%00b","","%F0%9F%99%82"}){const auto native=transcriptDecode(value);QCOMPARE(ask(java,"U\t"+hex(value)),native?"1:"+hex(*native):QByteArray("0:"));++count;}
  TranscriptSearch search;for(const auto& log:QStringList{"banana ABaba needle NEEDLE","ЖиВОЙ живой","Greek Σ σς İi plain","",QString("first")+QChar(0xfeff)+"bold"}){for(const auto& term:QStringList{"a","ABA","needle","ЖИВОЙ","σ","i","missing",""}){for(const auto& input:QStringList{term,"\n","\n","\n","\n",""}){const auto result=search.apply(log,input);const auto expected=QByteArray::number(result.start)+' '+QByteArray::number(result.end)+' '+QByteArray::number(search.index)+' '+hex(search.term);QCOMPARE(ask(java,"S\t"+hex(log)+'\t'+hex(input)),expected.trimmed());++count;}}}QCOMPARE(count,304U);java.closeWriteChannel();QVERIFY(java.waitForFinished());QCOMPARE(java.exitCode(),0);
 }
 void nativeLinksAreExplicitAndKeepTargetsSeparateFromLabels(){
  auto room=publicTranscriptRoom("chat:?Generated%20Public%20Room");QVERIFY(room);QCOMPARE(*room,QString("Generated Public Room"));for(const auto* value:{"chat:anon:?Private","chat:?Room&format=rss","chat:?key=not-canonical","javascript:alert(1)","file:///generated"}){QVERIFY(!publicTranscriptRoom(QString::fromLatin1(value)));}
  const auto spans=transcriptSpans("**bold** https://example.invalid/a*b*[[Friendly]] chat:?Generated%20Room javascript:alert(1)");QString body;bool external=false,publicRoom=false;for(const auto& span:spans){body+=span.text;if(span.style==TranscriptStyle::Link){QVERIFY(!span.target.startsWith("javascript"));if(span.target.startsWith("https:")){external=true;QCOMPARE(span.target,QString("https://example.invalid/a*b*"));QCOMPARE(span.text,QString("Friendly"));}else{publicRoom=true;QCOMPARE(span.target,QString("chat:?Generated%20Room"));}}}QVERIFY(external&&publicRoom);QVERIFY(body.contains("bold"));QVERIFY(body.contains("javascript:alert(1)"));const auto rating=transcriptSpans("**literal rating**",1);QCOMPARE(rating.size(),1);QCOMPARE(rating[0].text,QString("**literal rating**"));const auto phishing=transcriptSpans("https://real.invalid/[[https%3A%2F%2Fpretend.invalid]]");QCOMPARE(phishing.size(),1);QCOMPARE(phishing[0].text,QString("https://real.invalid/"));QCOMPARE(phishing[0].target,QString("https://real.invalid/"));
 }
 void formattingLabelsNeverBecomeExecutableAnchors(){const auto spans=formattedTranscriptSpans("x chat:bold[[Bold%20text]] chat:italic[[Italic]] chat:normal[[%3Cimg%20src%3Dfile%3A%2F%2Fx%3E]]");QCOMPARE(spans.size(),6);QCOMPARE(spans[1].style,TranscriptStyle::Bold);QCOMPARE(spans[1].text,QString("Bold text"));QCOMPARE(spans[3].style,TranscriptStyle::Italic);QVERIFY(spans.last().text.contains("<img src=file://x>"));for(const auto& span:spans){QVERIFY(span.target.isEmpty());}const auto malformed=formattedTranscriptSpans("chat:bold[[bad%xy]]");QCOMPARE(malformed.size(),1);QCOMPARE(malformed[0].style,TranscriptStyle::Plain);QCOMPARE(malformed[0].text,QString("chat:bold[[bad%xy]]"));}
};
QTEST_GUILESS_MAIN(ChatTranscriptTests)
#include "chat_transcript_tests.moc"
