// SPDX-License-Identifier: GPL-2.0-or-later
// Original UDP transport: Copyright (C) Azureus Software, Inc.
#pragma once
#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
namespace biglybtc::services::azureus_dht {
inline constexpr std::size_t maximumPacketBytes=8192;
enum class Kind { Request, Reply };
struct Address {
  std::array<std::uint8_t,16> bytes{};
  std::uint8_t length=4;
  std::uint16_t port=0;
  bool operator==(const Address&) const=default;
};
struct SkewObservation { Address originator;std::int64_t delta; };
enum class ErrorCode { Input, Version, Network, Limit };
struct Error { ErrorCode code;std::size_t consumed=0;std::optional<SkewObservation> skew; };
struct Context {
  std::int8_t localVersion=55;
  std::int32_t explicitMinimum=-1,network=0;
  std::int64_t now=0;
};
// Supplied host state only. This codec does not read configuration or choose an
// active network, transaction ID, clock, listener or advertised capability.
struct EncodeFields {
  Kind kind=Kind::Request;
  std::int8_t localVersion=55,remoteVersion=55;
  std::int32_t network=0,action=1024,transaction=0,instance=0;
  std::int64_t connection=0,now=0,receivedAt=0;
  std::optional<Address> originator; // unresolved -> original 0.0.0.0:0 output.
  std::int8_t flags=0,flags2=0;
};
struct Header {
  Kind kind=Kind::Request;
  std::int32_t action=0,transaction=0,network=0,instance=0;
  std::int64_t connection=0,time=0;
  std::int8_t version=0,vendor=-1,originatorVersion=0,flags=0,flags2=0;
  std::optional<Address> originator;
  std::uint16_t processingTime=0;
};
class DecodedHeader final {
public:
  DecodedHeader(const DecodedHeader&)=default;
  DecodedHeader& operator=(const DecodedHeader&)=default;
  DecodedHeader(DecodedHeader&&) noexcept;
  DecodedHeader& operator=(DecodedHeader&&) noexcept;
  const Header& fields() const { return fields_; }
  std::size_t consumed() const { return consumed_; }
  const std::optional<SkewObservation>& skew() const { return skew_; }
  std::string_view rawPacket() const { return packet_; }
  std::string_view body() const { return std::string_view(packet_).substr(consumed_); }
private:
  DecodedHeader(std::string,Header,std::size_t,std::optional<SkewObservation>);
  std::string packet_;
  Header fields_;
  std::size_t consumed_;
  std::optional<SkewObservation> skew_;
  friend std::expected<DecodedHeader,Error> decodeHeader(Kind,std::string,const Context&);
};
std::int8_t effectiveVersion(std::int8_t base,std::int32_t explicitMinimum);
std::int8_t minimumVersion(std::int32_t network,std::int32_t explicitMinimum);
std::expected<std::string,Error> encodeHeader(const EncodeFields&,std::string_view opaqueBody={});
std::expected<DecodedHeader,Error> decodeHeader(Kind,std::string packet,const Context&);
// Original header-object serializer semantics: local vendor/originator version,
// normalized mapped address, and zero reply processing time after deserialization.
// rawPacket()/body() retain the incoming bytes independently of this projection.
std::expected<std::string,Error> reencodeHeader(const DecodedHeader&,const Context&);
}
