// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHT packets: Copyright (C) Azureus Software, Inc.
#pragma once
#include "headers.hpp"
#include "values.hpp"
#include "positions.hpp"
#include <variant>
namespace biglybtc::services::azureus_find_value {
namespace udp=biglybtc::services::azureus_dht;
namespace values=biglybtc::services::azureus_values;
namespace nodes=values::nodes;
namespace positions=biglybtc::services::azureus_positions;
using ValueSet=std::span<const std::optional<values::Snapshot>>;
struct RequestFields {
  udp::EncodeFields header{.action=1030,.originator={}};
  std::optional<std::string_view> target;
  std::int32_t flags=0,maximumValues=0;
};
struct ReplyFields {
  udp::EncodeFields header{.kind=udp::Kind::Reply,.action=1031,.originator={}};
  bool continuation=false;
  std::int8_t diversification=1;
  std::int64_t remoteClockSkew=0;
  std::optional<ValueSet> values; // null -> contact mode; empty -> value mode.
  std::optional<std::span<const nodes::Snapshot>> contacts;
  std::optional<std::span<const std::optional<positions::WirePosition>>> positions;
};
struct Context { udp::Context header;bool v1Registered=true; };
enum class ErrorCode { Input,Version,Network,Limit,Action,Crypto,Thread };
struct Error {
  ErrorCode code;std::size_t consumed=0;std::optional<udp::SkewObservation> skew;
  std::size_t providerFailures=0;std::string partial; // Diagnostic, never sendable.
};
struct RequestBody { std::string target;std::int8_t flags=0;std::uint8_t maximumValues=0; };
struct FoundValues { std::int8_t diversification=1;std::vector<values::Value> values; };
struct FoundContacts { std::vector<nodes::Contact> contacts;positions::Positions positions;std::size_t providerFailures=0; };
struct ReplyBody { bool continuation=false;std::variant<FoundValues,FoundContacts> result; };
using Body=std::variant<RequestBody,ReplyBody>;
class Decoded final {
public:
  Decoded(const Decoded&);
  Decoded& operator=(const Decoded&);
  Decoded(Decoded&&) noexcept;
  Decoded& operator=(Decoded&&) noexcept;
  const udp::DecodedHeader& header() const { return header_; }
  const Body& body() const { return body_; }
  std::size_t consumed() const { return consumed_; }
  std::string_view trailing() const { return header_.rawPacket().substr(consumed_); }
private:
  Decoded(udp::DecodedHeader,Body,std::size_t);
  udp::DecodedHeader header_;Body body_;std::size_t consumed_;
  friend std::expected<Decoded,Error> decode(udp::Kind,std::string,const Context&,nodes::NodeCache&);
};
std::expected<std::string,Error> encodeRequest(const RequestFields&);
std::expected<std::string,Error> encodeReply(const ReplyFields&);
std::expected<Decoded,Error> decode(udp::Kind,std::string,const Context&,nodes::NodeCache&);
}
