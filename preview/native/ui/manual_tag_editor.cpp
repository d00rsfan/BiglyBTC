// SPDX-License-Identifier: GPL-2.0-or-later
#include "manual_tag_editor.hpp"
#include "../app/manual_tag_policy.hpp"
#include <QCheckBox>
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
namespace {
QString originalTrim(const QString& value){qsizetype first=0,last=value.size();while(first<last&&value[first].unicode()<=0x20){++first;}while(last>first&&value[last-1].unicode()<=0x20){--last;}return value.mid(first,last-first);}
}
std::optional<ManualTagEditorValues> manualTagEditorValues(ManualTagCommandPresentation packet) {
  try {
    if(!packet||!packet->generation||!packet->tags||!packet->tags->source||packet->failure.size()>8192
        ||(packet->readState!=ManualTagReadState::Ready&&packet->readState!=ManualTagReadState::Missing)){return {};}
    if(packet->status!=ManualTagCommandStatus::Loaded&&packet->status!=ManualTagCommandStatus::Saved&&packet->status!=ManualTagCommandStatus::Unchanged){return {};}
    const auto& document=*packet->tags->source;
    if(document.originalBytes().size()>core::Limits{}.inputBytes||document.root().changed){return {};}
    auto parsed=core::readSavedDownloadTags(document.originalBytes());if(!parsed){return {};}
    if(packet->readState==ManualTagReadState::Missing&&document.originalBytes()!="de"){return {};}
    ManualTagEditorValues out;out.tags=std::make_shared<const core::SavedDownloadTags>(std::move(*parsed));out.writable=packet->writable;
    for(const auto& issue:out.tags->issues){if(issue.code==core::SavedTagIssueCode::InvalidId||issue.code==core::SavedTagIssueCode::DuplicateId){out.writable=false;}}
    std::vector<bool> rowIssues(out.tags->tags.size(),false);for(const auto& issue:out.tags->issues){if(issue.tagIndex<rowIssues.size()){rowIssues[issue.tagIndex]=true;}}
    out.rows.reserve(out.tags->tags.size());
    for(std::size_t index=0;index<out.tags->tags.size();++index){
      const auto& tag=out.tags->tags[index];ManualTagEditorRow row;row.id=tag.id;row.visible=tag.visible;row.members=tag.members.size();
      bool issues=rowIssues[index];
      const auto* type=out.tags->source->find("3");
      const auto* record=type&&tag.sourceIndex<type->children.size()/2?&type->children[2*tag.sourceIndex+1]:nullptr;
      const auto* config=record?record->find("c"):nullptr;const auto* properties=record?record->find("p"):nullptr;
      if(properties){
        if(properties->type!=core::Node::Type::List||properties->children.size()!=tag.members.size()){issues=true;}
        else{for(const auto& value:properties->children){const auto* time=value.find("a");if(value.type!=core::Node::Type::Dictionary||(time&&time->type!=core::Node::Type::Integer)){issues=true;}}}
      }
      if(config){const auto* original=config->find("oname");if(original&&original->type!=core::Node::Type::Bytes){issues=true;}}
      const auto policy=record?app::classifyManualTagMembership(*record):app::ManualTagAutomation{false,false,false,true};
      issues=issues||policy.malformed;
      bool validName=false;
      if(tag.rawName){
        row.name=QString::fromUtf8(tag.rawName->data(),static_cast<qsizetype>(tag.rawName->size()));
        const auto roundtrip=row.name.toUtf8();validName=std::string_view(roundtrip.constData(),static_cast<std::size_t>(roundtrip.size()))==*tag.rawName;
        validName=validName&&!row.name.contains(QChar::Null)&&!row.name.contains(QLatin1Char('\n'))&&!row.name.contains(QLatin1Char('\r'));
      }
      row.label=!tag.rawName?QObject::tr("(saved name missing)"):!validName?QObject::tr("(invalid saved name)"):row.name.isEmpty()?QObject::tr("(empty name)"):row.name;
      if(validName&&tag.nameMayRequireLocalization){row.label+=QObject::tr(" [localization key]");row.detail=QObject::tr("Saved localization key: %1").arg(row.name);}
      row.editable=out.writable&&row.id&&tag.idKey==std::to_string(*row.id)&&validName&&!issues;
      row.canAddMember=row.editable&&policy.addAllowed();row.canRemoveMember=row.editable&&policy.removeAllowed();
      if(row.editable&&(policy.autoAdd||policy.autoRemove)){row.detail=QObject::tr("Automatic tag settings control one or both membership directions.");}
      if(!row.editable){row.detail=QObject::tr("Read only: saved fields are unknown, malformed, or require recovery.");}
      out.rows.push_back(std::move(row));
    }
    return out;
  }catch(...){return {};}
}
ManualTagManagerDialog::ManualTagManagerDialog(quint64 session,QWidget* parent):QDialog(parent),session_(session){
  setObjectName(QStringLiteral("manual-tag-manager"));setWindowTitle(tr("Tags"));resize(620,430);
  auto* layout=new QVBoxLayout(this);auto* title=new QLabel(tr("Manual download tags"),this);layout->addWidget(title);
  table_=new QTableWidget(0,3,this);table_->setObjectName(QStringLiteral("manual-tag-table"));table_->setHorizontalHeaderLabels({tr("Name"),tr("Visible"),tr("Saved members")});table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setSelectionMode(QAbstractItemView::SingleSelection);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);layout->addWidget(table_);
  name_=new QLineEdit(this);name_->setObjectName(QStringLiteral("manual-tag-name"));name_->setPlaceholderText(tr("Tag name"));name_->setMaxLength(4096);layout->addWidget(name_);
  auto* commands=new QHBoxLayout;create_=new QPushButton(tr("New"),this);rename_=new QPushButton(tr("Rename"),this);remove_=new QPushButton(tr("Delete…"),this);visible_=new QCheckBox(tr("Visible"),this);reload_=new QPushButton(tr("Reload"),this);
  create_->setObjectName(QStringLiteral("manual-tag-new"));rename_->setObjectName(QStringLiteral("manual-tag-rename"));remove_->setObjectName(QStringLiteral("manual-tag-delete"));visible_->setObjectName(QStringLiteral("manual-tag-visible"));reload_->setObjectName(QStringLiteral("manual-tag-reload"));
  commands->addWidget(create_);commands->addWidget(rename_);commands->addWidget(remove_);commands->addWidget(visible_);commands->addStretch();commands->addWidget(reload_);layout->addLayout(commands);
  status_=new QLabel(tr("Load the saved tag list to edit tags."),this);status_->setObjectName(QStringLiteral("manual-tag-status"));status_->setWordWrap(true);layout->addWidget(status_);
  auto* close=new QDialogButtonBox(QDialogButtonBox::Close,this);connect(close,&QDialogButtonBox::rejected,this,&QDialog::reject);layout->addWidget(close);
  connect(table_,&QTableWidget::itemSelectionChanged,this,&ManualTagManagerDialog::selectRow);connect(name_,&QLineEdit::textChanged,this,[this]{updateButtons();});
  connect(create_,&QPushButton::clicked,this,&ManualTagManagerDialog::create);connect(rename_,&QPushButton::clicked,this,&ManualTagManagerDialog::rename);connect(remove_,&QPushButton::clicked,this,&ManualTagManagerDialog::remove);connect(visible_,&QCheckBox::clicked,this,&ManualTagManagerDialog::visibility);connect(reload_,&QPushButton::clicked,this,[this]{ManualTagCommand command;command.kind=ManualTagCommandKind::Reload;intent(std::move(command));});updateButtons();
}
const ManualTagEditorRow* ManualTagManagerDialog::selected() const {const auto index=table_?table_->currentRow():-1;return index>=0&&static_cast<std::size_t>(index)<rows_.size()?&rows_[static_cast<std::size_t>(index)]:nullptr;}
void ManualTagManagerDialog::updateButtons(){
  const auto* row=selected();const bool idle=available_&&session_&&!pending_;const bool edit=idle&&writable_&&generation_;
  const bool name= !originalTrim(name_->text()).isEmpty()&&originalTrim(name_->text()).toUtf8().size()<=4096;
  const QPointer<ManualTagManagerDialog> guard(this);
  create_->setEnabled(edit&&name);if(!guard){return;}rename_->setEnabled(edit&&row&&row->editable&&name);if(!guard){return;}remove_->setEnabled(edit&&row&&row->editable);if(!guard){return;}visible_->setEnabled(edit&&row&&row->editable);if(!guard){return;}reload_->setEnabled(idle);
}
void ManualTagManagerDialog::selectRow(){if(rendering_){return;}const auto* row=selected();const QSignalBlocker blocker(visible_);if(row){name_->setText(row->name);visible_->setChecked(row->visible);if(!row->detail.isEmpty()){status_->setText(row->detail);}}updateButtons();}
void ManualTagManagerDialog::setPresentation(ManualTagCommandPresentation packet){
  if(!session_||!packet||packet->session!=session_||packet->request<lastRequest_||packet->generation<generation_){return;}
  auto values=manualTagEditorValues(packet);if(!values){return;}const auto old=selected()?selected()->id:std::optional<qint32>{};
  generation_=packet->generation;lastRequest_=packet->request;writable_=values->writable;rows_=std::move(values->rows);rendering_=true;
  const QSignalBlocker blocked(table_);table_->setRowCount(static_cast<int>(rows_.size()));int select=-1;
  for(std::size_t i=0;i<rows_.size();++i){const auto& row=rows_[i];auto* name=new QTableWidgetItem(row.label);name->setToolTip(row.detail);table_->setItem(static_cast<int>(i),0,name);table_->setItem(static_cast<int>(i),1,new QTableWidgetItem(row.visible?tr("Yes"):tr("No")));table_->setItem(static_cast<int>(i),2,new QTableWidgetItem(QString::number(static_cast<qulonglong>(row.members))));if(old&&row.id==old){select=static_cast<int>(i);}}
  if(select<0&&!rows_.empty()){select=0;}if(select>=0){table_->setCurrentCell(select,0);table_->selectRow(select);}rendering_=false;
  status_->setText(writable_?(packet->readState==ManualTagReadState::Missing?tr("No saved tags yet. Create a tag to begin."):tr("Saved tags loaded.")):tr("Read only. Reload after the profile or recovery state is resolved."));selectRow();updateButtons();
}
void ManualTagManagerDialog::setPending(const ManualTagCommand& command){if(command.session!=session_||!command.request||pending_){return;}pending_=command;status_->setText(tr("Waiting for the profile worker…"));updateButtons();}
void ManualTagManagerDialog::setResult(ManualTagCommandPresentation packet){
  if(!packet||!pending_||packet->session!=pending_->session||packet->request!=pending_->request||packet->kind!=pending_->kind||packet->failure.size()>8192){return;}
  const bool success=packet->status==ManualTagCommandStatus::Loaded||packet->status==ManualTagCommandStatus::Saved||packet->status==ManualTagCommandStatus::Unchanged;
  if(success){
    if(!packet->failure.isEmpty()||packet->generation<generation_||!manualTagEditorValues(packet)){return;}
    if((packet->status==ManualTagCommandStatus::Loaded)!=(pending_->kind==ManualTagCommandKind::Reload)){return;}
    if(packet->status==ManualTagCommandStatus::Saved&&(!packet->changed||packet->generation<=pending_->generation)){return;}
    if(packet->status==ManualTagCommandStatus::Unchanged&&(packet->changed||packet->generation<pending_->generation)){return;}
  }else if(packet->status!=ManualTagCommandStatus::Rejected&&packet->status!=ManualTagCommandStatus::Cancelled){return;}
  else if(packet->failure.isEmpty()){return;}
  pending_.reset();
  switch(packet->status){
    case ManualTagCommandStatus::Loaded:case ManualTagCommandStatus::Saved:case ManualTagCommandStatus::Unchanged:setPresentation(packet);break;
    case ManualTagCommandStatus::Rejected:case ManualTagCommandStatus::Cancelled:writable_=false;break;
  }
  status_->setText(packet->status==ManualTagCommandStatus::Saved?tr("Saved."):packet->status==ManualTagCommandStatus::Unchanged?tr("Unchanged."):packet->status==ManualTagCommandStatus::Loaded?(writable_?tr("Saved tags loaded."):tr("Read only.")):packet->status==ManualTagCommandStatus::Cancelled?tr("Cancelled. Reload before retrying."):tr("Rejected: %1").arg(packet->failure.left(512)));updateButtons();
}
void ManualTagManagerDialog::setAvailable(bool value){available_=value;updateButtons();}
void ManualTagManagerDialog::showFailure(const QString& message){pending_.reset();writable_=false;status_->setText(message.left(512));updateButtons();}
void ManualTagManagerDialog::invalidateSession(){session_=0;generation_=0;pending_.reset();rows_.clear();writable_=false;available_=false;updateButtons();reject();}
void ManualTagManagerDialog::intent(ManualTagCommand command){if(!available_||!session_||pending_||(command.kind!=ManualTagCommandKind::Reload&&!writable_)){return;}command.session=session_;command.generation=generation_;emit commandRequested(std::move(command));}
void ManualTagManagerDialog::create(){
  if(!create_->isEnabled()){return;}const auto name=originalTrim(name_->text());
  for(std::size_t i=0;i<rows_.size();++i){if(rows_[i].name==name){table_->setCurrentCell(static_cast<int>(i),0);table_->selectRow(static_cast<int>(i));status_->setText(tr("That tag already exists."));return;}}
  ManualTagCommand command;command.kind=ManualTagCommandKind::Create;command.name=name;intent(std::move(command));
}
void ManualTagManagerDialog::rename(){const auto* row=selected();if(!rename_->isEnabled()||!row||!row->id){return;}ManualTagCommand command;command.kind=ManualTagCommandKind::Rename;command.tagId=*row->id;command.name=originalTrim(name_->text());intent(std::move(command));}
void ManualTagManagerDialog::remove(){
  const auto* row=selected();if(!remove_->isEnabled()||!row||!row->id){return;}const auto id=*row->id;const auto generation=generation_;const auto name=row->label;const auto count=row->members;const QPointer<ManualTagManagerDialog> guard(this);
  const auto answer=QMessageBox::question(this,tr("Delete tag"),tr("Delete “%1” with %2 saved members?").arg(name).arg(static_cast<qulonglong>(count)),QMessageBox::Yes|QMessageBox::No,QMessageBox::No);
  if(!guard||answer!=QMessageBox::Yes||generation_!=generation||!selected()||selected()->id!=id||!remove_->isEnabled()){return;}ManualTagCommand command;command.kind=ManualTagCommandKind::Remove;command.tagId=id;intent(std::move(command));
}
void ManualTagManagerDialog::visibility(bool value){const auto* row=selected();if(!row){return;}const auto id=row->id;const auto previous=row->visible;{const QSignalBlocker block(visible_);visible_->setChecked(previous);}if(!visible_->isEnabled()||!id){return;}ManualTagCommand command;command.kind=ManualTagCommandKind::Visibility;command.tagId=*id;command.visible=value;intent(std::move(command));}
} // namespace biglybtc::ui
