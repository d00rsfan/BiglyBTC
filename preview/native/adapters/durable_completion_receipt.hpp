// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "original_torrent_identity.hpp"
#include "biglybtc/saved_download_history.hpp"
#include <chrono>
namespace biglybtc::engine {class PersistentSession;}
namespace biglybtc::storage {
class DownloadHistoryStore;
// A one-shot actual managed-owner completion followed by a durable retired
// checkpoint. Owning metadata proof only, never a torrent/payload permission.
class DurableCompletionReceipt final {
public:
  DurableCompletionReceipt(DurableCompletionReceipt&&)noexcept;
  DurableCompletionReceipt& operator=(DurableCompletionReceipt&&)noexcept;
  ~DurableCompletionReceipt();
  DurableCompletionReceipt(const DurableCompletionReceipt&)=delete;
  DurableCompletionReceipt& operator=(const DurableCompletionReceipt&)=delete;
private:
  enum class Transition {Incomplete,Complete};
  struct Data;std::unique_ptr<Data> data_;
  explicit DurableCompletionReceipt(std::unique_ptr<Data>);
  static std::expected<DurableCompletionReceipt,Error> issue(const ProfileLease&,
      const OriginalTorrentIdentity&,const core::SavedDownload&,
      std::array<std::uint64_t,3> actualOwner,std::uint64_t admission,
      std::string_view acceptedState,std::int64_t eventMillis,Transition,std::int64_t verifiedBytes,
      std::int64_t acceptedReceived,std::int64_t acceptedSent,std::int64_t payloadBytes);
  std::expected<void,Error> current(const ProfileLease&,bool exactEpoch)const;
  const core::DownloadHistoryNewRecord& record()const;
  bool monitored()const;
  friend class DownloadHistoryStore;
  friend class biglybtc::engine::PersistentSession;
};
struct CompletionHistoryOutcome {
  std::optional<DurableCompletionReceipt> receipt;
  std::optional<Error> warning;
};
} // namespace biglybtc::storage
