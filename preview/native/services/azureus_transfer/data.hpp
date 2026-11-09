// SPDX-License-Identifier: GPL-2.0-or-later
// Original DATA protocol: Copyright (C) Azureus Software, Inc.
#pragma once
#include "../azureus_dht/headers.hpp"
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
namespace biglybtc::services::azureus_transfer {
namespace udp=azureus_dht;
inline constexpr std::size_t MaximumDatagram=1430,MaximumData=1317,MaximumValue=16*1024,MaximumPieces=64;
enum class Type:std::uint8_t {ReadRequest=0,ReadReply=1,WriteRequest=2,WriteReply=3};
enum class Error {Input,Limit,Header,Range,State};
struct Data {
  Type type=Type::ReadRequest;
  std::string handler,key;
  std::int32_t start=0,length=0,total=0;
  std::string payload; // This physical fragment, not the unsliced whole value.
  bool operator==(const Data&)const=default;
};
struct Packet {udp::DecodedHeader header;Data data;};
std::expected<std::string,Error> encodeBody(const Data&);
std::expected<Data,Error> decodeBody(std::string_view);
std::expected<std::string,Error> encode(const udp::EncodeFields&,const Data&);
std::expected<Packet,Error> decode(std::string,const udp::Context&);
}
