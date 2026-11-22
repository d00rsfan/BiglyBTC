// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "sdk_shared_tag_budget.hpp"
#include <libtorrent/session_handle.hpp>
#include <expected>
#include <memory>
#include <cstdint>
namespace biglybtc::engine {
// Existing global/local SDK classes share the profile allocator. This owner
// retains no profile,source permission or control worker reference.
class SdkNetworkRates final {
public:
 static constexpr std::uint64_t GlobalKey=UINT64_MAX;
 static constexpr std::uint64_t LanKey=UINT64_MAX-1;
 static std::expected<std::unique_ptr<SdkNetworkRates>,SdkSharedQuotaError> create(
     libtorrent::session_handle,std::shared_ptr<SdkSharedTagBudget> global,
     std::shared_ptr<SdkSharedTagBudget> lan,bool includeUploadProtocol=true,
     bool includeDownloadProtocol=true);
 ~SdkNetworkRates();
 SdkNetworkRates(const SdkNetworkRates&)=delete;
 SdkNetworkRates& operator=(const SdkNetworkRates&)=delete;
 // Join actual network policy effects before the caller publishes an ack.
 std::expected<void,SdkSharedQuotaError> refresh();
 std::expected<void,SdkSharedQuotaError> configureProtocol(bool includeUpload,bool includeDownload);
 static bool supportsOriginalProtocol()noexcept;
private:
 struct Impl;explicit SdkNetworkRates(std::shared_ptr<Impl>);
 std::shared_ptr<Impl> impl_;
};
}
