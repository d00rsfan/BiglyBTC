// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include "biglybtc/saved_download_history.hpp"
#include <chrono>
#include <array>
#include <span>
#include <stop_token>
namespace biglybtc::engine {class PersistentSession;}
namespace biglybtc::storage {
class DownloadHistoryStore;
// One explicit Reset, from the actual idle normal owner + complete held saved
// library/state set. Never an intake/newness event or payload/network grant.
class HistoryReconstructionReceipt final {
public:
  HistoryReconstructionReceipt(HistoryReconstructionReceipt&&)noexcept;
  HistoryReconstructionReceipt& operator=(HistoryReconstructionReceipt&&)noexcept;
  ~HistoryReconstructionReceipt();
  HistoryReconstructionReceipt(const HistoryReconstructionReceipt&)=delete;
  HistoryReconstructionReceipt& operator=(const HistoryReconstructionReceipt&)=delete;
private:
  struct Data;std::unique_ptr<Data> data_;
  explicit HistoryReconstructionReceipt(std::unique_ptr<Data>);
  static std::expected<HistoryReconstructionReceipt,Error> issue(const ProfileLease&,
      std::array<std::uint64_t,3> actualOwner,std::stop_token,std::chrono::steady_clock::time_point);
  std::expected<void,Error> current(const ProfileLease&,bool exactEpoch)const;
  std::span<const core::DownloadHistoryNewRecord> records()const;
  friend class DownloadHistoryStore;
  friend class biglybtc::engine::PersistentSession;
};
} // namespace biglybtc::storage
