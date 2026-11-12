// SPDX-License-Identifier: GPL-2.0-or-later
#include "library_window.hpp"
#include "all_peers_page.hpp"
#include <QTabWidget>
namespace biglybtc::ui {
void LibraryWindow::updatePeersVisibility(){const bool active=peersPage_&&mainTabs_->currentWidget()==peersPage_&&columnSession_&&isVisible()&&!isMinimized()&&!closing_;if(active!=peersObserved_){peersObserved_=active;emit peersObservationChanged(active);}}
void LibraryWindow::showAllPeers(){if(closing_||!columnSession_){return;}if(peersPage_&&mainTabs_->indexOf(peersPage_)>=0){mainTabs_->setCurrentWidget(peersPage_);updatePeersVisibility();return;}
 auto* page=new AllPeersPage(columnSession_,mainTabs_);peersPage_=page;connect(page,&AllPeersPage::openTorrent,this,[this](const QString& hash){const auto target=model_->targetAt(model_->rowFor(hash));if(target){openTorrentDetails(*target,0);}});connect(page,&AllPeersPage::refreshRequested,this,[this]{updatePeersVisibility();});mainTabs_->addTab(page,tr("All Peers"));mainTabs_->setCurrentWidget(page);updatePeersVisibility();updateActions();}
void LibraryWindow::setPeersSnapshot(PeerPresentation packet){if(!packet||packet->session!=columnSession_||!peersPage_||closing_){return;}peersPage_->setSnapshot(std::move(packet));}
}
