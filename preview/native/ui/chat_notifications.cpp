// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_notifications.hpp"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>
#include <filesystem>
// Qt resource initialisers must be invoked outside a C++ namespace.
static void initialiseChatNotificationResources(){Q_INIT_RESOURCE(chat_notifications);}
namespace biglybtc::ui {
namespace {
void resources(){static const bool ready=[](){initialiseChatNotificationResources();return true;}();(void)ready;}
}
QIcon chatNotificationIcon(services::msgsync::ChatNotificationIcon state,bool dark){
 resources();QString name;
 switch(state){case services::msgsync::ChatNotificationIcon::Idle:name="gray";break;case services::msgsync::ChatNotificationIcon::Message:name="green";break;case services::msgsync::ChatNotificationIcon::Mention:name="red";break;case services::msgsync::ChatNotificationIcon::Dim:name="gray_text";break;}
 return QIcon(QStringLiteral(":/biglybtc/chat/dchat_%1%2.png").arg(name,dark?QStringLiteral("-dark"):QString{}));
}
QByteArray defaultChatNotificationSound(){resources();QFile file(QStringLiteral(":/biglybtc/chat/notification.wav"));if(!file.open(QIODevice::ReadOnly)){return {};}return file.readAll();}
QString selectedChatNotificationSound(QString file){
 // GeneralUtils.playSound uses String.trim(), not Unicode whitespace trimming.
 while(!file.isEmpty()&&file.front().unicode()<=32){file.remove(0,1);}
 while(!file.isEmpty()&&file.back().unicode()<=32){file.chop(1);}
 if(file.isEmpty()||file.startsWith(u'<')||file.contains(QChar(0))){return {};}
 const auto bytes=file.toUtf8();const std::filesystem::path path(std::string(bytes.constData(),std::size_t(bytes.size())));std::error_code error;
 if(!std::filesystem::exists(path,error)||error){return {};}
 const auto absolute=std::filesystem::absolute(path,error);if(error){return {};}
 return QString::fromStdString(absolute.string());
}
ChatNotificationDialog::ChatNotificationDialog(const ChatNotificationSettings& value,QWidget* parent):QDialog(parent),sound_(new QCheckBox(tr("Play notification sound"),this)),flash_(new QCheckBox(tr("Flash the message indicator"),this)),file_(new QLineEdit(QString::fromUtf8(value.soundFile.data(),qsizetype(value.soundFile.size())),this)){
 setObjectName(QStringLiteral("chat-notification-settings"));setWindowTitle(tr("Chat notifications"));setMinimumWidth(460);setWindowModality(Qt::WindowModal);setAttribute(Qt::WA_DeleteOnClose);
 sound_->setObjectName(QStringLiteral("chat-notification-sound"));flash_->setObjectName(QStringLiteral("chat-notification-flash"));file_->setObjectName(QStringLiteral("chat-notification-file"));file_->setMaxLength(16384);file_->setPlaceholderText(tr("Default sound"));sound_->setChecked(value.soundEnabled);sound_->setToolTip(tr("Plays once when a room gains unread messages; further messages in that unread room do not repeat it."));flash_->setChecked(value.flashEnabled);originalFile_=value.soundFile;initialFileText_=file_->text();
 auto* layout=new QVBoxLayout(this);layout->addWidget(sound_);layout->addWidget(flash_);auto* row=new QHBoxLayout;row->addWidget(file_,1);auto* browse=new QPushButton(tr("Browse…"),this);row->addWidget(browse);auto* restore=new QPushButton(tr("Default"),this);row->addWidget(restore);layout->addLayout(row);
 auto* test=new QPushButton(tr("Test sound"),this);test->setObjectName(QStringLiteral("chat-notification-test"));layout->addWidget(test);
 auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,this,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
 connect(restore,&QPushButton::clicked,file_,&QLineEdit::clear);connect(browse,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,tr("Notification sound"),file_->text(),tr("Audio files (*.wav *.aiff *.aif *.au);;All files (*)"));if(!path.isEmpty()){file_->setText(path);}});
 connect(test,&QPushButton::clicked,this,[this]{emit testSoundRequested(file_->text());});
}
ChatNotificationSettings ChatNotificationDialog::settings()const{return {sound_->isChecked(),flash_->isChecked(),file_->text()==initialFileText_?originalFile_:file_->text().toUtf8().toStdString()};}
ChatNotificationSound::ChatNotificationSound(QObject* parent,QString helper):QObject(parent),helper_(std::move(helper)){}
ChatNotificationSound::~ChatNotificationSound(){if(process_){process_->disconnect(this);process_->kill();}}
void ChatNotificationSound::stop(){cancelled_=true;if(process_){process_->kill();}}
bool ChatNotificationSound::play(QString file){
 // Original allows overlapping Clips. The Linux adapter deliberately permits
 // one live helper, dropping overlap rather than queueing stale future sounds.
 if(process_){return false;}
 if(!QFileInfo(helper_).isExecutable()){emit failed(tr("Notification sound needs paplay (pulseaudio-utils)"));return false;}
 const auto selected=selectedChatNotificationSound(std::move(file));const auto data=selected.isEmpty()?defaultChatNotificationSound():QByteArray{};
 if(selected.isEmpty()&&data.isEmpty()){emit failed(tr("The default chat notification sound is unavailable"));return false;}
 auto* child=new QProcess(this);process_=child;cancelled_=false;
#ifdef Q_OS_UNIX
 // In particular, never pass the running-image manifest lock to a sound
 // process, so it cannot retain that image after Native has exited.
 child->setUnixProcessParameters(QProcess::UnixProcessFlag::CloseFileDescriptors);
#endif
 QStringList args={QStringLiteral("--client-name=BiglyBTC"),QStringLiteral("--stream-name=Chat notification")};
 if(selected.isEmpty()){args+=QStringLiteral("--file-format=wav");}else{args+=QStringLiteral("--");args+=selected;}
 const auto finish=[this,child](bool ok,QString error){
  if(process_!=child){return;}const bool cancelled=cancelled_;process_=nullptr;child->setParent(nullptr);child->deleteLater();QPointer<ChatNotificationSound> guard(this);
  if(!ok&&!cancelled){emit failed(std::move(error));if(!guard){return;}}emit completed(ok&&!cancelled);
 };
 connect(child,&QProcess::started,this,[this,child,data]{if(process_!=child){return;}if(cancelled_){child->kill();return;}if(!data.isEmpty()){child->write(data);}child->closeWriteChannel();});
 connect(child,&QProcess::readyReadStandardOutput,child,[child]{child->readAllStandardOutput();});connect(child,&QProcess::readyReadStandardError,child,[child]{child->readAllStandardError();});
 connect(child,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[finish](int code,QProcess::ExitStatus state){finish(state==QProcess::NormalExit&&code==0,tr("Chat notification sound could not be played"));});
 connect(child,&QProcess::errorOccurred,this,[finish](QProcess::ProcessError error){if(error==QProcess::FailedToStart){finish(false,tr("Chat notification sound could not be started"));}});
 child->start(helper_,args,QIODevice::ReadWrite);return true;
}
}
