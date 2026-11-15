// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHT packets: Copyright (C) Azureus Software, Inc.
#include "query_storage.hpp"
#include <algorithm>
#include <bit>
namespace biglybtc::services::azureus_query_storage {
namespace {
struct Failure {ErrorCode code;};
void require(bool ok,ErrorCode code=ErrorCode::Input){if(!ok){throw Failure{code};}}
Error fromHeader(const udp::Error& e){constexpr ErrorCode codes[]{ErrorCode::Input,ErrorCode::Version,ErrorCode::Network,ErrorCode::Limit};return {codes[static_cast<unsigned>(e.code)],e.consumed,e.skew};}
void append(std::string& out,std::string_view s){require(s.size()<=udp::maximumPacketBytes-out.size(),ErrorCode::Limit);out+=s;}
void byte(std::string& out,std::uint8_t value){require(out.size()<udp::maximumPacketBytes,ErrorCode::Limit);out+=static_cast<char>(value);}
void count(std::string& out,std::size_t n){require(n<=32767,ErrorCode::Limit);byte(out,static_cast<std::uint8_t>(n>>8));byte(out,static_cast<std::uint8_t>(n));}
class Reader {
public:
 explicit Reader(std::string_view in):in_(in){}
 std::uint8_t byte(){require(at_<in_.size());return static_cast<std::uint8_t>(in_[at_++]);}
 std::size_t count(){auto n=std::uint16_t(byte())<<8;n|=byte();require(n<=32767);return n;}
 std::string_view take(std::size_t n){require(n<=in_.size()-at_);auto result=in_.substr(at_,n);at_+=n;return result;}
 std::size_t position()const{return at_;}
private:std::string_view in_;std::size_t at_=0;
};
}
std::expected<std::string,Error> encodeRequest(const RequestFields& f){
 if(f.header.kind!=udp::Kind::Request||f.header.action!=1038){return std::unexpected(Error{ErrorCode::Action,0,{}});}
 auto header=udp::encodeHeader(f.header);if(!header){return std::unexpected(fromHeader(header.error()));}auto out=std::move(*header);
 try{byte(out,f.body.headerLength);require(f.body.groups.size()<=maximumEntries,ErrorCode::Limit);count(out,f.body.groups.size());std::size_t total=0;
  for(const auto& group:f.body.groups){require(group.prefix.size()<=f.body.headerLength);require(group.suffixes.size()<=maximumEntries-total,ErrorCode::Limit);total+=group.suffixes.size();byte(out,static_cast<std::uint8_t>(group.prefix.size()));append(out,group.prefix);count(out,group.suffixes.size());for(const auto& suffix:group.suffixes){require(suffix.size()==f.body.headerLength-group.prefix.size());append(out,suffix);}}
  return out;
 }catch(const Failure& e){return std::unexpected(Error{e.code,out.size(),{}});}
}
std::expected<std::string,Error> encodeReply(const ReplyFields& f){
 if(f.header.kind!=udp::Kind::Reply||f.header.action!=1039){return std::unexpected(Error{ErrorCode::Action,0,{}});}
 auto header=udp::encodeHeader(f.header);if(!header){return std::unexpected(fromHeader(header.error()));}auto out=std::move(*header);
 try{const auto n=f.body.entries.size();require(n<=maximumEntries,ErrorCode::Limit);count(out,n);if(n){byte(out,f.body.headerLength);
   // Original Java size+7/8 is size bytes, including zero-filled spare bytes.
   std::string bitmap(n,'\0');for(std::size_t i=0;i<n;++i){if(f.body.entries[i]){require(f.body.entries[i]->size()==f.body.headerLength);bitmap[i/8]=static_cast<char>(static_cast<unsigned char>(bitmap[i/8])|(0x80u>>(i%8)));}}
   append(out,bitmap);for(const auto& row:f.body.entries){if(row){append(out,*row);}}
  }return out;
 }catch(const Failure& e){return std::unexpected(Error{e.code,out.size(),{}});}
}
std::expected<Decoded,Error> decode(udp::Kind kind,std::string bytes,const udp::Context& context){
 if(bytes.size()>udp::maximumPacketBytes){return std::unexpected(Error{ErrorCode::Limit,0,{}});}
 if(kind!=udp::Kind::Request&&kind!=udp::Kind::Reply){return std::unexpected(Error{ErrorCode::Input,0,{}});}
 auto header=udp::decodeHeader(kind,std::move(bytes),context);if(!header){return std::unexpected(fromHeader(header.error()));}
 if(header->fields().action!=(kind==udp::Kind::Request?1038:1039)){return std::unexpected(Error{ErrorCode::Action,header->consumed(),header->skew()});}
 Reader reader(header->body());try{Body body;
  if(kind==udp::Kind::Request){RequestBody request;request.headerLength=reader.byte();const auto n=reader.count();require(n<=maximumEntries,ErrorCode::Limit);request.groups.reserve(n);std::size_t total=0;
   for(std::size_t i=0;i<n;++i){const auto length=reader.byte();require(length<=request.headerLength);KeyGroup group;group.prefix=reader.take(length);const auto count=reader.count();require(count<=maximumEntries-total,ErrorCode::Limit);total+=count;group.suffixes.reserve(count);for(std::size_t j=0;j<count;++j){group.suffixes.emplace_back(reader.take(request.headerLength-length));}request.groups.push_back(std::move(group));}body=std::move(request);
  }else{ReplyBody reply;const auto n=reader.count();require(n<=maximumEntries,ErrorCode::Limit);reply.headerLength=0;if(n){reply.headerLength=reader.byte();const auto bitmap=reader.take(n);reply.entries.reserve(n);for(std::size_t i=0;i<n;++i){if(static_cast<unsigned char>(bitmap[i/8])&(0x80u>>(i%8))){reply.entries.emplace_back(reader.take(reply.headerLength));}else{reply.entries.emplace_back(std::nullopt);}}}body=std::move(reply);}
  const auto consumed=header->consumed()+reader.position();return Decoded(std::move(*header),std::move(body),consumed);
 }catch(const Failure& e){return std::unexpected(Error{e.code,header->consumed()+reader.position(),header->skew()});}
}
}
