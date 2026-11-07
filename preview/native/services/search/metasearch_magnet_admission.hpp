// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "saved_metasearch_result.hpp"
#include "source_bound_magnet.hpp"
namespace biglybtc::search {
struct MetasearchMagnetReview {
  MetasearchResultKey key;
  SavedMetasearchSelector engine;
  std::string query,name,identity;
};
// Worker-owned review/preparation. It performs no persistent write, metadata
// request, engine admission or destination creation until explicit confirm.
class MetasearchMagnetAdmission final {
public:
  static std::expected<std::unique_ptr<MetasearchMagnetAdmission>,storage::Error> prepare(
      const storage::ProfileLease&,const std::filesystem::path&,std::uint64_t,
      const SavedMetasearchMagnetHandle&,const engine::NetworkPolicy&,
      std::stop_token downloadReceiver);
  ~MetasearchMagnetAdmission();
  const MetasearchMagnetReview& description()const noexcept;
  const core::TorrentHash& hash()const noexcept;
  // Rejected means no save began. Uncertain requires recovery/review and never
  // a blind retry. Committed remains committed after query/cancel changes;
  // actual metadata admission is a separate Host operation with its own error.
  engine::PendingMagnetCommitResult confirm(storage::ProfileLease&,std::uint64_t,
      const std::filesystem::path& destination,const engine::NetworkPolicy&,
      std::stop_token dispatchStop);
private:
  struct Impl;explicit MetasearchMagnetAdmission(std::unique_ptr<Impl>)noexcept;
  std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::search
