// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/saved_download_history.hpp"
#include <QMetaType>
#include <QString>
namespace biglybtc::ui {
enum class DownloadHistoryKind {Reload,Erase,Clear,Reset};
enum class DownloadHistorySide {Active,Dead};
enum class DownloadHistoryStatus {Loaded,Saved,Unchanged,Rejected,Cancelled};
enum class DownloadHistoryReadState {Ready,Missing,Failed};
enum class DownloadHistoryReview {None,Candidate,Intent,DuplicateUid};
struct DownloadHistoryCommand {
  quint64 session=0,request=0,generation=0;
  DownloadHistoryKind kind=DownloadHistoryKind::Reload;
  DownloadHistorySide side=DownloadHistorySide::Active;
  qint64 uid=0; // Original signed64 record ID, never a download/provider target.
};
struct DownloadHistoryCommandPacket {
  quint64 session=0,request=0,generation=0;
  DownloadHistoryKind kind=DownloadHistoryKind::Reload;
  DownloadHistorySide side=DownloadHistorySide::Active;
  qint64 uid=0;
  DownloadHistoryStatus status=DownloadHistoryStatus::Rejected;
  DownloadHistoryReadState readState=DownloadHistoryReadState::Failed;
  DownloadHistoryReview review=DownloadHistoryReview::None;
  bool enabled=false,writable=false,changed=false,missingActive=false,missingDead=false;
  bool resetAvailable=false; // Passive hint; worker recaptures complete backend.
  std::shared_ptr<const core::SavedDownloadHistory> active,dead;
  QString failure,refreshFailure;
};
using DownloadHistoryCommandPresentation=std::shared_ptr<const DownloadHistoryCommandPacket>;
// Owning values only: no Store snapshot, lease, descriptor, live torrent or
// redownload permission enters the GUI. Host rechecks its real snapshot/CAS.
} // namespace biglybtc::ui
Q_DECLARE_METATYPE(biglybtc::ui::DownloadHistoryCommand)
Q_DECLARE_METATYPE(biglybtc::ui::DownloadHistoryCommandPresentation)
