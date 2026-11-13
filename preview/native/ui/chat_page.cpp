// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_page.hpp"
#include "../services/msgsync/chat_composer.hpp"
#include <QCheckBox>
#include <QMenu>
#include <QAction>
#include <QFont>
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
#include <QTextDocument>
#include <QVBoxLayout>
#include <algorithm>
namespace biglybtc::ui {
using namespace services::msgsync::composer;
namespace {
QString composerText(const QPlainTextEdit* editor){auto text=editor->document()->toRawText();text.replace(QChar::ParagraphSeparator,QLatin1Char('\n'));return text;}
class ComposerKeys final:public QObject {
 QPointer<QPlainTextEdit> editor_;QPointer<QPushButton> send_;bool composing_=false;quint64 generation_=0;QString room_;QMap<QString,ChatInputHistory> histories_;
 ChatInputHistory& history(){const auto generation=editor_->property("chat-generation").toULongLong();if(generation_!=generation){histories_.clear();generation_=generation;room_.clear();}const auto room=editor_->property("chat-room").toString();if(room_!=room){if(histories_.contains(room_)){histories_[room_].position=-1;histories_[room_].buffered.clear();}room_=room;}if(!histories_.contains(room_)&&histories_.size()>=32){histories_.erase(histories_.begin());}return histories_[room_];}
 bool eventFilter(QObject*,QEvent* event)override{
  if(event->type()==QEvent::InputMethod){composing_=!static_cast<QInputMethodEvent*>(event)->preeditString().isEmpty();}
  if(event->type()==QEvent::KeyPress&&!composing_&&editor_&&editor_->isEnabled()){const auto* key=static_cast<QKeyEvent*>(event);
   if(key->modifiers()==Qt::ControlModifier&&(key->key()==Qt::Key_B||key->key()==Qt::Key_I)){auto cursor=editor_->textCursor();const auto start=cursor.selectionStart();auto end=cursor.selectionEnd();const auto text=composerText(editor_);while(end>start&&text[end-1]==u' '){--end;}auto selected=text.mid(start,end-start);if(!selected.isEmpty()){const auto mark=key->key()==Qt::Key_B?QStringLiteral("**"):QStringLiteral("*");if(selected.startsWith(mark)&&selected.endsWith(mark)&&selected.size()>=mark.size()*2){selected=selected.mid(mark.size(),selected.size()-mark.size()*2);}else{selected=mark+selected+mark;}editor_->setPlainText(text.left(start)+selected+text.mid(end));cursor=editor_->textCursor();cursor.setPosition(start);cursor.setPosition(start+int(selected.size()),QTextCursor::KeepAnchor);editor_->setTextCursor(cursor);}return true;}
   if(key->text()==QStringLiteral("\t")){return true;}
   if(key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter){if(key->modifiers()&(Qt::ShiftModifier|Qt::ControlModifier|Qt::AltModifier)){editor_->insertPlainText(QStringLiteral("\n"));}else if(send_&&send_->isEnabled()){send_->click();}return true;}if(key->key()==Qt::Key_Up||key->key()==Qt::Key_Down){auto& state=history();editor_->setPlainText(key->key()==Qt::Key_Up?state.up(composerText(editor_)):state.down(composerText(editor_)));auto cursor=editor_->textCursor();cursor.movePosition(QTextCursor::End);editor_->setTextCursor(cursor);return true;}}
  return false;
 }
public:
 ComposerKeys(QPlainTextEdit* editor,QPushButton* send):QObject(editor),editor_(editor),send_(send){editor->installEventFilter(this);connect(send,&QPushButton::clicked,this,[this]{if(editor_&&send_&&send_->isEnabled()){history().submitted(composerText(editor_));}});}
};
}
ChatPage::ChatPage(QWidget* parent):QWidget(parent),rooms_(new QListWidget(this)),participants_(new QListWidget(this)),status_(new QLabel(this)),transcript_(new QTextBrowser(this)),composer_(new QPlainTextEdit(this)),join_(new QPushButton(tr("Join room…"),this)),leave_(new QPushButton(tr("Leave room"),this)),send_(new QPushButton(tr("Send"),this)),favourite_(new QCheckBox(tr("Favourite"),this)),history_(new QCheckBox(tr("Save history"),this)),sharedNickname_(new QCheckBox(tr("Shared nickname"),this)),nickname_(new QPushButton(tr("Nickname…"),this)),display_(new QPushButton(tr("Display…"),this)){
 setObjectName(QStringLiteral("general-chat-page"));rooms_->setObjectName(QStringLiteral("chat-rooms"));participants_->setObjectName(QStringLiteral("chat-participants"));transcript_->setObjectName(QStringLiteral("chat-transcript"));composer_->setObjectName(QStringLiteral("chat-composer"));send_->setObjectName(QStringLiteral("chat-send"));status_->setObjectName(QStringLiteral("chat-status"));
 new ComposerKeys(composer_,send_);history_->setToolTip(tr("Stop saving new messages when disabled; existing saved history is retained."));
 status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);transcript_->setOpenLinks(false);transcript_->setOpenExternalLinks(false);composer_->setMaximumHeight(100);composer_->setPlaceholderText(tr("Write a message — Enter sends, Shift+Enter starts a new line"));
 auto* root=new QVBoxLayout(this);root->addWidget(status_);auto* split=new QSplitter(this);root->addWidget(split,1);
 auto* left=new QWidget(split);auto* leftLayout=new QVBoxLayout(left);leftLayout->addWidget(new QLabel(tr("Public rooms"),left));leftLayout->addWidget(rooms_,1);leftLayout->addWidget(join_);leftLayout->addWidget(leave_);
 auto* centre=new QWidget(split);auto* middle=new QVBoxLayout(centre);auto* options=new QHBoxLayout;options->addWidget(favourite_);options->addWidget(history_);options->addWidget(sharedNickname_);options->addWidget(nickname_);options->addWidget(display_);options->addStretch();middle->addLayout(options);middle->addWidget(transcript_,1);middle->addWidget(composer_);middle->addWidget(send_,0,Qt::AlignRight);
 auto* right=new QWidget(split);auto* roster=new QVBoxLayout(right);roster->addWidget(new QLabel(tr("Participants"),right));roster->addWidget(participants_);participants_->setToolTip(tr("Identities seen in recent messages. A nickname alone does not prove identity."));split->setStretchFactor(1,1);
 connect(join_,&QPushButton::clicked,this,[this]{bool accepted=false;const auto key=QInputDialog::getText(this,tr("Join Public room"),tr("Room name"),QLineEdit::Normal,{},&accepted);if(accepted&&!key.trimmed().isEmpty()&&key.toUtf8().size()<=4096){emit joinRequested(key);}});
 connect(rooms_,&QListWidget::currentRowChanged,this,[this](int row){if(row>=0&&row<value_.rooms.size()){rememberDraft();emit selectionRequested(value_.rooms[row].key);}});
 connect(leave_,&QPushButton::clicked,this,[this]{if(!value_.selected.isEmpty()){emit leaveRequested(value_.selected);}});
 connect(composer_,&QPlainTextEdit::textChanged,this,&ChatPage::updateSend);
 connect(send_,&QPushButton::clicked,this,[this]{if(send_->isEnabled()){const auto raw=composerText(composer_);const auto text=trimChatInput(raw);pending_.insert(value_.selected,raw);updateSend();emit sendRequested(value_.selected,text);}});
 const auto optionsChanged=[this]{if(!value_.selected.isEmpty()){emit optionsRequested(value_.selected,favourite_->isChecked(),history_->isChecked());}};connect(favourite_,&QCheckBox::toggled,this,optionsChanged);connect(history_,&QCheckBox::toggled,this,optionsChanged);
 sharedNickname_->setObjectName(QStringLiteral("chat-shared-nickname"));nickname_->setObjectName(QStringLiteral("chat-nickname"));
 connect(sharedNickname_,&QCheckBox::toggled,this,[this](bool shared){if(!value_.selected.isEmpty()){emit nicknameModeRequested(value_.selected,shared);}});
 connect(nickname_,&QPushButton::clicked,this,[this]{const auto found=std::ranges::find_if(value_.rooms,[this](const auto& r){return r.key==value_.selected;});if(found==value_.rooms.end()){return;}const auto key=found->key;const bool mode=found->sharedNickname;bool accepted=false;const auto value=QInputDialog::getText(this,tr("Chat nickname"),mode?tr("Nickname shared by Public rooms"):tr("Nickname for this room"),QLineEdit::Normal,found->nickname,&accepted);if(accepted&&value.toUtf8().size()<=256){emit nicknameRequested(key,value,mode);}});
 display_->setObjectName(QStringLiteral("chat-display"));
 connect(display_,&QPushButton::clicked,this,[this]{
  auto* menu=new QMenu(this);menu->setObjectName(QStringLiteral("chat-display-menu"));const auto key=value_.selected;const auto generation=value_.generation;
  const auto add=[&](QString label,bool ratings,bool checked){auto* action=menu->addAction(label);action->setCheckable(true);action->setChecked(checked);connect(action,&QAction::triggered,this,[this,key,generation,ratings,checked](bool next){if(value_.generation==generation&&value_.selected==key){emit displayPreferenceRequested(key,ratings,checked,next);}});};
  add(tr("Hide ratings"),true,value_.hideRatings);add(tr("Hide search subscriptions"),false,value_.hideSubscriptions);connect(menu,&QMenu::aboutToHide,menu,&QObject::deleteLater);menu->popup(display_->mapToGlobal(QPoint(0,display_->height())));
 });
 connect(participants_,&QListWidget::itemDoubleClicked,this,[this](QListWidgetItem* item){if(!item||!composer_->isEnabled()){return;}const auto key=item->data(Qt::UserRole).toByteArray();const auto participant=std::ranges::find(value_.participants,key,&ChatParticipantView::publicKey);if(participant==value_.participants.end()){return;}auto cursor=composer_->textCursor();const auto position=cursor.position();cursor.setPosition(position);cursor.insertText(chatMention(composerText(composer_),position,participant->nickname));composer_->setTextCursor(cursor);composer_->setFocus();});
 participants_->setContextMenuPolicy(Qt::CustomContextMenu);
 connect(participants_,&QListWidget::customContextMenuRequested,this,[this](QPoint point){
  if(participantMenuOpen_||!value_.preferencesReady){return;}auto* item=participants_->itemAt(point);if(!item){return;}const auto publicKey=item->data(Qt::UserRole).toByteArray();const auto participant=std::ranges::find(value_.participants,publicKey,&ChatParticipantView::publicKey);if(participant==value_.participants.end()||participant->local){return;}const auto room=std::ranges::find(value_.rooms,value_.selected,&ChatRoomView::key);if(room==value_.rooms.end()||!room->joined){return;}
  auto* menu=new QMenu(this);menu->setObjectName(QStringLiteral("chat-participant-menu"));const auto key=value_.selected;const auto generation=value_.generation;participantMenuOpen_=true;
  const auto add=[&](QString label,bool pin,bool checked){auto* action=menu->addAction(label);action->setCheckable(true);action->setChecked(checked);connect(action,&QAction::triggered,this,[this,key,generation,publicKey,pin,checked](bool next){if(value_.generation==generation&&value_.selected==key&&std::ranges::find(value_.participants,publicKey,&ChatParticipantView::publicKey)!=value_.participants.end()){emit participantPreferenceRequested(key,publicKey,pin,checked,next);}});};
  add(tr("Ignore"),false,participant->ignored);add(tr("Pin"),true,participant->pinned);connect(menu,&QMenu::aboutToHide,this,[this,menu]{participantMenuOpen_=false;updateParticipants();menu->deleteLater();});menu->popup(participants_->viewport()->mapToGlobal(point));
 });
 setSnapshot({});
}
void ChatPage::setFixedRoom(QString key){fixed_=true;rooms_->parentWidget()->hide();if(fixedKey_==key){return;}fixedKey_=std::move(key);ChatViewSnapshot empty;empty.generation=value_.generation;empty.sequence=value_.sequence;empty.selected=fixedKey_;setSnapshot(std::move(empty));}
QString ChatPage::joinedRoom()const{const auto found=std::ranges::find(value_.rooms,value_.selected,&ChatRoomView::key);return found!=value_.rooms.end()&&found->joined?found->key:QString{};}
void ChatPage::rememberDraft(){if(!value_.selected.isEmpty()){drafts_.insert(value_.selected,composerText(composer_));while(drafts_.size()>256){drafts_.erase(drafts_.begin());}}}
void ChatPage::updateSend(){const auto selected=std::ranges::find_if(value_.rooms,[this](const auto& r){return r.key==value_.selected;});const bool enabled=selected!=value_.rooms.end()&&selected->joined&&selected->canSend&&!selected->readOnly&&!pending_.contains(value_.selected);const auto size=composerText(composer_).size();send_->setEnabled(enabled&&size<=MaximumInputCharacters&&!trimChatInput(composerText(composer_)).isEmpty());send_->setToolTip(size>MaximumInputCharacters?tr("Maximum message length: %1 characters").arg(MaximumInputCharacters):QString{});composer_->setEnabled(enabled);}
void ChatPage::submissionFailed(const QString& key){pending_.remove(key);updateSend();}
void ChatPage::submissionAccepted(const QString& key,const QString& text){const auto raw=pending_.take(key);if(value_.selected==key&&(composerText(composer_)==raw||composerText(composer_)==text)){composer_->clear();}if(drafts_.value(key)==raw||drafts_.value(key)==text){drafts_.remove(key);}updateSend();}
void ChatPage::setSnapshot(ChatViewSnapshot next){
 if(fixed_&&next.selected!=fixedKey_){return;}
 if(next.generation<value_.generation||(next.generation==value_.generation&&next.sequence<value_.sequence)){return;}
 const bool generationChanged=next.generation!=value_.generation;const bool selectionChanged=generationChanged||next.selected!=value_.selected;const bool messagesChanged=selectionChanged||next.messages!=value_.messages;if(generationChanged){drafts_.clear();pending_.clear();}else if(selectionChanged){rememberDraft();}const QSignalBlocker blockRooms(rooms_),blockFavourite(favourite_),blockHistory(history_),blockShared(sharedNickname_);value_=std::move(next);composer_->setProperty("chat-generation",QVariant::fromValue(qulonglong(value_.generation)));composer_->setProperty("chat-room",value_.selected);rooms_->clear();int selected=-1;
 for(qsizetype i=0;i<value_.rooms.size();++i){const auto& r=value_.rooms[i];auto label=(r.favourite?QStringLiteral("★ "):QString{})+r.key;if(r.unread>0){label+=tr(" (%1)").arg(r.unread);}auto* item=new QListWidgetItem(label,rooms_);item->setToolTip(r.key);if(r.key==value_.selected){selected=static_cast<int>(i);}}
 rooms_->setCurrentRow(selected);const auto found=std::ranges::find_if(value_.rooms,[this](const auto& r){return r.key==value_.selected;});const bool has=found!=value_.rooms.end();status_->setText(has&&!value_.availability.isEmpty()?value_.availability+QStringLiteral("\n")+found->status:(has?found->status:value_.availability));leave_->setEnabled(has&&found->joined);favourite_->setEnabled(has);history_->setEnabled(has);favourite_->setChecked(has&&found->favourite);history_->setChecked(has&&found->saveHistory);sharedNickname_->setEnabled(has);nickname_->setEnabled(has);sharedNickname_->setChecked(has&&found->sharedNickname);nickname_->setToolTip(has?found->nickname:QString{});display_->setEnabled(has&&found->joined&&value_.preferencesReady);
 if(messagesChanged){const auto* bar=transcript_->verticalScrollBar();const bool bottom=bar->value()>=bar->maximum()-2;const auto oldPosition=bar->value();transcript_->clear();QTextCursor cursor(transcript_->document());
 for(const auto& message:value_.messages){const auto time=message.time.isValid()?message.time.toLocalTime().toString(QStringLiteral("HH:mm ")):QString{};cursor.insertText(time+message.nickname+(message.action?QStringLiteral(" "):QStringLiteral(": "))+message.text+QStringLiteral("\n"));}
 if(bottom||selectionChanged){transcript_->verticalScrollBar()->setValue(transcript_->verticalScrollBar()->maximum());}else{transcript_->verticalScrollBar()->setValue(oldPosition);}}
 if(!participantMenuOpen_){updateParticipants();}
 if(selectionChanged){composer_->setPlainText(drafts_.value(value_.selected));}updateSend();
}
void ChatPage::updateParticipants(){
 const QSignalBlocker block(participants_);const auto selected=participants_->currentItem()?participants_->currentItem()->data(Qt::UserRole).toByteArray():QByteArray{};const auto scroll=participants_->verticalScrollBar()->value();
 for(qsizetype index=0;index<value_.participants.size();++index){const auto& participant=value_.participants[index];int found=-1;for(int row=int(index);row<participants_->count();++row){if(participants_->item(row)->data(Qt::UserRole).toByteArray()==participant.publicKey){found=row;break;}}QListWidgetItem* item=nullptr;if(found<0){item=new QListWidgetItem;participants_->insertItem(int(index),item);}else if(found==index){item=participants_->item(found);}else{item=participants_->takeItem(found);participants_->insertItem(int(index),item);}item->setData(Qt::UserRole,participant.publicKey);item->setText(participant.nickname);item->setToolTip(QString::fromLatin1(participant.publicKey.toHex()));auto font=item->font();font.setBold(participant.pinned);item->setFont(font);item->setForeground(participant.ignored?palette().brush(QPalette::Disabled,QPalette::Text):palette().brush(QPalette::Text));}
 while(participants_->count()>value_.participants.size()){delete participants_->takeItem(participants_->count()-1);}if(!selected.isEmpty()&&(!participants_->currentItem()||participants_->currentItem()->data(Qt::UserRole).toByteArray()!=selected)){for(int row=0;row<participants_->count();++row){if(participants_->item(row)->data(Qt::UserRole).toByteArray()==selected){participants_->setCurrentItem(participants_->item(row));break;}}}participants_->verticalScrollBar()->setValue(scroll);
}

}
