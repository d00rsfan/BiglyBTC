// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHT packets: Copyright (C) Azureus Software, Inc.
#include "find_node.hpp"
#include <algorithm>
#include <bit>
#include <utility>
namespace biglybtc::services::azureus_find_node {
namespace {
struct Failure { ErrorCode code; };
void require(bool value,ErrorCode code=ErrorCode::Input) { if (!value) { throw Failure{code}; } }
Error fromHeader(const udp::Error& source) {
  constexpr ErrorCode codes[]{ErrorCode::Input,ErrorCode::Version,ErrorCode::Network,ErrorCode::Limit};
  return {codes[static_cast<unsigned>(source.code)],source.consumed,source.skew,0,{}};
}
void append(std::string& output,std::string_view part) {
  require(part.size()<=udp::maximumPacketBytes-output.size(),ErrorCode::Limit);output+=part;
}
template<class E> void append(std::string& output,const std::expected<std::string,E>& part) {
  if (!part) {
    append(output,part.error().partial);
    throw Failure{part.error().code==decltype(part.error().code)::Limit?ErrorCode::Limit:ErrorCode::Input};
  }append(output,std::string_view(*part));
}
void writeInt(std::string& output,std::int32_t number) {
  require(4<=udp::maximumPacketBytes-output.size(),ErrorCode::Limit);const auto value=std::bit_cast<std::uint32_t>(number);
  for (int shift=24;shift>=0;shift-=8) { output+=static_cast<char>(value>>shift); }
}
std::int32_t readInt(std::string_view input,std::size_t offset) {
  std::uint32_t value=0;for (unsigned i=0;i<4;++i) { value=(value<<8)|static_cast<unsigned char>(input[offset+i]); }return std::bit_cast<std::int32_t>(value);
}
class Reader {
public:
  explicit Reader(std::string_view input):input_(input) {}
  std::uint8_t byte() { require(offset_<input_.size());return static_cast<std::uint8_t>(input_[offset_++]); }
  std::int32_t integer() {
    if (input_.size()-offset_<4) { offset_=input_.size();throw Failure{ErrorCode::Input}; }
    const auto value=readInt(input_,offset_);offset_+=4;return value;
  }
  std::string target() {
    const auto length=byte();require(length<=64);std::string value(length,'\0');
    const auto received=std::min<std::size_t>(length,input_.size()-offset_);std::copy_n(input_.begin()+static_cast<std::ptrdiff_t>(offset_),received,value.begin());offset_+=received;return value;
  }
  std::size_t position() const { return offset_; }
  void skip(std::size_t count) { require(count<=input_.size()-offset_);offset_+=count; }
  std::string_view rest() const { return input_.substr(offset_); }
private:
  std::string_view input_;std::size_t offset_=0;
};
}
Decoded::Decoded(udp::DecodedHeader header,Body body,std::size_t consumed)
    :header_(std::move(header)),body_(std::move(body)),consumed_(consumed) {}
Decoded::Decoded(Decoded&& other) noexcept
    :header_(std::move(other.header_)),body_(std::move(other.body_)),consumed_(std::exchange(other.consumed_,0)) { other.body_=RequestBody{}; }
Decoded& Decoded::operator=(Decoded&& other) noexcept {
  if (this!=&other) { header_=std::move(other.header_);body_=std::move(other.body_);consumed_=std::exchange(other.consumed_,0);other.body_=RequestBody{}; }return *this;
}
Decoded& Decoded::operator=(const Decoded& other) {
  if (this!=&other) { auto copy=other;*this=std::move(copy); }return *this;
}
std::expected<std::string,Error> encodeRequest(const RequestFields& fields) {
  if (fields.header.kind!=udp::Kind::Request || fields.header.action!=1028) { return std::unexpected(Error{ErrorCode::Action,0,{},0,{}}); }
  auto header=udp::encodeHeader(fields.header);if (!header) { return std::unexpected(fromHeader(header.error())); }
  std::string output=std::move(*header);const auto version=std::min(fields.header.localVersion,fields.header.remoteVersion);
  try {
    require(fields.target.has_value() && fields.target->size()<=64);output+=static_cast<char>(fields.target->size());append(output,*fields.target);
    if (version>=22) { writeInt(output,fields.status);writeInt(output,fields.estimatedSize); }
    if (version>=55 && fields.header.network==4) { append(output,stats::encodeUpload(fields.upload)); }
    return output;
  } catch (const Failure& failure) { return std::unexpected(Error{failure.code,0,{},0,std::move(output)}); }
}
std::expected<std::string,Error> encodeReply(const ReplyFields& fields) {
  if (fields.header.kind!=udp::Kind::Reply || fields.header.action!=1029) { return std::unexpected(Error{ErrorCode::Action,0,{},0,{}}); }
  auto header=udp::encodeHeader(fields.header);if (!header) { return std::unexpected(fromHeader(header.error())); }
  std::string output=std::move(*header);const auto version=std::min(fields.header.localVersion,fields.header.remoteVersion);
  try {
    if (version>=7) { writeInt(output,fields.randomId); }
    if (version>=12) { writeInt(output,fields.status); }
    if (version>=13) { writeInt(output,fields.estimatedSize); }
    append(output,positions::encodePositions(version,fields.positions));
    require(fields.contacts.has_value());append(output,nodes::encodeContacts(*fields.contacts));return output;
  } catch (const Failure& failure) { return std::unexpected(Error{failure.code,0,{},0,std::move(output)}); }
}
std::expected<Decoded,Error> decode(udp::Kind kind,std::string packet,const Context& context,nodes::NodeCache& cache) {
  if (packet.size()>udp::maximumPacketBytes) { return std::unexpected(Error{ErrorCode::Limit,0,{},0,{}}); }
  if (kind!=udp::Kind::Request && kind!=udp::Kind::Reply) { return std::unexpected(Error{ErrorCode::Input,0,{},0,{}}); }
  const std::size_t prefix=kind==udp::Kind::Request?16:8;
  if (packet.size()>=prefix && readInt(packet,kind==udp::Kind::Request?8:0)!=(kind==udp::Kind::Request?1028:1029)) {
    return std::unexpected(Error{ErrorCode::Action,prefix,{},0,{}});
  }
  auto header=udp::decodeHeader(kind,std::move(packet),context.header);if (!header) { return std::unexpected(fromHeader(header.error())); }
  const auto version=header->fields().version;Reader reader(header->body());std::size_t failures=0;
  try {
    Body body;
    if (kind==udp::Kind::Request) {
      RequestBody request;request.target=reader.target();
      if (version>=22) { request.status=reader.integer();request.estimatedSize=reader.integer(); }
      if (version>=55 && header->fields().network==4 && !reader.rest().empty()) {
        auto upload=stats::decodeUpload(reader.rest());require(upload.has_value(),ErrorCode::Limit);reader.skip(upload->consumed);request.upload=std::move(upload->stats);
      }body=std::move(request);
    } else {
      ReplyBody reply;
      if (version>=7) { reply.randomId=reader.integer(); }
      if (version>=12) { reply.status=reader.integer(); }
      if (version>=13) { reply.estimatedSize=reader.integer(); }
      auto points=positions::decodePositions(version,context.v1Registered,reader.rest());
      if (!points) {
        reader.skip(points.error().consumed);failures=points.error().providerFailures;throw Failure{points.error().code==positions::ErrorCode::Limit?ErrorCode::Limit:ErrorCode::Input};
      }
      reader.skip(points->consumed);failures=points->providerFailures;reply.providerFailures=failures;reply.positions=std::move(points->positions);
      auto contacts=nodes::decodeContacts(reader.rest(),cache);
      if (!contacts) {
        reader.skip(contacts.error().consumed);constexpr ErrorCode codes[]{ErrorCode::Input,ErrorCode::Crypto,ErrorCode::Limit,ErrorCode::Thread};throw Failure{codes[static_cast<unsigned>(contacts.error().code)]};
      }reader.skip(contacts->consumed);reply.contacts=std::move(contacts->contacts);body=std::move(reply);
    }
    const auto consumed=header->consumed()+reader.position();return Decoded(std::move(*header),std::move(body),consumed);
  } catch (const Failure& failure) { return std::unexpected(Error{failure.code,header->consumed()+reader.position(),header->skew(),failures,{}}); }
}
}
