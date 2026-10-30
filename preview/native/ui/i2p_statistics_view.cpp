// SPDX-License-Identifier: GPL-2.0-or-later
#include "i2p_statistics_view.hpp"
#include <QLabel>
#include <QVBoxLayout>
namespace biglybtc::ui {
I2pStatisticsView::I2pStatisticsView(QWidget* parent):QWidget(parent){
  setObjectName(QStringLiteral("statistics-i2p"));auto* layout=new QVBoxLayout(this);
  for(auto** item:{&state_,&traffic_,&peers_,&trackers_,&failure_}){*item=new QLabel(this);(*item)->setWordWrap(true);(*item)->setTextFormat(Qt::PlainText);layout->addWidget(*item);}
  state_->setObjectName(QStringLiteral("i2p-state"));traffic_->setObjectName(QStringLiteral("i2p-traffic"));
  auto* note=new QLabel(tr("Observed traffic of the current shared I2P session. Each torrent uses the original identity and its own queue slot, limits and saved counters. Measurements cover current admissions and remain separate from saved lifetime totals."),this);
  note->setWordWrap(true);layout->addWidget(note);layout->addStretch();beginSession(0);
}
void I2pStatisticsView::beginSession(quint64 session){session_=session;packet_.reset();state_->setText(tr("I2P is not enabled"));traffic_->clear();peers_->clear();trackers_->clear();failure_->clear();}
bool I2pStatisticsView::setPresentation(I2pPresentation packet){
  if(!acceptI2p(packet_,packet,session_)){return false;}packet_=std::move(packet);const auto& p=*packet_;
  QString state=p.hash.isEmpty()?p.state:p.state+QStringLiteral("\n")+p.hash;
  if(p.torrents.size()>1){for(const auto& row:p.torrents){state+=QStringLiteral("\n\n")+row.hash+QStringLiteral("\n")+row.state+tr(" — %1 / %2 B/s").arg(row.downloadRate).arg(row.uploadRate);if(!row.failure.isEmpty()){state+=QStringLiteral("\n")+row.failure;}}}
  state_->setText(state);
  traffic_->setText(tr("Verified: %1 bytes\nPayload received: %2 bytes; sent: %3 bytes\nDownload: %4 B/s; upload: %5 B/s").arg(p.verifiedBytes).arg(p.payloadDownloaded<0?QString::fromUtf8("—"):QString::number(p.payloadDownloaded)).arg(p.payloadUploaded<0?QString::fromUtf8("—"):QString::number(p.payloadUploaded)).arg(p.downloadRate).arg(p.uploadRate));
  peers_->setText(tr("Connected peers: %1").arg(p.peers));trackers_->setText(tr("Tracker announces: %1; replies: %2; errors: %3").arg(p.trackerAnnounces).arg(p.trackerReplies).arg(p.trackerErrors));failure_->setText(p.failure);return true;
}
}
