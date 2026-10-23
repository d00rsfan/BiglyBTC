// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHT packets: Copyright (C) Azureus Software, Inc.
#pragma once
#include "headers.hpp"
#include "options.hpp"
#include "values.hpp"
#include <variant>
namespace biglybtc::services::azureus_store {
namespace udp=biglybtc::services::azureus_dht;
namespace stats=biglybtc::services::azureus_ping;
namespace values=biglybtc::services::azureus_values;
namespace nodes=values::nodes;
using Keys=std::span<const std::optional<std::string_view>>;
using ValueSet=std::span<const std::optional<values::Snapshot>>;
using ValueSets=std::span<const std::optional<ValueSet>>;
struct RequestFields {
  udp::EncodeFields header{.action=1026,.originator={}};
  std::int32_t randomId=0;
  std::optional<Keys> keys;
  std::optional<ValueSets> valueSets;
};
struct ReplyFields {
  udp::EncodeFields header{.kind=udp::Kind::Reply,.action=1027,.originator={}};
  std::optional<std::string_view> diversification;
  std::optional<stats::UploadSnapshot> upload;
};
struct Context { udp::Context header; };
enum class ErrorCode { Input,Version,Network,Limit,Action,Crypto,Thread };
struct Error {
  ErrorCode code;std::size_t consumed=0;std::optional<udp::SkewObservation> skew;
  std::string partial; // Diagnostic only; failed bytes must never be sent.
};
struct RequestBody {
  std::int32_t randomId=0;
  std::vector<std::string> keys;
  std::vector<std::vector<values::Value>> valueSets;
};
struct ReplyBody { std::optional<std::string> diversification;std::optional<stats::UploadStats> upload; };
using Body=std::variant<RequestBody,ReplyBody>;
class Decoded final {
public:
  Decoded(const Decoded&)=default;
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
