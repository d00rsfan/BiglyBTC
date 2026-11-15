// SPDX-License-Identifier: GPL-2.0-or-later
// Original storage policy: Copyright (C) Azureus Software, Inc.
#include "recent_addresses.hpp"
#include <bit>
#include <charconv>
#include <map>
namespace biglybtc::services::azureus_identity {
namespace {
constexpr std::int64_t week=604800000;
std::int64_t elapsed(std::int64_t now,std::int64_t then){return std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(now)-std::bit_cast<std::uint64_t>(then));}
core::Node bytes(std::string_view value){core::Node n(core::Node::Type::Bytes);n.bytes=value;return n;}
core::Node integer(std::int64_t value){core::Node n(core::Node::Type::Integer);n.integer=value;return n;}
std::string ipv4(std::uint32_t n){return std::to_string(n>>24)+"."+std::to_string((n>>16)&255)+"."+std::to_string((n>>8)&255)+"."+std::to_string(n&255);}
}
std::expected<std::optional<std::string>,Error> mostRecent(const core::Node& root){
 try{if(root.type!=core::Node::Type::Dictionary){return std::unexpected(Error::Input);}const auto* value=root.find("most_recent");if(!value){return std::optional<std::string>{};}if(value->type!=core::Node::Type::Bytes){return std::unexpected(Error::Input);}if(value->bytes.size()>4096){return std::unexpected(Error::Limit);}return std::optional<std::string>{value->bytes};}catch(...){return std::unexpected(Error::Resources);}
}
std::expected<bool,Error> isRecent(const core::Node& root,std::string_view address){if(root.type!=core::Node::Type::Dictionary){return std::unexpected(Error::Input);}if(address.size()>4096){return std::unexpected(Error::Limit);}if(root.find(address)){return true;}auto recent=mostRecent(root);if(!recent){return std::unexpected(recent.error());}return recent->has_value()&&**recent==address;}
std::expected<std::string,Error> remember(const core::Node& root,std::string_view address,std::int64_t now){
 try{
  if(root.type!=core::Node::Type::Dictionary){return std::unexpected(Error::Input);}if(address.size()>4096||root.children.size()/2>maximumAddresses){return std::unexpected(Error::Limit);}std::map<std::string,core::Node> fields;
  for(std::size_t i=0;i+1<root.children.size();i+=2){const auto key=root.children[i].bytes;const auto& value=root.children[i+1];if(key=="most_recent"||key==address){continue;}if(value.type!=core::Node::Type::Integer){return std::unexpected(Error::Input);}if(elapsed(now,value.integer)<=week){fields.emplace(std::string(key),value);}}
  if(address!="most_recent"){fields.insert_or_assign(std::string(address),integer(now));}fields.insert_or_assign("most_recent",bytes(address));if(fields.size()>maximumAddresses){return std::unexpected(Error::Limit);}core::Node out(core::Node::Type::Dictionary);out.children.reserve(fields.size()*2);for(const auto& [key,value]:fields){out.children.push_back(bytes(key));out.children.push_back(value);}auto encoded=core::encodeValue(out);if(!encoded){return std::unexpected(Error::Limit);}return std::move(*encoded);
 }catch(...){return std::unexpected(Error::Resources);}
}
std::expected<std::string,Error> originalHostAddress(const QHostAddress& address){
 try{
  if(address.protocol()==QAbstractSocket::IPv4Protocol){return ipv4(address.toIPv4Address());}if(address.protocol()!=QAbstractSocket::IPv6Protocol){return std::unexpected(Error::Input);}const auto ip=address.toIPv6Address();bool mapped=ip.c[10]==255&&ip.c[11]==255;for(unsigned i=0;i<10;++i){mapped&=ip.c[i]==0;}if(mapped){std::uint32_t n=0;for(unsigned i=12;i<16;++i){n=(n<<8)|ip.c[i];}return ipv4(n);}std::string text;
  for(unsigned i=0;i<8;++i){if(i){text+=':';}char buffer[4];const auto value=std::uint16_t((std::uint16_t(ip.c[i*2])<<8)|ip.c[i*2+1]);const auto written=std::to_chars(buffer,buffer+4,value,16);if(written.ec!=std::errc{}){return std::unexpected(Error::Input);}text.append(buffer,written.ptr);}if(!address.scopeId().isEmpty()){text+='%';text+=address.scopeId().toUtf8().toStdString();}return text;
 }catch(...){return std::unexpected(Error::Resources);}
}
}
