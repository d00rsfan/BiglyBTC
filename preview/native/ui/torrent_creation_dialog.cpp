#include "torrent_creation_dialog.hpp"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QMetaObject>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <span>

namespace biglybtc::ui {
namespace {
// Preserve every original v1 choice; BEP52 formats allow only powers of two.
constexpr std::array<quint64,21> PieceSizes{32768,49152,65536,98304,131072,196608,262144,393216,
    524288,786432,1048576,1572864,2097152,3145728,4194304,8388608,16777216,33554432,67108864,134217728,268435456};
constexpr std::array<quint64,14> MerklePieceSizes{16384,32768,65536,131072,262144,524288,1048576,2097152,4194304,
    8388608,16777216,33554432,67108864,134217728};
constexpr quint64 MaximumBytes=quint64{1}<<44;
std::optional<quint64> nextRequest() {
  static std::atomic<quint64> next{1};auto current=next.load(std::memory_order_relaxed);
  while(current!=std::numeric_limits<quint64>::max()) {
    if(next.compare_exchange_weak(current,current+1,std::memory_order_relaxed)){return current;}
  }
  return {};
}
bool path(const QString& value) {return !value.isEmpty()&&value.size()<=32768&&!value.contains(QChar::Null)&&QDir::isAbsolutePath(value);}
bool hash(const QString& value) {
  if(value.size()!=40){return false;}for(const auto c:value){const auto n=c.unicode();if(!((n>='0'&&n<='9')||(n>='a'&&n<='f')||(n>='A'&&n<='F'))){return false;}}return true;
}
}
struct TorrentCreationDialog::Impl {
  TorrentCreationDialog* q;
  quint64 session=0,highest=0,revision=0,version=0,sequence=0,completed=0,total=0;
  bool busy=false,working=false,closing=false,piecesQueued=false;
  TorrentCreationRequest active;
  QString publishedOutput;
  QPointer<QComboBox> kind,piece,format;
  QPointer<QLineEdit> source,output,sourceTag;
  QPointer<QPlainTextEdit> trackers,comment,urlSeeds,httpSeeds;
  QPointer<QCheckBox> privateTorrent,import,dhtBackup,extraHashes;
  QPointer<QPushButton> sourceBrowse,outputBrowse,create,cancel;
  QPointer<QProgressBar> progress;
  QPointer<QLabel> status;
  QPointer<QFileDialog> picker;
  struct Guard {QPointer<TorrentCreationDialog> self;quint64 version;bool current()const{return self&&self->impl_->version==version;}};
  Guard guard()const{return {q,version};}
  bool bump(){if(version==std::numeric_limits<quint64>::max()){return false;}++version;return true;}
  bool text(const QString& value){const auto alive=guard();if(status){status->setText(value);}return alive.current();}
  bool controls(){
    const auto alive=guard();const bool editable=session&&!busy&&!working;
    const std::vector<QPointer<QWidget>> fields{kind.data(),piece.data(),format.data(),source.data(),output.data(),trackers.data(),comment.data(),privateTorrent.data(),import.data(),sourceTag.data(),urlSeeds.data(),httpSeeds.data(),dhtBackup.data(),extraHashes.data(),sourceBrowse.data(),outputBrowse.data()};
    for(const auto& field:fields){if(field){field->setEnabled(editable);}if(!alive.current()){return false;}}
    if(create){create->setEnabled(editable&&output&&(publishedOutput.isEmpty()||output->text()!=publishedOutput));}if(!alive.current()){return false;}
    if(cancel){cancel->setEnabled(!closing);}return alive.current();
  }
  bool pieceChoices(){
    if(!piece||!format){return false;}const auto alive=guard();const auto selected=piece->currentData().toULongLong();
    const bool merkle=format->currentIndex()!=0;QSignalBlocker blocked(piece);
    const auto valid=[&]{if(!alive.current()||!piece){blocked.dismiss();return false;}return true;};
    piece->clear();if(!valid()){return false;}
    piece->addItem(QStringLiteral("Automatic"),QVariant::fromValue(quint64{0}));if(!valid()){return false;}
    const auto choices=merkle?std::span<const quint64>(MerklePieceSizes):std::span<const quint64>(PieceSizes);
    for(const auto size:choices){piece->addItem(QStringLiteral("%1 KiB").arg(size/1024),QVariant::fromValue(size));if(!valid()){return false;}}
    const auto index=piece->findData(QVariant::fromValue(selected));piece->setCurrentIndex(index>=0?index:0);return valid();
  }
  std::optional<TorrentCreationOptions> options()const {
    if(!kind||!piece||!format||!source||!output||!trackers||!comment||!privateTorrent||!import||!sourceTag||!urlSeeds||!httpSeeds||!dhtBackup||!extraHashes
        ||kind->currentIndex()<0||kind->currentIndex()>1||piece->currentIndex()<0||format->currentIndex()<0||format->currentIndex()>2
        ||!path(source->text())||!path(output->text())||comment->toPlainText().toUtf8().size()>16384){return {};}
    TorrentCreationOptions value;value.sourceKind=static_cast<TorrentCreationSource>(kind->currentIndex());value.source=source->text();value.output=output->text();
    value.format=static_cast<TorrentCreationFormat>(format->currentIndex());
    value.pieceSize=piece->currentData().toULongLong();value.privateTorrent=privateTorrent->isChecked();value.comment=comment->toPlainText();value.importAfterCreation=import->isChecked();
    value.sourceTag=sourceTag->text();value.dhtBackup=dhtBackup->isChecked();
    value.extraHashes=extraHashes->isChecked()&&value.format!=TorrentCreationFormat::V2;
    if(value.sourceTag.toUtf8().size()>16384||value.sourceTag.contains(QChar::Null)) return {};
    std::size_t seedCount=0;
    for(const auto& pair:{std::pair{urlSeeds.data(),&value.urlSeeds},std::pair{httpSeeds.data(),&value.httpSeeds}}) {
      if(pair.first->toPlainText().size()>131072) return {};
      for(const auto& raw:pair.first->toPlainText().split(QChar('\n'))) {
        const auto text=raw.trimmed();if(text.isEmpty()) continue;
        const QUrl url(text,QUrl::StrictMode);
        if(++seedCount>64||text.toUtf8().size()>2048||!url.isValid()||url.host().isEmpty()
            ||(url.scheme()!=QStringLiteral("http")&&url.scheme()!=QStringLiteral("https"))) return {};
        for(const auto c:text) if(c.isSpace()||c.unicode()<32||c.unicode()==127) return {};
        pair.second->push_back(text);
      }
    }
    const auto choices=value.format==TorrentCreationFormat::V1?std::span<const quint64>(PieceSizes):std::span<const quint64>(MerklePieceSizes);
    if(value.pieceSize&&std::find(choices.begin(),choices.end(),value.pieceSize)==choices.end()){return {};}
    if(trackers->toPlainText().size()>131072){return {};}
    QStringList tier;std::size_t count=0;
    for(const auto& raw:trackers->toPlainText().split(QChar('\n'))){
      const auto tracker=raw.trimmed();
      if(tracker.isEmpty()){if(!tier.isEmpty()){value.trackerTiers.push_back(std::move(tier));tier={};}continue;}
      if(++count>64||tracker.toUtf8().size()>2048){return {};}
      for(const auto c:tracker){if(c.isSpace()||c.unicode()<32||c.unicode()==127){return {};}}
      const QUrl url(tracker,QUrl::StrictMode);const auto scheme=url.scheme();
      if(!url.isValid()||url.host().isEmpty()||(scheme!=QStringLiteral("http")&&scheme!=QStringLiteral("https")&&scheme!=QStringLiteral("udp"))){return {};}
      tier.push_back(tracker);
    }
    if(!tier.isEmpty()){value.trackerTiers.push_back(std::move(tier));}
    if(value.trackerTiers.size()>16){return {};}return value;
  }
  void choose(bool chooseOutput){
    if(picker||working||!kind){return;}const auto alive=guard();
    auto* dialog=new QFileDialog(q,chooseOutput?QStringLiteral("Choose new torrent output"):QStringLiteral("Choose torrent source"));picker=dialog;dialog->setAttribute(Qt::WA_DeleteOnClose);
    if(chooseOutput){dialog->setAcceptMode(QFileDialog::AcceptSave);dialog->setFileMode(QFileDialog::AnyFile);dialog->setNameFilter(QStringLiteral("Torrent metadata (*.torrent)"));dialog->setOption(QFileDialog::DontConfirmOverwrite);}
    else{dialog->setFileMode(kind->currentIndex()==1?QFileDialog::Directory:QFileDialog::ExistingFile);}
    QObject::connect(dialog,&QFileDialog::fileSelected,q,[alive,chooseOutput](const QString& selected){
      if(!alive.current()){return;}const auto field=chooseOutput?alive.self->impl_->output:alive.self->impl_->source;if(field){field->setText(selected);}
    });dialog->open();
  }
};
TorrentCreationDialog::TorrentCreationDialog(QWidget* parent):QDialog(parent),impl_(std::make_unique<Impl>()) {
  impl_->q=this;setObjectName(QStringLiteral("torrent-creation-dialog"));setWindowTitle(QStringLiteral("Create torrent"));resize(760,630);
  auto* layout=new QVBoxLayout(this);auto* form=new QFormLayout;
  impl_->kind=new QComboBox(this);impl_->kind->setObjectName(QStringLiteral("torrent-creation-source-kind"));impl_->kind->addItems({QStringLiteral("Single file"),QStringLiteral("Directory")});form->addRow(QStringLiteral("Source type"),impl_->kind);
  auto fileRow=[&](const QString& name,QPointer<QLineEdit>& field,QPointer<QPushButton>& button){
    auto* row=new QWidget(this);auto* line=new QHBoxLayout(row);line->setContentsMargins(0,0,0,0);field=new QLineEdit(row);field->setObjectName(name);field->setMaxLength(32768);
    button=new QPushButton(QStringLiteral("Browse…"),row);button->setObjectName(name+QStringLiteral("-browse"));line->addWidget(field);line->addWidget(button);return row;
  };
  form->addRow(QStringLiteral("Source"),fileRow(QStringLiteral("torrent-creation-source"),impl_->source,impl_->sourceBrowse));
  form->addRow(QStringLiteral("Output .torrent"),fileRow(QStringLiteral("torrent-creation-output"),impl_->output,impl_->outputBrowse));
  impl_->format=new QComboBox(this);impl_->format->setObjectName(QStringLiteral("torrent-creation-format"));
  impl_->format->addItems({QStringLiteral("V1"),QStringLiteral("V2"),QStringLiteral("Hybrid (V1 + V2)")});form->addRow(QStringLiteral("Format"),impl_->format);
  impl_->piece=new QComboBox(this);impl_->piece->setObjectName(QStringLiteral("torrent-creation-piece-size"));impl_->piece->addItem(QStringLiteral("Automatic"),QVariant::fromValue(quint64{0}));
  for(const auto size:PieceSizes){impl_->piece->addItem(QStringLiteral("%1 KiB").arg(size/1024),QVariant::fromValue(size));}form->addRow(QStringLiteral("Piece size"),impl_->piece);
  impl_->trackers=new QPlainTextEdit(this);impl_->trackers->setObjectName(QStringLiteral("torrent-creation-trackers"));impl_->trackers->setPlaceholderText(QStringLiteral("One tracker URL per line. Separate tiers with a blank line."));impl_->trackers->setMaximumHeight(130);form->addRow(QStringLiteral("Tracker tiers"),impl_->trackers);
  impl_->privateTorrent=new QCheckBox(QStringLiteral("Private torrent"),this);impl_->privateTorrent->setObjectName(QStringLiteral("torrent-creation-private"));form->addRow(QString{},impl_->privateTorrent);
  impl_->comment=new QPlainTextEdit(this);impl_->comment->setObjectName(QStringLiteral("torrent-creation-comment"));impl_->comment->setMaximumHeight(90);form->addRow(QStringLiteral("Comment"),impl_->comment);
  impl_->sourceTag=new QLineEdit(this);impl_->sourceTag->setObjectName(QStringLiteral("torrent-creation-source-tag"));impl_->sourceTag->setMaxLength(16384);form->addRow(QStringLiteral("Source tag"),impl_->sourceTag);
  for(const auto& pair:{std::pair{&impl_->urlSeeds,QStringLiteral("URL seeds")},std::pair{&impl_->httpSeeds,QStringLiteral("HTTP seeds")}}) {
    *pair.first=new QPlainTextEdit(this);(*pair.first)->setMaximumHeight(70);
    (*pair.first)->setObjectName(pair.first==&impl_->urlSeeds?QStringLiteral("torrent-creation-url-seeds"):QStringLiteral("torrent-creation-http-seeds"));
    (*pair.first)->setPlaceholderText(QStringLiteral("One HTTP(S) URL per line"));form->addRow(pair.second,*pair.first);
  }
  impl_->dhtBackup=new QCheckBox(QStringLiteral("Permit DHT backup"),this);impl_->dhtBackup->setObjectName(QStringLiteral("torrent-creation-dht-backup"));impl_->dhtBackup->setChecked(true);form->addRow(QString{},impl_->dhtBackup);
  impl_->extraHashes=new QCheckBox(QStringLiteral("Include SHA1 and ED2K hashes (V1 / hybrid)"),this);impl_->extraHashes->setObjectName(QStringLiteral("torrent-creation-extra-hashes"));form->addRow(QString{},impl_->extraHashes);
  impl_->import=new QCheckBox(QStringLiteral("Add to My Torrents (stopped) after successful creation"),this);impl_->import->setObjectName(QStringLiteral("torrent-creation-import"));form->addRow(QString{},impl_->import);auto* fields=new QWidget(this);fields->setLayout(form);
  auto* scroll=new QScrollArea(this);scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setWidget(fields);layout->addWidget(scroll);
  auto* note=new QLabel(QStringLiteral("Creates the selected torrent format without modifying source data. Existing output files are never overwritten."),this);note->setTextFormat(Qt::PlainText);note->setWordWrap(true);layout->addWidget(note);
  impl_->progress=new QProgressBar(this);impl_->progress->setObjectName(QStringLiteral("torrent-creation-progress"));impl_->progress->setRange(0,10000);impl_->progress->setValue(0);impl_->progress->setTextVisible(false);layout->addWidget(impl_->progress);
  impl_->status=new QLabel(this);impl_->status->setObjectName(QStringLiteral("torrent-creation-status"));impl_->status->setTextFormat(Qt::PlainText);impl_->status->setWordWrap(true);layout->addWidget(impl_->status);
  auto* buttons=new QHBoxLayout;buttons->addStretch();impl_->create=new QPushButton(QStringLiteral("Create"),this);impl_->create->setObjectName(QStringLiteral("torrent-creation-create"));impl_->cancel=new QPushButton(QStringLiteral("Cancel"),this);impl_->cancel->setObjectName(QStringLiteral("torrent-creation-cancel"));buttons->addWidget(impl_->create);buttons->addWidget(impl_->cancel);layout->addLayout(buttons);
  connect(impl_->create,&QPushButton::clicked,this,&TorrentCreationDialog::accept);connect(impl_->cancel,&QPushButton::clicked,this,&TorrentCreationDialog::reject);
  connect(impl_->sourceBrowse,&QPushButton::clicked,this,[this]{impl_->choose(false);});connect(impl_->outputBrowse,&QPushButton::clicked,this,[this]{impl_->choose(true);});
  connect(impl_->format,&QComboBox::currentIndexChanged,this,[this]{
    if(!impl_->bump()||impl_->piecesQueued){return;}impl_->piecesQueued=true;
    QMetaObject::invokeMethod(this,[this]{impl_->piecesQueued=false;impl_->pieceChoices();},Qt::QueuedConnection);
  });
  connect(impl_->output,&QLineEdit::textChanged,this,[this]{impl_->controls();});impl_->controls();
}
TorrentCreationDialog::~TorrentCreationDialog()=default;
bool TorrentCreationDialog::beginSession(quint64 session,quint64 revision) {
  if(!session||!revision||session<=impl_->highest||!impl_->bump()){return false;}const auto alive=impl_->guard();impl_->session=impl_->highest=session;impl_->revision=revision;
  impl_->working=impl_->closing=false;impl_->active={};impl_->sequence=impl_->completed=impl_->total=0;impl_->publishedOutput.clear();
  if(!impl_->text(QStringLiteral("Choose the source and a new output file."))||!impl_->controls()){return false;}return alive.current();
}
void TorrentCreationDialog::invalidateSession() {
  if(!impl_->bump()){return;}const auto alive=impl_->guard();impl_->session=0;impl_->working=false;impl_->active={};
  const auto picker=impl_->picker;if(picker){picker->close();}if(alive.current()){done(QDialog::Rejected);}
}
bool TorrentCreationDialog::pending()const noexcept{return impl_->working;}
void TorrentCreationDialog::setBusy(bool busy){if(!impl_->bump()){return;}impl_->busy=busy;impl_->controls();}
bool TorrentCreationDialog::setProgress(TorrentCreationProgressPresentation packet) {
  if(!packet||!impl_->working||packet->session!=impl_->active.session||packet->request!=impl_->active.request||packet->revision!=impl_->active.revision
      ||!packet->sequence||packet->sequence<=impl_->sequence||packet->completedBytes>packet->totalBytes||packet->totalBytes>MaximumBytes
      ||packet->completedBytes<impl_->completed||(impl_->total&&packet->totalBytes!=impl_->total)||!impl_->bump()){return false;}
  const auto alive=impl_->guard();impl_->sequence=packet->sequence;impl_->completed=packet->completedBytes;impl_->total=packet->totalBytes;
  if(impl_->progress){impl_->progress->setRange(0,packet->totalBytes?10000:0);}if(!alive.current()){return false;}
  if(impl_->progress&&packet->totalBytes){impl_->progress->setValue(static_cast<int>(packet->completedBytes*10000/packet->totalBytes));}if(!alive.current()){return false;}
  if(!impl_->closing){return impl_->text(packet->totalBytes?QStringLiteral("%1 of %2 source bytes processed").arg(packet->completedBytes).arg(packet->totalBytes):QStringLiteral("Preparing source data…"));}return true;
}
bool TorrentCreationDialog::setResult(TorrentCreationResultPresentation packet) {
  if(!packet||!impl_->working||packet->session!=impl_->active.session||packet->request!=impl_->active.request||packet->revision!=impl_->active.revision
      ||(packet->completed&&packet->cancelled)||(!packet->completed&&!packet->cancelled&&packet->failure.isEmpty())
      ||((packet->completed||packet->outputPublished)&&(!path(packet->output)||packet->output!=impl_->active.options.output))
      ||(packet->completed&&!hash(packet->hash))||!impl_->bump()){return false;}
  const auto alive=impl_->guard();const bool close=impl_->closing;impl_->working=false;
  if(packet->completed||packet->outputPublished){impl_->publishedOutput=packet->output;}
  QString message=packet->completed?QStringLiteral("Torrent created: %1\nInfo hash: %2").arg(packet->output,packet->hash):packet->cancelled?QStringLiteral("Torrent creation cancelled."):packet->failure;
  if(packet->outputPublished&&!packet->completed){message=QStringLiteral("The output file was published; do not retry this output.\n%1\n%2").arg(packet->output,packet->failure);}
  if(impl_->progress){impl_->progress->setRange(0,10000);}if(!alive.current()||!impl_->text(message)||!impl_->controls()||!alive.current()){return false;}
  if(packet->completed||close){done(packet->completed?QDialog::Accepted:QDialog::Rejected);}return true;
}
void TorrentCreationDialog::accept(){
  if(!impl_->session||impl_->busy||impl_->working){return;}
  const auto options=impl_->options();if(!options){impl_->text(QStringLiteral("Check the absolute paths, supported tracker URLs, tiers and comment length."));return;}
  if(!impl_->publishedOutput.isEmpty()&&options->output==impl_->publishedOutput){impl_->text(QStringLiteral("Choose a different new output file."));return;}
  const auto identity=nextRequest();if(!identity||!impl_->bump()){return;}impl_->active={impl_->session,*identity,impl_->revision,*options};impl_->working=true;impl_->closing=false;impl_->sequence=impl_->completed=impl_->total=0;
  const auto alive=impl_->guard();if(impl_->progress){impl_->progress->setRange(0,0);}if(!alive.current()||!impl_->text(QStringLiteral("Preparing torrent…"))||!impl_->controls()||!alive.current()){return;}emit createRequested(impl_->active);
}
void TorrentCreationDialog::reject(){
  if(!impl_->working){done(QDialog::Rejected);return;}if(impl_->closing||!impl_->bump()){return;}impl_->closing=true;const auto alive=impl_->guard();const auto request=impl_->active;
  if(!impl_->text(QStringLiteral("Cancelling; waiting for the creator…"))||!impl_->controls()||!alive.current()){return;}emit cancelRequested(request);
}
void TorrentCreationDialog::closeEvent(QCloseEvent* event){event->ignore();reject();}
} // namespace biglybtc::ui
