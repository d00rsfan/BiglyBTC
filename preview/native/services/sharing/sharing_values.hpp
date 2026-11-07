// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/saved_services.hpp"
namespace biglybtc::sharing {
enum class ShareValueError {Malformed,Unsupported,Ambiguous,Limit,Crypto,Memory};
struct SavedShareSelector {std::size_t sourceIndex=0;std::string file,cacheFile;bool operator==(const SavedShareSelector&)const=default;};
struct SharingValueRow {SavedShareSelector selected;std::optional<core::SavedShareKind> kind;bool available=false;std::string refusal;};
struct SharingValues {std::shared_ptr<const core::SavedShares> source;std::vector<SharingValueRow> rows;};
// Pure values only: no path traversal, profile, creator, manager or network.
std::expected<SharingValues,ShareValueError> readSharingValues(std::string);
bool supportedShareCacheName(std::string_view)noexcept;
std::expected<std::string,ShareValueError> originalShareFingerprint(std::string_view metadataUtf8);
std::expected<std::string,ShareValueError> appendPersistentShare(const SharingValues&,core::SavedShareKind,std::string_view canonicalPayload,std::string_view cacheName,std::string_view fingerprint);
std::expected<std::string,ShareValueError> eraseShare(const SharingValues&,const SavedShareSelector&);
} // namespace biglybtc::sharing
