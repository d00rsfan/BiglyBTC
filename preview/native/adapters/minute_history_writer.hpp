#pragma once
#include "biglybtc/minute_history.hpp"
#include "profile.hpp"

#include <stop_token>

namespace biglybtc::storage {

// Borrow the worker's held lease and recorder. This function alone prepares and
// finishes one plan; callers must not have another outstanding plan. Append only
// stats/YYYY/MM/DD.dat through no-follow directory fds. Existing bytes/mode stay
// intact; an invalid, oversized or unterminated existing file is never repaired.
// Every resulting file must satisfy the existing strict saved-history parser.
// New directories are 0700 and new files 0600; append and creation are fsynced.
// Cancellation is checked before the first mutation attempt only. After that,
// an error poisons the recorder, even when some complete records were appended;
// no truncate, cleanup of partial data, blind retry or two-file atomicity claim.
// A Close action has no persistent fd to close. External noncooperating writes
// are detected where observed, not prevented by a pathname/inode CAS protocol.
std::expected<void, Error> writeMinuteHistory(const ProfileLease& lease,
    core::MinuteHistoryRecorder& recorder, const core::MinuteHistoryRequest& request,
    std::stop_token stop = {});

} // namespace biglybtc::storage
