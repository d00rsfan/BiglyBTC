// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "rpc_profile.hpp"

namespace biglybtc::storage {

enum class RpcIdentityMember { Stopped, Archived };

// Worker-only publication for one selected identity. An archive selection can
// repair duplicate successors in one archive document. The application owner
// excludes retained selected handles, pending magnets and profile recovery.
std::expected<std::int64_t, Error> allocateRpcIdentity(const ProfileLease&,
    std::string originalPluginDirectory, const core::TorrentHash&, RpcIdentityMember);

}
