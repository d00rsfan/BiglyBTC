// SPDX-License-Identifier: GPL-2.0-or-later
// Original UDP transport: Copyright (C) Azureus Software, Inc.
#include "headers.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <type_traits>
#include <utility>
namespace biglybtc::services::azureus_dht {
namespace {
constexpr std::int8_t originatorVersion=9,vendorVersion=14,flagsVersion=51,flags2Version=53,processingVersion=54;
constexpr std::int8_t minimumAz=51,minimumCvs=51,minimumBigly=51;
struct Failure { ErrorCode code; };
void require(bool value,ErrorCode code=ErrorCode::Input) { if (!value) { throw Failure{code}; } }
std::int64_t subtract(std::int64_t left,std::int64_t right) {
  return std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(left)-std::bit_cast<std::uint64_t>(right));
}
template<class T> void append(std::string& output,T value) {
  static_assert(std::is_unsigned_v<T>);
  static_assert(std::endian::native==std::endian::little || std::endian::native==std::endian::big);
  if constexpr(std::endian::native==std::endian::little) { value=std::byteswap(value); }
  output.append(reinterpret_cast<const char*>(&value),sizeof(value));
}
void byte(std::string& output,std::int8_t value) { append(output,std::bit_cast<std::uint8_t>(value)); }
void integer(std::string& output,std::int32_t value) { append(output,std::bit_cast<std::uint32_t>(value)); }
void integer(std::string& output,std::int64_t value) { append(output,std::bit_cast<std::uint64_t>(value)); }
void address(std::string& output,const std::optional<Address>& value) {
  if (!value) { output.append("\4\0\0\0\0\0\0",7);return; }
  require(value->length==4 || value->length==16);
  append(output,value->length);output.append(reinterpret_cast<const char*>(value->bytes.data()),value->length);append(output,value->port);
}
Address normalize(Address value) {
  if (value.length==16 && std::ranges::all_of(value.bytes.begin(),value.bytes.begin()+10,[](auto byte){return byte==0;})
      && value.bytes[10]==255 && value.bytes[11]==255) {
    const auto original=value.bytes;value.bytes={};std::copy(original.begin()+12,original.end(),value.bytes.begin());value.length=4;
  }
  return value;
}
std::uint16_t processing(std::int64_t now,std::int64_t received) {
  if (received==0) { return 0; }
  const auto low=static_cast<std::uint16_t>(std::bit_cast<std::uint64_t>(subtract(now,received)));
  const auto shortValue=std::bit_cast<std::int16_t>(low);
  return shortValue<=0?1:static_cast<std::uint16_t>(shortValue);
}
std::string write(const Header& value,std::int8_t localVersion,std::uint16_t processingTime,std::string_view body) {
  require(body.size()<=maximumPacketBytes,ErrorCode::Limit);
  std::string output;output.reserve(56+body.size());
  if (value.kind==Kind::Request) { integer(output,value.connection);integer(output,value.action);integer(output,value.transaction); }
  else { integer(output,value.action);integer(output,value.transaction);integer(output,value.connection); }
  byte(output,value.version);
  if (value.version>=vendorVersion) { byte(output,0); }
  if (value.version>=originatorVersion) { integer(output,value.network); }
  if (value.kind==Kind::Request) {
    if (value.version>=originatorVersion) { byte(output,localVersion); }
    address(output,value.originator);integer(output,value.instance);integer(output,value.time);
  } else { integer(output,value.instance); }
  if (value.version>=flagsVersion) { byte(output,value.flags); }
  if (value.version>=flags2Version) { byte(output,value.flags2); }
  if (value.kind==Kind::Reply && value.version>=processingVersion) { append(output,processingTime); }
  require(output.size()<=maximumPacketBytes-body.size(),ErrorCode::Limit);output+=body;return output;
}
class Reader {
public:
  explicit Reader(std::string_view input):input_(input) {}
  template<class T> T unsignedInteger() {
    static_assert(std::is_unsigned_v<T>);
    const auto value=take(sizeof(T));T result;std::memcpy(&result,value.data(),sizeof(result));
    if constexpr(std::endian::native==std::endian::little) { result=std::byteswap(result); }return result;
  }
  std::int8_t byte() { return std::bit_cast<std::int8_t>(unsignedInteger<std::uint8_t>()); }
  std::int32_t int32() { return std::bit_cast<std::int32_t>(unsignedInteger<std::uint32_t>()); }
  std::int64_t int64() { return std::bit_cast<std::int64_t>(unsignedInteger<std::uint64_t>()); }
  Address address() {
    Address result;result.length=unsignedInteger<std::uint8_t>();require(result.length<=16);
    const auto data=take(result.length);std::memcpy(result.bytes.data(),data.data(),data.size());result.port=unsignedInteger<std::uint16_t>();
    // Java consumes the port before InetAddress validates the address width.
    require(result.length==4 || result.length==16);return normalize(result);
  }
  std::size_t position() const { return position_; }
  std::optional<SkewObservation> skew;
private:
  std::string_view take(std::size_t length) {
    if (length>input_.size()-position_) {
      position_=input_.size();throw Failure{ErrorCode::Input}; // DataInputStream consumes a partial field before EOF.
    }
    auto result=input_.substr(position_,length);position_+=length;return result;
  }
  std::string_view input_;std::size_t position_=0;
};
}
std::int8_t effectiveVersion(std::int8_t base,std::int32_t explicitMinimum) {
  const auto value=std::max(explicitMinimum,static_cast<std::int32_t>(std::bit_cast<std::uint8_t>(base)));
  return std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(value));
}
std::int8_t minimumVersion(std::int32_t network,std::int32_t explicitMinimum) {
  const auto base=network==1?minimumCvs:(network==0 || network==3)?minimumAz:minimumBigly;
  return effectiveVersion(base,explicitMinimum);
}
DecodedHeader::DecodedHeader(std::string packet,Header fields,std::size_t consumed,std::optional<SkewObservation> skew)
    :packet_(std::move(packet)),fields_(std::move(fields)),consumed_(consumed),skew_(std::move(skew)) {}
