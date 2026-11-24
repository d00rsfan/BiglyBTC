// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "network_policy.hpp"

namespace biglybtc::engine {

// Original JVM HTTP/SOCKS proxy preferences without unrelated torrent policy.
std::expected<std::optional<NetworkProxy>, NetworkPolicyError> readOriginalWebProxy(const core::Document*);

} // namespace biglybtc::engine
