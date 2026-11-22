// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "libtorrent/config.hpp"
#include <cstdint>
namespace libtorrent {class peer_connection;namespace aux {struct session_interface;}namespace biglybtc_tag_gate {
 bool upload_disabled(peer_connection const&) noexcept;
 bool download_disabled(peer_connection const&) noexcept;
 void clear_session(void*) noexcept;
 bool protocol_enabled(aux::session_interface&,peer_connection const&,int) noexcept;
 bool protocol_io(peer_connection const&,int) noexcept;
 bool mark_protocol_io(aux::session_interface const&,peer_connection const&,int,bool) noexcept;
 bool original_lan_peer(peer_connection const&) noexcept;
bool local_bypass(aux::session_interface const&,std::uint32_t) noexcept;
 void forget_peer(peer_connection const&) noexcept;
 void defer_grant_reset(peer_connection const&,int);
 bool consume_grant_reset(peer_connection const&,int) noexcept;
 bool register_shared_class(aux::session_interface*,std::uint32_t) noexcept;
 bool shared_changed(aux::session_interface&) noexcept;
 bool shared_peer_changed(peer_connection const&) noexcept;
}}
