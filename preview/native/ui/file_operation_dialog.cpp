#include "file_operation_dialog.hpp"
#include <QAbstractTableModel>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QShowEvent>
#include <QTableView>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <atomic>
#include <limits>

namespace biglybtc::ui {
namespace {
constexpr std::size_t MaximumPaths=65536, MaximumText=8*1024*1024;
std::optional<quint64> nextRequest() {
  static std::atomic<quint64> next{1};auto current=next.load(std::memory_order_relaxed);
  while(current!=std::numeric_limits<quint64>::max()) {
    if(next.compare_exchange_weak(current,current+1,std::memory_order_relaxed)){return current;}
  }
  return {};
}
bool bounded(const QString& value,int maximum=32768) { return value.size()<=maximum&&!value.contains(QChar::Null); }
bool hexadecimal(const QString& value) {
  return std::ranges::all_of(value,[](QChar c){const auto n=c.unicode();return (n>='0'&&n<='9')||(n>='a'&&n<='f')||(n>='A'&&n<='F');});
}
bool valid(const FileOperationRequest& value) {
  return value.session&&value.request&&value.revision&&value.target.generation
      &&(value.target.hash.size()==40||value.target.hash.size()==64)&&hexadecimal(value.target.hash)
      &&static_cast<unsigned>(value.operation)<=3&&bounded(value.destinationParent)
      &&(static_cast<unsigned>(value.operation)<2||!value.deleteTorrentFile.value_or(false));
}
bool moves(FileOperation operation) { return operation==FileOperation::Relocate||operation==FileOperation::Relink; }
QString title(FileOperation operation) {
  switch(operation) {
    case FileOperation::RemoveKeepData:return QStringLiteral("Remove torrent, keep data");
    case FileOperation::RemoveDeleteData:return QStringLiteral("Remove torrent and delete data");
    case FileOperation::Relocate:return QStringLiteral("Move torrent data");
    case FileOperation::Relink:return QStringLiteral("Locate existing torrent data");
  }
  return {};
}
bool valid(const FileOperationPlanPacket& value) {
  if(!valid(value.request)||value.approvalScopeDigest.size()!=64||!hexadecimal(value.approvalScopeDigest)
      ||value.paths.size()>MaximumPaths||!bounded(value.sourceRoot)||!bounded(value.targetRoot)) {return false;}
  if(value.request.deleteTorrentFile&&*value.request.deleteTorrentFile!=value.deleteTorrentFile){return false;}
  if(value.deleteTorrentFile){if((!value.archivePresent&&value.archiveBytes)||moves(value.request.operation)||value.archivePath.isEmpty()||!bounded(value.archivePath)||!QDir::isAbsolutePath(value.archivePath)){return false;}}
  else if(!value.archivePath.isEmpty()||value.archivePresent||value.archiveInternalState||value.archiveBytes){return false;}
  if(value.deleteTorrentFile&&!value.archiveInternalState){if(value.archiveBackupPath!=value.archivePath+QStringLiteral(".bak")||!bounded(value.archiveBackupPath)||(!value.archiveBackupPresent&&value.archiveBackupBytes)){return false;}}
  else if(!value.archiveBackupPath.isEmpty()||value.archiveBackupPresent||value.archiveBackupBytes){return false;}
  quint64 present=0,missing=0,bytes=0;std::size_t text=static_cast<std::size_t>(value.archivePath.size()+value.archiveBackupPath.size());
  for(const auto& path:value.paths) {
    if(path.source.isEmpty()||!bounded(path.source)||!bounded(path.destination)
        ||!QDir::isAbsolutePath(path.source)||(!path.destination.isEmpty()&&!QDir::isAbsolutePath(path.destination))) {return false;}
    text+=static_cast<std::size_t>(path.source.size()+path.destination.size());if(text>MaximumText){return false;}
    if(path.present) {if(path.bytes>std::numeric_limits<quint64>::max()-bytes){return false;}++present;bytes+=path.bytes;}
    else {++missing;}
  }
  return present==value.presentFiles&&missing==value.missingFiles&&bytes==value.totalBytes;
}
class PathsModel final:public QAbstractTableModel {
public:
  explicit PathsModel(QObject* parent):QAbstractTableModel(parent) {}
  int rowCount(const QModelIndex& parent={}) const override {return parent.isValid()?0:static_cast<int>(paths_.size());}
  int columnCount(const QModelIndex& parent={}) const override {return parent.isValid()?0:4;}
  QVariant headerData(int section,Qt::Orientation orientation,int role) const override {
    if(role!=Qt::DisplayRole||orientation!=Qt::Horizontal||section<0||section>=4){return {};}
    const std::array<QString,4> names{QStringLiteral("Data path"),QStringLiteral("Destination"),QStringLiteral("Present bytes"),QStringLiteral("State")};return names[static_cast<std::size_t>(section)];
  }
  QVariant data(const QModelIndex& index,int role) const override {
    if(!index.isValid()||index.row()<0||index.column()<0||index.column()>=4||static_cast<std::size_t>(index.row())>=paths_.size()){return {};}
    const auto& path=paths_[static_cast<std::size_t>(index.row())];
    if(role==Qt::UserRole&&index.column()==2&&path.present){return QVariant::fromValue(path.bytes);}
    if(role!=Qt::DisplayRole&&role!=Qt::ToolTipRole){return {};}
    switch(index.column()) {
      case 0:return path.source;case 1:return path.destination;
      case 2:return path.present?QString::number(path.bytes):QStringLiteral("—");
      case 3:return path.present?QStringLiteral("Present"):QStringLiteral("Missing");
    }
    return {};
  }
  bool setPaths(std::vector<FileOperationPath> values) {
    const QPointer<PathsModel> self(this);beginResetModel();if(!self){return false;}paths_=std::move(values);endResetModel();return self;
  }
private:
  std::vector<FileOperationPath> paths_;
};
}
struct FileOperationDialog::Impl {
  FileOperationDialog* q;
  FileOperationRequest active;
  quint64 highestSession=0,version=0,progress=0,completedFiles=0;
  bool busy=false,planning=false,executing=false,closing=false,dispatched=false,archiveChoiceKnown=false;
  FileOperationPlanPresentation accepted;
  QPointer<QLabel> status,summary;
  QPointer<QLineEdit> destination;
  QPointer<QFormLayout> destinationForm;
  QPointer<QCheckBox> confirm,archive;
  QPointer<QPushButton> browse,review,execute,cancel;
  QPointer<QTableView> table;
  QPointer<PathsModel> model;
  QPointer<QFileDialog> picker;
  struct Guard {QPointer<FileOperationDialog> self;quint64 version;bool current() const{return self&&self->impl_->version==version;}};
  Guard guard() const {return {q,version};}
  bool bump() {if(version==std::numeric_limits<quint64>::max()){return false;}++version;return true;}
  bool text(const QString& value) {const auto alive=guard();if(status){status->setText(value);}return alive.current();}
  bool controls() {
    const auto alive=guard();const bool editable=active.session&&!busy&&!planning&&!executing;
    for(const QPointer<QWidget>& widget:std::vector<QPointer<QWidget>>{destination.data(),browse.data(),review.data()}) {
      if(widget){widget->setEnabled(editable);}if(!alive.current()){return false;}
    }
    if(archive){archive->setEnabled(editable&&!moves(active.operation));}if(!alive.current()){return false;}
    if(confirm){confirm->setEnabled(editable&&accepted&&active.operation==FileOperation::RemoveDeleteData);}if(!alive.current()){return false;}
    if(execute){execute->setEnabled(editable&&accepted&&(active.operation!=FileOperation::RemoveDeleteData||(confirm&&confirm->isChecked())));}if(!alive.current()){return false;}
    if(cancel){cancel->setEnabled(!closing);}return alive.current();
  }
  bool unconfirm() {
    const auto alive=guard();const auto box=confirm;
    if(box){const bool previous=box->blockSignals(true);box->setChecked(false);if(box){box->blockSignals(previous);}}
    return alive.current();
  }
  void edited() {
    if(executing||!active.session||!bump()){return;}planning=false;accepted.reset();dispatched=false;
    if(unconfirm()&&text(QStringLiteral("Review the data paths before confirming."))){controls();}
  }
  void prepare() {
    if(!active.session||busy||planning||executing||!destination||!q->isVisible()){return;}
    if(moves(active.operation)&&(!bounded(destination->text())||!QDir::isAbsolutePath(destination->text()))) {
      text(QStringLiteral("Choose an absolute destination parent directory."));return;
    }
    const auto identity=nextRequest();if(!identity||!bump()){return;}
    active.request=*identity;active.destinationParent=moves(active.operation)?destination->text():QString{};
    active.deleteTorrentFile=moves(active.operation)?std::optional<bool>{false}:(archiveChoiceKnown&&archive?std::optional<bool>{archive->isChecked()}:std::nullopt);
    planning=true;dispatched=true;accepted.reset();const auto alive=guard();
    if(!unconfirm()||!text(QStringLiteral("Reviewing saved data paths…"))||!controls()||!alive.current()){return;}
    emit q->planRequested(active);
  }
  void choose() {
    if(!destination||picker){return;}const auto alive=guard();
    auto* dialog=new QFileDialog(q,QStringLiteral("Choose destination parent directory"));picker=dialog;
    dialog->setFileMode(QFileDialog::Directory);dialog->setOption(QFileDialog::ShowDirsOnly);dialog->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(dialog,&QFileDialog::fileSelected,q,[alive](const QString& path){if(alive.current()&&alive.self->impl_->destination){alive.self->impl_->destination->setText(path);}});
    dialog->open();
  }
};
FileOperationDialog::FileOperationDialog(QWidget* parent):QDialog(parent),impl_(std::make_unique<Impl>()) {
  impl_->q=this;setObjectName(QStringLiteral("file-operation-dialog"));resize(1000,560);
  auto* layout=new QVBoxLayout(this);auto* form=new QFormLayout;impl_->destinationForm=form;
  auto* row=new QWidget(this);auto* fields=new QHBoxLayout(row);fields->setContentsMargins(0,0,0,0);
  impl_->destination=new QLineEdit(row);impl_->destination->setObjectName(QStringLiteral("file-operation-destination"));impl_->destination->setMaxLength(32768);
  impl_->browse=new QPushButton(QStringLiteral("Browse…"),row);impl_->browse->setObjectName(QStringLiteral("file-operation-browse"));fields->addWidget(impl_->destination);fields->addWidget(impl_->browse);
  form->addRow(QStringLiteral("Destination parent"),row);layout->addLayout(form);
  impl_->summary=new QLabel(this);impl_->summary->setObjectName(QStringLiteral("file-operation-summary"));impl_->summary->setTextFormat(Qt::PlainText);impl_->summary->setWordWrap(true);layout->addWidget(impl_->summary);
  impl_->table=new QTableView(this);impl_->table->setObjectName(QStringLiteral("file-operation-paths"));impl_->model=new PathsModel(impl_->table);impl_->table->setModel(impl_->model);
  impl_->table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);impl_->table->setColumnWidth(0,370);impl_->table->setColumnWidth(1,300);layout->addWidget(impl_->table,1);
  impl_->archive=new QCheckBox(QStringLiteral("Delete the .torrent archive and backup permanently"),this);impl_->archive->setObjectName(QStringLiteral("file-operation-delete-archive"));
  impl_->archive->setToolTip(QStringLiteral("The saved preference is loaded by the worker. Changing this choice requires a new path review."));layout->addWidget(impl_->archive);
  impl_->confirm=new QCheckBox(QStringLiteral("Delete the listed data files permanently"),this);impl_->confirm->setObjectName(QStringLiteral("file-operation-confirm-delete"));layout->addWidget(impl_->confirm);
  impl_->status=new QLabel(this);impl_->status->setObjectName(QStringLiteral("file-operation-status"));impl_->status->setTextFormat(Qt::PlainText);impl_->status->setWordWrap(true);layout->addWidget(impl_->status);
  auto* buttons=new QHBoxLayout;impl_->review=new QPushButton(QStringLiteral("Review paths"),this);impl_->execute=new QPushButton(QStringLiteral("Confirm"),this);impl_->cancel=new QPushButton(QStringLiteral("Cancel"),this);
  impl_->review->setObjectName(QStringLiteral("file-operation-review"));impl_->execute->setObjectName(QStringLiteral("file-operation-execute"));impl_->cancel->setObjectName(QStringLiteral("file-operation-cancel"));
  buttons->addWidget(impl_->review);buttons->addStretch();buttons->addWidget(impl_->execute);buttons->addWidget(impl_->cancel);layout->addLayout(buttons);
  connect(impl_->destination,&QLineEdit::textChanged,this,[this]{impl_->edited();});connect(impl_->browse,&QPushButton::clicked,this,[this]{impl_->choose();});
  connect(impl_->review,&QPushButton::clicked,this,[this]{impl_->prepare();});connect(impl_->execute,&QPushButton::clicked,this,&FileOperationDialog::accept);connect(impl_->cancel,&QPushButton::clicked,this,&FileOperationDialog::reject);
  connect(impl_->archive,&QCheckBox::toggled,this,[this]{impl_->archiveChoiceKnown=true;impl_->edited();});
  connect(impl_->confirm,&QCheckBox::toggled,this,[this]{impl_->controls();});impl_->controls();
}
FileOperationDialog::~FileOperationDialog()=default;
bool FileOperationDialog::beginOperation(quint64 session,quint64 revision,Target target,FileOperation operation,QString destination) {
  FileOperationRequest request{session,1,revision,std::move(target),operation,std::move(destination)};
  if(!valid(request)||session<impl_->highestSession||!impl_->bump()){return false;}
  const auto alive=impl_->guard();impl_->highestSession=session;impl_->active=std::move(request);impl_->active.request=0;
  impl_->planning=impl_->executing=impl_->closing=impl_->dispatched=false;impl_->archiveChoiceKnown=false;impl_->accepted.reset();impl_->progress=impl_->completedFiles=0;
  setWindowTitle(title(operation));if(!alive.current()){return false;}
  if(impl_->destinationForm){impl_->destinationForm->setRowVisible(0,moves(operation));}if(!alive.current()){return false;}
  if(impl_->destination){const auto field=impl_->destination;const bool blocked=field->blockSignals(true);field->setText(impl_->active.destinationParent);if(field){field->blockSignals(blocked);field->setVisible(moves(operation));}}if(!alive.current()){return false;}
  if(impl_->browse){impl_->browse->setVisible(moves(operation));}if(!alive.current()){return false;}
  if(impl_->archive){const auto field=impl_->archive;const bool blocked=field->blockSignals(true);field->setChecked(false);if(field){field->blockSignals(blocked);field->setVisible(!moves(operation));}}if(!alive.current()){return false;}
  if(impl_->confirm){impl_->confirm->setVisible(operation==FileOperation::RemoveDeleteData);}if(!alive.current()||!impl_->unconfirm()){return false;}
  if(impl_->model&&!impl_->model->setPaths({})){return false;}if(!alive.current()){return false;}
  if(!impl_->text(QStringLiteral("Review the data paths before confirming."))||!impl_->controls()||!alive.current()){return false;}
  if(isVisible()){QMetaObject::invokeMethod(this,[alive]{if(alive.current()){alive.self->impl_->prepare();}},Qt::QueuedConnection);}return true;
}
void FileOperationDialog::invalidateSession() {
  if(!impl_->bump()){return;}const auto alive=impl_->guard();impl_->active={};impl_->accepted.reset();impl_->planning=impl_->executing=false;
  const auto picker=impl_->picker;if(picker){picker->close();}if(alive.current()){done(QDialog::Rejected);}
}
bool FileOperationDialog::pending() const noexcept {return impl_->planning||impl_->executing;}
FileOperationPlanPresentation FileOperationDialog::plan() const {return impl_->accepted;}
void FileOperationDialog::setBusy(bool busy) {
  if(!impl_->bump()){return;}impl_->busy=busy;const auto alive=impl_->guard();
  if(!impl_->controls()||!alive.current()){return;}
  if(!busy&&!impl_->dispatched&&isVisible()){QMetaObject::invokeMethod(this,[alive]{if(alive.current()){alive.self->impl_->prepare();}},Qt::QueuedConnection);}
}
bool FileOperationDialog::setPlan(FileOperationPlanPresentation packet) {
  if(!packet||!impl_->planning||packet->request!=impl_->active||!impl_->bump()){return false;}
  const auto alive=impl_->guard();impl_->planning=false;
  if(!packet->failure.isEmpty()||!valid(*packet)) {
    impl_->accepted.reset();if(impl_->text(packet->failure.isEmpty()?QStringLiteral("The data plan is unavailable. Review again."):packet->failure)){impl_->controls();}return false;
  }
  impl_->accepted=std::make_shared<const FileOperationPlanPacket>(*packet);const auto retained=impl_->accepted;
  impl_->archiveChoiceKnown=true;if(impl_->archive){const auto box=impl_->archive;const bool blocked=box->blockSignals(true);box->setChecked(retained->deleteTorrentFile);if(box){box->blockSignals(blocked);}}if(!alive.current()){return false;}

  if(!impl_->unconfirm()){return false;}auto paths=retained->paths;if(retained->deleteTorrentFile){paths.push_back({retained->archivePath,{},retained->archiveBytes,retained->archivePresent});if(!retained->archiveBackupPath.isEmpty()){paths.push_back({retained->archiveBackupPath,{},retained->archiveBackupBytes,retained->archiveBackupPresent});}}
  if(impl_->model&&!impl_->model->setPaths(std::move(paths))){return false;}if(!alive.current()){return false;}
  if(impl_->summary){impl_->summary->setText(QStringLiteral("%1 present files · %2 missing files · %3 bytes present").arg(retained->presentFiles).arg(retained->missingFiles).arg(retained->totalBytes));}if(!alive.current()){return false;}
  if(!impl_->text(retained->archiveInternalState?QStringLiteral("Torrent metadata is internal download state, removed with the library item. Confirm the reviewed plan."):QStringLiteral("Review complete. Confirm to apply this plan."))||!impl_->controls()){return false;}return alive.current();
}
bool FileOperationDialog::setProgress(FileOperationProgressPresentation packet) {
  if(!packet||!impl_->executing||packet->request!=impl_->active||!packet->sequence||packet->sequence<=impl_->progress
      ||packet->completedFiles>packet->totalFiles||packet->completedFiles<impl_->completedFiles||!bounded(packet->currentPath)){return false;}
  impl_->progress=packet->sequence;impl_->completedFiles=packet->completedFiles;
  return impl_->text(QStringLiteral("%1 of %2 files processed\n%3").arg(packet->completedFiles).arg(packet->totalFiles).arg(packet->currentPath));
}
bool FileOperationDialog::setResult(FileOperationResultPresentation packet) {
  if(!packet||!impl_->executing||packet->request!=impl_->active||(packet->completed&&packet->cancelled)
      ||(!packet->completed&&!packet->cancelled&&packet->failure.isEmpty())||!impl_->bump()){return false;}
  const auto alive=impl_->guard();const bool close=impl_->closing;impl_->executing=false;impl_->accepted.reset();
  if(!impl_->text(packet->completed?QStringLiteral("File operation completed."):packet->cancelled?QStringLiteral("File operation cancelled."):packet->failure)||!impl_->controls()||!alive.current()){return false;}
  if(packet->completed||close){done(packet->completed?QDialog::Accepted:QDialog::Rejected);}return true;
}
void FileOperationDialog::accept() {
  if(impl_->busy||impl_->planning||impl_->executing||!impl_->accepted||!impl_->confirm
      ||(impl_->active.operation==FileOperation::RemoveDeleteData&&!impl_->confirm->isChecked())||!impl_->bump()){return;}
  const FileOperationSubmission value{impl_->accepted,impl_->active.operation==FileOperation::RemoveDeleteData&&impl_->confirm->isChecked(),impl_->accepted->deleteTorrentFile};
  impl_->executing=true;impl_->progress=impl_->completedFiles=0;const auto alive=impl_->guard();
  if(!impl_->text(QStringLiteral("Applying file operation…"))||!impl_->controls()||!alive.current()){return;}emit executeRequested(value);
}
void FileOperationDialog::reject() {
  if(impl_->executing) {
    if(impl_->closing||!impl_->bump()){return;}impl_->closing=true;const auto alive=impl_->guard();const auto request=impl_->active;
    if(!impl_->text(QStringLiteral("Cancelling; waiting for the worker…"))||!impl_->controls()||!alive.current()){return;}emit cancelRequested(request);return;
  }
  const bool cancel=impl_->planning;const auto request=impl_->active;if(!impl_->bump()){return;}const auto alive=impl_->guard();impl_->planning=false;
  if(cancel){emit cancelRequested(request);}if(alive.current()){done(QDialog::Rejected);}
}
void FileOperationDialog::showEvent(QShowEvent* event) {
  const QPointer<FileOperationDialog> self(this);QDialog::showEvent(event);if(!self){return;}const auto alive=impl_->guard();
  QMetaObject::invokeMethod(this,[alive]{if(alive.current()&&!alive.self->impl_->dispatched){alive.self->impl_->prepare();}},Qt::QueuedConnection);
}
void FileOperationDialog::closeEvent(QCloseEvent* event) {event->ignore();reject();}
} // namespace biglybtc::ui
