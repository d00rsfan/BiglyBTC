#pragma once
#include "biglybtc/file_policy.hpp"
#include "biglybtc/library.hpp"
#include "profile.hpp"
namespace biglybtc::storage {
// Caller first retires any live handle. Original compressed priority encoding;
// conservative cache/resume invalidation precedes the saved preference edit.
std::expected<void, Error> setFilePolicies(const ProfileLease&,const core::TorrentHash&,
    std::span<const compat::FilePolicy>);
}
