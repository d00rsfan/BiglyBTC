// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../azureus_query_storage/query_storage.hpp"
#include <array>
#include <span>
namespace biglybtc::services::azureus_db_runtime::survey_graph {
using NodeId=std::array<std::uint8_t,20>;
struct Node {NodeId id{};std::array<std::uint8_t,16> address{};std::uint8_t addressLength=4;std::int8_t protocol=55;};
struct Query {std::size_t node=0;bool wireEligible=true;azureus_query_storage::RequestBody request;std::vector<std::size_t> keys;};
struct Topology {NodeId local{};std::size_t k=20;std::vector<Node> nodes;std::vector<std::string> keys;std::vector<Query> queries;std::vector<std::vector<std::size_t>> peersByKey;};
enum class Error {Input,Capacity};
// Pure owning graph; observations alone never grant peer or source authority.
std::expected<Topology,Error> topology(const NodeId& local,std::size_t k,std::span<const Node>,std::span<const std::string> keys);
}
