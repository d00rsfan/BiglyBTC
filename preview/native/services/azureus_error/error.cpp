// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHT packets: Copyright (C) Azureus Software, Inc.
#include "error.hpp"
#include <algorithm>
#include <bit>
#include <utility>

namespace biglybtc::services::azureus_error {
namespace {
struct Failure { ErrorCode code; };
void require(bool value,ErrorCode code=ErrorCode::Input) {
  if (!value) { throw Failure{code}; }
}
Error fromHeader(const udp::Error& error) {
  constexpr ErrorCode codes[]{ErrorCode::Input,ErrorCode::Version,ErrorCode::Network,ErrorCode::Limit};
  return {codes[static_cast<unsigned>(error.code)],error.consumed,error.skew,{}};
}
void append(std::string& output,std::string_view bytes) {
  require(bytes.size()<=udp::maximumPacketBytes-output.size(),ErrorCode::Limit);
  output+=bytes;
}
void byte(std::string& output,std::uint8_t value) {
  require(output.size()<udp::maximumPacketBytes,ErrorCode::Limit);
  output+=static_cast<char>(value);
}
void shortInteger(std::string& output,std::uint16_t value) {
  byte(output,static_cast<std::uint8_t>(value>>8));
  byte(output,static_cast<std::uint8_t>(value));
}
void integer(std::string& output,std::int32_t value) {
  const auto bits=std::bit_cast<std::uint32_t>(value);
  for (unsigned shift=32;shift!=0;shift-=8) { byte(output,static_cast<std::uint8_t>(bits>>(shift-8))); }
}
std::int32_t readInt(std::string_view input) {
  std::uint32_t value=0;
  for (unsigned i=0;i<4;++i) { value=(value<<8)|static_cast<unsigned char>(input[i]); }
  return std::bit_cast<std::int32_t>(value);
}
void address(std::string& output,const Endpoint& endpoint) {
  if (!endpoint.resolved) { append(output,std::string_view("\4\0\0\0\0\0\0",7));return; }
  const auto& value=*endpoint.resolved;
  require(value.length==4 || value.length==16);
  byte(output,value.length);
  append(output,std::string_view(reinterpret_cast<const char*>(value.bytes.data()),value.length));
  shortInteger(output,value.port);
}
void byteArray(std::string& output,std::optional<std::string_view> value,std::size_t maximum) {
  require(value.has_value() && value->size()<=maximum);
  if (maximum<256) { byte(output,static_cast<std::uint8_t>(value->size())); }
  else { shortInteger(output,static_cast<std::uint16_t>(value->size())); }
  append(output,*value);
}
udp::Address normalize(udp::Address value) {
  if (value.length==16 && std::ranges::all_of(value.bytes.begin(),value.bytes.begin()+10,[](auto b){return b==0;})
      && value.bytes[10]==255 && value.bytes[11]==255) {
    const auto original=value.bytes;value.bytes={};
    std::copy(original.begin()+12,original.end(),value.bytes.begin());value.length=4;
  }
  return value;
}
class Reader final {
public:
  explicit Reader(std::string_view input):input_(input) {}
  std::uint8_t byte() {
    require(position_<input_.size());return static_cast<std::uint8_t>(input_[position_++]);
  }
  std::uint16_t shortInteger() {
    const auto high=byte();const auto low=byte();return static_cast<std::uint16_t>((high<<8)|low);
  }
  std::int32_t integer() {
    std::uint32_t result=0;
    for (unsigned i=0;i<4;++i) { result=(result<<8)|byte(); }
    return std::bit_cast<std::int32_t>(result);
  }
  std::string byteArray(std::size_t maximum) {
    const std::size_t length=maximum<256?byte():shortInteger();require(length<=maximum);
    std::string result(length,'\0');
    const auto available=std::min(length,input_.size()-position_);
    std::copy_n(input_.begin()+static_cast<std::ptrdiff_t>(position_),available,result.begin());
    position_+=available;return result;
  }
  udp::Address address() {
    const auto bytes=byteArray(16);
    const auto port=shortInteger(); // Source validates the width after this read.
    require(bytes.size()==4 || bytes.size()==16);
    udp::Address result;result.length=static_cast<std::uint8_t>(bytes.size());result.port=port;
    std::copy(bytes.begin(),bytes.end(),result.bytes.begin());return normalize(result);
  }
  std::size_t position() const { return position_; }
private:
  std::string_view input_;
  std::size_t position_=0;
};
}

Decoded::Decoded(udp::DecodedHeader header,Body body,std::size_t consumed)
    :header_(std::move(header)),body_(std::move(body)),consumed_(consumed) {}
Decoded::Decoded(const Decoded&)=default;
Decoded& Decoded::operator=(const Decoded& other) {
  if (this!=&other) { auto copy=other;*this=std::move(copy); }return *this;
}
Decoded::Decoded(Decoded&& other) noexcept
    :header_(std::move(other.header_)),body_(std::move(other.body_)),consumed_(std::exchange(other.consumed_,0)) {
  other.body_={};
}
Decoded& Decoded::operator=(Decoded&& other) noexcept {
  if (this!=&other) {
    header_=std::move(other.header_);body_=std::move(other.body_);
    consumed_=std::exchange(other.consumed_,0);other.body_={};
  }
  return *this;
}
std::expected<std::string,Error> encodeReply(const ReplyFields& fields) {
  if (fields.header.kind!=udp::Kind::Reply || fields.header.action!=1032) {
    return std::unexpected(Error{ErrorCode::Action,0,{},{}});
  }
  auto header=udp::encodeHeader(fields.header);
  if (!header) { return std::unexpected(fromHeader(header.error())); }
  std::string output=std::move(*header);
  try {
    integer(output,fields.type);
    if (fields.type==1) { require(fields.originator.has_value());address(output,*fields.originator); }
    else if (fields.type==2) { byteArray(output,fields.request,255);byteArray(output,fields.signature,65535); }
    return output;
  } catch (const Failure& error) { return std::unexpected(Error{error.code,0,{},std::move(output)}); }
}
std::expected<Decoded,Error> decode(std::string packet,const udp::Context& context) {
  if (packet.size()>udp::maximumPacketBytes) { return std::unexpected(Error{ErrorCode::Limit,0,{},{}}); }
  // Dispatch consumes action and transaction before constructing a DHT reply.
  if (packet.size()>=8 && readInt(packet)!=1032) { return std::unexpected(Error{ErrorCode::Action,8,{},{}}); }
  auto header=udp::decodeHeader(udp::Kind::Reply,std::move(packet),context);
  if (!header) { return std::unexpected(fromHeader(header.error())); }
  Reader reader(header->body());
  try {
    Body body;body.type=reader.integer();
    if (body.type==1) { body.originator=reader.address(); }
    else if (body.type==2) { body.request=reader.byteArray(255);body.signature=reader.byteArray(65535); }
    const auto consumed=header->consumed()+reader.position();
    return Decoded(std::move(*header),std::move(body),consumed);
  } catch (const Failure& error) {
    return std::unexpected(Error{error.code,header->consumed()+reader.position(),header->skew(),{}});
  }
}
}
