// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHT packets: Copyright (C) Azureus Software, Inc.
#include "ping.hpp"
#include <algorithm>
#include <bit>
#include <utility>
namespace biglybtc::services::azureus_packets {
namespace {
struct Failure { ErrorCode code; };
Error fromHeader(const udp::Error& source) {
  constexpr ErrorCode codes[]{ErrorCode::Input,ErrorCode::Version,ErrorCode::Network,ErrorCode::Limit};
  return {codes[static_cast<unsigned>(source.code)],source.consumed,source.skew,0,{}};
}
void append(std::string& output,std::string_view part) {
  if (part.size()>udp::maximumPacketBytes-output.size()) { throw Failure{ErrorCode::Limit}; }output+=part;
}
template<class E> void append(std::string& output,const std::expected<std::string,E>& part) {
  if (!part) {
    append(output,part.error().partial);
    throw Failure{part.error().code==decltype(part.error().code)::Limit?ErrorCode::Limit:ErrorCode::Input};
  }
  append(output,std::string_view(*part));
}
std::int32_t action(std::string_view packet,std::size_t offset) {
  std::uint32_t value=0;for (std::size_t i=0;i<4;++i) { value=(value<<8)|static_cast<unsigned char>(packet[offset+i]); }
  return std::bit_cast<std::int32_t>(value);
}
}
Decoded::Decoded(udp::DecodedHeader header,Body body,std::size_t consumed)
    :header_(std::move(header)),body_(std::move(body)),consumed_(consumed) {}
Decoded::Decoded(Decoded&& other) noexcept
    :header_(std::move(other.header_)),body_(std::move(other.body_)),consumed_(std::exchange(other.consumed_,0)) { other.body_=RequestBody{}; }
Decoded& Decoded::operator=(Decoded&& other) noexcept {
  if (this!=&other) {
    header_=std::move(other.header_);body_=std::move(other.body_);consumed_=std::exchange(other.consumed_,0);other.body_=RequestBody{};
  }return *this;
}
Decoded& Decoded::operator=(const Decoded& other) {
  if (this!=&other) { auto copy=other;*this=std::move(copy); }return *this;
}
std::expected<std::string,Error> encodeRequest(const RequestFields& fields) {
  if (fields.header.kind!=udp::Kind::Request || fields.header.action!=1024) { return std::unexpected(Error{ErrorCode::Action,0,{},0,{}}); }
  auto header=udp::encodeHeader(fields.header);if (!header) { return std::unexpected(fromHeader(header.error())); }
  std::string output=std::move(*header);
  try {
    stats::EncodeOptions options;options.version=std::min(fields.header.localVersion,fields.header.remoteVersion);
    options.network=fields.header.network;options.networks=fields.networks;options.counts=fields.counts;options.upload=fields.upload;
    append(output,stats::encodeOptions(options));return output;
  } catch (const Failure& failure) { return std::unexpected(Error{failure.code,0,{},0,std::move(output)}); }
}
std::expected<std::string,Error> encodeReply(const ReplyFields& fields) {
  if (fields.header.kind!=udp::Kind::Reply || fields.header.action!=1025) { return std::unexpected(Error{ErrorCode::Action,0,{},0,{}}); }
  auto header=udp::encodeHeader(fields.header);if (!header) { return std::unexpected(fromHeader(header.error())); }
  std::string output=std::move(*header);const auto version=std::min(fields.header.localVersion,fields.header.remoteVersion);
  try {
    append(output,positions::encodePositions(version,fields.positions));
    if (version>=52) {
      auto selected=fields.contacts;
      if (selected) { selected=selected->first(std::min<std::size_t>(selected->size(),16)); }
      append(output,contacts::encodeContacts(selected));
    }
    if (version>=55 && fields.header.network==4) { append(output,stats::encodeUpload(fields.upload)); }
    return output;
  } catch (const Failure& failure) { return std::unexpected(Error{failure.code,0,{},0,std::move(output)}); }
}
std::expected<Decoded,Error> decode(udp::Kind kind,std::string packet,const Context& context) {
  if (packet.size()>udp::maximumPacketBytes) { return std::unexpected(Error{ErrorCode::Limit,0,{},0,{}}); }
  if (kind!=udp::Kind::Request && kind!=udp::Kind::Reply) { return std::unexpected(Error{ErrorCode::Input,0,{},0,{}}); }
  const std::size_t prefix=kind==udp::Kind::Request?16:8;
  if (packet.size()>=prefix && action(packet,kind==udp::Kind::Request?8:0)!=(kind==udp::Kind::Request?1024:1025)) {
    return std::unexpected(Error{ErrorCode::Action,prefix,{},0,{}});
  }
  auto header=udp::decodeHeader(kind,std::move(packet),context.header);if (!header) { return std::unexpected(fromHeader(header.error())); }
  const auto input=header->body();const auto version=header->fields().version;const auto network=header->fields().network;
  if (kind==udp::Kind::Request) {
    auto options=stats::decodeOptions(version,network,input);
    if (!options) { return std::unexpected(Error{ErrorCode::Input,header->consumed()+options.error().consumed,header->skew(),0,{}}); }
    const auto consumed=header->consumed()+options->consumed;
    return Decoded(std::move(*header),RequestBody{std::move(options->networks),std::move(options->counts),std::move(options->upload)},consumed);
  }
  auto point=positions::decodePositions(version,context.v1Registered,input);
  if (!point) { return std::unexpected(Error{ErrorCode::Input,header->consumed()+point.error().consumed,header->skew(),point.error().providerFailures,{}}); }
  std::size_t consumed=point->consumed;ReplyBody body;body.positions=std::move(point->positions);body.providerFailures=point->providerFailures;
  if (version>=52) {
    auto alternative=contacts::decodeContacts(input.substr(consumed),context.monotonicNow);
    if (!alternative) { return std::unexpected(Error{ErrorCode::Input,header->consumed()+consumed+alternative.error().consumed,header->skew(),body.providerFailures,{}}); }
    consumed+=alternative->consumed;body.contacts=std::move(alternative->contacts);
  }
  if (version>=55 && network==4) {
    auto upload=stats::decodeUpload(input.substr(consumed));
    if (!upload) { return std::unexpected(Error{ErrorCode::Limit,header->consumed()+consumed,header->skew(),body.providerFailures,{}}); }
    consumed+=upload->consumed;body.upload=std::move(upload->stats);
  }
  consumed+=header->consumed();return Decoded(std::move(*header),std::move(body),consumed);
}
}
