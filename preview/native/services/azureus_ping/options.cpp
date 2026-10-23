// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHT wire helpers: Copyright (C) Azureus Software, Inc.
#include "options.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
namespace biglybtc::services::azureus_ping {
namespace {
struct Failure { ErrorCode code=ErrorCode::Input; };
void require(bool value,ErrorCode code=ErrorCode::Input) { if (!value) { throw Failure{code}; } }
void byte(std::string& output,std::uint8_t value) { output+=static_cast<char>(value); }
void integer(std::string& output,std::uint32_t value) {
  for (int shift=24;shift>=0;shift-=8) { byte(output,static_cast<std::uint8_t>(value>>shift)); }
}
// Java long->float is round-to-nearest/ties-even regardless of native fenv.
std::uint32_t floatBits(std::int64_t value) {
  if (value==0) { return 0; }
  const auto sign=value<0?0x80000000U:0U;
  const auto magnitude=value<0?0-std::bit_cast<std::uint64_t>(value):static_cast<std::uint64_t>(value);
  unsigned exponent=63-std::countl_zero(magnitude);
  std::uint64_t significand;
  if (exponent<=23) { significand=magnitude<<(23-exponent); }
  else {
    const auto shift=exponent-23;significand=magnitude>>shift;
    const auto remainder=magnitude&((std::uint64_t{1}<<shift)-1),half=std::uint64_t{1}<<(shift-1);
    if (remainder>half || (remainder==half && (significand&1))) { ++significand; }
    if (significand==(std::uint64_t{1}<<24)) { significand>>=1;++exponent; }
  }
  return sign|((exponent+127)<<23)|(static_cast<std::uint32_t>(significand)&0x7fffff);
}
std::int64_t floatLong(std::uint32_t bits) {
  static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559);
  const float value=std::bit_cast<float>(bits);
  if (std::isnan(value)) { return 0; }
  if (value>=0x1p63f) { return std::numeric_limits<std::int64_t>::max(); }
  if (value<=-0x1p63f) { return std::numeric_limits<std::int64_t>::min(); }
  return static_cast<std::int64_t>(value);
}
void upload(std::string& output,const UploadSnapshot& value) {
  require(value.received.size()<=static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())
      && value.sent.size()<=static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()),ErrorCode::Limit);
  byte(output,2);integer(output,floatBits(value.totalReceived));integer(output,floatBits(value.totalSent));
  auto received=value.received.size(),sent=value.sent.size();
  if (received>24 || sent>24-received) {
    if (received<12) { sent=24-received; }
    else if (sent<12) { received=24-sent; }
    else { received=sent=12; }
  }
  const auto write=[&output](std::span<const CountryRate> countries,std::size_t count,bool incoming) {
    byte(output,static_cast<std::uint8_t>(count));
    for (std::size_t i=0;i<count;++i) {
      std::u16string_view code=countries[i].code;
      if (code.size()>2) { code=code==u"I2P"?u"X0":code==u"Tor"?u"X1":u"X2"; }
      require(!code.empty());byte(output,static_cast<std::uint8_t>(code[0]));
      require(code.size()>1);byte(output,static_cast<std::uint8_t>(code[1]));
      integer(output,floatBits(incoming?countries[i].received:countries[i].sent));
    }
  };
  write(value.received,received,true);write(value.sent,sent,false);
}
class Reader {
public:
  explicit Reader(std::string_view bytes):bytes_(bytes) {}
  std::uint8_t byte() {
    if (position_==bytes_.size()) { throw Failure{}; }
    return static_cast<std::uint8_t>(bytes_[position_++]);
  }
  std::uint32_t integer() {
    std::uint32_t result=0;for (unsigned i=0;i<4;++i) { result=(result<<8)|byte(); }return result;
  }
  std::u16string country() {
    const auto character=[this]() { const auto b=byte();return static_cast<char16_t>(b<128?b:0xff00|b); };
    std::u16string code;code+=character();code+=character();
    return code==u"X0"?u"I2P":code==u"X1"?u"Tor":code;
  }
  std::size_t position() const { return position_; }
private:
  std::string_view bytes_;std::size_t position_=0;
};
std::optional<UploadStats> readUpload(Reader& reader) {
  try {
    const auto version=reader.byte();
    if (version==0 || version==1) {
      const auto count=reader.byte();if (count==0 || count>24) { return {}; }
      LegacyStats result;result.reserve(count);
      for (unsigned i=0;i<count;++i) {
        auto code=reader.country();const auto bits=reader.integer();
        // Original unsuffixed 0x00000000ffffffff is int -1; it does not
        // make the readInt unsigned before widening and multiplying.
        const auto value=version==0?static_cast<std::int64_t>(std::bit_cast<std::int32_t>(bits))*1024:floatLong(bits);
        result.push_back({std::move(code),value,0});
      }
      return UploadStats{std::move(result)};
    }
    if (version==2) {
      const auto received=floatLong(reader.integer());const auto sent=floatLong(reader.integer());
      CountryMap result;result[u""]={received,sent};
      for (unsigned direction=0;direction<2;++direction) {
        const auto count=reader.byte();
        if (count>0 && count<=24) {
          for (unsigned i=0;i<count;++i) {
            auto code=reader.country();const auto value=floatLong(reader.integer());result[std::move(code)][direction]=value;
          }
        }
      }
      return UploadStats{std::move(result)};
    }
    return {};
  } catch (const Failure&) { return {}; }
}
}
std::expected<std::string,Error> encodeOptions(const EncodeOptions& value) {
  std::string output;
  try {
    if (value.version>=52) {
      const auto size=value.networks && value.counts?value.networks->size():0;
      require(size<=16);byte(output,static_cast<std::uint8_t>(size));
      for (std::size_t i=0;i<size;++i) {
        byte(output,static_cast<std::uint8_t>((*value.networks)[i]));
        require(i<value.counts->size());byte(output,static_cast<std::uint8_t>((*value.counts)[i]));
      }
    }
    if (value.network==4 && value.version>=55) {
      if (value.upload) { upload(output,*value.upload); }else { byte(output,255); }
    }
    require(output.size()<=maximumBodyBytes,ErrorCode::Limit);return output;
  } catch (const Failure& failure) { return std::unexpected(Error{failure.code,0,std::move(output)}); }
}
std::expected<std::string,Error> encodeUpload(std::optional<UploadSnapshot> value) {
  std::string output;
  try {
    if (value) { upload(output,*value); }else { byte(output,255); }
    return output;
  } catch (const Failure& failure) { return std::unexpected(Error{failure.code,0,std::move(output)}); }
}
std::expected<UploadDecoded,Error> decodeUpload(std::string_view input) {
  if (input.size()>maximumBodyBytes) { return std::unexpected(Error{ErrorCode::Limit,0,{}}); }
  Reader reader(input);auto stats=readUpload(reader);
  return UploadDecoded{std::move(stats),reader.position(),std::string(input.substr(reader.position()))};
}
std::expected<OptionsDecoded,Error> decodeOptions(std::int8_t version,std::int32_t network,std::string_view input) {
  if (input.size()>maximumBodyBytes) { return std::unexpected(Error{ErrorCode::Limit,0,{}}); }
  Reader reader(input);OptionsDecoded result;
  try {
    if (version>=52) {
      const auto count=reader.byte();require(count<=16);result.networks.reserve(count);result.counts.reserve(count);
      for (unsigned i=0;i<count;++i) { result.networks.push_back(reader.byte());result.counts.push_back(reader.byte()); }
    }
    if (network==4 && version>=55) { result.upload=readUpload(reader); }
    result.consumed=reader.position();result.trailing=input.substr(result.consumed);return result;
  } catch (const Failure& failure) { return std::unexpected(Error{failure.code,reader.position(),{}}); }
}
}
