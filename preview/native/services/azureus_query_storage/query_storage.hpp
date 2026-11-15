// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHT packets: Copyright (C) Azureus Software, Inc.
#pragma once
#include "headers.hpp"
#include <vector>
#include <variant>
namespace biglybtc::services::azureus_query_storage {
namespace udp=azureus_dht;
inline constexpr std::size_t maximumEntries=8192;
struct KeyGroup {std::string prefix;std::vector<std::string> suffixes;bool operator==(const KeyGroup&)const=default;};
struct RequestBody {std::uint8_t headerLength=6;std::vector<KeyGroup> groups;bool operator==(const RequestBody&)const=default;};
struct ReplyBody {std::uint8_t headerLength=2;std::vector<std::optional<std::string>> entries;bool operator==(const ReplyBody&)const=default;};
struct RequestFields {udp::EncodeFields header{.action=1038,.originator={}};RequestBody body;};
struct ReplyFields {udp::EncodeFields header{.kind=udp::Kind::Reply,.action=1039,.originator={}};ReplyBody body;};
enum class ErrorCode {Input,Version,Network,Limit,Action};
struct Error {ErrorCode code;std::size_t consumed=0;std::optional<udp::SkewObservation> skew;};
using Body=std::variant<RequestBody,ReplyBody>;
class Decoded final {
public:
 const udp::DecodedHeader& header()const{return header_;}
 const Body& body()const{return body_;}
 std::size_t consumed()const{return consumed_;}
 std::string_view trailing()const{return header_.rawPacket().substr(consumed_);}
private:
 Decoded(udp::DecodedHeader h,Body b,std::size_t n):header_(std::move(h)),body_(std::move(b)),consumed_(n){}
 udp::DecodedHeader header_;Body body_;std::size_t consumed_;
 friend std::expected<Decoded,Error> decode(udp::Kind,std::string,const udp::Context&);
};
std::expected<std::string,Error> encodeRequest(const RequestFields&);
std::expected<std::string,Error> encodeReply(const ReplyFields&);
std::expected<Decoded,Error> decode(udp::Kind,std::string,const udp::Context&);
}
