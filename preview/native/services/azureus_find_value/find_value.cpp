// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHT packets: Copyright (C) Azureus Software, Inc.
#include "find_value.hpp"
#include <algorithm>
#include <bit>
#include <utility>
namespace biglybtc::services::azureus_find_value {
namespace {
struct Failure { ErrorCode code; };
void require(bool v,ErrorCode code=ErrorCode::Input) { if (!v) { throw Failure{code}; } }
Error fromHeader(const udp::Error& e) {
  constexpr ErrorCode codes[]{ErrorCode::Input,ErrorCode::Version,ErrorCode::Network,ErrorCode::Limit};return {codes[static_cast<unsigned>(e.code)],e.consumed,e.skew,0,{}};
}
void append(std::string& out,std::string_view part) { require(part.size()<=udp::maximumPacketBytes-out.size(),ErrorCode::Limit);out+=part; }
void byte(std::string& out,std::uint8_t b) { require(out.size()<udp::maximumPacketBytes,ErrorCode::Limit);out+=static_cast<char>(b); }
template<class E> void append(std::string& out,const std::expected<std::string,E>& part) {
  if (!part) { append(out,part.error().partial);throw Failure{part.error().code==decltype(part.error().code)::Limit?ErrorCode::Limit:ErrorCode::Input}; }append(out,std::string_view(*part));
}
std::int32_t readInt(std::string_view input,std::size_t offset) { std::uint32_t n=0;for (unsigned i=0;i<4;++i) { n=(n<<8)|static_cast<unsigned char>(input[offset+i]); }return std::bit_cast<std::int32_t>(n); }
std::int64_t negate(std::int64_t n) { return std::bit_cast<std::int64_t>(std::uint64_t{0}-std::bit_cast<std::uint64_t>(n)); }
class Reader {
public:
  explicit Reader(std::string_view input):input_(input) {}
  std::uint8_t byte() { require(offset_<input_.size());return static_cast<std::uint8_t>(input_[offset_++]); }
  std::string target() {
    const auto length=byte();require(length<=64);std::string result(length,'\0');const auto n=std::min<std::size_t>(length,input_.size()-offset_);
    std::copy_n(input_.begin()+static_cast<std::ptrdiff_t>(offset_),n,result.begin());offset_+=n;return result;
  }
  std::size_t position() const { return offset_; }
  std::string_view rest() const { return input_.substr(offset_); }
  void skip(std::size_t n) { require(n<=input_.size()-offset_);offset_+=n; }
private:
  std::string_view input_;std::size_t offset_=0;
};
}
Decoded::Decoded(udp::DecodedHeader h,Body b,std::size_t n):header_(std::move(h)),body_(std::move(b)),consumed_(n) {}
Decoded::Decoded(const Decoded&)=default;
Decoded::Decoded(Decoded&& other) noexcept:header_(std::move(other.header_)),body_(std::move(other.body_)),consumed_(std::exchange(other.consumed_,0)) { other.body_=RequestBody{}; }
Decoded& Decoded::operator=(Decoded&& other) noexcept {
  if (this!=&other) { header_=std::move(other.header_);body_=std::move(other.body_);consumed_=std::exchange(other.consumed_,0);other.body_=RequestBody{}; }return *this;
}
Decoded& Decoded::operator=(const Decoded& other) { if (this!=&other) { auto copy=other;*this=std::move(copy); }return *this; }
std::expected<std::string,Error> encodeRequest(const RequestFields& f) {
  if (f.header.kind!=udp::Kind::Request || f.header.action!=1030) { return std::unexpected(Error{ErrorCode::Action,0,{},0,{}}); }
  auto h=udp::encodeHeader(f.header);if (!h) { return std::unexpected(fromHeader(h.error())); }std::string out=std::move(*h);
  try {
    require(f.target.has_value() && f.target->size()<=64);byte(out,static_cast<std::uint8_t>(f.target->size()));append(out,*f.target);
    byte(out,static_cast<std::uint8_t>(f.flags));byte(out,static_cast<std::uint8_t>(std::min<std::int32_t>(f.maximumValues,255)));return out;
  }catch (const Failure& e) { return std::unexpected(Error{e.code,0,{},0,std::move(out)}); }
}
std::expected<std::string,Error> encodeReply(const ReplyFields& f) {
  if (f.header.kind!=udp::Kind::Reply || f.header.action!=1031) { return std::unexpected(Error{ErrorCode::Action,0,{},0,{}}); }
  auto h=udp::encodeHeader(f.header);if (!h) { return std::unexpected(fromHeader(h.error())); }std::string out=std::move(*h);const auto version=std::min(f.header.localVersion,f.header.remoteVersion);
  try {
    if (version>=6) { byte(out,f.continuation?1:0); }byte(out,f.values?1:0);
    if (f.values) {
      if (version>=6) { byte(out,std::bit_cast<std::uint8_t>(f.diversification)); }
      append(out,values::encodeValues(version,negate(f.remoteClockSkew),f.values));
    } else {
      require(f.contacts.has_value());append(out,nodes::encodeContacts(*f.contacts));if (version>=16) { append(out,positions::encodePositions(version,f.positions)); }
    }return out;
  }catch (const Failure& e) { return std::unexpected(Error{e.code,0,{},0,std::move(out)}); }
}
std::expected<Decoded,Error> decode(udp::Kind kind,std::string packet,const Context& context,nodes::NodeCache& cache) {
  if (packet.size()>udp::maximumPacketBytes) { return std::unexpected(Error{ErrorCode::Limit,0,{},0,{}}); }
  if (kind!=udp::Kind::Request && kind!=udp::Kind::Reply) { return std::unexpected(Error{ErrorCode::Input,0,{},0,{}}); }
  const std::size_t prefix=kind==udp::Kind::Request?16:8;
  if (packet.size()>=prefix && readInt(packet,kind==udp::Kind::Request?8:0)!=(kind==udp::Kind::Request?1030:1031)) { return std::unexpected(Error{ErrorCode::Action,prefix,{},0,{}}); }
  auto header=udp::decodeHeader(kind,std::move(packet),context.header);if (!header) { return std::unexpected(fromHeader(header.error())); }Reader reader(header->body());const auto version=header->fields().version;std::size_t failures=0;
  try {
    Body body;
    if (kind==udp::Kind::Request) { RequestBody request;request.target=reader.target();request.flags=std::bit_cast<std::int8_t>(reader.byte());request.maximumValues=reader.byte();body=std::move(request); }
    else {
      ReplyBody reply;if (version>=6) { reply.continuation=reader.byte()!=0; }const auto isValue=reader.byte()!=0;
      if (isValue) {
        FoundValues found;if (version>=6) { found.diversification=std::bit_cast<std::int8_t>(reader.byte()); }
        auto decoded=values::decodeValuesPrefix(version,0,reader.rest(),cache);
        if (!decoded) { reader.skip(decoded.error().consumed);constexpr ErrorCode codes[]{ErrorCode::Input,ErrorCode::Crypto,ErrorCode::Limit,ErrorCode::Thread};throw Failure{codes[static_cast<unsigned>(decoded.error().code)]}; }
        reader.skip(decoded->consumed);found.values=std::move(decoded->values);reply.result=std::move(found);
      } else {
        FoundContacts found;auto decoded=nodes::decodeContacts(reader.rest(),cache);
        if (!decoded) { reader.skip(decoded.error().consumed);constexpr ErrorCode codes[]{ErrorCode::Input,ErrorCode::Crypto,ErrorCode::Limit,ErrorCode::Thread};throw Failure{codes[static_cast<unsigned>(decoded.error().code)]}; }
        reader.skip(decoded->consumed);found.contacts=std::move(decoded->contacts);
        if (version>=16) {
          auto point=positions::decodePositions(version,context.v1Registered,reader.rest());
          if (!point) { reader.skip(point.error().consumed);failures=point.error().providerFailures;throw Failure{point.error().code==positions::ErrorCode::Limit?ErrorCode::Limit:ErrorCode::Input}; }
          reader.skip(point->consumed);failures=point->providerFailures;found.providerFailures=failures;found.positions=std::move(point->positions);
        }reply.result=std::move(found);
      }body=std::move(reply);
    }const auto consumed=header->consumed()+reader.position();return Decoded(std::move(*header),std::move(body),consumed);
  }catch (const Failure& e) { return std::unexpected(Error{e.code,header->consumed()+reader.position(),header->skew(),failures,{}}); }
}
}
