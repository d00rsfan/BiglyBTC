// SPDX-License-Identifier: GPL-2.0-or-later
#include "manual_tag_editor.hpp"
#include "biglybtc/saved_download_tag_edits.hpp"
#include "../app/manual_tag_policy.hpp"
#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
namespace biglybtc::ui {
namespace {
QString originalTrim(const QString& value){qsizetype first=0,last=value.size();while(first<last&&value[first].unicode()<=0x20){++first;}while(last>first&&value[last-1].unicode()<=0x20){--last;}return value.mid(first,last-first);}
}
std::optional<ManualTagEditorValues> manualTagEditorValues(ManualTagCommandPresentation packet) {
  try {
    if(!packet||!packet->generation||!packet->tags||!packet->tags->source||packet->failure.size()>8192||packet->constraintStatus.size()>8192
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
      row.constraintEditable=row.editable;
      row.groupEditable=row.editable&&record&&core::ordinarySavedTagGroup(*out.tags,*record);
      if(config){if(const auto* group=config->find("gr")){
        if(group->type!=core::Node::Type::Bytes||group->bytes.size()>4096){row.groupEditable=false;}
        else{const auto text=QString::fromUtf8(group->bytes.data(),static_cast<qsizetype>(group->bytes.size()));
          if(text.toUtf8().toStdString()!=group->bytes){row.groupEditable=false;}else{row.group=text;}}
      }}
      if(record){const auto colour=core::readSavedTagColour(*record);
        row.colourEditable=row.editable&&core::ordinarySavedTagGroup(*out.tags,*record)&&bool(colour);
        if(colour&&*colour){const auto& rgb=**colour;row.colour=std::array<qint32,3>{rgb[0],rgb[1],rgb[2]};}
      }
      const auto text=[&](const core::Node* value,QString& destination){
        if(!value){return;}if(value->type!=core::Node::Type::Bytes){row.constraintEditable=false;return;}
        destination=QString::fromUtf8(value->bytes.data(),static_cast<qsizetype>(value->bytes.size()));
        if(destination.toUtf8().toStdString()!=value->bytes||value->bytes.size()>4096){row.constraintEditable=false;}
      };
      if(config){
        if(const auto* property=config->find("pp.constraint")){
          if(property->type!=core::Node::Type::List){row.constraintEditable=false;}
          else{if(!property->children.empty()){text(&property->children[0],row.constraintExpression);}
            if(property->children.size()>1){text(&property->children[1],row.constraintOptions);}}
        }
        if(const auto* enabled=config->find("pp.enabled.constraint")){
          if(enabled->type!=core::Node::Type::Integer){row.constraintEditable=false;}
          else{row.constraintEnabled=enabled->integer==1;}
        }
      }
      const bool ordinaryGroup=record&&core::ordinarySavedTagMembershipEffects(*out.tags,*record);
      row.canAddMember=row.editable&&ordinaryGroup&&policy.addAllowed();row.canRemoveMember=row.editable&&ordinaryGroup&&policy.removeAllowed();
      if(row.editable&&!ordinaryGroup){row.detail=QObject::tr("Configured tag or group effects are unavailable; membership is read only.");}
      if(row.editable&&ordinaryGroup&&(policy.autoAdd||policy.autoRemove)){row.detail=QObject::tr("Automatic tag settings control one or both membership directions.");}
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
  auto* groupRow=new QHBoxLayout;group_=new QLineEdit(this);group_->setObjectName(QStringLiteral("manual-tag-group"));group_->setMaxLength(4096);group_->setPlaceholderText(tr("Group (blank clears)"));groupSave_=new QPushButton(tr("Save group"),this);groupSave_->setObjectName(QStringLiteral("manual-tag-group-save"));groupRow->addWidget(group_);groupRow->addWidget(groupSave_);layout->addLayout(groupRow);
  auto* colourRow=new QHBoxLayout;colourLabel_=new QLabel(this);colourLabel_->setObjectName(QStringLiteral("manual-tag-colour-value"));colourChoose_=new QPushButton(tr("Choose colour…"),this);colourChoose_->setObjectName(QStringLiteral("manual-tag-colour-choose"));colourDefault_=new QPushButton(tr("Default colour"),this);colourDefault_->setObjectName(QStringLiteral("manual-tag-colour-default"));colourRow->addWidget(colourLabel_);colourRow->addStretch();colourRow->addWidget(colourChoose_);colourRow->addWidget(colourDefault_);layout->addLayout(colourRow);
  layout->addWidget(new QLabel(tr("Automatic membership constraint"),this));
  constraintExpression_=new QPlainTextEdit(this);constraintExpression_->setObjectName(QStringLiteral("manual-tag-constraint-expression"));constraintExpression_->setMaximumHeight(90);layout->addWidget(constraintExpression_);
  constraintOptions_=new QLineEdit(this);constraintOptions_->setObjectName(QStringLiteral("manual-tag-constraint-options"));constraintOptions_->setMaxLength(4096);constraintOptions_->setPlaceholderText(tr("Options: am=0; add/remove, am=1; add, am=2; remove, am=3; new downloads"));layout->addWidget(constraintOptions_);
  auto* constraintRow=new QHBoxLayout;constraintEnabled_=new QCheckBox(tr("Constraint enabled"),this);constraintEnabled_->setObjectName(QStringLiteral("manual-tag-constraint-enabled"));constraintSave_=new QPushButton(tr("Save constraint"),this);constraintSave_->setObjectName(QStringLiteral("manual-tag-constraint-save"));constraintRow->addWidget(constraintEnabled_);constraintRow->addStretch();constraintRow->addWidget(constraintSave_);layout->addLayout(constraintRow);
  auto* scope=new QLabel(tr("Saving configures the original tag property. Invalid, unsupported or unavailable rules leave membership unchanged. Automatic rules run only in a normal profile session."),this);scope->setWordWrap(true);layout->addWidget(scope);
  status_=new QLabel(tr("Load the saved tag list to edit tags."),this);status_->setObjectName(QStringLiteral("manual-tag-status"));status_->setWordWrap(true);layout->addWidget(status_);
  auto* close=new QDialogButtonBox(QDialogButtonBox::Close,this);connect(close,&QDialogButtonBox::rejected,this,&QDialog::reject);layout->addWidget(close);
  connect(colourChoose_,&QPushButton::clicked,this,&ManualTagManagerDialog::chooseColour);connect(colourDefault_,&QPushButton::clicked,this,&ManualTagManagerDialog::clearColour);
  connect(group_,&QLineEdit::textChanged,this,[this]{updateButtons();});connect(groupSave_,&QPushButton::clicked,this,&ManualTagManagerDialog::saveGroup);
  connect(constraintExpression_,&QPlainTextEdit::textChanged,this,[this]{updateButtons();});connect(constraintOptions_,&QLineEdit::textChanged,this,[this]{updateButtons();});connect(constraintEnabled_,&QCheckBox::toggled,this,[this]{updateButtons();});connect(constraintSave_,&QPushButton::clicked,this,&ManualTagManagerDialog::saveConstraint);
  connect(table_,&QTableWidget::itemSelectionChanged,this,&ManualTagManagerDialog::selectRow);connect(name_,&QLineEdit::textChanged,this,[this]{updateButtons();});
  connect(create_,&QPushButton::clicked,this,&ManualTagManagerDialog::create);connect(rename_,&QPushButton::clicked,this,&ManualTagManagerDialog::rename);connect(remove_,&QPushButton::clicked,this,&ManualTagManagerDialog::remove);connect(visible_,&QCheckBox::clicked,this,&ManualTagManagerDialog::visibility);connect(reload_,&QPushButton::clicked,this,[this]{ManualTagCommand command;command.kind=ManualTagCommandKind::Reload;intent(std::move(command));});updateButtons();
}
const ManualTagEditorRow* ManualTagManagerDialog::selected() const {const auto index=table_?table_->currentRow():-1;return index>=0&&static_cast<std::size_t>(index)<rows_.size()?&rows_[static_cast<std::size_t>(index)]:nullptr;}
void ManualTagManagerDialog::updateButtons(){
  const auto* row=selected();const bool idle=available_&&session_&&!pending_;const bool edit=idle&&writable_&&generation_;
  const bool name= !originalTrim(name_->text()).isEmpty()&&originalTrim(name_->text()).toUtf8().size()<=4096;
  const QPointer<ManualTagManagerDialog> guard(this);
  create_->setEnabled(edit&&name);if(!guard){return;}rename_->setEnabled(edit&&row&&row->editable&&name);if(!guard){return;}remove_->setEnabled(edit&&row&&row->editable);if(!guard){return;}visible_->setEnabled(edit&&row&&row->editable);if(!guard){return;}reload_->setEnabled(idle);if(!guard){return;}
  const bool constraint=edit&&row&&row->constraintEditable;
  constraintExpression_->setEnabled(constraint);if(!guard){return;}constraintOptions_->setEnabled(constraint);if(!guard){return;}constraintEnabled_->setEnabled(constraint);if(!guard){return;}
  constraintSave_->setEnabled(constraint&&constraintExpression_->toPlainText().toUtf8().size()<=4096&&constraintOptions_->text().toUtf8().size()<=4096);
  if(!guard){return;}const bool group=edit&&row&&row->groupEditable;group_->setEnabled(group);if(!guard){return;}groupSave_->setEnabled(group&&originalTrim(group_->text()).toUtf8().size()<=4096);
  if(!guard){return;}const bool colour=edit&&row&&row->colourEditable;
  const bool displayable=!row||!row->colour||std::all_of(row->colour->begin(),row->colour->end(),[](qint32 channel){return channel>=0&&channel<=255;});
  colourChoose_->setEnabled(colour&&displayable);if(!guard){return;}colourDefault_->setEnabled(colour&&row->colour.has_value());
}
void ManualTagManagerDialog::selectRow(){
  if(rendering_){return;}const auto* row=selected();const QSignalBlocker a(visible_),b(constraintExpression_),c(constraintOptions_),d(constraintEnabled_),e(name_),f(group_);
  if(row){group_->setText(row->group.value_or(QString{}));name_->setText(row->name);visible_->setChecked(row->visible);constraintExpression_->setPlainText(row->constraintExpression);constraintOptions_->setText(row->constraintOptions);constraintEnabled_->setChecked(row->constraintEnabled);if(!row->detail.isEmpty()){status_->setText(row->detail);}}
  else{group_->clear();constraintExpression_->clear();constraintOptions_->clear();constraintEnabled_->setChecked(true);}
  if(row&&row->colour){const auto& rgb=*row->colour;const bool displayable=std::all_of(rgb.begin(),rgb.end(),[](qint32 channel){return channel>=0&&channel<=255;});colourLabel_->setText(tr("Saved colour: %1,%2,%3%4").arg(rgb[0]).arg(rgb[1]).arg(rgb[2]).arg(displayable?QString{}:tr(" (outside display range)")));}
  else{colourLabel_->setText(row&&row->colourEditable?tr("Default colour: %1,%2,%3").arg(core::ManualTagDefaultRgb[0]).arg(core::ManualTagDefaultRgb[1]).arg(core::ManualTagDefaultRgb[2]):tr("Colour/default inheritance unavailable; saved data preserved."));}
  updateButtons();
}
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
  if(!packet||!pending_||packet->session!=pending_->session||packet->request!=pending_->request||packet->kind!=pending_->kind||packet->failure.size()>8192||packet->constraintStatus.size()>8192){return;}
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
  status_->setText(packet->status==ManualTagCommandStatus::Saved?tr("Saved."):packet->status==ManualTagCommandStatus::Unchanged?tr("Unchanged."):packet->status==ManualTagCommandStatus::Loaded?(writable_?tr("Saved tags loaded."):tr("Read only.")):packet->status==ManualTagCommandStatus::Cancelled?tr("Cancelled. Reload before retrying."):tr("Rejected: %1").arg(packet->failure.left(512)));if(!packet->constraintStatus.isEmpty()&&success){status_->setText(status_->text()+QLatin1Char(' ')+packet->constraintStatus.left(512));}updateButtons();
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
void ManualTagManagerDialog::saveConstraint(){
  const auto* row=selected();if(!row||!row->id||!constraintSave_->isEnabled()){return;}
  const auto id=*row->id;const auto generation=generation_;const auto expression=originalTrim(constraintExpression_->toPlainText());
  const auto options=constraintOptions_->text();const bool enabled=constraintEnabled_->isChecked();
  if(originalTrim(row->constraintExpression).isEmpty()&&!expression.isEmpty()&&row->members){
    const QPointer<ManualTagManagerDialog> guard(this);
    const auto answer=QMessageBox::question(this,tr("Configure tag constraint"),tr("This tag already has saved members. An automatic constraint may change membership when supported current facts are available. Continue?"),QMessageBox::Ok|QMessageBox::Cancel,QMessageBox::Cancel);
    if(!guard||answer!=QMessageBox::Ok||generation_!=generation||!selected()||selected()->id!=std::optional<qint32>{id}||!constraintSave_->isEnabled()){return;}
    if(originalTrim(constraintExpression_->toPlainText())!=expression||constraintOptions_->text()!=options||constraintEnabled_->isChecked()!=enabled){return;}
  }
  ManualTagCommand command;command.kind=ManualTagCommandKind::SetConstraint;command.tagId=id;command.constraintExpression=expression;command.constraintOptions=options;command.constraintEnabled=enabled;intent(std::move(command));
}
void ManualTagManagerDialog::saveGroup(){
  const auto* row=selected();if(!row||!row->id||!groupSave_->isEnabled()){return;}
  ManualTagCommand command;command.kind=ManualTagCommandKind::SetGroup;command.tagId=*row->id;
  const auto value=originalTrim(group_->text());if(!value.isEmpty()){command.group=value;}
  // Exact original classic entry: trim, then blank means setGroup(null).
  // The typed worker API separately preserves an explicit empty string.
  intent(std::move(command));
}
void ManualTagManagerDialog::chooseColour(){
  const auto* row=selected();if(!row||!row->id||!colourChoose_->isEnabled()){return;}
  const auto id=*row->id;const auto generation=generation_;const auto rgb=row->colour.value_or(std::array<qint32,3>{core::ManualTagDefaultRgb[0],core::ManualTagDefaultRgb[1],core::ManualTagDefaultRgb[2]});
  const QPointer<ManualTagManagerDialog> guard(this);
  QPointer<QColorDialog> picker(new QColorDialog(QColor(rgb[0],rgb[1],rgb[2]),this));
  if(!guard||!picker){return;}picker->setObjectName(QStringLiteral("manual-tag-colour-picker"));picker->setOption(QColorDialog::DontUseNativeDialog);
  if(!guard||!picker){return;}const auto result=picker->exec();
  if(!guard){return;}if(!picker){return;}const auto selectedColour=picker->selectedColor();picker->deleteLater();
  if(result!=QDialog::Accepted||!selectedColour.isValid()||generation_!=generation||!colourChoose_->isEnabled()){return;}
  row=selected();if(!row||row->id!=id){return;}
  ManualTagCommand command;command.kind=ManualTagCommandKind::SetColour;command.tagId=id;command.colour=std::array<qint32,3>{selectedColour.red(),selectedColour.green(),selectedColour.blue()};intent(std::move(command));
}
void ManualTagManagerDialog::clearColour(){
  const auto* row=selected();if(!row||!row->id||!colourDefault_->isEnabled()){return;}
  ManualTagCommand command;command.kind=ManualTagCommandKind::SetColour;command.tagId=*row->id;intent(std::move(command));
}
} // namespace biglybtc::ui
