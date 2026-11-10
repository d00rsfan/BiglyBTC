// SPDX-License-Identifier: GPL-2.0-or-later
#include "ordering.hpp"
#include <QCoreApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QtEndian>
#include <algorithm>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
using namespace biglybtc::services::msgsync;
namespace {
void need(bool value,const char* text){if(!value){throw std::runtime_error(text);}}
MessageId id(int value){MessageId out{};qToBigEndian<quint64>(static_cast<quint64>(value),out.data());return out;}
[[maybe_unused]] int number(const MessageId& value){return static_cast<int>(qFromBigEndian<quint64>(value.data()));}
DisplayMessage row(int value,int parent,std::int64_t seq,std::int64_t time,unsigned arrival){return {id(value),parent<0?std::optional<MessageId>{}:id(parent),seq,time,arrival};}
[[maybe_unused]] void integer(QByteArray& data,qint32 value){const auto at=data.size();data.resize(at+4);qToBigEndian(value,data.data()+at);}
[[maybe_unused]] void wide(QByteArray& data,qint64 value){const auto at=data.size();data.resize(at+8);qToBigEndian(value,data.data()+at);}
}
int main(int argc,char** argv){QCoreApplication application(argc,argv);try{
 std::vector<std::vector<DisplayMessage>> cases{{},{row(1,-1,0,10,1)},{row(2,1,2,1,1),row(1,-1,0,10,2)},{row(1,2,2,10,1),row(2,1,1,20,2)},{row(3,1,3,5,1),row(1,-1,0,10,2),row(2,1,2,20,3)}};
 auto ordered=displayMessageOrder(cases[2]);need(ordered&&(*ordered)==std::vector<std::size_t>{1,0},"Previous message lost causal order under timestamp skew");
 std::mt19937 random(0xB17C);for(unsigned c=0;c<384;++c){std::vector<DisplayMessage> messages;const auto n=1+random()%48;for(unsigned i=0;i<n;++i){const int parent=random()%4==0?-1:static_cast<int>(random()%n);messages.push_back(row(static_cast<int>(i),parent,random()%6?static_cast<int>(random()%70):0,100000+random()%10000,i+1));}std::shuffle(messages.begin(),messages.end(),random);cases.push_back(std::move(messages));}
 std::vector<std::vector<std::uint64_t>> expected;for(const auto& messages:cases){auto result=displayMessageOrder(messages);need(bool(result)&&result->size()==messages.size(),"Ordering dropped a bounded/cyclic record");std::vector<std::uint64_t> values;for(const auto i:*result){values.push_back(messages[i].ordinal);}expected.push_back(std::move(values));}
#ifdef BIGLYBTC_ORDER_ORACLE_ROOT
 QByteArray input;integer(input,static_cast<qint32>(cases.size()));for(const auto& messages:cases){integer(input,static_cast<qint32>(messages.size()));for(const auto& m:messages){integer(input,number(m.id));integer(input,m.previous?number(*m.previous):-1);wide(input,m.sequence);wide(input,m.timestamp);integer(input,static_cast<qint32>(m.ordinal));}}
 QProcess java;auto env=QProcessEnvironment::systemEnvironment();for(const auto* key:{"JAVA_TOOL_OPTIONS","JDK_JAVA_OPTIONS","_JAVA_OPTIONS","CLASSPATH","LD_PRELOAD","LD_AUDIT"}){env.remove(QLatin1StringView(key));}java.setProcessEnvironment(env);const QString root=QStringLiteral(BIGLYBTC_ORDER_ORACLE_ROOT),core=QStringLiteral(BIGLYBTC_ORDER_ORACLE_CORE);java.setWorkingDirectory(root+QStringLiteral("/full-handler"));java.start(QStringLiteral(BIGLYBTC_ORDER_ORACLE_JAVA),{QStringLiteral("-Xmx128m"),QStringLiteral("-XX:ActiveProcessorCount=1"),QStringLiteral("-Djava.security.manager=FullChatFence"),QStringLiteral("-Dbiglybtc.test.root=")+root,QStringLiteral("-Dbiglybtc.test.classes=")+core,QStringLiteral("-Duser.home=")+root+QStringLiteral("/home"),QStringLiteral("-Djava.io.tmpdir=")+root+QStringLiteral("/tmp"),QStringLiteral("-cp"),root+QStringLiteral("/full-handler/classes:")+root+QStringLiteral("/classes:")+core,QStringLiteral("com.vuze.plugins.azmsgsync.OriginalRoomOrder")});need(java.waitForStarted(3000),"Start fenced ordering reference");need(java.write(input)==input.size(),"Ordering fixture write");java.closeWriteChannel();if(!java.waitForFinished(15000)||java.exitCode()!=0){if(java.state()!=QProcess::NotRunning){java.kill();java.waitForFinished(3000);}std::cerr<<java.readAllStandardError().right(1200).toStdString();throw std::runtime_error("Original ordering oracle failed");}
 const auto output=java.readAllStandardOutput();qsizetype at=0;const auto read=[&](){need(output.size()-at>=4,"Short Java ordering output");const auto value=qFromBigEndian<qint32>(output.constData()+at);at+=4;return value;};for(std::size_t c=0;c<cases.size();++c){need(read()==static_cast<int>(expected[c].size()),"Original/native ordering sizes differ");for(const auto ordinal:expected[c]){if(read()!=static_cast<int>(ordinal)){std::cerr<<"Case "<<c<<'\n';throw std::runtime_error("Original/native previous-tree ordering differs");}}}need(at==output.size(),"Trailing ordering reference output");
#endif
 std::cout<<"PASS previous-message trees, branches, skew, ties and cycles: "<<cases.size()<<" bounded fixtures\n";return 0;
 }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
