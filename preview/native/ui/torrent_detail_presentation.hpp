// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../engine/torrent_inspection.hpp"
#include "all_peers_presentation.hpp"
#include "library_model.hpp"
#include <QMetaType>
#include <QString>
#include <memory>
namespace biglybtc::ui {
struct TorrentDetailInterest {
 quint64 session=0,request=0;QString hash;
 engine::InspectionSection section=engine::InspectionSection::General;int firstPiece=0;
 bool operator==(const TorrentDetailInterest&)const=default;
};
struct TorrentTrackerCommand {quint64 session=0,request=0,admission=0;Target target;bool scrape=false;};
struct TorrentOptionsCommand {
 quint64 session=0,request=0;Target target;bool rates=true;std::array<int,2> expected{},desired{};
};
struct TorrentDetailPacket {
 TorrentDetailInterest interest;quint64 sequence=0,epoch=0;qint64 observedMillis=0;
 engine::TorrentInspection actual;PeerPresentation peers;QString failure;
 std::shared_ptr<const core::SavedTableLayout> layout;
 std::array<int,4> savedOptions{};bool optionsKnown=false,optionsWritable=false;
};
using TorrentDetailPresentation=std::shared_ptr<const TorrentDetailPacket>;
}
Q_DECLARE_METATYPE(biglybtc::ui::TorrentTrackerCommand)
Q_DECLARE_METATYPE(biglybtc::ui::TorrentOptionsCommand)
Q_DECLARE_METATYPE(biglybtc::ui::TorrentDetailInterest)
Q_DECLARE_METATYPE(biglybtc::ui::TorrentDetailPresentation)
