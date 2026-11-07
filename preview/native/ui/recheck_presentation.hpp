// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "library_model.hpp"
#include <QMetaType>
#include <memory>
#include <stop_token>
namespace biglybtc::ui {
struct RecheckKey {quint64 session=0,request=0,revision=0;bool operator==(const RecheckKey&)const=default;};
struct RecheckRequest {RecheckKey key;DownloadTargets targets;std::shared_ptr<std::stop_source> receiver;};
enum class RecheckStage {Retiring,Checking,Saving,Finished,Cancelled,Failed};
struct RecheckPacket {
 RecheckKey key;quint64 sequence=0;Target target;quint64 targetIndex=0,totalTargets=0;
 RecheckStage stage=RecheckStage::Retiring;quint64 scannedBytes=0,totalBytes=0,goodPieces=0,badPieces=0,missingPieces=0,unknownPieces=0;
 bool pieceMapSaved=false,saveUncertain=false,targetFinished=false,terminal=false,resumed=false;
 QString message;
};
using RecheckPresentation=std::shared_ptr<const RecheckPacket>;
}
Q_DECLARE_METATYPE(biglybtc::ui::RecheckKey)
Q_DECLARE_METATYPE(biglybtc::ui::RecheckRequest)
Q_DECLARE_METATYPE(biglybtc::ui::RecheckPresentation)
