// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "libtorrent/config.hpp"
#include <cstdint>
#define TORRENT_BIGLYBTC_TAG_GATE_ABI 1
// Private peer_class_set now has dynamic storage;old arrays are incompatible.
#define TORRENT_BIGLYBTC_TAG_CLASS_ABI UINT64_C(0x4242435300020002)
// All controls execute on the actual SDK network thread. Logical masks are
// keyed by actual session/class;no profile,caller bool or torrent data lives here.
extern "C" TORRENT_EXPORT int biglybtc_tag_gate_set_v1(void*,std::uint32_t,int) noexcept;
extern "C" TORRENT_EXPORT int biglybtc_tag_gate_peer_v1(void*) noexcept;
extern "C" TORRENT_EXPORT std::uint64_t biglybtc_tag_gate_features_v1() noexcept;

extern "C" TORRENT_EXPORT int biglybtc_tag_gate_notify_peer_v1(void*) noexcept;

extern "C" TORRENT_EXPORT std::uint64_t biglybtc_tag_class_abi_v1() noexcept;

// Original LAN policy: byte groups are bypassed on the SDK's actual local
// class;directional prohibitions still inspect the retained tag class.
#define TORRENT_BIGLYBTC_TAG_LAN_BYPASS_ABI 1
extern "C" TORRENT_EXPORT int biglybtc_tag_lan_bypass_set_v1(void*,std::uint32_t,int) noexcept;

// Session-wide Original transfer-processor policy:bit0 bypasses protocol
// upload,bit1 bypasses protocol download. Payload remains quota-controlled.
#define TORRENT_BIGLYBTC_PROTOCOL_BYPASS_ABI 1
extern "C" TORRENT_EXPORT int biglybtc_protocol_bypass_set_v1(void*,int) noexcept;
