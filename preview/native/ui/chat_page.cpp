// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_page.hpp"
#include <QCheckBox>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QPointer>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextCursor>
#include <QVBoxLayout>
#include <algorithm>
namespace biglybtc::ui {
namespace {
class ComposerKeys final:public QObject {
 QPointer<QPushButton> send_;bool composing_=false;
 bool eventFilter(QObject*,QEvent* event)override{
  if(event->type()==QEvent::InputMethod){composing_=!static_cast<QInputMethodEvent*>(event)->preeditString().isEmpty();}
  if(event->type()==QEvent::KeyPress&&!composing_){const auto* key=static_cast<QKeyEvent*>(event);if((key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter)&&!(key->modifiers()&Qt::ShiftModifier)){if(send_&&send_->isEnabled()){send_->click();}return true;}}
  return false;
 }
public:
 ComposerKeys(QPlainTextEdit* editor,QPushButton* send):QObject(editor),send_(send){editor->installEventFilter(this);}
};
}
ChatPage::ChatPage(QWidget* parent):QWidget(parent),rooms_(new QListWidget(this)),participants_(new QListWidget(this)),status_(new QLabel(this)),transcript_(new QTextBrowser(this)),composer_(new QPlainTextEdit(this)),join_(new QPushButton(tr("Join room…"),this)),leave_(new QPushButton(tr("Leave room"),this)),send_(new QPushButton(tr("Send"),this)),favourite_(new QCheckBox(tr("Favourite"),this)),history_(new QCheckBox(tr("Save history"),this)),sharedNickname_(new QCheckBox(tr("Shared nickname"),this)),nickname_(new QPushButton(tr("Nickname…"),this)){
 setObjectName(QStringLiteral("general-chat-page"));rooms_->setObjectName(QStringLiteral("chat-rooms"));participants_->setObjectName(QStringLiteral("chat-participants"));transcript_->setObjectName(QStringLiteral("chat-transcript"));composer_->setObjectName(QStringLiteral("chat-composer"));send_->setObjectName(QStringLiteral("chat-send"));status_->setObjectName(QStringLiteral("chat-status"));
 new ComposerKeys(composer_,send_);history_->setToolTip(tr("Stop saving new messages when disabled; existing saved history is retained."));
 status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);transcript_->setOpenLinks(false);transcript_->setOpenExternalLinks(false);composer_->setMaximumHeight(100);composer_->setPlaceholderText(tr("Write a message — Enter sends, Shift+Enter starts a new line"));
 auto* root=new QVBoxLayout(this);root->addWidget(status_);auto* split=new QSplitter(this);root->addWidget(split,1);
 auto* left=new QWidget(split);auto* leftLayout=new QVBoxLayout(left);leftLayout->addWidget(new QLabel(tr("Public rooms"),left));leftLayout->addWidget(rooms_,1);leftLayout->addWidget(join_);leftLayout->addWidget(leave_);
 auto* centre=new QWidget(split);auto* middle=new QVBoxLayout(centre);auto* options=new QHBoxLayout;options->addWidget(favourite_);options->addWidget(history_);options->addWidget(sharedNickname_);options->addWidget(nickname_);options->addStretch();middle->addLayout(options);middle->addWidget(transcript_,1);middle->addWidget(composer_);middle->addWidget(send_,0,Qt::AlignRight);
 auto* right=new QWidget(split);auto* roster=new QVBoxLayout(right);roster->addWidget(new QLabel(tr("Participants"),right));roster->addWidget(participants_);participants_->setToolTip(tr("Identities seen in recent messages. A nickname alone does not prove identity."));split->setStretchFactor(1,1);
 connect(join_,&QPushButton::clicked,this,[this]{bool accepted=false;const auto key=QInputDialog::getText(this,tr("Join Public room"),tr("Room name"),QLineEdit::Normal,{},&accepted);if(accepted&&!key.trimmed().isEmpty()&&key.toUtf8().size()<=4096){emit joinRequested(key);}});
 connect(rooms_,&QListWidget::currentRowChanged,this,[this](int row){if(row>=0&&row<value_.rooms.size()){rememberDraft();emit selectionRequested(value_.rooms[row].key);}});
 connect(leave_,&QPushButton::clicked,this,[this]{if(!value_.selected.isEmpty()){emit leaveRequested(value_.selected);}});
 connect(composer_,&QPlainTextEdit::textChanged,this,&ChatPage::updateSend);
 connect(send_,&QPushButton::clicked,this,[this]{if(send_->isEnabled()){const auto text=composer_->toPlainText();pending_.insert(value_.selected,text);updateSend();emit sendRequested(value_.selected,text);}});
 const auto optionsChanged=[this]{if(!value_.selected.isEmpty()){emit optionsRequested(value_.selected,favourite_->isChecked(),history_->isChecked());}};connect(favourite_,&QCheckBox::toggled,this,optionsChanged);connect(history_,&QCheckBox::toggled,this,optionsChanged);
 sharedNickname_->setObjectName(QStringLiteral("chat-shared-nickname"));nickname_->setObjectName(QStringLiteral("chat-nickname"));
 connect(sharedNickname_,&QCheckBox::toggled,this,[this](bool shared){if(!value_.selected.isEmpty()){emit nicknameModeRequested(value_.selected,shared);}});
 connect(nickname_,&QPushButton::clicked,this,[this]{const auto found=std::ranges::find_if(value_.rooms,[this](const auto& r){return r.key==value_.selected;});if(found==value_.rooms.end()){return;}const auto key=found->key;const bool mode=found->sharedNickname;bool accepted=false;const auto value=QInputDialog::getText(this,tr("Chat nickname"),mode?tr("Nickname shared by Public rooms"):tr("Nickname for this room"),QLineEdit::Normal,found->nickname,&accepted);if(accepted&&value.toUtf8().size()<=256){emit nicknameRequested(key,value,mode);}});
 setSnapshot({});
}
void ChatPage::rememberDraft(){if(!value_.selected.isEmpty()){drafts_.insert(value_.selected,composer_->toPlainText());while(drafts_.size()>256){drafts_.erase(drafts_.begin());}}}
void ChatPage::updateSend(){const auto selected=std::ranges::find_if(value_.rooms,[this](const auto& r){return r.key==value_.selected;});const bool enabled=selected!=value_.rooms.end()&&selected->joined&&selected->canSend&&!selected->readOnly&&!pending_.contains(value_.selected);send_->setEnabled(enabled&&!composer_->toPlainText().trimmed().isEmpty());composer_->setEnabled(enabled);}
void ChatPage::submissionFailed(const QString& key){pending_.remove(key);updateSend();}
void ChatPage::submissionAccepted(const QString& key,const QString& text){pending_.remove(key);if(value_.selected==key&&composer_->toPlainText()==text){composer_->clear();}if(drafts_.value(key)==text){drafts_.remove(key);}updateSend();}
void ChatPage::setSnapshot(ChatViewSnapshot next){
 if(next.generation<value_.generation||(next.generation==value_.generation&&next.sequence<value_.sequence)){return;}
 const bool generationChanged=next.generation!=value_.generation;const bool selectionChanged=generationChanged||next.selected!=value_.selected;const bool messagesChanged=selectionChanged||next.messages!=value_.messages;if(generationChanged){drafts_.clear();pending_.clear();}else if(selectionChanged){rememberDraft();}const QSignalBlocker blockRooms(rooms_),blockFavourite(favourite_),blockHistory(history_),blockShared(sharedNickname_);value_=std::move(next);rooms_->clear();int selected=-1;
 for(qsizetype i=0;i<value_.rooms.size();++i){const auto& r=value_.rooms[i];auto label=(r.favourite?QStringLiteral("★ "):QString{})+r.key;if(r.unread>0){label+=tr(" (%1)").arg(r.unread);}auto* item=new QListWidgetItem(label,rooms_);item->setToolTip(r.key);if(r.key==value_.selected){selected=static_cast<int>(i);}}
 rooms_->setCurrentRow(selected);const auto found=std::ranges::find_if(value_.rooms,[this](const auto& r){return r.key==value_.selected;});const bool has=found!=value_.rooms.end();status_->setText(has?found->status:value_.availability);leave_->setEnabled(has&&found->joined);favourite_->setEnabled(has);history_->setEnabled(has);favourite_->setChecked(has&&found->favourite);history_->setChecked(has&&found->saveHistory);sharedNickname_->setEnabled(has);nickname_->setEnabled(has);sharedNickname_->setChecked(has&&found->sharedNickname);nickname_->setToolTip(has?found->nickname:QString{});
 if(messagesChanged){const auto* bar=transcript_->verticalScrollBar();const bool bottom=bar->value()>=bar->maximum()-2;const auto oldPosition=bar->value();transcript_->clear();QTextCursor cursor(transcript_->document());
 for(const auto& message:value_.messages){const auto time=message.time.isValid()?message.time.toLocalTime().toString(QStringLiteral("HH:mm ")):QString{};cursor.insertText(time+message.nickname+QStringLiteral(": ")+message.text+QStringLiteral("\n"));}
 if(bottom||selectionChanged){transcript_->verticalScrollBar()->setValue(transcript_->verticalScrollBar()->maximum());}else{transcript_->verticalScrollBar()->setValue(oldPosition);}}
 participants_->clear();for(const auto& participant:value_.participants){auto* item=new QListWidgetItem(participant.nickname,participants_);item->setToolTip(QString::fromLatin1(participant.publicKey.toHex()));}
 if(selectionChanged){composer_->setPlainText(drafts_.value(value_.selected));}updateSend();
}
}
