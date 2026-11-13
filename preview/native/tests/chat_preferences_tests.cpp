// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_profile.hpp"
#include <QTemporaryDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QFile>
#include <QTest>
#include <algorithm>
#include <array>
#include <stdexcept>
using namespace biglybtc;
namespace {
std::string b(std::string_view value){return std::to_string(value.size())+":"+std::string(value);}
std::string integer(std::int64_t value){return "i"+std::to_string(value)+"e";}
std::string dict(std::vector<std::pair<std::string,std::string>> rows){std::sort(rows.begin(),rows.end());std::string out="d";for(const auto& [key,value]:rows){out+=b(key)+value;}return out+"e";}
QByteArray hex(std::string_view value){return QByteArray(value.data(),qsizetype(value.size())).toHex();}
std::string canonical(const core::Node& node){if(node.type==core::Node::Type::Dictionary){std::vector<std::pair<std::string,std::string>> values;for(std::size_t n=0;n<node.children.size();n+=2){values.emplace_back(std::string(node.children[n].bytes),canonical(node.children[n+1]));}return dict(std::move(values));}if(node.type==core::Node::Type::List){std::string result="l";for(const auto& value:node.children){result+=canonical(value);}return result+"e";}if(node.type==core::Node::Type::Integer){return integer(node.integer);}return b(node.bytes);}
QList<QByteArray> ask(QProcess& java,QByteArray command){java.write(command+'\n');java.waitForBytesWritten(1000);if(!java.canReadLine()){java.waitForReadyRead(3000);}return java.readLine().trimmed().split('\t');}
std::string read(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly)){throw std::runtime_error("Generated preference file");}return f.readAll().toStdString();}
void write(const QString& path,std::string_view value){QFile f(path);if(!f.open(QIODevice::WriteOnly|QIODevice::Truncate)||f.write(value.data(),qint64(value.size()))!=qint64(value.size())){throw std::runtime_error("Generated preference write");}}
}
class ChatPreferencesTests final:public QObject{
 Q_OBJECT
private slots:
 void originalPropertyKeysBooleansAndUpdatesMatch(){
  if(!qEnvironmentVariableIsSet("BIGLYBTC_CHAT_PREFERENCES_JAVA")){QSKIP("Original preference bodies not configured");}QTemporaryDir root;QVERIFY(root.isValid());auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);QVERIFY(storage::ConfigFile::createMissing(*lease,"biglybt.config","de"));auto profile=app::ChatProfile::open(*lease);QVERIFY(profile);QProcess java;auto env=QProcessEnvironment::systemEnvironment();for(const auto* key:{"JAVA_TOOL_OPTIONS","JDK_JAVA_OPTIONS","_JAVA_OPTIONS","CLASSPATH","LD_PRELOAD","LD_AUDIT"}){env.remove(QLatin1StringView(key));}java.setProcessEnvironment(env);java.start(qEnvironmentVariable("BIGLYBTC_CHAT_PREFERENCES_JAVA"),{"-Xmx128m","-cp",qEnvironmentVariable("BIGLYBTC_CHAT_PREFERENCES_CLASSPATH"),"ChatPreferencesOracle",root.path()});QVERIFY(java.waitForStarted());const std::array<std::int64_t,8> values{-1,0,1,2,INT64_C(4294967296),INT64_C(4294967297),INT64_MIN,INT64_MAX};unsigned count=0;
  for(unsigned i=0;i<values.size();++i){for(unsigned j=0;j<values.size();++j){std::string publicKey(i%2?49:25,'x');for(unsigned n=0;n<publicKey.size();++n){publicKey[n]=char(i*19+j*11+n);}const auto key=app::ChatProfile::participantKey(publicKey);QVERIFY(key);const auto props=dict({{"ignored",integer(values[j])},{"pinned",integer(values[(j+2)%values.size()])},{"spammer","i1e"},{"future",b(std::string("opaque\0\xff",8))}});const auto source=dict({{*key,props},{"azbuddy.dchat.ui.hide.ratings",integer(values[i])},{"azbuddy.dchat.ui.hide.search_subs",integer(values[(i+3)%values.size()])},{"unknown-root",b("retained")}});write(root.path()+"/biglybt.config",source);std::array<std::string,1> keys{publicKey};auto before=(*profile)->preferences(keys);QVERIFY(before);QCOMPARE(read(root.path()+"/biglybt.config"),source);auto reference=ask(java,"R\t"+hex(publicKey)+'\t'+hex(source)+"\t0");QCOMPARE(reference.size(),3);QCOMPARE(reference[0].toStdString(),*key);const auto expected=QByteArray::number(before->display.hideRatings?1:0)+' '+QByteArray::number(before->display.hideSearchSubscriptions?1:0)+' '+QByteArray::number(before->participants.at(publicKey).ignored?1:0)+' '+QByteArray::number(before->participants.at(publicKey).pinned?1:0);QCOMPARE(reference[1],expected);
   for(bool pin:{false,true}){for(bool desired:{false,true}){write(root.path()+"/biglybt.config",source);const auto prior=pin?before->participants.at(publicKey).pinned:before->participants.at(publicKey).ignored;auto changed=(*profile)->saveParticipant(publicKey,pin?app::ChatParticipantField::Pinned:app::ChatParticipantField::Ignored,prior,desired);QVERIFY(changed);QCOMPARE(*changed,prior!=desired);auto oracle=ask(java,(pin?"P\t":"I\t")+hex(publicKey)+'\t'+hex(source)+(desired?"\t1":"\t0"));QCOMPARE(oracle.size(),3);auto native=core::Document::parse(read(root.path()+"/biglybt.config"));QVERIFY(native);auto original=core::Document::parse(QByteArray::fromHex(oracle[2]).toStdString());QVERIFY(original);QCOMPARE(canonical(native->root()),canonical(original->root()));++count;}}
  }}QCOMPARE(count,256U);java.closeWriteChannel();QVERIFY(java.waitForFinished());QCOMPARE(java.exitCode(),0);
 }
 void rootRemovalRetainsSourceAndSharedCopies(){
  const std::string raw="d1:ai1e1:b3:rawe";auto doc=core::Document::parse(raw);QVERIFY(doc);const auto copy=*doc;QVERIFY(doc->eraseValue("a"));QCOMPARE(doc->originalBytes(),raw);QCOMPARE(doc->serialize(),std::string("d1:b3:rawe"));QCOMPARE(copy.serialize(),raw);QVERIFY(doc->eraseValue("b"));QCOMPARE(doc->serialize(),std::string("de"));auto absent=doc->eraseValue("c");QVERIFY(absent&&!*absent);QCOMPARE(copy.serialize(),raw);
 }
 void staleWritesLegacyPinsUnknownFieldsAndEmptyMaps(){
  QTemporaryDir root;auto lease=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(lease);QVERIFY(storage::ConfigFile::createMissing(*lease,"biglybt.config","de"));auto profile=app::ChatProfile::open(*lease);QVERIFY(profile);const std::string publicKey(25,'q');auto key=app::ChatProfile::participantKey(publicKey);QVERIFY(key);const auto legacy=std::string("azbuddy.chat.pinned.")+key->substr(std::string("azbuddy.chat.props.").size());const auto source=dict({{*key,dict({{"future",b("retained")},{"spammer","i1e"}})},{legacy,"i1e"},{"unknown-root","i9e"}});write(root.path()+"/biglybt.config",source);std::array<std::string,1> keys{publicKey};auto prefs=(*profile)->preferences(keys);QVERIFY(prefs);QVERIFY(prefs->participants.at(publicKey).pinned&&!prefs->participants.at(publicKey).ignored);QCOMPARE(read(root.path()+"/biglybt.config"),source);auto changed=(*profile)->saveParticipant(publicKey,app::ChatParticipantField::Ignored,false,true);QVERIFY2(changed,changed?"":changed.error().message.c_str());
  QVERIFY(*changed);
  auto cfg=storage::ConfigFile::load(*lease,"biglybt.config");QVERIFY(cfg);QVERIFY(!cfg->document().find(legacy));const auto* props=cfg->document().find(*key);QVERIFY(props&&props->find("pinned")->integer==1&&props->find("future")->bytes=="retained"&&props->find("spammer")->integer==1);auto saved=read(root.path()+"/biglybt.config");QVERIFY(!(*profile)->saveParticipant(publicKey,app::ChatParticipantField::Ignored,false,false));QCOMPARE(read(root.path()+"/biglybt.config"),saved);changed=(*profile)->saveDisplay(app::ChatDisplayField::Ratings,false,true);QVERIFY2(changed,changed?"":changed.error().message.c_str());
  QVERIFY(*changed);
  saved=read(root.path()+"/biglybt.config");QVERIFY(!(*profile)->saveDisplay(app::ChatDisplayField::Ratings,false,false));QCOMPARE(read(root.path()+"/biglybt.config"),saved);const std::string emptyKey(25,'r');auto target=app::ChatProfile::participantKey(emptyKey);QVERIFY(target);QVERIFY((*profile)->saveParticipant(emptyKey,app::ChatParticipantField::Ignored,false,true));QVERIFY((*profile)->saveParticipant(emptyKey,app::ChatParticipantField::Ignored,true,false));cfg=storage::ConfigFile::load(*lease,"biglybt.config");QVERIFY(cfg&&!cfg->document().find(*target));QVERIFY(!app::ChatProfile::participantKey(std::string(15,'x')));QVERIFY(!app::ChatProfile::participantKey(std::string(513,'x')));QFile::remove(root.path()+"/biglybt.config");const auto backup=read(root.path()+"/biglybt.config.bak");QVERIFY(!(*profile)->saveDisplay(app::ChatDisplayField::Subscriptions,false,true));QCOMPARE(read(root.path()+"/biglybt.config.bak"),backup);QVERIFY(!QFile::exists(root.path()+"/biglybt.config"));
 }
};
QTEST_GUILESS_MAIN(ChatPreferencesTests)
#include "chat_preferences_tests.moc"
