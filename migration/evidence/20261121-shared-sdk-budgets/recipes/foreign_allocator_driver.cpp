// SPDX-License-Identifier: GPL-2.0-or-later
// The production allocator/binder TUs are compiled into a separate test image.
// Keep one C++/libc runtime; no session object is created in this module.
#include <libtorrent/biglybtc_shared_quota.hpp>
#include <libtorrent/request_blocks.hpp>
#include <cstdlib>
namespace libtorrent::aux {bool owns_shared_quota(biglybtc_shared_quota_v1 const&)noexcept;}
// Only the rejection branch is qualified here. The private scheduler is not
// exported by the real SDK; reaching it means rejection happened too late.
namespace libtorrent {bool request_a_block(torrent&,peer_connection&){std::abort();}}
extern "C" bool foreign_allocator_owns(const biglybtc_shared_quota_v1* operations)noexcept{return operations&&libtorrent::aux::owns_shared_quota(*operations);}
extern "C" int foreign_allocator_probe(const biglybtc_shared_quota_v1* operations)noexcept{return biglybtc_tag_shared_bind_v1(reinterpret_cast<void*>(std::uintptr_t{1}),3,operations);}
