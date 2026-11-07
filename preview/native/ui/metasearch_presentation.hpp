// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QByteArray>
#include "torrent_intake_presentation.hpp"
#include <QMetaType>
#include <QString>
#include <memory>
#include <optional>
#include <stop_token>
#include <vector>
namespace biglybtc::ui {
struct MetasearchEngineKey {quint64 sourceIndex=0;qint64 id=0;QByteArray uid;bool operator==(const MetasearchEngineKey&)const=default;};
struct MetasearchCatalogKey {quint64 session=0,page=0,request=0;bool operator==(const MetasearchCatalogKey&)const=default;};
struct MetasearchEngineChoice {MetasearchEngineKey key;QString name,reason;bool enabled=false,queryable=false;};
struct MetasearchCatalogPacket {MetasearchCatalogKey key;quint64 generation=0;bool normalNetwork=false,truncated=false;std::vector<MetasearchEngineChoice> engines;QString failure;};
using MetasearchCatalogPresentation=std::shared_ptr<const MetasearchCatalogPacket>;
struct MetasearchQueryKey {quint64 session=0,page=0,request=0,catalogGeneration=0;bool operator==(const MetasearchQueryKey&)const=default;};
struct MetasearchQueryCommand {MetasearchQueryKey key;QString query;std::vector<MetasearchEngineKey> engines;std::shared_ptr<std::stop_source> receiver;};
struct MetasearchResultKey {quint64 queryNonce=0,rowId=0;bool operator==(const MetasearchResultKey&)const=default;};
enum class MetasearchState {Started,Results,Ready,Rejected,Cancelled};
enum class MetasearchEngineState {Queued,Searching,Ready,Failed,Cancelled};
struct MetasearchEngineProgress {MetasearchEngineKey key;QString name,message;MetasearchEngineState state=MetasearchEngineState::Queued;quint64 results=0;};
struct MetasearchResultRow {MetasearchResultKey key;MetasearchEngineKey engine;QString source,title,publication,link,details,downloadReason;std::optional<qint64> bytes,seeds,peers;std::optional<double> rank;bool downloadable=false;};
struct MetasearchPacket {MetasearchQueryKey key;quint64 sequence=0,queryNonce=0;MetasearchState state=MetasearchState::Rejected;std::vector<MetasearchEngineProgress> engines;std::vector<MetasearchResultRow> rows;bool truncated=false;QString message;};
using MetasearchPresentation=std::shared_ptr<const MetasearchPacket>;
struct MetasearchDownloadKey {MetasearchQueryKey query;MetasearchResultKey result;quint64 request=0;bool operator==(const MetasearchDownloadKey&)const=default;};
struct MetasearchDownloadCommand {MetasearchDownloadKey key;std::shared_ptr<std::stop_source> receiver;};
struct MetasearchMagnetReview {quint64 revision=0;QString identity,name,source,query;};
struct MetasearchMagnetSubmission {MetasearchDownloadKey key;quint64 revision=0;QString destination;TorrentIntakeNetwork network=TorrentIntakeNetwork::Public;};
enum class MetasearchDownloadState {Started,ReviewReady,MagnetReviewReady,Finished,NeedsReview,Rejected,Cancelled};
struct MetasearchDownloadPacket {MetasearchDownloadKey key;MetasearchDownloadState state=MetasearchDownloadState::Rejected;QString message;TorrentIntakePresentation review;std::optional<MetasearchMagnetReview> magnetReview;bool magnetCommitted=false,magnetAdmitted=false,magnetUncertain=false;QString pendingHash;};
using MetasearchDownloadPresentation=std::shared_ptr<const MetasearchDownloadPacket>;
inline constexpr std::size_t MaximumMetasearchEngines=256,MaximumMetasearchQueryEngines=8,MaximumMetasearchRows=1000,MaximumMetasearchChars=2048,MaximumMetasearchQueryBytes=1024;
}
Q_DECLARE_METATYPE(biglybtc::ui::MetasearchCatalogKey)
Q_DECLARE_METATYPE(biglybtc::ui::MetasearchCatalogPresentation)
Q_DECLARE_METATYPE(biglybtc::ui::MetasearchQueryKey)
Q_DECLARE_METATYPE(biglybtc::ui::MetasearchQueryCommand)
Q_DECLARE_METATYPE(biglybtc::ui::MetasearchPresentation)
Q_DECLARE_METATYPE(biglybtc::ui::MetasearchResultKey)
Q_DECLARE_METATYPE(biglybtc::ui::MetasearchDownloadKey)
Q_DECLARE_METATYPE(biglybtc::ui::MetasearchDownloadCommand)
Q_DECLARE_METATYPE(biglybtc::ui::MetasearchDownloadPresentation)

Q_DECLARE_METATYPE(biglybtc::ui::MetasearchMagnetSubmission)
