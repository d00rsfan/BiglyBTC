// SPDX-License-Identifier: GPL-2.0-or-later
#include "data.hpp"
#include <bit>
namespace biglybtc::services::azureus_transfer {
namespace {
void integer(std::string& out,std::int32_t value){const auto bits=std::bit_cast<std::uint32_t>(value);for(int i=24;i>=0;i-=8){out+=static_cast<char>(bits>>i);}}
std::int32_t integer(std::string_view in,std::size_t p){std::uint32_t out=0;for(unsigned i=0;i<4;++i){out=(out<<8)|static_cast<unsigned char>(in[p+i]);}return std::bit_cast<std::int32_t>(out);}
bool fields(const Data& value){
  if(static_cast<unsigned>(value.type)>3||value.handler.size()>64||value.key.size()>255||value.payload.size()>MaximumData){return false;}
  if(value.start<0||value.length<0||value.total<0||static_cast<std::size_t>(value.total)>MaximumValue){return false;}
  if(value.type==Type::ReadRequest||value.type==Type::WriteReply){return value.payload.empty()&&value.total==0&&static_cast<std::size_t>(value.start)<=MaximumValue&&static_cast<std::size_t>(value.length)<=MaximumValue-static_cast<std::size_t>(value.start);}
  return value.length==static_cast<std::int32_t>(value.payload.size())&&value.start<=value.total&&value.length<=value.total-value.start;
}
}
std::expected<std::string,Error> encodeBody(const Data& value){
  if(!fields(value)){return std::unexpected(Error::Range);}std::string out;out.reserve(17+value.handler.size()+value.key.size()+value.payload.size());
  out+=static_cast<char>(value.type);out+=static_cast<char>(value.handler.size());out+=value.handler;out+=static_cast<char>(value.key.size());out+=value.key;
  integer(out,value.start);integer(out,value.length);integer(out,value.total);out+=static_cast<char>(value.payload.size()>>8);out+=static_cast<char>(value.payload.size());out+=value.payload;return out;
}
std::expected<Data,Error> decodeBody(std::string_view in){
  Data out;std::size_t p=0;
  const auto bytes=[&](std::size_t maximum)->std::expected<std::string,Error>{if(p==in.size()){return std::unexpected(Error::Input);}const auto n=static_cast<unsigned char>(in[p++]);if(n>maximum||n>in.size()-p){return std::unexpected(Error::Input);}std::string result(in.substr(p,n));p+=n;return result;};
  if(in.empty()){return std::unexpected(Error::Input);}out.type=static_cast<Type>(static_cast<unsigned char>(in[p++]));auto handler=bytes(64);if(!handler){return std::unexpected(handler.error());}out.handler=std::move(*handler);auto key=bytes(255);if(!key){return std::unexpected(key.error());}out.key=std::move(*key);
  if(in.size()-p<14){return std::unexpected(Error::Input);}out.start=integer(in,p);out.length=integer(in,p+4);out.total=integer(in,p+8);p+=12;const auto n=(static_cast<unsigned char>(in[p])<<8)|static_cast<unsigned char>(in[p+1]);p+=2;
  if(static_cast<std::size_t>(n)!=in.size()-p){return std::unexpected(Error::Input);}out.payload=std::string(in.substr(p));if(!fields(out)){return std::unexpected(Error::Range);}return out;
}
std::expected<std::string,Error> encode(const udp::EncodeFields& header,const Data& data){
  if(header.kind!=udp::Kind::Request||header.action!=1035||header.localVersion<51||header.remoteVersion<51){return std::unexpected(Error::Header);}auto body=encodeBody(data);if(!body){return std::unexpected(body.error());}auto out=udp::encodeHeader(header,*body);if(!out){return std::unexpected(Error::Header);}if(out->size()>MaximumDatagram){return std::unexpected(Error::Limit);}return std::move(*out);
}
std::expected<Packet,Error> decode(std::string wire,const udp::Context& context){
  if(wire.size()>MaximumDatagram){return std::unexpected(Error::Limit);}auto header=udp::decodeHeader(udp::Kind::Request,std::move(wire),context);if(!header||header->fields().action!=1035||(header->fields().version<51||header->fields().originatorVersion<51||header->fields().originatorVersion>55)){return std::unexpected(Error::Header);}auto body=decodeBody(header->body());if(!body){return std::unexpected(body.error());}return Packet{std::move(*header),std::move(*body)};
}
}
