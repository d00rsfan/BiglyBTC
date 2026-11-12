// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/column_layout.hpp"
#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <memory>
#include <vector>
namespace biglybtc::ui {
struct PeerRow {
 QString key,hash,name,address,localAddress,localInterface,client,peerId,protocol,encryption,source,countryCode,countryName,asn,asnStatus;
 quint64 admission=0;quint16 port=0,localPort=0;
 qint64 received=0,sent=0;int downRate=0,upRate=0,protocolDownRate=0,protocolUpRate=0,state=0,progressPpm=-1,pieces=0,totalPieces=0,rtt=0,downloadRequests=0,uploadRequests=0,queuedBytes=0;
 bool incoming=false,seed=false,interesting=false,choked=false,remoteInterested=false,remoteChoked=false,snubbed=false,optimistic=false;
 QByteArray availability;
};
struct PeerPacket {
 quint64 session=0,sequence=0;std::vector<PeerRow> rows;
 std::size_t admitted=0,downloading=0,seeding=0,queued=0,checking=0,paused=0,failed=0,unavailable=0,prepared=0,preparationTotal=0;
 bool current=true,available=false,truncated=false,asnAllowed=false,countryAvailable=false;
 QString failure;
 std::optional<qint64> sessionReceived,sessionSent;
 std::shared_ptr<const core::SavedTableLayout> layout;
};
using PeerPresentation=std::shared_ptr<const PeerPacket>;
}
Q_DECLARE_METATYPE(biglybtc::ui::PeerPresentation)