DecodedHeader::DecodedHeader(DecodedHeader&& other) noexcept
    :packet_(std::move(other.packet_)),fields_(other.fields_),consumed_(std::exchange(other.consumed_,0)),skew_(std::move(other.skew_)) {
  other.packet_.clear();other.fields_={};other.skew_.reset();
}
DecodedHeader& DecodedHeader::operator=(DecodedHeader&& other) noexcept {
  if (this!=&other) {
    packet_=std::move(other.packet_);fields_=other.fields_;consumed_=std::exchange(other.consumed_,0);skew_=std::move(other.skew_);
    other.packet_.clear();other.fields_={};other.skew_.reset();
  }return *this;
}
std::expected<std::string,Error> encodeHeader(const EncodeFields& source,std::string_view body) {
  try {
    require(source.kind==Kind::Request || source.kind==Kind::Reply);
    Header value;value.kind=source.kind;value.action=source.action;value.transaction=source.transaction;value.connection=source.connection;
    value.version=std::min(source.localVersion,source.remoteVersion);value.network=source.network;value.instance=source.instance;
    value.originator=source.originator;value.time=source.now;value.flags=source.flags;value.flags2=source.flags2;
    return write(value,source.localVersion,processing(source.now,source.receivedAt),body);
  } catch (const Failure& error) { return std::unexpected(Error{error.code,0,{}}); }
}
std::expected<DecodedHeader,Error> decodeHeader(Kind kind,std::string packet,const Context& context) {
  if (packet.size()>maximumPacketBytes) { return std::unexpected(Error{ErrorCode::Limit,0,{}}); }
  Reader reader(packet);
  try {
    require(kind==Kind::Request || kind==Kind::Reply);
    Header result;result.kind=kind;
    if (kind==Kind::Request) { result.connection=reader.int64();result.action=reader.int32();result.transaction=reader.int32(); }
    else { result.action=reader.int32();result.transaction=reader.int32();result.connection=reader.int64(); }
    result.version=reader.byte();
    if (result.version>=vendorVersion) { result.vendor=reader.byte(); }
    if (result.version>=originatorVersion) { result.network=reader.int32(); }
    require(result.version>=minimumVersion(result.network,context.explicitMinimum),ErrorCode::Version);
    require(result.network==context.network,ErrorCode::Network);
    if (kind==Kind::Request) {
      result.originatorVersion=result.version>=originatorVersion?reader.byte():result.version;
      result.originator=reader.address();result.instance=reader.int32();result.time=reader.int64();
      reader.skew=SkewObservation{*result.originator,subtract(context.now,result.time)};
    } else { result.instance=reader.int32(); }
    if (result.version>=flagsVersion) { result.flags=reader.byte(); }
    if (result.version>=flags2Version) { result.flags2=reader.byte(); }
    if (kind==Kind::Reply && result.version>=processingVersion) { result.processingTime=reader.unsignedInteger<std::uint16_t>(); }
    return DecodedHeader(std::move(packet),std::move(result),reader.position(),reader.skew);
  } catch (const Failure& error) { return std::unexpected(Error{error.code,reader.position(),reader.skew}); }
}
std::expected<std::string,Error> reencodeHeader(const DecodedHeader& source,const Context& context) {
  if (source.rawPacket().empty()) { return std::unexpected(Error{ErrorCode::Input,0,{}}); }
  try { return write(source.fields(),context.localVersion,0,{}); }
  catch (const Failure& error) { return std::unexpected(Error{error.code,0,{}}); }
}
}
