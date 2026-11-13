// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat_transcript.hpp"
#include "chat_page.hpp"
#include "../services/msgsync/transcript_policy.hpp"
#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QKeyEvent>
#include <QMenu>
#include <QPointer>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QScopeGuard>
#include <QTextBrowser>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>
#include <map>
namespace biglybtc::ui {
namespace {
namespace policy=services::msgsync;
QString text(const QTextDocument* document){auto value=document->toRawText();value.replace(QChar::ParagraphSeparator,QLatin1Char('\n'));return value;}
class TranscriptKeys final:public QObject {
 QPointer<QLineEdit> search_;QPointer<QTextBrowser> browser_;
 bool eventFilter(QObject* object,QEvent* event)override{
  if(event->type()!=QEvent::ShortcutOverride&&event->type()!=QEvent::KeyPress){return false;}const auto* key=static_cast<QKeyEvent*>(event);const bool find=key->matches(QKeySequence::Find);const auto* widget=qobject_cast<QWidget*>(object);const bool copy=browser_&&widget&&(widget==browser_||browser_->isAncestorOf(widget))&&key->matches(QKeySequence::Copy);if(!find&&!copy){return false;}event->accept();if(event->type()==QEvent::KeyPress){if(find&&search_){search_->setFocus();}else if(copy){auto value=browser_->textCursor().selectedText();value.replace(QChar::ParagraphSeparator,QLatin1Char('\n'));if(!value.isEmpty()){QApplication::clipboard()->setText(value);}}}return true;
 }
public:
 TranscriptKeys(QLineEdit* search,QTextBrowser* browser,QObject* parent):QObject(parent),search_(search),browser_(browser){}
 void bind(QWidget* scope){scope->installEventFilter(this);for(auto* child:scope->findChildren<QWidget*>()){child->installEventFilter(this);}}
};
class SearchEdit final:public QLineEdit {
 void keyPressEvent(QKeyEvent* event)override{if(event->key()==Qt::Key_Escape){clear();event->accept();return;}QLineEdit::keyPressEvent(event);}
public:using QLineEdit::QLineEdit;
};
}
struct ChatTranscript::Impl {
 struct Range {QByteArray id;int start=0,end=0;};
 struct Anchor {QByteArray id;int offset=0;bool end=false;};
 struct State {QString input;policy::TranscriptSearch model;Anchor anchor,position;int scroll=0;bool view=false,bottom=true;};
 struct Link {QString target;std::uint64_t generation=0;QString room;};
 ChatTranscript* owner;QTextBrowser* browser;SearchEdit* search;TranscriptKeys* keys;
 std::uint64_t generation=0,nextLink=0;QString room,ownNickname;QVector<ChatMessageView> messages;QVector<Range> ranges;
 std::map<QString,State> states;std::map<QString,Link> links;bool changing=false;
 Impl(ChatTranscript* owner,QWidget* scope):owner(owner),browser(new QTextBrowser(owner)),search(new SearchEdit(owner)){
  owner->setObjectName(QStringLiteral("chat-transcript-container"));browser->setObjectName(QStringLiteral("chat-transcript"));search->setObjectName(QStringLiteral("chat-search"));
  browser->setOpenLinks(false);browser->setOpenExternalLinks(false);browser->setMinimumHeight(0);search->setPlaceholderText(ChatTranscript::tr("Search…"));search->setClearButtonEnabled(true);search->setMaximumWidth(240);search->setToolTip(ChatTranscript::tr("Enter: next match; Esc: clear"));
  auto* layout=new QVBoxLayout(owner);layout->setContentsMargins(0,0,0,0);layout->addWidget(search,0,Qt::AlignRight);layout->addWidget(browser,1);
  (void)scope;keys=new TranscriptKeys(search,browser,owner);
  QObject::connect(search,&QLineEdit::textChanged,owner,[this](const QString& value){if(changing){return;}auto& state=states[room];state.input=value;select(state.model.apply(text(browser->document()),policy::composer::trimChatInput(value)));});
  QObject::connect(search,&QLineEdit::returnPressed,owner,[this]{select(states[room].model.apply(text(browser->document()),QStringLiteral("\n")));});
  QObject::connect(browser,&QTextBrowser::anchorClicked,owner,[this](const QUrl& url){const auto found=links.find(url.toString());if(found==links.end()||found->second.generation!=generation||found->second.room!=room){return;}const auto target=found->second.target;if(auto key=policy::publicTranscriptRoom(target)){emit this->owner->publicRoomRequested(*key);return;}if(policy::externalTranscriptLink(target)){emit this->owner->externalLinkRequested(QUrl(target,QUrl::StrictMode));}});
  browser->setContextMenuPolicy(Qt::CustomContextMenu);QObject::connect(browser,&QTextBrowser::customContextMenuRequested,owner,[this](QPoint point){menu(point);});

 }

