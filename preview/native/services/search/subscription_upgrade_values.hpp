// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "subscription_seed_values.hpp"

namespace biglybtc::search {

struct SubscriptionUpgradeValues {
  std::string configuration;
  std::string definition;
  std::string definitionName;
};

std::expected<SubscriptionUpgradeValues,SubscriptionSeedError> planI2pSubscriptionUpgrade(
    std::string configuration,std::string oldDefinition,std::optional<std::string> newDefinition,
    const I2pSubscriptionUpgrade& selected);

} // namespace biglybtc::search
