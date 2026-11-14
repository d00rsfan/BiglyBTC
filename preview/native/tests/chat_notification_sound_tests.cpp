// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_notifications.hpp"
#include <QtTest>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <fcntl.h>
#include <unistd.h>
using namespace biglybtc::ui;
namespace {
void write(const QString& path,QByteArray bytes){QFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()){qFatal("Generated sound fixture");}}
QJsonObject receipt(const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly)){return {};}return QJsonDocument::fromJson(file.readAll()).object();}
QString helper(const QString& root){const auto path=root+"/generated helper";write(path,R"PY(#!/usr/bin/python3
import hashlib,json,os,sys,time
raw=sys.stdin.buffer.read()
fd=int(os.environ.get('BIGLYBTC_CHAT_SOUND_FD','-1'))
try:os.fstat(fd);leaked=True
except OSError:leaked=False
with open(os.environ['BIGLYBTC_CHAT_SOUND_RECEIPT'],'w') as out:json.dump({'args':sys.argv[1:],'sha256':hashlib.sha256(raw).hexdigest(),'bytes':len(raw),'leaked':leaked},out)
if os.environ.get('BIGLYBTC_CHAT_SOUND_HOLD')=='1':time.sleep(120)
)PY");QFile::setPermissions(path,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner);return path;}
}
class NotificationSoundTests final:public QObject{
 Q_OBJECT
private slots:
 void exactOriginalAssetsAndDialogSettings(){
  const auto wave=defaultChatNotificationSound();QCOMPARE(wave.size(),6160);QCOMPARE(QCryptographicHash::hash(wave,QCryptographicHash::Sha256).toHex(),QByteArray("41fbbe77e6e309a46a57bcef17f783bf365f9de67d35d70af4c93d90d86f53bb"));QCOMPARE(wave.first(4),QByteArray("RIFF"));
  for(bool dark:{false,true}){for(auto icon:{biglybtc::services::msgsync::ChatNotificationIcon::Idle,biglybtc::services::msgsync::ChatNotificationIcon::Message,biglybtc::services::msgsync::ChatNotificationIcon::Mention,biglybtc::services::msgsync::ChatNotificationIcon::Dim}){QVERIFY(!chatNotificationIcon(icon,dark).isNull());}}
  ChatNotificationSettings settings;settings.soundEnabled=true;settings.soundFile="generated.wav";auto* dialog=new ChatNotificationDialog(settings);dialog->show();QSignalSpy test(dialog,&ChatNotificationDialog::testSoundRequested);auto* file=dialog->findChild<QLineEdit*>("chat-notification-file");QVERIFY(file);QCOMPARE(file->text(),QStringLiteral("generated.wav"));dialog->findChild<QCheckBox*>("chat-notification-flash")->setChecked(false);QVERIFY(!dialog->settings().flashEnabled);QTest::mouseClick(dialog->findChild<QPushButton*>("chat-notification-test"),Qt::LeftButton);QCOMPARE(test.size(),1);QCOMPARE(test[0][0].toString(),QStringLiteral("generated.wav"));delete dialog;
 }
 void originalDefaultFileSelectionAndTypedArguments(){
  QTemporaryDir root;QVERIFY(root.isValid());const auto path=root.path()+"/generated $(literal) ; &.wav";write(path,"generated audio data");QVERIFY(selectedChatNotificationSound({}).isEmpty());QVERIFY(selectedChatNotificationSound("  <default>  ").isEmpty());QVERIFY(selectedChatNotificationSound(root.path()+"/absent.wav").isEmpty());QCOMPARE(selectedChatNotificationSound(" \t"+path+"\n"),path);
  const auto old=qgetenv("BIGLYBTC_CHAT_SOUND_RECEIPT");qputenv("BIGLYBTC_CHAT_SOUND_RECEIPT",(root.path()+"/receipt.json").toUtf8());const auto restore=qScopeGuard([&]{if(old.isNull()){qunsetenv("BIGLYBTC_CHAT_SOUND_RECEIPT");}else{qputenv("BIGLYBTC_CHAT_SOUND_RECEIPT",old);}});
  ChatNotificationSound sound(nullptr,helper(root.path()));QSignalSpy done(&sound,&ChatNotificationSound::completed),failed(&sound,&ChatNotificationSound::failed);QVERIFY(sound.play(path));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,3000);QVERIFY(done[0][0].toBool());QVERIFY(failed.empty());const auto data=receipt(root.path()+"/receipt.json");const auto args=data["args"].toArray();QCOMPARE(args.last().toString(),path);QCOMPARE(args[args.size()-2].toString(),QStringLiteral("--"));QCOMPARE(data["bytes"].toInt(),0);
 }
 void bundledSoundUsesPipeAndDoesNotInheritParentDescriptors(){
  QTemporaryDir root;QVERIFY(root.isValid());const auto output=root.path()+"/receipt.json",probe=root.path()+"/generated-lock";write(probe,"generated lock");const int fd=::open(probe.toUtf8().constData(),O_RDONLY);QVERIFY(fd>=0);const int high=::fcntl(fd,F_DUPFD,200);::close(fd);QVERIFY(high>=0);const auto close=qScopeGuard([&]{::close(high);});
  const auto oldReceipt=qgetenv("BIGLYBTC_CHAT_SOUND_RECEIPT"),oldFd=qgetenv("BIGLYBTC_CHAT_SOUND_FD");qputenv("BIGLYBTC_CHAT_SOUND_RECEIPT",output.toUtf8());qputenv("BIGLYBTC_CHAT_SOUND_FD",QByteArray::number(high));const auto restore=qScopeGuard([&]{if(oldReceipt.isNull()){qunsetenv("BIGLYBTC_CHAT_SOUND_RECEIPT");}else{qputenv("BIGLYBTC_CHAT_SOUND_RECEIPT",oldReceipt);}if(oldFd.isNull()){qunsetenv("BIGLYBTC_CHAT_SOUND_FD");}else{qputenv("BIGLYBTC_CHAT_SOUND_FD",oldFd);}});
  ChatNotificationSound sound(nullptr,helper(root.path()));QSignalSpy done(&sound,&ChatNotificationSound::completed);QVERIFY(sound.play({}));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,3000);QVERIFY(done[0][0].toBool());auto data=receipt(output);QCOMPARE(data["bytes"].toInt(),6160);QVERIFY(!data["leaked"].toBool());QCOMPARE(data["sha256"].toString(),QStringLiteral("41fbbe77e6e309a46a57bcef17f783bf365f9de67d35d70af4c93d90d86f53bb"));QVERIFY(data["args"].toArray().contains(QStringLiteral("--file-format=wav")));
 }
 void overlapIsDroppedAndProfileStopCancelsTheHelper(){
  QTemporaryDir root;QVERIFY(root.isValid());const auto output=root.path()+"/receipt.json";const auto oldReceipt=qgetenv("BIGLYBTC_CHAT_SOUND_RECEIPT"),oldHold=qgetenv("BIGLYBTC_CHAT_SOUND_HOLD");qputenv("BIGLYBTC_CHAT_SOUND_RECEIPT",output.toUtf8());qputenv("BIGLYBTC_CHAT_SOUND_HOLD","1");const auto restore=qScopeGuard([&]{if(oldReceipt.isNull()){qunsetenv("BIGLYBTC_CHAT_SOUND_RECEIPT");}else{qputenv("BIGLYBTC_CHAT_SOUND_RECEIPT",oldReceipt);}if(oldHold.isNull()){qunsetenv("BIGLYBTC_CHAT_SOUND_HOLD");}else{qputenv("BIGLYBTC_CHAT_SOUND_HOLD",oldHold);}});
  ChatNotificationSound sound(nullptr,helper(root.path()));QSignalSpy done(&sound,&ChatNotificationSound::completed),failed(&sound,&ChatNotificationSound::failed);QVERIFY(sound.play({}));QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(output),3000);QVERIFY(!sound.play({}));sound.stop();QTRY_VERIFY_WITH_TIMEOUT(!sound.playing(),3000);QCOMPARE(done.size(),1);QVERIFY(!done[0][0].toBool());QVERIFY(failed.empty());
 }
 void absentBackendReportsFailureAndAllowsReentrantDestruction(){
  QPointer<ChatNotificationSound> sound=new ChatNotificationSound(nullptr,"/generated/absent-audio-helper");QObject::connect(sound,&ChatNotificationSound::failed,sound,[&]{delete sound.data();});auto* ptr=sound.data();QVERIFY(!ptr->play({}));QVERIFY(!sound);
 }
};
QTEST_MAIN(NotificationSoundTests)
#include "chat_notification_sound_tests.moc"
