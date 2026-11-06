// SPDX-License-Identifier: GPL-2.0-or-later
#include "category_editor.hpp"
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
namespace biglybtc::ui {
std::shared_ptr<const core::SavedCategories> categoryEditorValues(CategoryCommandPresentation packet){
  try{
    if(!packet||packet->recovery.name.toUtf8().size()>4096||packet->recovery.reason.size()>1024
        ||packet->recovery.totalMembers>128||packet->recovery.acknowledgedMembers>packet->recovery.totalMembers
        ||(packet->recovery.state!=CategoryRecoveryInfo::State::None&&packet->recovery.state!=CategoryRecoveryInfo::State::Review
            &&packet->recovery.state!=CategoryRecoveryInfo::State::Resumable)
        ||(packet->recovery.state==CategoryRecoveryInfo::State::Resumable&&!packet->recoveryRequired)
        ||!packet->generation||!packet->categories||!packet->categories->source
        ||packet->failure.size()>8192||packet->refreshFailure.size()>8192
        ||(packet->readState!=CategoryReadState::Ready&&packet->readState!=CategoryReadState::Missing)){return {};}
    if(packet->status!=CategoryCommandStatus::Loaded&&packet->status!=CategoryCommandStatus::Saved&&packet->status!=CategoryCommandStatus::Unchanged){return {};}
    if(!packet->failure.isEmpty()){return {};}
    const auto& source=*packet->categories->source;
    if(source.root().changed||(packet->readState==CategoryReadState::Missing&&source.originalBytes()!="de")){return {};}
    auto fresh=core::readSavedCategories(source.originalBytes());if(!fresh){return {};}
    return std::make_shared<const core::SavedCategories>(std::move(*fresh));
  }catch(...){return {};}
}
CategoryManagerDialog::CategoryManagerDialog(quint64 session,QWidget* parent):QDialog(parent),session_(session){
  setObjectName(QStringLiteral("category-manager"));setWindowTitle(tr("Categories"));resize(500,360);
  auto* layout=new QVBoxLayout(this);layout->addWidget(new QLabel(tr("Saved download categories"),this));
  table_=new QTableWidget(0,2,this);table_->setObjectName(QStringLiteral("category-table"));table_->setHorizontalHeaderLabels({tr("Name"),tr("Type")});table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setSelectionMode(QAbstractItemView::SingleSelection);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);layout->addWidget(table_);
  name_=new QLineEdit(this);name_->setObjectName(QStringLiteral("category-name"));name_->setMaxLength(4096);name_->setPlaceholderText(tr("New category name"));layout->addWidget(name_);
  auto* row=new QHBoxLayout;create_=new QPushButton(tr("New"),this);remove_=new QPushButton(tr("Delete…"),this);reload_=new QPushButton(tr("Reload"),this);create_->setObjectName(QStringLiteral("category-new"));remove_->setObjectName(QStringLiteral("category-delete"));reload_->setObjectName(QStringLiteral("category-reload"));resume_=new QPushButton(tr("Resume deletion…"),this);resume_->setObjectName(QStringLiteral("category-resume-delete"));row->addWidget(create_);row->addWidget(remove_);row->addWidget(resume_);row->addStretch();row->addWidget(reload_);layout->addLayout(row);
  status_=new QLabel(tr("Load the saved categories before editing."),this);status_->setObjectName(QStringLiteral("category-status"));status_->setWordWrap(true);layout->addWidget(status_);
  auto* close=new QDialogButtonBox(QDialogButtonBox::Close,this);connect(close,&QDialogButtonBox::rejected,this,&QDialog::reject);layout->addWidget(close);
  connect(table_,&QTableWidget::itemSelectionChanged,this,[this]{updateButtons();});connect(name_,&QLineEdit::textChanged,this,[this]{updateButtons();});connect(create_,&QPushButton::clicked,this,&CategoryManagerDialog::create);connect(remove_,&QPushButton::clicked,this,&CategoryManagerDialog::remove);connect(resume_,&QPushButton::clicked,this,&CategoryManagerDialog::resumeRemove);connect(reload_,&QPushButton::clicked,this,[this]{CategoryCommand command;command.kind=CategoryCommandKind::Reload;intent(std::move(command));});updateButtons();
}
const core::SavedCategory* CategoryManagerDialog::selected()const{const auto row=table_?table_->currentRow():-1;return categories_&&row>=0&&static_cast<std::size_t>(row)<categories_->categories.size()?&categories_->categories[static_cast<std::size_t>(row)]:nullptr;}
void CategoryManagerDialog::updateButtons(){
  const bool idle=available_&&session_&&!pending_;const auto* value=selected();const QPointer<CategoryManagerDialog> guard(this);
  create_->setEnabled(idle&&writable_&&generation_&&name_->text().toUtf8().size()<=4096);if(!guard){return;}
  remove_->setEnabled(idle&&writable_&&generation_&&deletionSupported_&&value&&value->kind==core::SavedCategoryKind::User);if(!guard){return;}reload_->setEnabled(idle);if(!guard){return;}
  resume_->setVisible(recovery_.state!=CategoryRecoveryInfo::State::None);
  if(!guard){return;}
  resume_->setEnabled(idle&&generation_&&deletionSupported_&&recovery_.state==CategoryRecoveryInfo::State::Resumable);
}
void CategoryManagerDialog::setPresentation(CategoryCommandPresentation packet){
  if(!session_||!packet||packet->session!=session_||packet->request<lastRequest_||packet->generation<generation_){return;}
  auto values=categoryEditorValues(packet);if(!values){return;}const auto previous=selected()?std::optional{selected()->name}:std::nullopt;
  generation_=packet->generation;lastRequest_=packet->request;writable_=packet->writable&&!packet->recoveryRequired;deletionSupported_=packet->deletionSupported;recovery_=packet->recovery;categories_=std::move(values);
  const QSignalBlocker blocked(table_);table_->setRowCount(static_cast<int>(categories_->categories.size()));int select=-1;
  for(std::size_t i=0;i<categories_->categories.size();++i){const auto& value=categories_->categories[i];
    const auto label=value.kind==core::SavedCategoryKind::All?tr("All"):value.kind==core::SavedCategoryKind::Uncategorized?tr("Uncategorized"):value.name.empty()?tr("(empty name)"):QString::fromStdString(value.name);
    table_->setItem(static_cast<int>(i),0,new QTableWidgetItem(label));table_->setItem(static_cast<int>(i),1,new QTableWidgetItem(value.kind==core::SavedCategoryKind::User?tr("User"):tr("Built in (read only)")));
    if(previous&&value.name==*previous){select=static_cast<int>(i);}}
  if(select<0&&!categories_->categories.empty()){select=0;}if(select>=0){table_->setCurrentCell(select,0);table_->selectRow(select);}
  if(recovery_.state==CategoryRecoveryInfo::State::Resumable){
    status_->setText(tr("Deletion of “%1”: %2 of %3 member writes acknowledged. %4 Resume revalidates every guarded file; it cannot override a conflict.")
        .arg(recovery_.name).arg(recovery_.acknowledgedMembers).arg(recovery_.totalMembers)
        .arg(recovery_.definitionAcknowledged?tr("Definition removal acknowledged."):tr("Definition retained until all members are cleared.")));
  }else if(recovery_.state==CategoryRecoveryInfo::State::Review){
    status_->setText(tr("Deletion requires review; all files are preserved. %1").arg(recovery_.reason.left(512)));
  }else{status_->setText(packet->restartRequired?tr("Recovery session: close and reopen the profile after resolution before starting transfers."):packet->recoveryRequired?tr("Read only: interrupted deletion or recovery requires explicit resolution."):!writable_?tr("Read only. Reload after the profile or recovery state is resolved."):!deletionSupported_?tr("Loaded. Deletion is unavailable in this backend."):tr("Saved categories loaded."));}updateButtons();
}
void CategoryManagerDialog::setPending(const CategoryCommand& request){if(request.session!=session_||!request.request||pending_){return;}pending_=request;status_->setText(tr("Waiting for the profile worker…"));updateButtons();}
void CategoryManagerDialog::setResult(CategoryCommandPresentation packet){
  if(!packet||!pending_||packet->session!=pending_->session||packet->request!=pending_->request||packet->kind!=pending_->kind||packet->requestedTarget!=pending_->target){return;}
  pending_.reset();const QPointer<CategoryManagerDialog> guard(this);
  if(packet->status==CategoryCommandStatus::Loaded||packet->status==CategoryCommandStatus::Saved||packet->status==CategoryCommandStatus::Unchanged){
    if(packet->refreshFailure.isEmpty()){setPresentation(packet);if(!guard){return;}}
    else{writable_=false;recovery_.state=CategoryRecoveryInfo::State::Review;status_->setText(tr("Saved; refresh failed: %1").arg(packet->refreshFailure.left(512)));}
  }else{writable_=false;recovery_.state=CategoryRecoveryInfo::State::Review;status_->setText(packet->status==CategoryCommandStatus::Cancelled?tr("Cancelled. Reload before retrying."):tr("Rejected: %1").arg(packet->failure.left(512)));}
  updateButtons();
}
void CategoryManagerDialog::setAvailable(bool value){available_=value;updateButtons();}
void CategoryManagerDialog::showFailure(const QString& value){pending_.reset();writable_=false;recovery_.state=CategoryRecoveryInfo::State::Review;status_->setText(value.left(512));updateButtons();}
void CategoryManagerDialog::invalidateSession(){session_=0;generation_=0;pending_.reset();writable_=false;available_=false;const QPointer<CategoryManagerDialog> guard(this);updateButtons();if(guard){reject();}}
void CategoryManagerDialog::intent(CategoryCommand request){if(!available_||!session_||pending_||(request.kind!=CategoryCommandKind::Reload&&request.kind!=CategoryCommandKind::ResumeRemove&&!writable_)){return;}request.session=session_;request.generation=generation_;emit commandRequested(std::move(request));}
void CategoryManagerDialog::create(){if(!create_->isEnabled()){return;}CategoryCommand request;request.kind=CategoryCommandKind::Create;request.name=name_->text();intent(std::move(request));}
void CategoryManagerDialog::remove(){
  const auto* value=selected();if(!remove_->isEnabled()||!value||value->kind!=core::SavedCategoryKind::User){return;}
  const auto name=value->name;const auto generation=generation_;const QPointer<CategoryManagerDialog> guard(this);
  const auto answer=QMessageBox::question(this,tr("Delete category"),tr("Remove “%1” and clear its saved download assignments?").arg(QString::fromStdString(name)),QMessageBox::Yes|QMessageBox::No,QMessageBox::No);
  if(!guard||answer!=QMessageBox::Yes||generation_!=generation||!selected()||selected()->name!=name||!remove_->isEnabled()){return;}
  CategoryCommand request;request.kind=CategoryCommandKind::Remove;request.name=QString::fromStdString(name);intent(std::move(request));
}
void CategoryManagerDialog::resumeRemove(){
  if(!resume_->isEnabled()||recovery_.state!=CategoryRecoveryInfo::State::Resumable){return;}
  const auto name=recovery_.name;const auto generation=generation_;
  const QPointer<CategoryManagerDialog> guard(this);
  const auto answer=QMessageBox::question(this,tr("Resume category deletion"),
      tr("Continue the acknowledged deletion of “%1”? Every original file will be checked again. Conflicting or unacknowledged changes will be preserved for review.").arg(name),
      QMessageBox::Yes|QMessageBox::No,QMessageBox::No);
  if(!guard||answer!=QMessageBox::Yes||generation_!=generation||recovery_.name!=name||!resume_->isEnabled()){return;}
  CategoryCommand command;command.kind=CategoryCommandKind::ResumeRemove;command.name=name;intent(std::move(command));
}
} // namespace biglybtc::ui
