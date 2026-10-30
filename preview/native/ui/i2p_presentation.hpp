// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QMetaType>
#include <QString>
#include <memory>
#include <vector>
#include <set>
namespace biglybtc::ui {
struct I2pTorrentPacket {
  QString hash,state,failure;
  qint64 verifiedBytes=0,payloadDownloaded=-1,payloadUploaded=-1;
  int downloadRate=0,uploadRate=0,peers=0;
  bool queued=false;
  quint64 admission=0,generation=0;
  int downloadLimit=0,uploadLimit=0;
  quint32 queueUnavailable=0;
  bool completionPositionUnavailable=false;
};
struct I2pPacket {
  quint64 session=0,sequence=0;
  QString state,hash,failure;
  qint64 verifiedBytes=0,payloadDownloaded=-1,payloadUploaded=-1;
  int downloadRate=0,uploadRate=0,peers=0;
  quint64 trackerAnnounces=0,trackerReplies=0,trackerErrors=0;
  std::vector<I2pTorrentPacket> torrents;
  int globalDownloadLimit=0,globalUploadLimit=0;
  quint32 loopbackPeerClasses=0;
};
using I2pPresentation=std::shared_ptr<const I2pPacket>;
inline bool acceptI2p(const I2pPresentation& current,const I2pPresentation& next,quint64 session){
  if(next){
    if(next->torrents.size()>32){return false;}std::set<QString> seen;
    for(const auto& row:next->torrents){if(row.hash.isEmpty()||!seen.insert(row.hash).second||!row.admission||row.verifiedBytes<0||row.payloadDownloaded< -1||row.payloadUploaded< -1||row.downloadRate<0||row.uploadRate<0||row.peers<0){return false;}}
  }
  return next&&session&&next->session==session&&next->sequence&&(!current||next->sequence>current->sequence)
    &&next->verifiedBytes>=0&&next->payloadDownloaded>=-1&&next->payloadUploaded>=-1
    &&next->downloadRate>=0&&next->uploadRate>=0&&next->peers>=0;
}
}
Q_DECLARE_METATYPE(biglybtc::ui::I2pPresentation)
