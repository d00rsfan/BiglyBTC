// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHT packets: Copyright (C) Azureus Software, Inc.
#pragma once
#include "headers.hpp"
#include "options.hpp"
#include "positions.hpp"
#include "contacts.hpp"
#include <variant>
namespace biglybtc::services::azureus_packets {
namespace udp=biglybtc::services::azureus_dht;
namespace stats=biglybtc::services::azureus_ping;
namespace positions=biglybtc::services::azureus_positions;
namespace contacts=biglybtc::services::azureus_contacts;
struct RequestFields {
  udp::EncodeFields header;
  std::optional<std::span<const std::int32_t>> networks,counts;
  std::optional<stats::UploadSnapshot> upload;
};
struct ReplyFields {
  udp::EncodeFields header{.kind=udp::Kind::Reply,.action=1025,.originator={}};
  std::optional<std::span<const std::optional<positions::WirePosition>>> positions;
  std::optional<std::span<const contacts::Snapshot>> contacts;
  std::optional<stats::UploadSnapshot> upload;
};
struct Context { udp::Context header;std::int64_t monotonicNow=0;bool v1Registered=true; };
enum class ErrorCode { Input,Version,Network,Limit,Action };
struct Error {
  ErrorCode code;
  std::size_t consumed=0;
  std::optional<udp::SkewObservation> skew;
  std::size_t providerFailures=0;
  std::string partial; // encoder diagnostic only, never a sendable packet.
};
struct RequestBody { std::vector<std::int32_t> networks,counts;std::optional<stats::UploadStats> upload; };
struct ReplyBody {
  positions::Positions positions;
  std::vector<contacts::Contact> contacts;
  std::optional<stats::UploadStats> upload;
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
  friend std::expected<Decoded,Error> decode(udp::Kind,std::string,const Context&);
};
std::expected<std::string,Error> encodeRequest(const RequestFields&);
std::expected<std::string,Error> encodeReply(const ReplyFields&);
std::expected<Decoded,Error> decode(udp::Kind,std::string,const Context&);
}
