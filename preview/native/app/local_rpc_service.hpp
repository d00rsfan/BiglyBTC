// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "library_model.hpp"
#include "local_session.hpp"
#include "managed_download.hpp"
#include "managed_file_details.hpp"
#include "profile.hpp"
#include "rpc_http.hpp"
#include "rpc_identity_store.hpp"

#include <QEventLoop>
#include <array>

namespace biglybtc::app {

class LocalRpcService final {
public:
  using Library = std::function<ui::Presentation()>;
  struct TransferStatistics {
    qint64 downloadedBytes;
    qint64 uploadedBytes;
    qint64 secondsActive;
  };
  struct CumulativeTransferStatistics {
    TransferStatistics totals;
    std::optional<qint64> filesAdded;
  };
  using CurrentStatistics = std::function<std::optional<TransferStatistics>()>;
  using CumulativeStatistics = std::function<std::optional<CumulativeTransferStatistics>()>;
  using AuthorizeWrite = std::function<bool(const storage::ConfigFile&)>;
  using TorrentCommand = std::function<std::expected<void, storage::Error>(ui::DownloadTargets)>;
  using RateChanges = std::array<std::optional<int>, 2>;
  using SetTorrentRates = std::function<std::expected<void, storage::Error>(ui::DownloadTargets, const RateChanges&)>;
  using ControlDownloads = std::function<std::expected<void, storage::Error>(ui::DownloadTargets, core::DownloadAction)>;
  using AllocateIdentity = std::function<std::expected<qint64, storage::Error>(
      ui::Target, const std::string&, storage::RpcIdentityMember)>;
  using TorrentMetadata = std::function<std::optional<engine::CurrentTorrentDetails>(
      const core::TorrentHash&, engine::CurrentTorrentFileQuery)>;
  using ManagedDownloads = std::function<std::vector<engine::ManagedDownloadStatus>()>;

  LocalRpcService(const storage::ProfileLease&, std::string originalPluginDirectory, Library, CurrentStatistics,
      CumulativeStatistics,
      AuthorizeWrite, TorrentMetadata, ManagedDownloads, ControlDownloads, TorrentCommand, TorrentCommand,
      SetTorrentRates, AllocateIdentity);
  bool start(quint16, plugins::rpc::HttpCredentials);
  void poll();
  std::optional<qint64> captureRemovedTorrentId(const core::TorrentHash&, std::stop_token) const noexcept;
  void noteRemovedTorrent(qint64 id) noexcept;
  quint16 port() const { return listener_.port(); }

private:
  plugins::rpc::DispatchResult dispatch(const plugins::rpc::Call&);
  plugins::rpc::DispatchResult sessionStatistics(const plugins::rpc::Call&) const;
  plugins::rpc::DispatchResult sessionSettings(const plugins::rpc::Call&) const;
  plugins::rpc::DispatchResult writeSessionSettings(const plugins::rpc::Call&) const;
  plugins::rpc::DispatchResult controlTorrents(const plugins::rpc::Call&, core::DownloadAction);
  plugins::rpc::DispatchResult applyTorrentCommand(const plugins::rpc::Call&,
      const TorrentCommand&, QString archivedFailure, qsizetype arguments = 1);
  plugins::rpc::DispatchResult setTorrentRateLimits(const plugins::rpc::Call&);
  std::optional<storage::ConfigFile> authorizedSettings() const;
  std::expected<void, storage::Error> refreshInventory(quint64 revision, bool force);

  const storage::ProfileLease& lease_;
  std::string originalPluginDirectory_;
  Library library_;
  CurrentStatistics currentStatistics_;
  CumulativeStatistics cumulativeStatistics_;
  AuthorizeWrite authorizeWrite_;
  TorrentMetadata metadata_;
  ManagedDownloads managedDownloads_;
  ControlDownloads controlDownloads_;
  TorrentCommand recheckDownloads_;
  TorrentCommand removeDownloads_;
  SetTorrentRates setTorrentRates_;
  AllocateIdentity allocateIdentity_;
  QEventLoop loop_;
  plugins::rpc::LocalRpcHttpServer listener_;
  quint64 revision_ = 0;
  QMap<QString, qint64> persistedIds_;
  QMap<QString, bool> persistedForce_;
  QMap<QString, qint64> persistedAdded_;
  QMap<qint64, QJsonObject> recentRows_;
  std::array<qint64, 256> pendingRemovals_{};
  std::size_t pendingRemovalCount_ = 0;
  std::vector<storage::PersistedRpcIdentity> archived_;
};

} // namespace biglybtc::app