 QString selection()const{auto value=browser->textCursor().selectedText();value.replace(QChar::ParagraphSeparator,QLatin1Char('\n'));return value;}
 void select(policy::TranscriptSelection selected){auto cursor=browser->textCursor();const auto length=browser->document()->characterCount()-1;cursor.setPosition(int(std::clamp<qsizetype>(selected.start,0,length)));cursor.setPosition(int(std::clamp<qsizetype>(selected.end,0,length)),QTextCursor::KeepAnchor);browser->setTextCursor(cursor);browser->ensureCursorVisible();}
 void menu(QPoint point){
  auto* popup=new QMenu(owner);popup->setObjectName(QStringLiteral("chat-transcript-menu"));const auto selected=selection();auto* copy=popup->addAction(ChatTranscript::tr("Copy"));copy->setEnabled(!selected.isEmpty());QObject::connect(copy,&QAction::triggered,popup,[selected]{QApplication::clipboard()->setText(selected);});
  const auto anchor=browser->anchorAt(point);const auto found=links.find(anchor);if(found!=links.end()){const auto target=found->second.target;auto* address=popup->addAction(ChatTranscript::tr("Copy link address"));QObject::connect(address,&QAction::triggered,popup,[target]{QApplication::clipboard()->setText(target);});}
  auto* all=popup->addAction(ChatTranscript::tr("Select all"));QObject::connect(all,&QAction::triggered,browser,&QTextBrowser::selectAll);QObject::connect(popup,&QMenu::aboutToHide,popup,&QObject::deleteLater);popup->popup(browser->viewport()->mapToGlobal(point));
 }
 Anchor capture(int position,bool tail=true)const{
  if(tail&&position==browser->document()->characterCount()-1){return {{},0,true};}
  for(const auto& range:ranges){if(position>=range.start&&position<=range.end){return {range.id,position-range.start,false};}}return {};
 }
 int restore(const Anchor& anchor)const{
  const auto length=browser->document()->characterCount()-1;if(anchor.end){return length;}for(const auto& range:ranges){if(range.id==anchor.id){return range.start+std::clamp(anchor.offset,0,range.end-range.start);}}return length;
 }
 void saveView(){auto& state=states[room];const auto cursor=browser->textCursor();state.anchor=capture(cursor.anchor(),!cursor.hasSelection());state.position=capture(cursor.position(),!cursor.hasSelection());state.scroll=browser->verticalScrollBar()->value();state.bottom=state.scroll>=browser->verticalScrollBar()->maximum()-2;state.view=true;}
 void render(const ChatMessageView& message,QTextCursor& cursor){
  const auto start=cursor.position();QTextCharFormat prefix;prefix.setFontWeight(QFont::DemiBold);prefix.setToolTip(QString::fromLatin1(message.publicKey.toHex()));const auto time=message.time.isValid()?message.time.toLocalTime().toString(QStringLiteral("HH:mm ")):QString{};cursor.insertText(time+message.nickname+(message.action?QStringLiteral(" "):QStringLiteral(": ")),prefix);
  for(const auto& span:policy::highlightTranscriptNickname(policy::transcriptSpans(message.text,message.origin),ownNickname)){
   QTextCharFormat format;format.setFontWeight((span.style==policy::TranscriptStyle::Bold||span.style==policy::TranscriptStyle::Nickname)?QFont::Bold:QFont::Normal);format.setFontItalic(span.style==policy::TranscriptStyle::Italic||(message.local&&message.action&&span.style==policy::TranscriptStyle::Plain));
   if(span.style==policy::TranscriptStyle::Link&&nextLink<std::numeric_limits<std::uint64_t>::max()){const auto handle=QStringLiteral("biglybtc-chat-action:")+QString::number(++nextLink);links.emplace(handle,Link{span.target,generation,room});format.setAnchor(true);format.setAnchorHref(handle);format.setForeground(owner->palette().brush(QPalette::Link));format.setFontUnderline(true);format.setToolTip(span.target);}
   cursor.insertText(span.text,format);
  }
  cursor.insertText(QStringLiteral("\n"),QTextCharFormat{});ranges.push_back({message.id,start,cursor.position()});
 }
 void set(std::uint64_t nextGeneration,QString nextRoom,std::span<const ChatMessageView> incoming,QString nextNickname){
  const bool rebound=generation!=nextGeneration||room!=nextRoom;const bool nicknameChanged=ownNickname!=nextNickname;if(!rebound&&!nicknameChanged&&std::equal(messages.begin(),messages.end(),incoming.begin(),incoming.end())){return;}
  if(!rebound){saveView();}else{saveView();if(generation!=nextGeneration){states.clear();}generation=nextGeneration;room=std::move(nextRoom);if(!states.contains(room)&&states.size()>=32){states.erase(states.begin());}}
  ownNickname=std::move(nextNickname);auto& state=states[room];{const QSignalBlocker block(search);search->setText(state.input);}
  const bool append=!rebound&&!nicknameChanged&&incoming.size()>=std::size_t(messages.size())&&std::equal(messages.begin(),messages.end(),incoming.begin());const auto first=append?std::size_t(messages.size()):0;changing=true;browser->setUpdatesEnabled(false);const auto done=qScopeGuard([this]{changing=false;browser->setUpdatesEnabled(true);});const QSignalBlocker block(browser->document()),blockBrowser(browser);
  if(!append){browser->clear();ranges.clear();links.clear();}QTextCursor cursor(browser->document());cursor.movePosition(QTextCursor::End);for(std::size_t index=first;index<incoming.size();++index){render(incoming[index],cursor);}messages=QVector<ChatMessageView>(incoming.begin(),incoming.end());
  if(state.view){auto selected=browser->textCursor();const auto present=[&](const Anchor& anchor){return anchor.end||std::ranges::any_of(ranges,[&](const auto& range){return range.id==anchor.id;});};selected.setPosition(restore(state.anchor));if(present(state.anchor)&&present(state.position)){selected.setPosition(restore(state.position),QTextCursor::KeepAnchor);}browser->setTextCursor(selected);browser->verticalScrollBar()->setValue(state.bottom?browser->verticalScrollBar()->maximum():state.scroll);}else{auto selected=browser->textCursor();selected.movePosition(QTextCursor::End);browser->setTextCursor(selected);browser->verticalScrollBar()->setValue(browser->verticalScrollBar()->maximum());}
 }
};
ChatTranscript::ChatTranscript(QWidget* scope,QWidget* parent):QWidget(parent),impl_(std::make_unique<Impl>(this,scope)){}
ChatTranscript::~ChatTranscript()=default;
void ChatTranscript::bindShortcuts(QWidget* scope){impl_->keys->bind(scope);}
void ChatTranscript::setMessages(std::uint64_t generation,QString room,std::span<const ChatMessageView> messages,QString ownNickname){impl_->set(generation,std::move(room),messages,std::move(ownNickname));}
}
