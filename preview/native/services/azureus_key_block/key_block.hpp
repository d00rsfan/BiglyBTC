// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHT packets: Copyright (C) Azureus Software, Inc.
#pragma once
#include "../azureus_dht/headers.hpp"
#include <variant>
namespace biglybtc::services::azureus_key_block {
namespace udp=azureus_dht;
struct RequestBody {std::int32_t randomId=0;std::string request,signature;bool operator==(const RequestBody&)const=default;};
struct ReplyBody {bool operator==(const ReplyBody&)const=default;};
struct RequestFields {udp::EncodeFields header{.action=1036,.originator={}};RequestBody body;};
struct ReplyFields {udp::EncodeFields header{.kind=udp::Kind::Reply,.action=1037,.originator={}};};
enum class ErrorCode {Input,Version,Network,Limit,Action};
struct Error {ErrorCode code;std::size_t consumed=0;std::optional<udp::SkewObservation> skew;};
using Body=std::variant<RequestBody,ReplyBody>;
class Decoded final {
public:
 const udp::DecodedHeader& header()const{return header_;}const Body& body()const{return body_;}
 std::size_t consumed()const{return consumed_;}std::string_view trailing()const{return header_.rawPacket().substr(consumed_);}
private:
 Decoded(udp::DecodedHeader h,Body b,std::size_t n):header_(std::move(h)),body_(std::move(b)),consumed_(n){}
 udp::DecodedHeader header_;Body body_;std::size_t consumed_;
 friend std::expected<Decoded,Error> decode(udp::Kind,std::string,const udp::Context&);
};
std::expected<std::string,Error> encodeRequest(const RequestFields&);
std::expected<std::string,Error> encodeReply(const ReplyFields&);
std::expected<Decoded,Error> decode(udp::Kind,std::string,const udp::Context&);
}
