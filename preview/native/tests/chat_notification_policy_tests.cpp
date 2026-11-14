#include "../services/msgsync/notification_policy.hpp"
#include <QtTest>
#include <QProcess>
#include <QTemporaryDir>
#include <random>
using namespace biglybtc::services::msgsync;
namespace {
QString hex(const QString& s){QString out;for(QChar c:s){out+=QStringLiteral("%1").arg(static_cast<unsigned>(c.unicode()),4,16,QLatin1Char('0'));}return out;}
QString image(ChatNotificationIcon v){switch(v){case ChatNotificationIcon::Idle:return "idle";case ChatNotificationIcon::Message:return "message";case ChatNotificationIcon::Mention:return "mention";case ChatNotificationIcon::Dim:return "dim";}return {};}
}
class NotificationPolicyTests final:public QObject{
 Q_OBJECT
private slots:
 void actualOriginalTimerAndRawNicknameTraces(){
  QTemporaryDir root;QVERIFY(root.isValid());QByteArray input;QList<QByteArray> expected;
  const auto nick=[&](const QString& text,const QString& name){input+="N\t"+hex(text).toLatin1()+'\t'+hex(name).toLatin1()+'\n';QStringList values;for(int value:notificationNickLocations(text,name)){values+=QString::number(value);}expected+=values.join(',').toLatin1();};
  const QList<QString> names={QStringLiteral("A"),QStringLiteral("Generated Actor"),QStringLiteral("Ім'я"),QStringLiteral("040ABC"),QString::fromUtf16(u"\U0001f680"),QString{},QStringLiteral("ana")};
  const QList<QString> surrounds={QString{},QStringLiteral("_"),QStringLiteral("A"),QStringLiteral("9"),QStringLiteral("Ⅻ"),QStringLiteral("é"),QStringLiteral("Ж"),QString(QChar(0xa0)),QString(QChar(0x301)),QString(QChar(0xd800))};
  for(const auto& name:names){auto wire=name;wire.replace(u' ',QChar(0xa0));for(const auto& a:surrounds){for(const auto& b:surrounds){nick(a+wire+b+QStringLiteral(" https://example.invalid/")+wire+QStringLiteral("?q=")+wire,name);}}}
  std::mt19937 rng(4217);ChatPendingNotifications state;ChatNotificationSettings settings;std::vector<ChatPendingNotification> rooms={{"Generated-A",1,0},{"Generated-B",2,0},{"Generated-C",3,0}};
  std::uint64_t generation=1;
  for(int n=0;n<1024;++n){if(n&&n%127==0){++generation;}
   for(auto& room:rooms){room.count=rng()%9;room.disabled=(rng()%4)==0;room.mention=(rng()%5)==0;room.alive=(rng()%16)!=0;if((rng()%27)==0){++room.epoch;}}
   if(n%19==0){for(auto& room:rooms){room.count=0;}}
   settings.soundEnabled=(rng()%3)!=0;settings.flashEnabled=(rng()%3)!=0;
   state.update(generation,rooms);const auto pulse=state.tick(settings);expected+=(image(pulse.icon)+' '+QString::number(pulse.sound?1:0)).toLatin1();
   input+="T\t"+QByteArray::number(qulonglong(generation))+'\t'+QByteArray::number(settings.soundEnabled?1:0)+'\t'+QByteArray::number(settings.flashEnabled?1:0)+'\t';
   for(std::size_t i=0;i<rooms.size();++i){const auto& r=rooms[i];if(i){input+=',';}input+=hex(QString::fromStdString(r.key)).toLatin1()+':'+QByteArray::number(qulonglong(r.epoch))+':'+QByteArray::number(r.count)+':'+QByteArray::number(r.disabled?1:0)+':'+QByteArray::number(r.mention?1:0)+':'+QByteArray::number(r.alive?1:0);}input+='\n';
  }
  QProcess java;java.start(qEnvironmentVariable("BIGLYBTC_CHAT_NOTIFICATION_JAVA"),{"-Xmx128m","-cp",qEnvironmentVariable("BIGLYBTC_CHAT_NOTIFICATION_CLASSPATH"),"ChatNotificationOracle",root.path()});QVERIFY(java.waitForStarted());QCOMPARE(java.write(input),qint64(input.size()));java.closeWriteChannel();QVERIFY(java.waitForFinished(20000));QVERIFY2(java.exitCode()==0,java.readAllStandardError().constData());auto actual=java.readAllStandardOutput().split('\n');if(actual.last().isEmpty()){actual.removeLast();}QCOMPARE(actual.size(),expected.size());
  for(qsizetype n=0;n<expected.size();++n){QVERIFY2(actual[n]==expected[n],qPrintable(QStringLiteral("Selected original trace %1: expected [%2], got [%3]").arg(n).arg(QString::fromLatin1(expected[n]),QString::fromLatin1(actual[n]))));}
  qInfo("Qualified %lld selected original notification/nickname traces",static_cast<long long>(expected.size()));
 }
 void readAndRefillBeforeTheNextTickDoesNotCreateAnotherSound(){
  ChatPendingNotifications state;ChatNotificationSettings settings;settings.soundEnabled=true;std::vector<ChatPendingNotification> rooms={{"Generated",17,1}};state.update(1,rooms);QVERIFY(state.tick(settings).sound);
  rooms[0].count=0;state.update(1,rooms);QVERIFY(state.running());rooms[0].count=2;state.update(1,rooms);QVERIFY(!state.tick(settings).sound);
  rooms[0].count=0;state.update(1,rooms);QCOMPARE(state.tick(settings).icon,ChatNotificationIcon::Idle);QVERIFY(!state.running());rooms[0].count=1;state.update(1,rooms);QVERIFY(state.tick(settings).sound);
 }
 void passiveSnapshotsAndRoomCountIncreasesDoNotCreateEdges(){
  ChatPendingNotifications state;ChatNotificationSettings settings;settings.soundEnabled=true;std::vector<ChatPendingNotification> rooms={{"Generated",17,1}};state.update(1,rooms);QVERIFY(state.tick(settings).sound);
  for(int n=0;n<30;++n){state.update(1,rooms);}rooms[0].count=9;state.update(1,rooms);QVERIFY(!state.tick(settings).sound);++rooms[0].epoch;state.update(1,rooms);QVERIFY(state.tick(settings).sound);
  state.update(2,rooms);QVERIFY(state.tick(settings).sound);state.reset();QVERIFY(!state.running());
 }
};
QTEST_GUILESS_MAIN(NotificationPolicyTests)
#include "chat_notification_policy_tests.moc"
