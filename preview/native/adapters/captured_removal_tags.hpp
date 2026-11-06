// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include "biglybtc/library.hpp"
#include <vector>

namespace biglybtc::storage {
class ManualTagStore;
class DurableRemovalReceipt;
struct OriginalTorrentIdentity;
// Actual pre-stop manual-namespace capture, not a GUI list or live-download
// permission. Only the existing worker-owned Store can construct a positive.
class CapturedRemovalTags final {
public:
  CapturedRemovalTags(CapturedRemovalTags&&)noexcept;
  CapturedRemovalTags& operator=(CapturedRemovalTags&&)noexcept;
  ~CapturedRemovalTags();
  CapturedRemovalTags(const CapturedRemovalTags&)=delete;
  CapturedRemovalTags& operator=(const CapturedRemovalTags&)=delete;
private:
  struct Data;std::unique_ptr<Data> data_;
  explicit CapturedRemovalTags(std::unique_ptr<Data>);
  std::expected<void,Error> current(const ProfileLease&,const core::TorrentHash&)const;
  std::expected<void,Error> matchesIdentity(const OriginalTorrentIdentity&)const;
  const std::vector<std::string>& names()const;
  friend class ManualTagStore;
  friend class DurableRemovalReceipt;
};
} // namespace biglybtc::storage
