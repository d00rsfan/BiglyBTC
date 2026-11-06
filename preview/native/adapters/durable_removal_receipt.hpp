// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include "captured_removal_tags.hpp"
#include "biglybtc/library.hpp"
#include "biglybtc/saved_download_history.hpp"
#include <chrono>
#include <stop_token>
#include <array>
#include <span>

namespace biglybtc::storage {
class DownloadHistoryStore;
class PreparedFileOperation;
struct FileOperationResult;
struct FileOperationConfirmation;
enum class FileOperationStage;
struct RemovalHistoryOutcome;
// Private, single-use witness of a fresh successful metadata removal. Neither
// a saved row, UI request, completed checkpoint nor journal bytes can issue one.
// This has no payload, network, restore or generic history-append authority.
class DurableRemovalReceipt final {
public:
  DurableRemovalReceipt(DurableRemovalReceipt&&)noexcept;
  DurableRemovalReceipt& operator=(DurableRemovalReceipt&&)noexcept;
  ~DurableRemovalReceipt();
  DurableRemovalReceipt(const DurableRemovalReceipt&)=delete;
  DurableRemovalReceipt& operator=(const DurableRemovalReceipt&)=delete;
private:
  struct Data;std::unique_ptr<Data> data_;
  explicit DurableRemovalReceipt(std::unique_ptr<Data>);
  struct Postimage {std::string name;std::optional<std::string_view> bytes;
    std::optional<std::array<std::uint64_t,11>> metadata;};
  static std::expected<DurableRemovalReceipt,Error> issue(const ProfileLease&,
      const core::SavedDownload&,std::string_view acceptedBeforeState,
      std::span<const Postimage>,std::array<std::uint64_t,2> activeDirectory,
      std::int64_t eventMillis,std::chrono::steady_clock::time_point eventEnd,
      CapturedRemovalTags*);
  std::expected<void,Error> current(const ProfileLease&,bool exactEpoch)const;
  const core::DownloadHistoryNewRecord& record()const;
  bool monitored()const;
  const std::optional<Error>& tagWarning()const;
  friend class DownloadHistoryStore;
  friend std::expected<FileOperationResult,Error> applyFileOperation(const ProfileLease&,
      PreparedFileOperation&&,const FileOperationConfirmation&,
      void(*)(FileOperationStage),std::stop_token,RemovalHistoryOutcome*,CapturedRemovalTags*);
};
// Optional sink filled only AFTER actual fresh apply/journal cleanup success.
// Auxiliary issuance/history failure never turns that success into rollback.
struct RemovalHistoryOutcome {
  std::optional<DurableRemovalReceipt> receipt;
  std::optional<Error> warning;
  std::optional<Error> tagWarning; // Auxiliary: move remains valid with existing tags.
};
} // namespace biglybtc::storage
