// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "state_file_io.hpp"

namespace biglybtc::storage::detail {
inline constexpr char RestoreDirectory[] = ".BiglyBTC.restore-data";
inline constexpr char RestoreRequest[] = ".BiglyBTC.restore-request";
inline constexpr char RestoreRequestSaving[] = ".BiglyBTC.restore-request.saving";
inline constexpr char RestorePrevious[] = ".BiglyBTC.restore-previous";
inline constexpr char RestorePrior[] = ".BiglyBTC.restore-preimages";
inline constexpr char RestorePriorSaving[] = ".BiglyBTC.restore-preimages.saving";
inline constexpr char RestoreApplied[] = ".BiglyBTC.restore-applied";

std::expected<void, Error> publishProfileRestoreRequest(StateHandle profile, const StateStamp& staged,
    std::stop_token stop);
}
