// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHT packets: Copyright (C) Azureus Software, Inc.
#include "key_block.hpp"
#include <bit>
namespace biglybtc::services::azureus_key_block {
namespace {
Error fromHeader(const udp::Error& e){constexpr ErrorCode codes[]{ErrorCode::Input,ErrorCode::Version,ErrorCode::Network,ErrorCode::Limit};return {codes[static_cast<unsigned>(e.code)],e.consumed,e.skew};}
void number(std::string& out,std::uint32_t n,unsigned width){for(unsigned i=width;i>0;--i){out+=static_cast<char>(n>>(8*(i-1)));}}
}
std::expected<std::string,Error> encodeRequest(const RequestFields& f){
 if(f.header.kind!=udp::Kind::Request||f.header.action!=1036){return std::unexpected(Error{ErrorCode::Action,0,{}});}
 auto header=udp::encodeHeader(f.header);if(!header){return std::unexpected(fromHeader(header.error()));}
 const auto body=std::size_t{7}+f.body.request.size()+f.body.signature.size();
 if(f.body.request.size()>255||f.body.signature.size()>65535||body>udp::maximumPacketBytes-header->size()){return std::unexpected(Error{ErrorCode::Limit,header->size(),{}});}
 number(*header,std::bit_cast<std::uint32_t>(f.body.randomId),4);number(*header,static_cast<std::uint32_t>(f.body.request.size()),1);*header+=f.body.request;number(*header,static_cast<std::uint32_t>(f.body.signature.size()),2);*header+=f.body.signature;return std::move(*header);
}
std::expected<std::string,Error> encodeReply(const ReplyFields& f){
 if(f.header.kind!=udp::Kind::Reply||f.header.action!=1037){return std::unexpected(Error{ErrorCode::Action,0,{}});}
 auto result=udp::encodeHeader(f.header);if(!result){return std::unexpected(fromHeader(result.error()));}return std::move(*result);
}
std::expected<Decoded,Error> decode(udp::Kind kind,std::string bytes,const udp::Context& context){
 if(bytes.size()>udp::maximumPacketBytes){return std::unexpected(Error{ErrorCode::Limit,0,{}});}
 if(kind!=udp::Kind::Request&&kind!=udp::Kind::Reply){return std::unexpected(Error{ErrorCode::Input,0,{}});}
 auto header=udp::decodeHeader(kind,std::move(bytes),context);if(!header){return std::unexpected(fromHeader(header.error()));}
 if(header->fields().action!=(kind==udp::Kind::Request?1036:1037)){return std::unexpected(Error{ErrorCode::Action,header->consumed(),header->skew()});}
 std::size_t at=0;const auto body=header->body();
 try{
  Body decoded=ReplyBody{};
  if(kind==udp::Kind::Request){
   const auto read=[&](unsigned n){if(n>body.size()-at){throw ErrorCode::Input;}std::uint32_t v=0;for(unsigned i=0;i<n;++i){v=(v<<8)|static_cast<unsigned char>(body[at++]);}return v;};
   const auto take=[&](std::size_t n){if(n>body.size()-at){throw ErrorCode::Input;}std::string s(body.substr(at,n));at+=n;return s;};
   RequestBody request;request.randomId=std::bit_cast<std::int32_t>(read(4));const auto requestSize=read(1);request.request=take(requestSize);const auto signatureSize=read(2);request.signature=take(signatureSize);decoded=std::move(request);
  }
  const auto consumed=header->consumed()+at;return Decoded(std::move(*header),std::move(decoded),consumed);
 }catch(ErrorCode error){return std::unexpected(Error{error,header->consumed()+at,header->skew()});}
}
}
