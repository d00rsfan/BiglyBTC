// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "libtorrent/config.hpp"
#include <cstdint>
#define TORRENT_BIGLYBTC_SHARED_QUOTA_ABI UINT64_C(0x4242515300010001)
// Opaque callbacks may cross separately qualified SDK images. No SDK C++
// object,allocator,profile lease or filesystem operation crosses this boundary.
struct biglybtc_shared_quota_v1 {
 std::uint64_t abi=TORRENT_BIGLYBTC_SHARED_QUOTA_ABI;
 std::uint32_t size=sizeof(biglybtc_shared_quota_v1),reserved=0;
 void* scope=nullptr;
 void* budget=nullptr;
 void (*retain_scope)(void*) noexcept=nullptr;
 void (*release_scope)(void*) noexcept=nullptr;
 void (*enter)(void*) noexcept=nullptr;
 void (*leave)(void*) noexcept=nullptr;
 void (*retain_budget)(void*) noexcept=nullptr;
 void (*release_budget)(void*) noexcept=nullptr;
 std::uint64_t (*epoch)(void*) noexcept=nullptr;
 int (*limit)(void*,int) noexcept=nullptr;
 int (*available)(void*,int) noexcept=nullptr;
 void (*use)(void*,int,int) noexcept=nullptr;
 void (*give_back)(void*,int,int) noexcept=nullptr;
 std::uint64_t (*queue_request)(void*,void* const*,int,int,int,int,bool) noexcept=nullptr;
 // A negative claim revokes undelivered manager credit:discard it and retry.
 // -1 denotes a closed/missing request;-2 acknowledges a changed policy.
 int (*claim_request)(void*,std::uint64_t,int,bool) noexcept=nullptr;
 void (*cancel_request)(void*,std::uint64_t,bool) noexcept=nullptr;
};
// Errors: -1 argument,-2 closed/exhausted epoch,-3 conflicting existing rates,
// -4 bounded scope capacity,-5 allocation. Returned budget owns one reference.
extern "C" TORRENT_EXPORT std::uint64_t biglybtc_shared_quota_features_v1() noexcept;
extern "C" TORRENT_EXPORT void* biglybtc_shared_quota_scope_create_v1() noexcept;
extern "C" TORRENT_EXPORT void biglybtc_shared_quota_scope_close_v1(void*) noexcept;
extern "C" TORRENT_EXPORT void biglybtc_shared_quota_scope_release_v1(void*) noexcept;
extern "C" TORRENT_EXPORT int biglybtc_shared_quota_acquire_v1(void*,std::uint64_t,int,int,biglybtc_shared_quota_v1*) noexcept;
extern "C" TORRENT_EXPORT int biglybtc_shared_quota_set_v1(void*,int,int) noexcept;
// Network-thread-only binding to global class0 or one actual tag class>=3.
// Built-in local/TCP classes1/2 stay outside this binding. A session keeps
// one arbiter identity; foreign scopes cannot enter an existing quota manager.
extern "C" TORRENT_EXPORT int biglybtc_tag_shared_bind_v1(void*,std::uint32_t,biglybtc_shared_quota_v1 const*) noexcept;
