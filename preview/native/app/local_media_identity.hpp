// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "profile.hpp"
#include <QUuid>

namespace biglybtc::app {

// The default azupnpav installation reads this same UUID from biglybt.config.
std::expected<QUuid, storage::Error> loadOrCreateLocalMediaId(const storage::ProfileLease&);

} // namespace biglybtc::app
