// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHT packets: Copyright (C) Azureus Software, Inc.
#pragma once
#include "headers.hpp"

namespace biglybtc::services::azureus_error {
namespace udp=biglybtc::services::azureus_dht;

struct Endpoint {
  std::optional<udp::Address> resolved; // An unresolved object writes 0.0.0.0:0.
};
struct ReplyFields {
  udp::EncodeFields header{.kind=udp::Kind::Reply,.action=1032,.originator={}};
  std::int32_t type=0;
  std::optional<Endpoint> originator; // null object is a source input failure.
  std::optional<std::string_view> request,signature;
};
enum class ErrorCode { Input,Version,Network,Limit,Action };
struct Error {
  ErrorCode code;
  std::size_t consumed=0;
  std::optional<udp::SkewObservation> skew;
  std::string partial; // Diagnostic, never sendable.
};
struct Body {
  std::int32_t type=0;
  std::optional<udp::Address> originator;
  std::optional<std::string> request,signature;
};
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
  udp::DecodedHeader header_;
  Body body_;
  std::size_t consumed_;
  friend std::expected<Decoded,Error> decode(std::string,const udp::Context&);
};
std::expected<std::string,Error> encodeReply(const ReplyFields&);
std::expected<Decoded,Error> decode(std::string packet,const udp::Context&);
}
