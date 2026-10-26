#include "torrent_intake_dialog.hpp"
#include <QAbstractTableModel>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QUrl>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QTableView>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>

namespace biglybtc::ui {
namespace {
bool path(const QString& value) {
  return !value.trimmed().isEmpty()&&value.size()<=4096&&!value.contains(QChar(u'\0'))&&QDir::isAbsolutePath(value);
}
bool metadataPath(const QString& value){return path(value)&&value.endsWith(QStringLiteral(".torrent"),Qt::CaseInsensitive);}
bool trackerDraft(const QString& text,core::TorrentIntakeTrackerTiers& result) {
  if(text.size()>static_cast<qsizetype>(core::MaximumIntakeTrackerBytes)){return false;}
  std::vector<std::string> tier;
  for(const auto& raw:text.split(u'\n')){
    const auto line=raw.trimmed();
    if(line.isEmpty()){if(!tier.empty()){result.push_back(std::move(tier));tier.clear();}continue;}
    const auto url=QUrl(line,QUrl::StrictMode);
    if(!url.isValid()||url.host().isEmpty()){return false;}
    tier.push_back(line.toStdString());
    if(tier.size()>core::MaximumIntakeTrackers||result.size()>core::MaximumIntakeTrackerTiers){return false;}
  }
  if(!tier.empty()){result.push_back(std::move(tier));}return core::validTorrentIntakeTrackerTiers(result);
}
QString trackerText(const core::TorrentIntakeTrackerFields& fields) {
  auto tiers=fields.tiers;bool found=false;
  for(const auto& tier:tiers){found|=std::ranges::find(tier,fields.announce)!=tier.end();}
  if(!fields.announce.empty()&&!fields.announce.starts_with("dht://")&&!found){tiers.insert(tiers.begin(),{fields.announce});}
  QStringList groups;for(const auto& tier:tiers){QStringList urls;for(const auto& url:tier){urls.push_back(QString::fromStdString(url));}if(!urls.empty()){groups.push_back(urls.join(u'\n'));}}
  return groups.join(QStringLiteral("\n\n"));
}
quint64 nextIntakeRequest() noexcept {
  // GUI-thread only, process-wide so destroying/reopening a dialog never reuses
  // a still-retained Host preview token in this or another Window.
  static quint64 next=0;
  return next==std::numeric_limits<quint64>::max()?0:++next;
}
}
class IntakeFileModel final:public QAbstractTableModel {
public:
  explicit IntakeFileModel(QObject* parent):QAbstractTableModel(parent){}
  TorrentIntakePresentation source;
  std::vector<compat::FilePolicy> draft;
  void apply(TorrentIntakePresentation value) {
    QPointer<IntakeFileModel> guard(this);beginResetModel();if(!guard){return;}
    source=std::move(value);draft.clear();
    if(source){for(const auto& row:source->files){draft.push_back({row.padding,0});}}
    endResetModel();
  }
  int rowCount(const QModelIndex& parent={})const override{return parent.isValid()||!source?0:static_cast<int>(source->files.size());}
  int columnCount(const QModelIndex& parent={})const override{return parent.isValid()?0:4;}
  QVariant headerData(int section,Qt::Orientation direction,int role)const override {
    if(direction!=Qt::Horizontal||role!=Qt::DisplayRole||section<0||section>=4){return {};}
    const QStringList labels{tr("Download"),tr("Path"),tr("Size (bytes)"),tr("Priority")};return labels[section];
  }
  QVariant data(const QModelIndex& at,int role)const override {
    if(!at.isValid()||at.row()<0||at.row()>=rowCount()||at.column()<0||at.column()>=4){return {};}
    const auto n=static_cast<std::size_t>(at.row());const auto& row=source->files[n];
    if(role==Qt::UserRole+1){return row.index;}
    if(at.column()==0&&role==Qt::CheckStateRole){return draft[n].skipped?Qt::Unchecked:Qt::Checked;}
    if(role!=Qt::DisplayRole&&role!=Qt::AccessibleTextRole){return {};}
    switch(at.column()){
      case 0:return row.padding?tr("Padding"):QString{};
      case 1:return row.path;
      case 2:return row.bytes;
      case 3:return draft[n].priority>0?tr("High"):draft[n].priority<0?tr("Low"):tr("Normal");
    }
    return {};
  }
  Qt::ItemFlags flags(const QModelIndex& at)const override {
    if(!at.isValid()||at.row()<0||at.row()>=rowCount()){return Qt::NoItemFlags;}
    auto result=Qt::ItemIsEnabled|Qt::ItemIsSelectable;
    if(at.column()==0&&!source->files[static_cast<std::size_t>(at.row())].padding){result|=Qt::ItemIsUserCheckable;}
    return result;
  }
  bool setData(const QModelIndex& at,const QVariant& value,int role)override {
    if(role!=Qt::CheckStateRole||at.column()!=0||at.row()<0||at.row()>=rowCount()
        ||source->files[static_cast<std::size_t>(at.row())].padding
        ||(value.toInt()!=Qt::Checked&&value.toInt()!=Qt::Unchecked)){return false;}
    auto& row=draft[static_cast<std::size_t>(at.row())];row.skipped=value.toInt()!=Qt::Checked;
    if(row.skipped){row.priority=0;}
    emit dataChanged(index(at.row(),0),index(at.row(),3));return true;
  }
  void priority(const QModelIndexList& indices,int value) {
    int first=rowCount(),last=-1;
    for(const auto& at:indices){if(at.row()<0||at.row()>=rowCount()){continue;}
      const auto n=static_cast<std::size_t>(at.row());
      if(!source->files[n].padding&&!draft[n].skipped){draft[n].priority=value;first=std::min(first,at.row());last=std::max(last,at.row());}}
    if(last>=0){emit dataChanged(index(first,3),index(last,3));}
  }
  qint64 selectedBytes()const {
    qint64 result=0;if(!source){return result;}
    for(std::size_t n=0;n<draft.size();++n){if(!draft[n].skipped&&!source->files[n].padding){result+=source->files[n].bytes;}}
    return result;
  }
};
TorrentIntakeDialog::TorrentIntakeDialog(QWidget* parent):QDialog(parent) {
  setObjectName(QStringLiteral("torrent-import-dialog"));setWindowTitle(tr("Open Torrent File"));
  auto* layout=new QVBoxLayout(this);auto* form=new QFormLayout;
  auto* sourceRow=new QWidget(this);auto* sourceLayout=new QHBoxLayout(sourceRow);sourceLayout->setContentsMargins(0,0,0,0);
  source_=new QLineEdit(sourceRow);source_->setObjectName(QStringLiteral("import-source"));source_->setMaxLength(4096);source_->setAccessibleName(tr("Torrent file"));
  auto* sourceBrowse=new QPushButton(tr("Browse…"),sourceRow);sourceBrowse->setObjectName(QStringLiteral("import-browse-torrent"));
  load_=new QPushButton(tr("Load files"),sourceRow);load_->setObjectName(QStringLiteral("import-load-files"));
  sourceLayout->addWidget(source_,1);sourceLayout->addWidget(sourceBrowse);sourceLayout->addWidget(load_);
  form->addRow(tr("Torrent file:"),sourceRow);
  auto* destinationRow=new QWidget(this);auto* destinationLayout=new QHBoxLayout(destinationRow);destinationLayout->setContentsMargins(0,0,0,0);
  destination_=new QLineEdit(destinationRow);destination_->setObjectName(QStringLiteral("import-destination"));destination_->setMaxLength(4096);destination_->setAccessibleName(tr("Download directory"));
  auto* destinationBrowse=new QPushButton(tr("Browse…"),destinationRow);destinationBrowse->setObjectName(QStringLiteral("import-browse-destination"));
  destinationLayout->addWidget(destination_,1);destinationLayout->addWidget(destinationBrowse);
  form->addRow(tr("Download directory:"),destinationRow);layout->addLayout(form);
  table_=new QTableView(this);table_->setObjectName(QStringLiteral("import-file-table"));table_->setAccessibleName(tr("Files to download"));
  model_=new IntakeFileModel(this);table_->setModel(model_);table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::ExtendedSelection);table_->setAlternatingRowColors(true);table_->verticalHeader()->hide();
  table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
  for(int column:{0,2,3}){table_->horizontalHeader()->setSectionResizeMode(column,QHeaderView::ResizeToContents);}
  layout->addWidget(table_,1);
  auto* priorities=new QHBoxLayout;priorities->addWidget(new QLabel(tr("Selected files:"),this));
  priority_=new QComboBox(this);priority_->setObjectName(QStringLiteral("import-file-priority"));
  priority_->addItem(tr("Normal"),0);priority_->addItem(tr("High"),1);priority_->addItem(tr("Low"),-1);
  auto* applyPriority=new QPushButton(tr("Set priority"),this);applyPriority->setObjectName(QStringLiteral("import-apply-priority"));
  priorities->addWidget(priority_);priorities->addWidget(applyPriority);priorities->addStretch();layout->addLayout(priorities);
  startMode_=new QComboBox(this);startMode_->setObjectName(QStringLiteral("import-start-mode"));startMode_->setAccessibleName(tr("Start mode"));
  startMode_->addItem(tr("Queued"),static_cast<int>(TorrentIntakeStartMode::Queued));
  startMode_->addItem(tr("Stopped"),static_cast<int>(TorrentIntakeStartMode::Stopped));
  startMode_->addItem(tr("Paused"),static_cast<int>(TorrentIntakeStartMode::Paused));
  startMode_->addItem(tr("Forced"),static_cast<int>(TorrentIntakeStartMode::Forced));
  startMode_->setToolTip(tr("Queued follows queue rules. Paused waits for Resume. Forced bypasses ordinary queue limits."));
  auto* startRow=new QFormLayout;startRow->addRow(tr("Start mode:"),startMode_);layout->addLayout(startRow);
  connect(startMode_,&QComboBox::currentIndexChanged,this,[this]{++revision_;schedule();});
  sequential_=new QCheckBox(tr("Download pieces in order"),this);sequential_->setObjectName(QStringLiteral("import-sequential"));
  sequential_->setAccessibleName(tr("Sequential download"));sequential_->setToolTip(tr("Request pieces in order. Existing saved torrent preferences are kept."));
  layout->addWidget(sequential_);connect(sequential_,&QCheckBox::toggled,this,[this]{++revision_;schedule();});
  auto* limits=new QFormLayout;
  const auto rate=[this](const QString& name){
    auto* box=new QSpinBox(this);box->setObjectName(name);box->setRange(0,static_cast<int>(core::MaximumTorrentIntakeRate/1024));
    box->setSuffix(tr(" KiB/s"));box->setSpecialValueText(tr("Unlimited"));box->setToolTip(tr("This torrent only. 1 KiB = 1024 bytes; zero is unlimited."));
    connect(box,&QSpinBox::valueChanged,this,[this]{++revision_;schedule();});return box;
  };
  downloadLimit_=rate(QStringLiteral("import-download-limit"));uploadLimit_=rate(QStringLiteral("import-upload-limit"));
  downloadLimit_->setAccessibleName(tr("Torrent download limit"));uploadLimit_->setAccessibleName(tr("Torrent upload limit"));
  limits->addRow(tr("Download limit:"),downloadLimit_);limits->addRow(tr("Upload limit:"),uploadLimit_);layout->addLayout(limits);
  editTrackers_=new QCheckBox(tr("Edit tracker tiers"),this);editTrackers_->setObjectName(QStringLiteral("import-edit-trackers"));layout->addWidget(editTrackers_);
  trackers_=new QPlainTextEdit(this);trackers_->setObjectName(QStringLiteral("import-trackers"));trackers_->setAccessibleName(tr("Tracker tiers"));
  trackers_->setPlaceholderText(tr("One tracker URL per line; separate tiers with an empty line."));trackers_->setMaximumHeight(92);trackers_->setReadOnly(true);
  trackers_->setToolTip(tr("HTTP, HTTPS or UDP URLs. Empty removes trackers; this does not enable DHT. Unchecked keeps the torrent's original tracker fields."));layout->addWidget(trackers_);
  connect(editTrackers_,&QCheckBox::toggled,this,[this]{++revision_;schedule();});
  connect(trackers_,&QPlainTextEdit::textChanged,this,[this]{++revision_;schedule();});
  status_=new QLabel(this);status_->setObjectName(QStringLiteral("import-file-status"));status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);layout->addWidget(status_);
  auto* buttons=new QDialogButtonBox(QDialogButtonBox::Open|QDialogButtonBox::Cancel,this);add_=buttons->button(QDialogButtonBox::Open);add_->setObjectName(QStringLiteral("import-confirm"));layout->addWidget(buttons);
  connect(buttons,&QDialogButtonBox::accepted,this,&TorrentIntakeDialog::accept);connect(buttons,&QDialogButtonBox::rejected,this,&TorrentIntakeDialog::reject);
  connect(load_,&QPushButton::clicked,this,&TorrentIntakeDialog::requestPreview);
  connect(source_,&QLineEdit::textChanged,this,[this]{if(!committing_){preview_.reset();loading_=false;failure_.clear();}++revision_;schedule();});
  connect(destination_,&QLineEdit::textChanged,this,[this]{schedule();});
  connect(model_,&QAbstractItemModel::dataChanged,this,[this]{schedule();},Qt::QueuedConnection);
  connect(sourceBrowse,&QPushButton::clicked,this,[this]{
    QPointer<TorrentIntakeDialog> guard(this);
    const auto selected=QFileDialog::getOpenFileName(this,tr("Open Torrent File"),{},tr("Torrent files (*.torrent)"));
    if(guard&&source_&&!selected.isEmpty()){source_->setText(selected);}
  });
  connect(destinationBrowse,&QPushButton::clicked,this,[this]{
    QPointer<TorrentIntakeDialog> guard(this);
    const auto selected=QFileDialog::getExistingDirectory(this,tr("Download directory"),destination_?destination_->text():QString{});
    if(guard&&destination_&&!selected.isEmpty()){destination_->setText(selected);}
  });
  connect(applyPriority,&QPushButton::clicked,this,[this]{
    if(!busy_&&!pending()&&model_&&table_&&table_->selectionModel()&&priority_){
      model_->priority(table_->selectionModel()->selectedRows(),priority_->currentData().toInt());
    }
  });
  resize(800,680);schedule();
}
bool TorrentIntakeDialog::beginSession(quint64 session,QString directory) {
  if(!session||session<session_||committing_){return false;}
  QPointer<TorrentIntakeDialog> guard(this);
  const auto current=[&]{return guard&&session_==session;};
  if(session!=session_){
    session_=session;request_={};preview_.reset();loading_=false;failure_.clear();++revision_;
    if(downloadLimit_){downloadLimit_->setValue(0);}if(!current()){return false;}
    if(uploadLimit_){uploadLimit_->setValue(0);}if(!current()){return false;}
    if(sequential_){sequential_->setChecked(false);}if(!current()){return false;}
    if(startMode_&&startMode_->currentIndex()!=0){
      // Qt6.12 emits indexChanged before finishing private text/accessibility
      // work. Deliver notifications only once that setter stack has returned.
      QPointer<QComboBox> mode=startMode_;const auto oldText=mode->currentText();const bool wasBlocked=mode->signalsBlocked();
      {QSignalBlocker blocked(mode.data());mode->setCurrentIndex(0);if(!mode){blocked.dismiss();}}
      if(!current()||!mode||startMode_!=mode){return false;}
      const int index=mode->currentIndex();const auto text=mode->currentText();const auto revision=revision_;
      const auto same=[&]{return current()&&mode&&startMode_==mode&&mode->currentIndex()==index&&mode->currentText()==text;};
      if(!wasBlocked){
        emit mode->currentIndexChanged(index);
        if(!same()||revision_!=revision+1){return false;}
        if(text!=oldText){emit mode->currentTextChanged(text);if(!same()||revision_!=revision+1){return false;}}
      }
    }
    if(!current()){return false;}
    if(editTrackers_){editTrackers_->setChecked(false);}if(!current()){return false;}
    if(!setTrackerDraftText({})||!current()){return false;}
  }
  if(destination_&&destination_->text().isEmpty()){destination_->setText(directory);}
  if(!current()){return false;}schedule();return true;
}
void TorrentIntakeDialog::invalidateSession() {
  session_=0;loading_=false;committing_=false;request_={};preview_.reset();failure_.clear();++revision_;
  QDialog::reject();
}
void TorrentIntakeDialog::setBusy(bool busy){busy_=busy;schedule();}
void TorrentIntakeDialog::requestPreview() {
  if(!session_||busy_||pending()||!source_||!metadataPath(source_->text())||nextRequest_==std::numeric_limits<quint64>::max()){return;}
  const auto next=nextIntakeRequest();
  if(!next){nextRequest_=std::numeric_limits<quint64>::max();schedule();return;}
  nextRequest_=next;request_={session_,next,source_->text()};loading_=true;preview_.reset();failure_.clear();++revision_;schedule();
  emit previewRequested(request_);
}
void TorrentIntakeDialog::setPreview(TorrentIntakePresentation packet) {
  if(!loading_||!packet||packet->source!=request_||packet->source.session!=session_||!source_||source_->text()!=request_.metadata){return;}
  loading_=false;++revision_;
  if(!validTorrentIntake(packet)){preview_.reset();failure_=packet->failure.isEmpty()?tr("The torrent file list could not be loaded."):packet->failure;}
  else{preview_=std::make_shared<const TorrentIntakePacket>(*packet);failure_.clear();}
  if(!resetTrackerDraft()){return;}schedule();
}
bool TorrentIntakeDialog::setTrackerDraftText(const QString& value) {
  const auto session=session_;const auto frame=preview_;QPointer<TorrentIntakeDialog> guard(this);QPointer<QPlainTextEdit> editor=trackers_;
  if(!editor){return false;}const bool changed=editor->toPlainText()!=value,wasBlocked=editor->signalsBlocked();
  if(!changed){return true;}
  {QSignalBlocker blocked(editor.data());editor->setPlainText(value);if(!editor){blocked.dismiss();}}
  const auto same=[&]{return guard&&session_==session&&preview_==frame&&editor&&trackers_==editor&&editor->toPlainText()==value;};
  if(!same()){return false;}
  const auto revision=revision_;if(changed&&!wasBlocked){emit editor->textChanged();if(!same()||revision_!=revision+1){return false;}}
  return same();
}
bool TorrentIntakeDialog::resetTrackerDraft() {
  const auto frame=preview_;const auto session=session_;QPointer<TorrentIntakeDialog> guard(this);
  const auto current=[&]{return guard&&session_==session&&preview_==frame;};
  const auto text=frame&&frame->trackers?trackerText(*frame->trackers):QString{};
  if(editTrackers_){editTrackers_->setChecked(false);}if(!current()||!editTrackers_){return false;}
  return current()&&setTrackerDraftText(text);
}
void TorrentIntakeDialog::rejectPreview(const TorrentIntakeRequest& request) {
  if(!loading_||request!=request_){return;}loading_=false;failure_=tr("The file list could not be loaded. Try again when the client is ready.");++revision_;schedule();
}
void TorrentIntakeDialog::rejectSubmission(const TorrentIntakeSubmission& submission) {
  if(!committing_||!preview_||submission.session!=session_||submission.request!=request_.request||submission.revision!=preview_->revision){return;}
  committing_=false;failure_=tr("The torrent could not be added. Your file choices were kept.");++revision_;schedule();
}
void TorrentIntakeDialog::setResult(TorrentIntakeResultPresentation packet) {
  if(!committing_||!packet||!preview_||packet->session!=session_||packet->request!=request_.request||packet->revision!=preview_->revision){return;}
  committing_=false;++revision_;
  if(packet->imported){QDialog::accept();return;}
  failure_=packet->failure.isEmpty()?tr("The torrent could not be added. Your file choices were kept."):packet->failure;schedule();
}
void TorrentIntakeDialog::schedule() {
  if(queued_){return;}queued_=true;
  QMetaObject::invokeMethod(this,[this]{queued_=false;render();},Qt::QueuedConnection);
}
void TorrentIntakeDialog::render() {
  const auto revision=revision_;const auto frame=preview_;QPointer<TorrentIntakeDialog> guard(this);
  const auto current=[&]{return guard&&revision_==revision&&preview_==frame&&source_&&destination_&&startMode_
      &&status_&&load_&&add_&&table_&&model_&&priority_&&downloadLimit_&&uploadLimit_&&sequential_&&editTrackers_&&trackers_;};
  if(model_&&model_->source!=frame){model_->apply(frame);if(!current()){return;}}
  if(!source_||!destination_||!startMode_||!status_||!load_||!add_||!table_||!model_||!priority_||!downloadLimit_||!uploadLimit_||!sequential_||!editTrackers_||!trackers_){return;}
  source_->setEnabled(!busy_&&!pending());if(!current()||!source_){return;}
  destination_->setEnabled(!busy_&&!committing_);if(!current()||!destination_){return;}
  table_->setEnabled(!busy_&&!pending());if(!current()||!table_){return;}
  priority_->setEnabled(!busy_&&!pending()&&bool(frame));if(!current()||!priority_){return;}
  startMode_->setEnabled(!busy_&&!pending()&&(!frame||model_->selectedBytes()!=0));if(!current()||!startMode_){return;}
  sequential_->setEnabled(!busy_&&!pending());if(!current()||!sequential_){return;}
  downloadLimit_->setEnabled(!busy_&&!pending());if(!current()||!downloadLimit_){return;}
  uploadLimit_->setEnabled(!busy_&&!pending());if(!current()||!uploadLimit_){return;}
  const bool canEditTrackers=frame&&frame->trackers.has_value();
  editTrackers_->setEnabled(!busy_&&!pending()&&canEditTrackers);if(!current()||!editTrackers_){return;}
  trackers_->setEnabled(!busy_&&!pending()&&canEditTrackers);if(!current()||!trackers_){return;}
  trackers_->setReadOnly(!canEditTrackers||!editTrackers_->isChecked());if(!current()||!trackers_){return;}
  core::TorrentIntakeTrackerTiers trackerValues;
  const bool validTrackers=!editTrackers_->isChecked()||(canEditTrackers&&trackerDraft(trackers_->toPlainText(),trackerValues));
  load_->setEnabled(session_&&!busy_&&!pending()&&metadataPath(source_->text())&&nextRequest_!=std::numeric_limits<quint64>::max());if(!current()||!load_){return;}
  const bool valid=frame&&model_->source==frame&&frame->source.metadata==source_->text();
  add_->setEnabled(session_&&!busy_&&!pending()&&valid&&validTrackers&&path(destination_->text()));if(!current()||!add_){return;}
  QString status=failure_;
  if(status.isEmpty()){
    status=!validTrackers?tr("Enter valid HTTP, HTTPS or UDP tracker URLs (up to 16 tiers and 64 URLs).")
        :loading_?tr("Loading torrent files…"):committing_?tr("Adding torrent…")
        :!frame?tr("Choose a torrent file and load its files."):model_->selectedBytes()==0
        ?tr("No files selected. The torrent will be added stopped.")
        :tr("%1 bytes selected. Existing files are checked before downloading.").arg(model_->selectedBytes());
  }
  if(frame&&!frame->trackers){status+=tr("\nOriginal tracker fields are kept unchanged; this editor cannot represent their format or size.");}
  status_->setText(status.left(4096));
}
void TorrentIntakeDialog::accept() {
  if(!session_||busy_||pending()||!source_||!destination_||!startMode_||!model_||!preview_||!downloadLimit_||!uploadLimit_||!sequential_||!editTrackers_||!trackers_
      ||model_->source!=preview_||preview_->source.metadata!=source_->text()||!path(destination_->text())){return;}
  bool validMode=false;const auto mode=startMode_->currentData().toInt(&validMode);
  if(!validMode||mode<static_cast<int>(TorrentIntakeStartMode::Queued)||mode>static_cast<int>(TorrentIntakeStartMode::Forced)){return;}
  const bool selected=model_->selectedBytes()!=0;
  const bool force=selected&&mode==static_cast<int>(TorrentIntakeStartMode::Forced);
  const bool paused=selected&&mode==static_cast<int>(TorrentIntakeStartMode::Paused);
  const bool start=selected&&(force||mode==static_cast<int>(TorrentIntakeStartMode::Queued));
  std::optional<core::TorrentIntakeTrackerTiers> trackers;
  if(editTrackers_->isChecked()){if(!preview_->trackers){return;}trackers.emplace();if(!trackerDraft(trackers_->toPlainText(),*trackers)){return;}}
  TorrentIntakeSubmission submission{session_,request_.request,preview_->revision,destination_->text(),start,model_->draft,
      {static_cast<std::int64_t>(downloadLimit_->value())*1024,static_cast<std::int64_t>(uploadLimit_->value())*1024},sequential_->isChecked(),force,paused,std::move(trackers)};
  committing_=true;failure_.clear();++revision_;schedule();emit submitted(std::move(submission));
}
void TorrentIntakeDialog::reject() {
  if(committing_){return;}
  const auto cancelled=request_;const bool cancel=loading_;loading_=false;request_={};preview_.reset();++revision_;
  QPointer<TorrentIntakeDialog> guard(this);
  if(cancel){emit previewCancelled(cancelled);}
  if(guard){QDialog::reject();}
}
} // namespace biglybtc::ui
