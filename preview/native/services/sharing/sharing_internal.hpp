// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "sharing_values.hpp"
#include "profile_file_snapshot.hpp"
#include "torrent_creator.hpp"
#include <stop_token>
namespace biglybtc::sharing::detail {
using Error=storage::Error;using Code=storage::ErrorCode;
namespace io = storage::detail;
using Fd=io::StateDescriptor;
using io::StateHandle;
using io::StateStamp;
using io::InvalidStateHandle;
struct Failure {Error error;};
inline void require(bool ok,Code code,const char* why){if(!ok){throw Failure{{code,why}};}}
template<class T>T take(std::expected<T,Error> value){if(!value){throw Failure{value.error()};}return std::move(*value);}
inline void take(std::expected<void,Error> value){if(!value){throw Failure{value.error()};}}
struct Named {std::string name,bytes;Fd fd;StateStamp identity{};bool present=false;};
bool sameFile(const StateStamp&,const StateStamp&)noexcept;
bool sameDirectory(const StateStamp&,const StateStamp&)noexcept;
Named named(StateHandle,std::string,std::size_t);
Fd directoryAt(StateHandle,std::string_view,bool create=false);
Fd absoluteDirectory(const std::filesystem::path&);
void actualProfile(const storage::ProfileLease&,const std::filesystem::path&);
struct CatalogRead {SharingValues values;std::optional<storage::RecoverySource> sourceKind;bool needsRecovery=false;};
CatalogRead loadCatalog(const storage::ProfileLease&);
struct SharePolicy {std::string announce,comment,conversions,ignore;bool extraHashes=false,dhtBackup=true;bool operator==(const SharePolicy&)const=default;};
SharePolicy policy(const storage::ProfileLease&);
struct PayloadEntry {std::string relative;StateStamp identity{};bool directory=false;};
struct Payload {std::filesystem::path source;std::vector<PayloadEntry> entries;std::string fingerprint;std::uint64_t bytes=0;};
Payload payload(const std::filesystem::path&,core::SavedShareKind,const SharePolicy&,std::stop_token={});
bool samePayload(const Payload&,const Payload&)noexcept;
bool matchesSavedShareFingerprint(const SharingValues&,const SharingValueRow&,const Payload&)noexcept;
struct Cache {Fd shares,directory;Named file;StateStamp sharesIdentity{},directoryIdentity{};};
Cache cache(const storage::ProfileLease&,std::string_view);
std::string selectedRaw(const SharingValues&,std::size_t);
std::string sha256(std::string_view);
} // namespace biglybtc::sharing::detail
