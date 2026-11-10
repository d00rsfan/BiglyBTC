// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "message.hpp"
namespace biglybtc::services::msgsync {
using DiscoveryKey=std::array<std::uint8_t,20>;
// Original DHTPluginStorageManager: SHA1(encoded-key || one offset byte).
Result<DiscoveryKey> diversifiedDiscoveryKey(const DiscoveryKey&,unsigned offset);
Result<std::vector<DiscoveryKey>> diversifiedDiscoveryKeys(const DiscoveryKey&,
    std::int8_t type,std::span<const unsigned> offsets);
}
