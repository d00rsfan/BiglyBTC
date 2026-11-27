// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "profile.hpp"
#include <QSslConfiguration>
#include <expected>
#include <string_view>

namespace biglybtc::tracker::detail {
std::expected<QSslConfiguration,storage::Error> loadTrackerTlsIdentity(std::string_view keyStore);
}
