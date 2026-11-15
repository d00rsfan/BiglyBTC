// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include "tag_strip_settings.hpp"
namespace biglybtc::app {
std::expected<ui::TagStripSettings,storage::Error> saveTagStripSettings(const storage::ProfileLease&,const ui::TagStripSettings&,const ui::TagStripSettings&);
}
