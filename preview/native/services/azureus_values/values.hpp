// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHT values: Copyright (C) Azureus Software, Inc.
#pragma once
#include "nodes.hpp"
namespace biglybtc::services::azureus_values {
namespace nodes=biglybtc::services::azureus_nodes;
inline constexpr std::size_t maximumBodyBytes=8192,maximumValueBytes=512;
struct Snapshot {
  std::int32_t version=0;
  std::int64_t created=0;
  std::optional<std::string_view> bytes;
  std::optional<nodes::Snapshot> originator;
  std::int32_t flags=0,lifeHours=0;
  std::int8_t replication=-1;
};
struct Value {
  std::int32_t version=-1;
  std::int64_t created=0;
  std::string bytes;
  nodes::Contact originator;
  std::uint8_t flags=0,lifeHours=0;
  std::int8_t replication=-1;
  std::int8_t replicationFactor() const { return replication==-1?-1:static_cast<std::int8_t>(replication&15); }
  std::int8_t replicationFrequency() const { return replication==-1?-1:static_cast<std::int8_t>(replication>>4); }
  bool isLocal() const { return false; }
};
enum class ErrorCode { Input,Crypto,Limit,Thread };
struct Error { ErrorCode code;std::size_t consumed=0;std::string partial; };
struct Decoded { std::vector<Value> values;std::size_t consumed=0;std::string trailing; };
std::expected<std::string,Error> encodeValue(std::int8_t version,std::int64_t skew,const std::optional<Snapshot>&);
std::expected<std::string,Error> encodeValues(std::int8_t version,std::int64_t skew,std::optional<std::span<const std::optional<Snapshot>>>);
std::expected<Decoded,Error> decodeValue(std::int8_t version,std::int64_t skew,std::string_view,nodes::NodeCache&);
std::expected<Decoded,Error> decodeValues(std::int8_t version,std::int64_t skew,std::string_view,nodes::NodeCache&);
// Nested packet parsing: own values/IDs, report consumed, omit unused tail copy.
std::expected<Decoded,Error> decodeValuesPrefix(std::int8_t version,std::int64_t skew,std::string_view,nodes::NodeCache&);
}
