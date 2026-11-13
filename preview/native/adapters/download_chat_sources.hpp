// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "local_rating.hpp"
#include <memory>
namespace biglybtc::storage {
struct DownloadChatSourceStatistics {std::uint64_t fullReads=0,hits=0;std::size_t entries=0;};
// Bounded read-only chat subjects for one retained profile OFD. Fresh anchored
// file/permission/recovery/cache/journal observations precede every memo hit.
// This supplies no torrent Start/resume, identity or message-commit authority.
class DownloadChatSources final {
public:
 static std::expected<std::shared_ptr<DownloadChatSources>,Error> create(const ProfileLease&);
 ~DownloadChatSources();DownloadChatSources(const DownloadChatSources&)=delete;
 std::expected<DownloadChatSnapshot,Error> read(const core::TorrentHash&);
 DownloadChatSourceStatistics statistics()const noexcept;
private:
 struct Impl;explicit DownloadChatSources(std::unique_ptr<Impl>);
 std::unique_ptr<Impl> impl_;
};
}
