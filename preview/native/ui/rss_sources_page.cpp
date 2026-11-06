// SPDX-License-Identifier: GPL-2.0-or-later
#include "rss_sources_page.hpp"
#include <QComboBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <array>
#include <algorithm>

namespace biglybtc::ui {
RssSourcesPage::RssSourcesPage(QWidget* parent):QWidget(parent){
  qRegisterMetaType<RssCatalogRequest>();qRegisterMetaType<RssCatalogPresentation>();qRegisterMetaType<RssFetchKey>();qRegisterMetaType<RssFetchCommand>();qRegisterMetaType<RssFetchPresentation>();
  setObjectName(QStringLiteral("rss-sources-page"));auto* layout=new QVBoxLayout(this);auto* tools=new QHBoxLayout;
  sourcesBox_=new QComboBox(this);sourcesBox_->setObjectName(QStringLiteral("rss-source-selector"));tools->addWidget(sourcesBox_,1);
  refresh_=new QPushButton(tr("Refresh Sources"),this);refresh_->setObjectName(QStringLiteral("rss-refresh-sources"));tools->addWidget(refresh_);
  fetch_=new QPushButton(tr("Fetch"),this);fetch_->setObjectName(QStringLiteral("rss-fetch"));tools->addWidget(fetch_);
  cancel_=new QPushButton(tr("Cancel"),this);cancel_->setObjectName(QStringLiteral("rss-cancel"));tools->addWidget(cancel_);layout->addLayout(tools);
  status_=new QLabel(tr("Refresh to read configured RSS sources."),this);status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);status_->setObjectName(QStringLiteral("rss-status"));layout->addWidget(status_);
  results_=new QTableWidget(0,6,this);results_->setObjectName(QStringLiteral("rss-results"));results_->setHorizontalHeaderLabels({tr("Title"),tr("Size"),tr("Seeds"),tr("Peers"),tr("Published (raw)"),tr("Link (text only)")});
  results_->setEditTriggers(QAbstractItemView::NoEditTriggers);results_->setSelectionBehavior(QAbstractItemView::SelectRows);results_->setSelectionMode(QAbstractItemView::SingleSelection);results_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);layout->addWidget(results_);
  connect(refresh_,&QPushButton::clicked,this,&RssSourcesPage::refreshSources);connect(fetch_,&QPushButton::clicked,this,&RssSourcesPage::fetch);connect(cancel_,&QPushButton::clicked,this,&RssSourcesPage::cancelFetch);
  connect(sourcesBox_,&QComboBox::currentIndexChanged,this,[this]{updateControls();});updateControls();
}
RssSourcesPage::~RssSourcesPage(){if(fetchPending_&&fetchPending_->receiver){fetchPending_->receiver->request_stop();}}
void RssSourcesPage::invalidateSession(){
  ++viewRevision_;const QPointer<RssSourcesPage> alive(this);
  const auto pending=fetchPending_;fetchPending_.reset();catalogPending_.reset();session_=page_=generation_=0;normal_=false;sources_.clear();
  if(pending&&pending->receiver){pending->receiver->request_stop();}updateControls();
  if(alive&&pending){emit cancelRequested(pending->key);} // Final access: receiver may destroy/rebind.
}
void RssSourcesPage::beginSession(quint64 session,quint64 page){
  const auto revision=++viewRevision_;const auto pending=fetchPending_;fetchPending_.reset();catalogPending_.reset();session_=session;page_=page;generation_=0;normal_=false;cancelling_=false;sources_.clear();updating_=true;
  if(pending&&pending->receiver){pending->receiver->request_stop();}QPointer<RssSourcesPage> alive(this);
  sourcesBox_->clear();if(!alive||viewRevision_!=revision){return;}results_->setRowCount(0);if(!alive||viewRevision_!=revision){return;}updating_=false;status_->setText(tr("Refresh to read configured RSS sources."));if(!alive||viewRevision_!=revision){return;}updateControls();
  if(alive&&pending){emit cancelRequested(pending->key);} // State installed before reentry; final access.
}
void RssSourcesPage::setAvailable(bool value,bool preview){available_=value;preview_=preview;updateControls();}
void RssSourcesPage::updateControls(){
  QPointer<RssSourcesPage> alive(this);const auto revision=viewRevision_;
  const auto idle=[this]{return available_&&!updating_&&session_&&page_&&!catalogPending_&&!fetchPending_;};
  refresh_->setEnabled(idle());if(!alive||viewRevision_!=revision){return;}sourcesBox_->setEnabled(idle());if(!alive||viewRevision_!=revision){return;}
  const auto index=sourcesBox_->currentIndex();fetch_->setEnabled(idle()&&generation_&&!preview_&&normal_&&index>=0&&static_cast<std::size_t>(index)<sources_.size()&&sources_[static_cast<std::size_t>(index)].selectable);if(!alive||viewRevision_!=revision){return;}
  cancel_->setEnabled(fetchPending_.has_value()&&!cancelling_);
}
void RssSourcesPage::refreshSources(){if(!available_||!session_||!page_||catalogPending_||fetchPending_){return;}emit catalogRequested({session_,page_,0});}
void RssSourcesPage::setCatalogPending(RssCatalogRequest key){if(key.session!=session_||key.page!=page_){return;}catalogPending_=key;const auto revision=++viewRevision_;QPointer<RssSourcesPage> alive(this);status_->setText(tr("Reading saved sources…"));if(alive&&viewRevision_==revision){updateControls();}}
void RssSourcesPage::fetch(){
  const auto index=sourcesBox_->currentIndex();if(!available_||updating_||!generation_||preview_||!normal_||catalogPending_||fetchPending_||index<0||static_cast<std::size_t>(index)>=sources_.size()||!sources_[static_cast<std::size_t>(index)].selectable){return;}
  RssFetchCommand command{{session_,page_,0,generation_,sources_[static_cast<std::size_t>(index)].key},std::make_shared<std::stop_source>()};
  fetchPending_=command;cancelling_=false;const auto revision=++viewRevision_;QPointer<RssSourcesPage> alive(this);updateControls();if(alive&&viewRevision_==revision){emit fetchRequested(std::move(command));}
}
void RssSourcesPage::setFetchPending(const RssFetchCommand& command){if(command.key.session!=session_||command.key.page!=page_||!command.receiver){return;}fetchPending_=command;cancelling_=command.receiver->stop_requested();const auto revision=++viewRevision_;QPointer<RssSourcesPage> alive(this);status_->setText(cancelling_?tr("Cancelling…"):tr("Fetching selected source…"));if(alive&&viewRevision_==revision){updateControls();}}
void RssSourcesPage::cancelFetch(){
  if(!fetchPending_||cancelling_){return;}const auto command=*fetchPending_;cancelling_=true;if(command.receiver){command.receiver->request_stop();}
  QPointer<RssSourcesPage> alive(this);status_->setText(tr("Cancelling…"));if(!alive){return;}updateControls();if(alive){emit cancelRequested(command.key);}
}
void RssSourcesPage::setCatalog(RssCatalogPresentation packet){
  if(!packet||!catalogPending_||packet->key!=*catalogPending_||packet->key.session!=session_||packet->key.page!=page_){return;}
  const auto revision=++viewRevision_;catalogPending_.reset();normal_=packet->normalNetwork;generation_=packet->generation;updating_=true;
  const bool valid=packet->sources.size()<=MaximumRssSourceRows&&std::ranges::all_of(packet->sources,[](const auto& item){return item.name.size()<=2048&&item.url.size()<=2048&&item.key.uid.size()<=256;});
  if(valid){sources_=packet->sources;}
  else{sources_.clear();normal_=false;}
  QPointer<RssSourcesPage> alive(this);sourcesBox_->clear();if(!alive||viewRevision_!=revision){return;}
  // Iterate the immutable owning packet: synchronous widget signals may replace sources_.
  if(valid){for(const auto& item:packet->sources){sourcesBox_->addItem(item.name+QStringLiteral(" — ")+item.url);if(!alive||viewRevision_!=revision){return;}}}
  updating_=false;status_->setText(!packet->failure.isEmpty()?packet->failure.left(512):sources_.empty()?tr("No configured RSS sources."):preview_||!normal_?tr("Saved sources are read only in this session."):packet->truncated?tr("Showing the first 256 configured RSS sources."):tr("Select a source and choose Fetch. Links are displayed as text only."));if(alive&&viewRevision_==revision){updateControls();}
}
void RssSourcesPage::setFetchResult(RssFetchPresentation packet){
  if(!packet||!fetchPending_||packet->key!=fetchPending_->key||packet->key.session!=session_||packet->key.page!=page_){return;}
  if(packet->state==RssFetchState::Started){if(!cancelling_){status_->setText(tr("Fetching selected source…"));}return;}
  const bool stopped=fetchPending_->receiver&&fetchPending_->receiver->stop_requested();const auto revision=++viewRevision_;fetchPending_.reset();cancelling_=false;
  QPointer<RssSourcesPage> alive(this);
  if(packet->state==RssFetchState::Ready&&!stopped&&packet->rows.size()<=MaximumRssResultRows){
    results_->setRowCount(static_cast<int>(packet->rows.size()));if(!alive||viewRevision_!=revision){return;}
    for(std::size_t row=0;row<packet->rows.size();++row){const auto& value=packet->rows[row];const std::array<QString,6> fields{value.title,value.size,value.seeds,value.peers,value.publication,value.link};
      for(int column=0;column<6;++column){results_->setItem(static_cast<int>(row),column,new QTableWidgetItem(fields[static_cast<std::size_t>(column)].left(static_cast<qsizetype>(MaximumRssDisplayChars))));if(!alive||viewRevision_!=revision){return;}}
    }
  }
  status_->setText(stopped||packet->state==RssFetchState::Cancelled?tr("Cancelled"):packet->message.left(512));if(alive&&viewRevision_==revision){updateControls();}
}
void RssSourcesPage::refuseCatalog(RssCatalogRequest key){auto packet=std::make_shared<RssCatalogPacket>();packet->key=key;packet->failure=tr("Sources could not be read; try Refresh Sources when the current operation finishes.");setCatalog(std::move(packet));}
void RssSourcesPage::refuseFetch(const RssFetchCommand& command){auto packet=std::make_shared<RssFetchPacket>();packet->key=command.key;packet->state=command.receiver&&command.receiver->stop_requested()?RssFetchState::Cancelled:RssFetchState::Rejected;packet->message=tr("Fetch could not start; refresh the saved source before retrying.");setFetchResult(std::move(packet));}
} // namespace biglybtc::ui
