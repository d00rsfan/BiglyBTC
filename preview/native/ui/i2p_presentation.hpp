// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QMetaType>
#include <QString>
#include <memory>
namespace biglybtc::ui {
struct I2pPacket {
  quint64 session=0,sequence=0;
  QString state,hash,failure;
  qint64 verifiedBytes=0,payloadDownloaded=-1,payloadUploaded=-1;
  int downloadRate=0,uploadRate=0,peers=0;
  quint64 trackerAnnounces=0,trackerReplies=0,trackerErrors=0;
};
using I2pPresentation=std::shared_ptr<const I2pPacket>;
inline bool acceptI2p(const I2pPresentation& current,const I2pPresentation& next,quint64 session){
  return next&&session&&next->session==session&&next->sequence&&(!current||next->sequence>current->sequence)
    &&next->verifiedBytes>=0&&next->payloadDownloaded>=-1&&next->payloadUploaded>=-1
    &&next->downloadRate>=0&&next->uploadRate>=0&&next->peers>=0;
}
}
Q_DECLARE_METATYPE(biglybtc::ui::I2pPresentation)
