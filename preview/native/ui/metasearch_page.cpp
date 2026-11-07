// SPDX-License-Identifier: GPL-2.0-or-later
#include "metasearch_page.hpp"
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <ranges>
#include <QStringList>
#include <limits>
#include <set>
namespace biglybtc::ui {
namespace {
class SortItem final:public QTableWidgetItem {
public:
 explicit SortItem(QString text,std::optional<qint64> number={}):QTableWidgetItem(std::move(text)),integer_(number){}
 SortItem(QString text,double number):QTableWidgetItem(std::move(text)),real_(number){}
 bool operator<(const QTableWidgetItem& other)const override {
  const auto* value=dynamic_cast<const SortItem*>(&other);if(!value){return QTableWidgetItem::operator<(other);}
  const bool known=integer_.has_value()||real_.has_value(),otherKnown=value->integer_.has_value()||value->real_.has_value();
  if(known!=otherKnown){
   // Qt reverses operator< for descending order. Missing numbers remain
   // missing and are placed after known values in either sort direction.
   const bool descending=tableWidget()&&tableWidget()->horizontalHeader()->sortIndicatorOrder()==Qt::DescendingOrder;
   return descending?!known:known;
  }if(integer_&&value->integer_){return *integer_<*value->integer_;}if(real_&&value->real_){return *real_<*value->real_;}
  return text().compare(value->text(),Qt::CaseInsensitive)<0;
 }
private:std::optional<qint64> integer_;std::optional<double> real_;
};
QString number(std::optional<qint64> value){return value?QString::number(*value):QStringLiteral("—");}
QString state(MetasearchEngineState value){switch(value){case MetasearchEngineState::Queued:return QObject::tr("Waiting");case MetasearchEngineState::Searching:return QObject::tr("Searching");case MetasearchEngineState::Ready:return QObject::tr("Complete");case MetasearchEngineState::Failed:return QObject::tr("Failed");case MetasearchEngineState::Cancelled:return QObject::tr("Cancelled");}return {};}
}
MetasearchPage::MetasearchPage(QWidget* parent):QWidget(parent){
 setObjectName("classic-metasearch-page");auto* outer=new QVBoxLayout(this);auto* query=new QHBoxLayout;
 queryText_=new QLineEdit(this);queryText_->setObjectName("metasearch-query");queryText_->setPlaceholderText(tr("Search torrents"));queryText_->setMaxLength(1024);
 submit_=new QPushButton(tr("Search"),this);submit_->setObjectName("metasearch-submit");cancel_=new QPushButton(tr("Cancel Search"),this);cancel_->setObjectName("metasearch-cancel");refresh_=new QPushButton(tr("Reload Engines"),this);refresh_->setObjectName("metasearch-refresh");query->addWidget(queryText_,1);query->addWidget(submit_);query->addWidget(cancel_);query->addWidget(refresh_);outer->addLayout(query);
 auto* filters=new QHBoxLayout;filterText_=new QLineEdit(this);filterText_->setObjectName("metasearch-filter");filterText_->setPlaceholderText(tr("Filter result names"));minimum_=new QSpinBox(this);maximum_=new QSpinBox(this);minimum_->setObjectName("metasearch-min-size");maximum_->setObjectName("metasearch-max-size");for(auto* box:{minimum_,maximum_}){box->setRange(0,std::numeric_limits<int>::max());box->setSpecialValueText(tr("Any"));box->setSuffix(tr(" MiB"));}filters->addWidget(filterText_,1);filters->addWidget(new QLabel(tr("Minimum:"),this));filters->addWidget(minimum_);filters->addWidget(new QLabel(tr("Maximum:"),this));filters->addWidget(maximum_);outer->addLayout(filters);
 auto* split=new QSplitter(this);engines_=new QListWidget(split);engines_->setObjectName("metasearch-engines");engines_->setMinimumWidth(180);table_=new QTableWidget(split);table_->setObjectName("metasearch-results");table_->setColumnCount(8);table_->setHorizontalHeaderLabels({tr("Name"),tr("Size (bytes)"),tr("Seeds"),tr("Peers"),tr("Source"),tr("Rank"),tr("Published"),tr("Link")});table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setSelectionMode(QAbstractItemView::SingleSelection);table_->horizontalHeader()->setStretchLastSection(true);table_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);table_->setSortingEnabled(true);sortResults(2,Qt::DescendingOrder);split->addWidget(engines_);split->addWidget(table_);split->setStretchFactor(1,1);outer->addWidget(split,1);
 auto* actions=new QHBoxLayout;downloadButton_=new QPushButton(tr("Download…"),this);downloadButton_->setObjectName("metasearch-download");downloadCancel_=new QPushButton(tr("Cancel Download"),this);downloadCancel_->setObjectName("metasearch-download-cancel");copy_=new QPushButton(tr("Copy Link"),this);copy_->setObjectName("metasearch-copy-link");details_=new QPushButton(tr("Details…"),this);details_->setObjectName("metasearch-details");for(auto* button:{downloadButton_,downloadCancel_,copy_,details_}){actions->addWidget(button);}actions->addStretch();outer->addLayout(actions);selectionStatus_=new QLabel(this);selectionStatus_->setObjectName("metasearch-selection-status");selectionStatus_->setTextFormat(Qt::PlainText);selectionStatus_->setWordWrap(true);outer->addWidget(selectionStatus_);
 engineStatus_=new QLabel(this);engineStatus_->setObjectName("metasearch-engine-status");engineStatus_->setTextFormat(Qt::PlainText);engineStatus_->setWordWrap(true);status_=new QLabel(this);status_->setObjectName("metasearch-status");status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);outer->addWidget(engineStatus_);outer->addWidget(status_);
 connect(submit_,&QPushButton::clicked,this,&MetasearchPage::submit);connect(queryText_,&QLineEdit::returnPressed,this,&MetasearchPage::submit);connect(cancel_,&QPushButton::clicked,this,&MetasearchPage::cancelSearch);connect(refresh_,&QPushButton::clicked,this,&MetasearchPage::refresh);connect(downloadButton_,&QPushButton::clicked,this,&MetasearchPage::download);connect(downloadCancel_,&QPushButton::clicked,this,&MetasearchPage::cancelDownload);connect(copy_,&QPushButton::clicked,this,&MetasearchPage::copyLink);connect(details_,&QPushButton::clicked,this,&MetasearchPage::details);connect(table_,&QTableWidget::itemSelectionChanged,this,&MetasearchPage::controls);connect(queryText_,&QLineEdit::textChanged,this,&MetasearchPage::controls);connect(engines_,&QListWidget::itemChanged,this,[this]{filter();});connect(filterText_,&QLineEdit::textChanged,this,&MetasearchPage::filter);connect(minimum_,&QSpinBox::valueChanged,this,&MetasearchPage::filter);connect(maximum_,&QSpinBox::valueChanged,this,&MetasearchPage::filter);status(tr("Load the saved search engines to begin."));controls();
}
MetasearchPage::~MetasearchPage(){auto d=std::move(download_);auto q=std::move(query_);if(d&&d->receiver){d->receiver->request_stop();}if(q&&q->receiver){q->receiver->request_stop();}}
bool MetasearchPage::bump(){if(revision_==std::numeric_limits<quint64>::max()){available_=false;return false;}++revision_;return true;}
void MetasearchPage::status(QString message){status_->setText(message.left(8192));}
void MetasearchPage::stopOwned(){
 const bool hadQuery=query_.has_value(),hadDownload=download_.has_value();
 MetasearchQueryKey queryKey{};MetasearchDownloadKey downloadKey{};
 std::shared_ptr<std::stop_source> queryReceiver,downloadReceiver;
 if(query_){queryKey=query_->key;queryReceiver=query_->receiver;}
 if(download_){downloadKey=download_->key;downloadReceiver=download_->receiver;}
 query_.reset();download_.reset();searchRunning_=false;nonce_=0;
 QPointer<MetasearchPage> alive(this);const auto revision=revision_;
 if(downloadReceiver){downloadReceiver->request_stop();}if(queryReceiver){queryReceiver->request_stop();}
 if(!alive||revision_!=revision){return;}if(hadDownload){emit downloadCancelRequested(downloadKey);if(!alive){return;}}if(hadQuery){emit cancelRequested(queryKey);}
}
void MetasearchPage::beginSession(quint64 session,quint64 page){
 if(!bump()){return;}const auto revision=revision_;QPointer<MetasearchPage> alive(this);stopOwned();if(!alive||revision_!=revision){return;}session_=session;page_=page;catalog_.reset();catalogRequest_.reset();catalogPending_=false;sequence_=0;rows_.clear();engines_->clear();if(!alive||revision_!=revision){return;}table_->setRowCount(0);if(!alive||revision_!=revision){return;}engineStatus_->clear();if(!alive||revision_!=revision){return;}status(tr("Load the saved search engines to begin."));if(alive&&revision_==revision){controls();}
}
void MetasearchPage::invalidateSession(){beginSession(0,0);}
void MetasearchPage::setAvailable(bool available,bool preview){
 if(!bump()){return;}const auto revision=revision_;QPointer<MetasearchPage> alive(this);available_=available;const bool revoke=preview&&!preview_;preview_=preview;if(revoke){stopOwned();if(!alive||revision_!=revision){return;}}controls();
}
void MetasearchPage::refresh(){
 if(!available_||!session_||!page_){return;}if(!bump()){return;}const auto revision=revision_;QPointer<MetasearchPage> alive(this);stopOwned();if(!alive||revision_!=revision){return;}rows_.clear();table_->setRowCount(0);if(!alive||revision_!=revision){return;}catalogPending_=true;catalogRequest_=MetasearchCatalogKey{session_,page_,0};status(tr("Reading saved search engines…"));if(!alive||revision_!=revision){return;}controls();if(alive&&revision_==revision){emit catalogRequested(*catalogRequest_);}
}
void MetasearchPage::setCatalogPending(MetasearchCatalogKey key){if(key.session!=session_||key.page!=page_||!key.request){return;}if(!bump()){return;}catalogRequest_=key;catalogPending_=true;controls();}
void MetasearchPage::refuseCatalog(MetasearchCatalogKey key){if(!catalogRequest_||key!=*catalogRequest_){return;}if(!bump()){return;}catalogPending_=false;catalogRequest_.reset();QPointer<MetasearchPage> alive(this);status(tr("The engine read could not start. Reload engines after the current operation."));if(alive){controls();}}
void MetasearchPage::setCatalog(MetasearchCatalogPresentation packet){
 if(!packet||!catalogRequest_||packet->key!=*catalogRequest_||packet->key.session!=session_||packet->key.page!=page_){return;}if(!bump()){return;}const auto revision=revision_;QPointer<MetasearchPage> alive(this);catalogRequest_.reset();catalogPending_=false;catalog_=packet;engines_->clear();if(!alive||revision_!=revision){return;}std::size_t checked=0;
 for(std::size_t n=0;n<std::min(packet->engines.size(),MaximumMetasearchEngines);++n){const auto& engine=packet->engines[n];auto* item=new QListWidgetItem(engines_);if(!alive||revision_!=revision){return;}item->setData(Qt::UserRole,static_cast<qulonglong>(n));if(!alive||revision_!=revision){return;}item->setText(engine.name.left(MaximumMetasearchChars)+(engine.reason.isEmpty()?QString{}:QStringLiteral(" — ")+engine.reason.left(MaximumMetasearchChars)));if(!alive||revision_!=revision){return;}const bool eligible=engine.enabled&&engine.queryable;if(eligible){item->setFlags(item->flags()|Qt::ItemIsUserCheckable);if(!alive||revision_!=revision){return;}item->setCheckState(checked++<MaximumMetasearchQueryEngines?Qt::Checked:Qt::Unchecked);}else{item->setFlags(item->flags()&~Qt::ItemIsEnabled);}if(!alive||revision_!=revision){return;}}
 status(!packet->failure.isEmpty()?packet->failure:preview_||!packet->normalNetwork?tr("Preview lists saved engines. Search and download require a normal session."):packet->truncated?tr("The first saved engines are shown. Select up to eight supported engines."):tr("Select up to eight enabled engines, then enter a search."));if(alive&&revision_==revision){controls();}
}
std::vector<MetasearchEngineKey> MetasearchPage::checkedEngines()const{std::vector<MetasearchEngineKey> out;if(!catalog_){return out;}for(int n=0;n<engines_->count();++n){const auto* item=engines_->item(n);const auto index=item->data(Qt::UserRole).toULongLong();if(item->checkState()==Qt::Checked&&index<catalog_->engines.size()){const auto& value=catalog_->engines[index];if(value.enabled&&value.queryable){out.push_back(value.key);}}}return out;}
void MetasearchPage::submit(){
 if(!available_||preview_||!catalog_||!catalog_->normalNetwork||!catalog_->generation||catalogPending_){return;}const auto engines=checkedEngines();const auto text=queryText_->text().trimmed();if(text.isEmpty()||text.toUtf8().size()>static_cast<qsizetype>(MaximumMetasearchQueryBytes)||text.contains(QChar::Null)||engines.empty()||engines.size()>MaximumMetasearchQueryEngines){return;}
 if(!bump()){return;}const auto revision=revision_;QPointer<MetasearchPage> alive(this);stopOwned();if(!alive||revision_!=revision){return;}MetasearchQueryCommand command{{session_,page_,0,catalog_->generation},text,engines,std::make_shared<std::stop_source>()};query_=command;searchRunning_=true;sequence_=0;nonce_=0;rows_.clear();table_->setRowCount(0);if(!alive||revision_!=revision){command.receiver->request_stop();return;}status(tr("Starting search…"));if(!alive||revision_!=revision){command.receiver->request_stop();return;}controls();if(alive&&revision_==revision){emit searchRequested(std::move(command));}
}
void MetasearchPage::setSearchPending(MetasearchQueryCommand command){if(!query_||command.key.session!=session_||command.key.page!=page_||command.key.catalogGeneration!=query_->key.catalogGeneration||command.receiver!=query_->receiver||!command.key.request){return;}if(!bump()){command.receiver->request_stop();return;}query_=std::move(command);controls();}
void MetasearchPage::refuseSearch(const MetasearchQueryCommand& command){if(!query_||command.receiver!=query_->receiver||command.key.session!=session_||command.key.page!=page_){return;}if(!bump()){return;}const auto revision=revision_;QPointer<MetasearchPage> alive(this);const auto receiver=query_->receiver;searchRunning_=false;receiver->request_stop();if(!alive||revision_!=revision){return;}status(tr("Search could not start. Reload engines and try again."));if(alive){controls();}}
void MetasearchPage::cancelSearch(){if(!query_){return;}if(!bump()){return;}const auto revision=revision_;QPointer<MetasearchPage> alive(this);const auto key=query_->key;const auto receiver=query_->receiver;const auto downloadReceiver=download_?download_->receiver:std::shared_ptr<std::stop_source>{};searchRunning_=false;receiver->request_stop();if(downloadReceiver){downloadReceiver->request_stop();}if(!alive||revision_!=revision){return;}status(tr("Search cancelled. Earlier results remain visible."));if(!alive||revision_!=revision){return;}controls();if(alive&&revision_==revision){emit cancelRequested(key);}}
void MetasearchPage::setSearchResult(MetasearchPresentation packet){
 if(!packet||!query_||packet->key!=query_->key||packet->sequence<=sequence_||packet->key.session!=session_||packet->key.page!=page_){return;}if(query_->receiver->stop_requested()&&packet->state!=MetasearchState::Cancelled){return;}if(!bump()){return;}const auto revision=revision_;QPointer<MetasearchPage> alive(this);sequence_=packet->sequence;nonce_=packet->queryNonce;searchRunning_=packet->state==MetasearchState::Started||packet->state==MetasearchState::Results;rows_.clear();std::set<std::pair<quint64,quint64>> ids;
 for(const auto& row:packet->rows){if(rows_.size()==MaximumMetasearchRows){break;}if(!row.key.queryNonce||row.key.queryNonce!=nonce_||!row.key.rowId||!ids.emplace(row.key.queryNonce,row.key.rowId).second){continue;}rows_.push_back(row);}
 render();if(!alive||revision_!=revision){return;}QStringList engines;for(const auto& engine:packet->engines){engines.push_back(tr("%1: %2 (%3)%4").arg(engine.name.left(256),state(engine.state)).arg(engine.results).arg(engine.message.isEmpty()?QString{}:QStringLiteral(" — ")+engine.message.left(512)));}engineStatus_->setText(engines.join(QStringLiteral("\n")));if(!alive||revision_!=revision){return;}status(packet->message+(packet->truncated?tr(" The displayed result limit was reached."):QString{}));if(alive&&revision_==revision){controls();}
}
void MetasearchPage::sortResults(int column,Qt::SortOrder order){
 if(column<0||column>=table_->columnCount()||(order!=Qt::AscendingOrder&&order!=Qt::DescendingOrder)){return;}
 const auto revision=revision_;QPointer<MetasearchPage> alive(this);
 // Set the ordering source first. Native QTableWidget::sortItems may otherwise
 // start comparisons before updating its header indicator on a direction flip.
 table_->horizontalHeader()->setSortIndicator(column,order);
 if(!alive||revision_!=revision){return;}table_->sortItems(column,order);
}
void MetasearchPage::render(){
 const auto packet=rows_;const auto revision=revision_;QPointer<MetasearchPage> alive(this);table_->setSortingEnabled(false);if(!alive||revision_!=revision){return;}table_->setRowCount(0);if(!alive||revision_!=revision){return;}
 for(const auto& row:packet){const int index=table_->rowCount();table_->insertRow(index);if(!alive||revision_!=revision){return;}std::array<QTableWidgetItem*,8> cells{new SortItem(row.title.left(MaximumMetasearchChars)),new SortItem(number(row.bytes),row.bytes),new SortItem(number(row.seeds),row.seeds),new SortItem(number(row.peers),row.peers),new SortItem(row.source.left(MaximumMetasearchChars)),row.rank?new SortItem(QString::number(*row.rank,'g',6),*row.rank):new SortItem(QStringLiteral("—")),new SortItem(row.publication.left(MaximumMetasearchChars)),new SortItem(row.link.left(MaximumMetasearchChars))};
  cells[0]->setData(Qt::UserRole,QVariant::fromValue(row.key));for(int col=0;col<8;++col){table_->setItem(index,col,cells[static_cast<std::size_t>(col)]);if(!alive||revision_!=revision){for(int pending=col+1;pending<8;++pending){delete cells[static_cast<std::size_t>(pending)];}return;}}
 }
 table_->setSortingEnabled(true);if(alive&&revision_==revision){filter();}
}
std::optional<MetasearchResultRow> MetasearchPage::selected()const{const auto rows=table_->selectionModel()->selectedRows();if(rows.size()!=1||table_->isRowHidden(rows.front().row())){return {};}const auto* cell=table_->item(rows.front().row(),0);if(!cell){return {};}const auto key=cell->data(Qt::UserRole).value<MetasearchResultKey>();const auto row=std::ranges::find(rows_,key,&MetasearchResultRow::key);return row!=rows_.end()?std::optional{*row}:std::nullopt;}
void MetasearchPage::filter(){
 const auto revision=revision_;QPointer<MetasearchPage> alive(this);const auto engines=checkedEngines();const auto text=filterText_->text();const qint64 minimum=qint64(minimum_->value())*1024*1024,maximum=qint64(maximum_->value())*1024*1024;
 for(int n=0;n<table_->rowCount();++n){const auto* cell=table_->item(n,0);if(!cell){continue;}const auto key=cell->data(Qt::UserRole).value<MetasearchResultKey>();const auto row=std::ranges::find(rows_,key,&MetasearchResultRow::key);const bool show=row!=rows_.end()&&row->title.contains(text,Qt::CaseInsensitive)&&std::ranges::find(engines,row->engine)!=engines.end()&&(!minimum||(row->bytes&&*row->bytes>=minimum))&&(!maximum||(row->bytes&&*row->bytes<=maximum));table_->setRowHidden(n,!show);if(!alive||revision_!=revision){return;}}
 controls();
}
void MetasearchPage::controls(){
 const auto revision=revision_;QPointer<MetasearchPage> alive(this);const auto engines=checkedEngines();const auto text=queryText_->text().trimmed();submit_->setEnabled(available_&&!preview_&&catalog_&&catalog_->normalNetwork&&!catalogPending_&&!text.isEmpty()&&text.toUtf8().size()<=static_cast<qsizetype>(MaximumMetasearchQueryBytes)&&!engines.empty()&&engines.size()<=MaximumMetasearchQueryEngines);if(!alive||revision_!=revision){return;}cancel_->setEnabled(query_&&searchRunning_&&!query_->receiver->stop_requested());if(!alive||revision_!=revision){return;}refresh_->setEnabled(available_&&session_&&page_&&!catalogPending_);if(!alive||revision_!=revision){return;}const auto row=selected();const bool live=query_&&!query_->receiver->stop_requested()&&row&&row->key.queryNonce==nonce_;downloadButton_->setEnabled(available_&&!preview_&&catalog_&&catalog_->normalNetwork&&live&&row->downloadable&&!searchRunning_&&!download_);if(!alive||revision_!=revision){return;}downloadCancel_->setEnabled(download_&&!download_->receiver->stop_requested());if(!alive||revision_!=revision){return;}copy_->setEnabled(row&&!row->link.isEmpty());if(!alive||revision_!=revision){return;}details_->setEnabled(row.has_value());if(!alive||revision_!=revision){return;}selectionStatus_->setText(row&&!row->downloadable?row->downloadReason.left(MaximumMetasearchChars):QString{});
}
void MetasearchPage::download(){const auto row=selected();if(!row||!query_||query_->receiver->stop_requested()||!available_||preview_||!catalog_||!catalog_->normalNetwork||!row->downloadable||searchRunning_||download_||row->key.queryNonce!=nonce_){return;}if(!bump()){return;}const auto revision=revision_;QPointer<MetasearchPage> alive(this);MetasearchDownloadCommand command{{query_->key,row->key,0},std::make_shared<std::stop_source>()};download_=command;status(tr("Fetching selected torrent metadata for review…"));if(!alive||revision_!=revision){command.receiver->request_stop();return;}controls();if(alive&&revision_==revision){emit downloadRequested(std::move(command));}}
void MetasearchPage::setDownloadPending(MetasearchDownloadCommand command){if(!download_||command.receiver!=download_->receiver||command.key.query!=download_->key.query||command.key.result!=download_->key.result||!command.key.request){return;}if(!bump()){command.receiver->request_stop();return;}download_=std::move(command);controls();}
void MetasearchPage::refuseDownload(const MetasearchDownloadCommand& command){if(!download_||command.receiver!=download_->receiver){return;}if(!bump()){return;}const auto revision=revision_;QPointer<MetasearchPage> alive(this);const auto receiver=download_->receiver;download_.reset();receiver->request_stop();if(!alive||revision_!=revision){return;}status(tr("The selected download could not start. Search again if its source changed."));if(alive){controls();}}
void MetasearchPage::cancelDownload(){if(!download_){return;}if(!bump()){return;}const auto revision=revision_;QPointer<MetasearchPage> alive(this);const auto key=download_->key;const auto receiver=download_->receiver;receiver->request_stop();if(!alive||revision_!=revision){return;}status(tr("Download cancelled."));if(!alive||revision_!=revision){return;}controls();if(alive&&revision_==revision){emit downloadCancelRequested(key);}}
void MetasearchPage::setDownloadResult(MetasearchDownloadPresentation packet){if(!packet||!download_||packet->key!=download_->key||packet->key.query.session!=session_||packet->key.query.page!=page_){return;}if(!bump()){return;}const auto revision=revision_;QPointer<MetasearchPage> alive(this);const bool stopped=download_->receiver->stop_requested();if(packet->state!=MetasearchDownloadState::Started&&packet->state!=MetasearchDownloadState::ReviewReady){download_.reset();}status(stopped&&packet->state!=MetasearchDownloadState::Finished?tr("Download cancelled."):packet->message);if(alive&&revision_==revision){controls();}}
void MetasearchPage::copyLink(){const auto row=selected();if(row){QApplication::clipboard()->setText(row->link);}}
void MetasearchPage::details(){const auto row=selected();if(!row){return;}auto* dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle(tr("Search Result Details"));auto* layout=new QVBoxLayout(dialog);auto* text=new QPlainTextEdit(dialog);text->setReadOnly(true);text->setPlainText(tr("Name: %1\nSource: %2\nSize: %3\nSeeds: %4\nPeers: %5\nPublished: %6\nLink: %7\nDetails: %8").arg(row->title,row->source,number(row->bytes),number(row->seeds),number(row->peers),row->publication,row->link,row->details));layout->addWidget(text);auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close,dialog);connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::close);layout->addWidget(buttons);dialog->resize(640,360);dialog->open();}
}
