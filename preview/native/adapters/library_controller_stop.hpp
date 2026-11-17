// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "library_commands.hpp"
namespace biglybtc::storage {
enum class LibraryControllerStopTarget { Stopped=70, Queued=75 };
// Existing owner observations;not inferred from saved startup/positions. The
// original stopped/error shortcut depends on the actual disk-manager presence.
struct LibraryControllerState {std::int32_t state=0;bool diskManagerPresent=false;};
// Saved intent for direct DownloadManagerController.stopIt,target70/75. This is
// distinct from ManagerUtils/UI Queue or paused-stop wrappers. No engine/payload
// call is made here;the real owner must request and observe actual retirement.
// Direct setStateQueued,including its existing paused-force restoration.
std::expected<LibraryCommandResult,Error> applyLibraryControllerQueue(const ProfileLease&,const core::TorrentHash&);
std::expected<LibraryCommandResult,Error> applyLibraryControllerStop(const ProfileLease&,
    const core::TorrentHash&,LibraryControllerStopTarget,LibraryControllerState);
}
