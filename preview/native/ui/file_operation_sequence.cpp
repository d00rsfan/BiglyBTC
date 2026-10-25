#include "file_operation_sequence.hpp"
#include "file_operation_dialog.hpp"
#include <QDialog>
#include <QHash>
#include <QMessageBox>
#include <QSet>
#include <algorithm>
namespace biglybtc::ui {
namespace {
bool hash(const QString& text) {
  return (text.size()==40||text.size()==64)&&std::ranges::all_of(text,[](QChar c){return (c>=u'0'&&c<=u'9')||(c>=u'a'&&c<=u'f')||(c>=u'A'&&c<=u'F');});
}
QString outcome(FileOperationOutcome value) {
  switch(value){case FileOperationOutcome::Completed:return QObject::tr("Completed");case FileOperationOutcome::Failed:return QObject::tr("Failed");case FileOperationOutcome::Cancelled:return QObject::tr("Cancelled");case FileOperationOutcome::NotAttempted:return QObject::tr("Not attempted");}return {};
}
}
struct FileOperationSequence::Impl {
  FileOperationSequence* q;QPointer<QWidget> owner;QPointer<FileOperationDialog> dialog;QPointer<QMessageBox> summary;
  FileOperationSequenceReport value;QHash<QString,Target> current;QSet<QString> wanted;
  std::optional<FileOperationRequest> request;QString destination;
  quint64 revision=0,waitRevision=0,dialogToken=0,nextToken=0;
  std::size_t index=0;bool started=false,ended=false,busy=false,queued=false,executing=false,stopping=false,silent=false;
  explicit Impl(FileOperationSequence* self,QWidget* parent):q(self),owner(parent){}
  void queue() {if(queued||ended||!started){return;}queued=true;QMetaObject::invokeMethod(q,[this]{queued=false;advance();},Qt::QueuedConnection);}
  void itemDone(quint64 token) {
    if(!token||token!=dialogToken){return;}
    if(executing){stopping=true;value.cancelled=true;return;} // Retain the real in-flight result even after child deletion.
    dialogToken=0;dialog.clear();request.reset();
    auto& item=value.items[index];if(item.outcome==FileOperationOutcome::NotAttempted){item.outcome=FileOperationOutcome::Cancelled;value.cancelled=true;stopping=true;}
    ++index;queue();
  }
  void closeChildLater() {
    const auto token=dialogToken;QPointer<FileOperationSequence> self(q);const auto child=dialog;
    QMetaObject::invokeMethod(q,[self,child,token]{if(self&&child&&self->impl_->dialogToken==token){child->reject();}},Qt::QueuedConnection);
  }
  void finish() {
    if(ended){return;}ended=true;
    const auto result=std::make_shared<const FileOperationSequenceReport>(value);
    QPointer<FileOperationSequence> self(q);const bool show=!silent&&owner;
    emit q->finished(result);if(!self||!show||silent||!owner){return;}
    const auto completed=std::ranges::count_if(value.items,[](const auto& item){return item.outcome==FileOperationOutcome::Completed;});
    const auto failed=std::ranges::count_if(value.items,[](const auto& item){return item.outcome==FileOperationOutcome::Failed||!item.failure.isEmpty();});
    auto* box=new QMessageBox(failed?QMessageBox::Warning:QMessageBox::Information,QObject::tr("File operations"),{},QMessageBox::Ok,owner);
    summary=box;const QPointer<QMessageBox> alive(box);
    box->setAttribute(Qt::WA_DeleteOnClose);if(!self||!alive){return;}
    box->setObjectName(QStringLiteral("file-operation-sequence-result"));if(!self||!alive){return;}
    box->setTextFormat(Qt::PlainText);if(!self||!alive){return;}
    box->setText(QObject::tr("%1 of %2 file operations completed. %3 reported a problem.").arg(static_cast<qlonglong>(completed)).arg(static_cast<qulonglong>(value.items.size())).arg(static_cast<qlonglong>(failed)));if(!self||!alive){return;}
    QStringList lines;for(const auto& item:value.items){lines<<QStringLiteral("%1 [%2]: %3%4").arg(item.name,item.hash,outcome(item.outcome),item.failure.isEmpty()?QString{}:QStringLiteral(" — ")+item.failure);}
    box->setDetailedText(lines.join(u'\n'));if(self&&alive){box->open();}
  }
  void advance() {
    if(ended||!started||dialogToken||busy){return;}
    if(stopping||index==value.items.size()){finish();return;}
    if(waitRevision&&revision<=waitRevision){return;}
    const auto found=current.constFind(value.items[index].hash.toUpper());
    if(found==current.cend()){value.items[index].outcome=FileOperationOutcome::Failed;value.items[index].failure=QObject::tr("Torrent is no longer in the current library.");stopping=true;finish();return;}
    const auto target=*found;waitRevision=0;executing=false;request.reset();
    auto* child=new FileOperationDialog(owner);child->setAttribute(Qt::WA_DeleteOnClose);dialog=child;dialogToken=++nextToken;
    const auto token=dialogToken;QPointer<FileOperationSequence> self(q);const QPointer<FileOperationDialog> expected(child);
    QObject::connect(child,&FileOperationDialog::planRequested,q,[this,token](FileOperationRequest input){
      if(token!=dialogToken||ended||stopping||input.session!=value.session){return;}request=input;executing=false;emit q->planRequested(std::move(input));
    });
    QObject::connect(child,&FileOperationDialog::executeRequested,q,[this,token](FileOperationSubmission input){
      if(token!=dialogToken||ended||stopping||!input.plan||!request||input.plan->request!=*request){return;}executing=true;emit q->executeRequested(std::move(input));
    });
    QObject::connect(child,&FileOperationDialog::cancelRequested,q,[this,token](FileOperationRequest input){
      if(token!=dialogToken||ended||!request||input!=*request){return;}stopping=true;value.cancelled=true;emit q->cancelRequested(std::move(input));
    });
    QObject::connect(child,&QDialog::finished,q,[this,token](int){itemDone(token);});
    QObject::connect(child,&QObject::destroyed,q,[this,token]{
      if(token!=dialogToken){return;}dialog.clear();if(executing){stopping=true;value.cancelled=true;return;}itemDone(token);
    });
    const bool began=child->beginOperation(value.session,target.generation,target,value.operation,destination);
    if(!self||!expected||ended||dialogToken!=token){return;}
    if(!began){value.items[index].outcome=FileOperationOutcome::Failed;value.items[index].failure=QObject::tr("The selected torrent changed before its paths could be reviewed.");stopping=true;closeChildLater();return;}
    child->setBusy(busy);if(!self||!expected){return;}child->show();if(!self||!expected){return;}
    child->setWindowTitle(QObject::tr("%1 — torrent %2 of %3 — %4").arg(child->windowTitle()).arg(static_cast<qulonglong>(index+1)).arg(static_cast<qulonglong>(value.items.size())).arg(value.items[index].name));
  }
};
FileOperationSequence::FileOperationSequence(QWidget* owner):QObject(owner),impl_(std::make_unique<Impl>(this,owner)){qRegisterMetaType<FileOperationSequencePresentation>();}
FileOperationSequence::~FileOperationSequence()=default;
bool FileOperationSequence::begin(quint64 session,FileOperation operation,DownloadTargets targets,QStringList names) {
  if(impl_->started||!session||targets.size()<2||targets.size()>MaximumFileOperationSequence||names.size()!=static_cast<qsizetype>(targets.size())||static_cast<unsigned>(operation)>3){return false;}
  const auto revision=targets.front().generation;if(!revision){return false;}
  QSet<QString> wanted;QHash<QString,Target> current;std::vector<FileOperationItemResult> items;
  for(std::size_t n=0;n<targets.size();++n){const auto& target=targets[n];if(!hash(target.hash)||target.generation!=revision||wanted.contains(target.hash.toUpper())){return false;}wanted.insert(target.hash.toUpper());current.insert(target.hash.toUpper(),target);items.push_back({target.hash,names[static_cast<qsizetype>(n)].left(256),FileOperationOutcome::NotAttempted,{}});}
  impl_->wanted=std::move(wanted);impl_->current=std::move(current);impl_->value.items=std::move(items);
  impl_->value.session=session;impl_->value.operation=operation;impl_->revision=revision;impl_->started=true;impl_->queue();return true;
}
void FileOperationSequence::setPresentation(const Presentation& packet) {
  if(!active()||!packet||packet->revision<=impl_->revision||packet->items.size()>LibraryModel::MaximumRows){return;}
  QHash<QString,Target> next;QSet<QString> seen;for(const auto& item:packet->items){if(impl_->wanted.contains(item.hash.toUpper())){if(seen.contains(item.hash.toUpper())){return;}seen.insert(item.hash.toUpper());next.insert(item.hash.toUpper(),{item.hash,packet->revision});}}
  impl_->revision=packet->revision;impl_->current=std::move(next);impl_->queue();
}
void FileOperationSequence::setBusy(bool value) {
  const bool was=impl_->busy;impl_->busy=value;QPointer<FileOperationSequence> self(this);const auto child=impl_->dialog;
  if(child){child->setBusy(value);}if(!self){return;}
  if(!value&&was&&impl_->request&&(impl_->executing||(child&&child->pending()))){
    if(impl_->executing){auto failure=std::make_shared<FileOperationResultPacket>();failure->request=*impl_->request;failure->failure=tr("The worker finished without a file-operation result. Refresh before retrying.");setResult(failure);}
    else{auto failure=std::make_shared<FileOperationPlanPacket>();failure->request=*impl_->request;failure->failure=tr("The path review did not finish.");setPlan(failure);}
  }
  if(self){impl_->queue();}
}
bool FileOperationSequence::setPlan(FileOperationPlanPresentation packet) {
  if(!active()||!packet||!impl_->request||packet->request!=*impl_->request||impl_->executing||!impl_->dialog){return false;}
  QPointer<FileOperationSequence> self(this);const bool accepted=impl_->dialog->setPlan(packet);if(!self){return accepted;}
  if(!active()||!impl_->request||packet->request!=*impl_->request){return accepted;}
  if(!packet->failure.isEmpty()){auto& item=impl_->value.items[impl_->index];item.outcome=FileOperationOutcome::Failed;item.failure=packet->failure.left(2048);impl_->stopping=true;impl_->closeChildLater();}
  return accepted;
}
bool FileOperationSequence::setResult(FileOperationResultPresentation packet) {
  if(!active()||!packet||!impl_->executing||!impl_->request||packet->request!=*impl_->request||(packet->completed&&packet->cancelled)||(!packet->completed&&!packet->cancelled&&packet->failure.isEmpty())){return false;}
  auto& item=impl_->value.items[impl_->index];item.outcome=packet->completed?FileOperationOutcome::Completed:packet->cancelled?FileOperationOutcome::Cancelled:FileOperationOutcome::Failed;item.failure=packet->failure.left(2048);
  impl_->executing=false;impl_->waitRevision=packet->request.target.generation;impl_->destination=packet->request.destinationParent;
  impl_->stopping=impl_->stopping||!packet->completed||!packet->failure.isEmpty();impl_->value.cancelled=impl_->value.cancelled||packet->cancelled;
  QPointer<FileOperationSequence> self(this);const auto child=impl_->dialog;
  if(!child){impl_->itemDone(impl_->dialogToken);return true;}
  const bool accepted=child->setResult(packet);
  if(self&&active()&&(!packet->completed||!accepted)){impl_->stopping=true;impl_->closeChildLater();}return accepted;
}
void FileOperationSequence::invalidateSession() {
  impl_->silent=true;impl_->ended=true;impl_->dialogToken=0;impl_->request.reset();const auto child=impl_->dialog;impl_->dialog.clear();const auto summary=impl_->summary;impl_->summary.clear();QPointer<FileOperationSequence> self(this);
  if(child){child->invalidateSession();}if(self&&summary){summary->close();}
}
bool FileOperationSequence::closeForOwner() {
  impl_->silent=true;impl_->stopping=true;impl_->value.cancelled=true;const bool pending=impl_->executing;const auto child=impl_->dialog;
  if(child){child->reject();}return !pending;
}
FileOperationDialog* FileOperationSequence::dialog()const noexcept{return impl_->dialog;}
bool FileOperationSequence::active()const noexcept{return impl_->started&&!impl_->ended;}
FileOperationSequencePresentation FileOperationSequence::report()const{return std::make_shared<const FileOperationSequenceReport>(impl_->value);}
}
