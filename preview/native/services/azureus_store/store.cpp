// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHT packets: Copyright (C) Azureus Software, Inc.
#include "store.hpp"
#include <algorithm>
#include <bit>
#include <utility>
namespace biglybtc::services::azureus_store {
namespace {
struct Failure { ErrorCode code; };
void require(bool value,ErrorCode code=ErrorCode::Input) { if (!value) { throw Failure{code}; } }
Error fromHeader(const udp::Error& e) {
  constexpr ErrorCode codes[]{ErrorCode::Input,ErrorCode::Version,ErrorCode::Network,ErrorCode::Limit};return {codes[static_cast<unsigned>(e.code)],e.consumed,e.skew,{}};
}
void append(std::string& out,std::string_view part) { require(part.size()<=udp::maximumPacketBytes-out.size(),ErrorCode::Limit);out+=part; }
void byte(std::string& out,std::uint8_t b) { require(out.size()<udp::maximumPacketBytes,ErrorCode::Limit);out+=static_cast<char>(b); }
void writeInt(std::string& out,std::int32_t number) { const auto n=std::bit_cast<std::uint32_t>(number);for (int shift=24;shift>=0;shift-=8) { byte(out,static_cast<std::uint8_t>(n>>shift)); } }
std::int32_t readInt(std::string_view in,std::size_t offset) {
  std::uint32_t n=0;for (unsigned i=0;i<4;++i) { n=(n<<8)|static_cast<unsigned char>(in[offset+i]); }return std::bit_cast<std::int32_t>(n);
}
void bytes(std::string& out,std::optional<std::string_view> data) { require(data.has_value() && data->size()<=255);byte(out,static_cast<std::uint8_t>(data->size()));append(out,*data); }
class Reader {
public:
  explicit Reader(std::string_view input):input_(input) {}
  std::uint8_t byte() { require(offset_<input_.size());return static_cast<std::uint8_t>(input_[offset_++]); }
  std::int32_t integer() { std::uint32_t n=0;for (unsigned i=0;i<4;++i) { n=(n<<8)|byte(); }return std::bit_cast<std::int32_t>(n); }
  std::string bytes() {
    const auto length=byte();std::string result(length,'\0');const auto n=std::min<std::size_t>(length,input_.size()-offset_);
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
Decoded::Decoded(Decoded&& other) noexcept:header_(std::move(other.header_)),body_(std::move(other.body_)),consumed_(std::exchange(other.consumed_,0)) { other.body_=RequestBody{}; }
Decoded& Decoded::operator=(Decoded&& other) noexcept {
  if (this!=&other) { header_=std::move(other.header_);body_=std::move(other.body_);consumed_=std::exchange(other.consumed_,0);other.body_=RequestBody{}; }return *this;
}
Decoded& Decoded::operator=(const Decoded& other) { if (this!=&other) { auto copy=other;*this=std::move(copy); }return *this; }
std::expected<std::string,Error> encodeRequest(const RequestFields& f) {
  if (f.header.kind!=udp::Kind::Request || f.header.action!=1026) { return std::unexpected(Error{ErrorCode::Action,0,{},{}}); }
  auto header=udp::encodeHeader(f.header);if (!header) { return std::unexpected(fromHeader(header.error())); }std::string out=std::move(*header);const auto version=std::min(f.header.localVersion,f.header.remoteVersion);
  try {
    if (version>=7) { writeInt(out,f.randomId); }require(f.keys.has_value() && f.keys->size()<=255);byte(out,static_cast<std::uint8_t>(f.keys->size()));
    for (const auto& key:*f.keys) { bytes(out,key); }require(f.valueSets.has_value() && f.valueSets->size()<=255);byte(out,static_cast<std::uint8_t>(f.valueSets->size()));
    for (const auto& set:*f.valueSets) {
      const auto encoded=values::encodeValues(version,0,set);
      if (!encoded) { append(out,encoded.error().partial);throw Failure{encoded.error().code==values::ErrorCode::Limit?ErrorCode::Limit:ErrorCode::Input}; }append(out,*encoded);
    }return out;
  }catch (const Failure& e) { return std::unexpected(Error{e.code,0,{},std::move(out)}); }
}
std::expected<std::string,Error> encodeReply(const ReplyFields& f) {
  if (f.header.kind!=udp::Kind::Reply || f.header.action!=1027) { return std::unexpected(Error{ErrorCode::Action,0,{},{}}); }
  auto header=udp::encodeHeader(f.header);if (!header) { return std::unexpected(fromHeader(header.error())); }std::string out=std::move(*header);const auto version=std::min(f.header.localVersion,f.header.remoteVersion);
  try {
    if (version>=6) { bytes(out,f.diversification); }
    if (version>=55 && f.header.network==4) {
      const auto upload=stats::encodeUpload(f.upload);if (!upload) { append(out,upload.error().partial);throw Failure{upload.error().code==stats::ErrorCode::Limit?ErrorCode::Limit:ErrorCode::Input}; }append(out,*upload);
    }return out;
  }catch (const Failure& e) { return std::unexpected(Error{e.code,0,{},std::move(out)}); }
}
std::expected<Decoded,Error> decode(udp::Kind kind,std::string packet,const Context& context,nodes::NodeCache& cache) {
  if (packet.size()>udp::maximumPacketBytes) { return std::unexpected(Error{ErrorCode::Limit,0,{},{}}); }
  if (kind!=udp::Kind::Request && kind!=udp::Kind::Reply) { return std::unexpected(Error{ErrorCode::Input,0,{},{}}); }
  const std::size_t prefix=kind==udp::Kind::Request?16:8;
  if (packet.size()>=prefix && readInt(packet,kind==udp::Kind::Request?8:0)!=(kind==udp::Kind::Request?1026:1027)) { return std::unexpected(Error{ErrorCode::Action,prefix,{},{}}); }
  auto header=udp::decodeHeader(kind,std::move(packet),context.header);if (!header) { return std::unexpected(fromHeader(header.error())); }Reader reader(header->body());const auto version=header->fields().version;
  try {
    Body body;
    if (kind==udp::Kind::Request) {
      RequestBody request;if (version>=7) { request.randomId=reader.integer(); }const auto keys=reader.byte();
      for (unsigned i=0;i<keys;++i) { request.keys.push_back(reader.bytes()); }const auto sets=reader.byte();
      for (unsigned i=0;i<sets;++i) {
        auto set=values::decodeValuesPrefix(version,header->skew()->delta,reader.rest(),cache);
        if (!set) { reader.skip(set.error().consumed);constexpr ErrorCode codes[]{ErrorCode::Input,ErrorCode::Crypto,ErrorCode::Limit,ErrorCode::Thread};throw Failure{codes[static_cast<unsigned>(set.error().code)]}; }
        reader.skip(set->consumed);request.valueSets.push_back(std::move(set->values));
      }body=std::move(request);
    } else {
      ReplyBody reply;if (version>=6) { reply.diversification=reader.bytes(); }
      if (version>=55 && header->fields().network==4 && !reader.rest().empty()) {
        auto upload=stats::decodeUpload(reader.rest());require(upload.has_value(),ErrorCode::Limit);reader.skip(upload->consumed);reply.upload=std::move(upload->stats);
      }body=std::move(reply);
    }const auto consumed=header->consumed()+reader.position();return Decoded(std::move(*header),std::move(body),consumed);
  }catch (const Failure& e) { return std::unexpected(Error{e.code,header->consumed()+reader.position(),header->skew(),{}}); }
}
}
