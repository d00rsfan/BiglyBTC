// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../plugins/BiglyBTC-plugin-azrating/rating_data.hpp"
#include <QHash>
#include <QByteArray>
#include <QString>
#include <span>
namespace biglybtc::ui {
struct SwarmRatingCell {QString text,tooltip;bool running=false;};
using SwarmRatingValues=QHash<QByteArray,SwarmRatingCell>;
// Original ColumnRC_Rating completed-result projection; no lookup or storage.
std::optional<SwarmRatingCell> presentSwarmRatings(std::span<const plugins::rating::RatingData>);
}
