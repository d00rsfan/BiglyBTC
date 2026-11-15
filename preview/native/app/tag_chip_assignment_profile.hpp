// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include "tag_strip_commands.hpp"
#include <functional>
#include <stop_token>
namespace biglybtc::app {
using CategoryOwnerRebase=std::function<std::expected<void,storage::Error>(const core::TorrentHash&)>;
// Filesystem-only, fresh actual inventory and Store CAS. Caller additionally
// validates its real Host/session/model revision before invoking this helper.
ui::TagChipAssignmentResult assignTagChip(const storage::ProfileLease&,const ui::TagChipAssignmentCommand&,std::stop_token={},CategoryOwnerRebase={});
}
