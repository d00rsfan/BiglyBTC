// SPDX-License-Identifier: GPL-2.0-or-later
#include "library_window.hpp"
#include "all_peers_page.hpp"
#include "table_viewport_guard.hpp"
#include <QTabWidget>
#include <algorithm>
namespace biglybtc::ui {
void LibraryWindow::updatePeersVisibility(){updateTorrentDetailInterest();const bool active=peersPage_&&mainTabs_->currentWidget()==peersPage_&&columnSession_&&isVisible()&&!isMinimized()&&!closing_;if(active!=peersObserved_){peersObserved_=active;emit peersObservationChanged(active);}}
void LibraryWindow::showAllPeers(){if(closing_||!columnSession_){return;}if(peersPage_&&mainTabs_->indexOf(peersPage_)>=0){mainTabs_->setCurrentWidget(peersPage_);updatePeersVisibility();return;}
 auto* page=new AllPeersPage(columnSession_,mainTabs_);peersPage_=page;connect(page,&AllPeersPage::openTorrent,this,[this](const QString& hash){const auto target=model_->targetAt(model_->rowFor(hash));if(target){openTorrentDetails(*target,0);}});connect(page,&AllPeersPage::refreshRequested,this,[this]{updatePeersVisibility();});mainTabs_->addTab(page,tr("All Peers"));mainTabs_->setCurrentWidget(page);updatePeersVisibility();updateActions();}
void LibraryWindow::setPeersSnapshot(PeerPresentation packet){if(!packet||packet->session!=columnSession_||!peersPage_||closing_){return;}peersPage_->setSnapshot(std::move(packet));}
void LibraryWindow::updateTorrentDetailInterest(){
 if(!torrentDetailPages_){return;}TorrentDetailInterest next;const auto target=selected();
 const auto* page=mainTabs_->currentWidget();const bool details=page&&!page->property("BiglyBTC.torrentDetailsHash").toString().isEmpty();
 if(columnSession_&&target&&model_->accepts(*target)&&isVisible()&&!isMinimized()&&!closing_&&!offlinePreview_&&(details||(inlineDetails_&&page==torrentsPage_))){next.session=columnSession_;next.hash=target->hash;next.section=torrentDetailPages_->section();next.firstPiece=torrentDetailPages_->firstPiece();}
 if(next.session==torrentDetailInterest_.session&&next.hash==torrentDetailInterest_.hash&&next.section==torrentDetailInterest_.section&&next.firstPiece==torrentDetailInterest_.firstPiece){return;}
 if(torrentDetailInterest_.request==UINT64_MAX){return;}next.request=torrentDetailInterest_.request+1;torrentDetailInterest_=next;latestTorrentDetail_.reset();torrentDetailPages_->setOptionsWritable(false);if(!next.hash.isEmpty()){torrentDetailPages_->beginInterest(next);}emit torrentDetailObservationChanged(next);
}
void LibraryWindow::setTorrentDetailSnapshot(TorrentDetailPresentation packet){
 if(closing_||!packet||packet->interest!=torrentDetailInterest_||packet->interest.session!=columnSession_||!model_->targetAt(model_->rowFor(packet->interest.hash))){return;}
 if(packet->interest.section==engine::InspectionSection::Files){if(packet->layout&&detailFilesLayoutSession_!=columnSession_){applyTorrentTableLayout(filesTable_,*packet->layout,{"name","size","priority","native.download","native.type","done","%","remaining"});detailFilesLayoutSession_=columnSession_;}const TableViewportGuard guard(filesTable_);filesModel_->setLiveProgress(packet->interest.hash,packet->actual.fileCompleted);}
 latestTorrentDetail_=packet;torrentDetailPages_->setOptionsWritable(packet->optionsKnown&&packet->optionsWritable&&!busy_&&!torrentOptionsPending_);
 QStringList tags;
 if(latestManualTags_&&latestManualTags_->session==columnSession_&&latestManualTags_->membership){for(const auto& row:latestManualTags_->membership->rows()){if(std::ranges::any_of(row.resolvedHashes,[&](const auto& hash){return QString::fromStdString(hash.hex())==packet->interest.hash;})){tags.push_back(row.rawName?QString::fromUtf8(*row.rawName):tr("Unnamed tag"));}}}
 torrentDetailPages_->setTags(tags);torrentDetailPages_->apply(std::move(packet));
}

void LibraryWindow::setTorrentOptionsFinished(quint64 session,quint64 request,QString failure){
 if(session!=columnSession_||request!=torrentOptionsPending_){return;}torrentOptionsPending_=0;torrentDetailPages_->optionsFinished(failure);
 if(latestTorrentDetail_){torrentDetailPages_->setOptionsWritable(latestTorrentDetail_->optionsWritable&&!busy_);}
}

void LibraryWindow::setTorrentTrackerFinished(quint64 session,quint64 request,QString failure){if(session!=columnSession_||request!=torrentTrackerPending_){return;}torrentTrackerPending_=0;torrentDetailPages_->trackerFinished(failure);}

}
