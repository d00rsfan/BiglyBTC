// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHT packets: Copyright (C) Azureus Software, Inc.
#pragma once
#include "headers.hpp"
#include "options.hpp"
#include "positions.hpp"
#include "nodes.hpp"
#include <variant>
namespace biglybtc::services::azureus_find_node {
namespace udp=biglybtc::services::azureus_dht;
namespace stats=biglybtc::services::azureus_ping;
namespace positions=biglybtc::services::azureus_positions;
namespace nodes=biglybtc::services::azureus_nodes;
struct RequestFields {
  udp::EncodeFields header{.action=1028,.originator={}};
  std::optional<std::string_view> target;
  std::int32_t status=0,estimatedSize=0;
  std::optional<stats::UploadSnapshot> upload;
};
struct ReplyFields {
  udp::EncodeFields header{.kind=udp::Kind::Reply,.action=1029,.originator={}};
  std::int32_t randomId=0,status=-1,estimatedSize=0;
  std::optional<std::span<const std::optional<positions::WirePosition>>> positions;
  std::optional<std::span<const nodes::Snapshot>> contacts;
};
struct Context { udp::Context header;bool v1Registered=true; };
enum class ErrorCode { Input,Version,Network,Limit,Action,Crypto,Thread };
struct Error {
  ErrorCode code;
  std::size_t consumed=0;
  std::optional<udp::SkewObservation> skew;
  std::size_t providerFailures=0;
  std::string partial; // Diagnostic only. Failed output must never be transmitted.
};
struct RequestBody {
  std::string target;
  std::int32_t status=0,estimatedSize=0;
  std::optional<stats::UploadStats> upload;
};
struct ReplyBody {
  std::int32_t randomId=0,status=-1,estimatedSize=0;
  positions::Positions positions;
  std::vector<nodes::Contact> contacts;
  std::size_t providerFailures=0;
};
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
  udp::DecodedHeader header_;
  Body body_;
  std::size_t consumed_;
  friend std::expected<Decoded,Error> decode(udp::Kind,std::string,const Context&,nodes::NodeCache&);
};
std::expected<std::string,Error> encodeRequest(const RequestFields&);
std::expected<std::string,Error> encodeReply(const ReplyFields&);
// Cache is borrowed for this call only. Endpoints remain inert data; no lookup,
// routing registration, provider lifecycle or network activity takes place.
std::expected<Decoded,Error> decode(udp::Kind,std::string,const Context&,nodes::NodeCache&);
}
