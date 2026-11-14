// SPDX-License-Identifier: GPL-2.0-or-later
// Original NAT wire: Copyright (C) Azureus Software, Inc.
#pragma once
#include "../azureus_nodes/nodes.hpp"
#include <biglybtc/bencode.hpp>
namespace biglybtc::services::azureus_nat {
namespace nodes=azureus_nodes;namespace udp=azureus_dht;
inline constexpr std::size_t MaximumMessage=16*1024;
enum class Error {Input,Limit,Contact};
template<class T>using Result=std::expected<T,Error>;
enum class Type:std::uint8_t {Bind=0,BindReply,Punch,PunchReply,Connect,ConnectReply,TunnelInbound,TunnelOutbound,Query,QueryReply,Close,CloseReply};
struct Message {
 Type type=Type::Bind;std::optional<bool> ok;std::optional<std::uint16_t> port;
 std::optional<std::string> target,origin,ip,clientData;
 // origin: original exported binary contact. clientData: bounded bencoded map,
 // never interpreted as a command or added to DHT publication by this codec.
 bool operator==(const Message&)const=default;
};
std::string_view handlerKey()noexcept;
udp::Address canonicalAddress(udp::Address);
Result<std::string> socketAddress(const udp::Address&);
Result<std::string> publishKey(std::span<const std::uint8_t> nodeId);
Result<std::string> encodePublishValue(const nodes::Snapshot&);
Result<nodes::Contact> decodePublishValue(std::string_view,nodes::NodeCache&);
Result<std::string> encode(const Message&);
Result<Message> decode(std::string,nodes::NodeCache&);
}
