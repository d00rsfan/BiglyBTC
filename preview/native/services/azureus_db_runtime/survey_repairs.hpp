// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "survey_topology.hpp"
namespace biglybtc::services::azureus_db_runtime::survey_graph {
// Current contact-state observations, not grants to a peer or endpoint.
struct Knowledge {NodeId id{};bool hasState=false,responseCounted=false;std::int32_t consecutiveFailures=0;std::vector<std::size_t> knownKeys;};
struct AssumedCopy {std::size_t key=0,node=0;};
struct Repairs {std::vector<std::vector<std::size_t>> targetsByKey;std::vector<AssumedCopy> assumed;};
// Factors are the actual per-mapping maximum over original raw values (-1..15).
std::expected<Repairs,Error> repairs(const Topology&,const NodeId& currentLocal,std::span<const Knowledge>,std::span<const std::int8_t> factors);
}
