// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHT framing: Copyright (C) Azureus Software, Inc.
#include "positions.hpp"
#include <bit>
namespace biglybtc::services::azureus_positions {
namespace {
struct Failure { ErrorCode code=ErrorCode::Input; };
void require(bool value,ErrorCode code=ErrorCode::Input) { if (!value) { throw Failure{code}; } }
std::uint32_t canonical(std::uint32_t value) {
  return (value&0x7f800000)==0x7f800000 && (value&0x007fffff)!=0?0x7fc00000:value;
}
void byte(std::string& output,std::uint8_t value) {
  require(output.size()<maximumBodyBytes,ErrorCode::Limit);output+=static_cast<char>(value);
}
void bytes(std::string& output,std::string_view value) {
  require(value.size()<=maximumBodyBytes-output.size(),ErrorCode::Limit);output+=value;
}
void integer(std::string& output,std::uint32_t value) {
  for (int shift=24;shift>=0;shift-=8) { byte(output,static_cast<std::uint8_t>(value>>shift)); }
}
class Reader {
public:
  explicit Reader(std::string_view input):input_(input) {}
  std::uint8_t byte() {
    require(position_<input_.size());return static_cast<std::uint8_t>(input_[position_++]);
  }
  std::uint32_t integer() {
    std::uint32_t result=0;for (unsigned i=0;i<4;++i) { result=(result<<8)|byte(); }return result;
  }
  std::optional<WirePosition> provider(bool registered,std::int8_t type) {
    if (!registered || type!=1) { return {}; }
    const auto mark=position_;
    try {
      std::array<std::uint32_t,4> bits{};for (auto& value:bits) { value=integer(); }return vivaldiV1(bits);
    } catch (const Failure&) { position_=mark;++failures;return {}; }
  }
  std::size_t position() const { return position_; }
  std::size_t failures=0;
private:
  std::string_view input_;std::size_t position_=0;
};
}
WirePosition vivaldiV1(const std::array<std::uint32_t,4>& ieeeBits) {
  WirePosition value{1,16,{}};
  for (auto bits:ieeeBits) { integer(value.bytes,canonical(bits)); }return value;
}
std::expected<std::string,Error> encodePositions(std::int8_t version,std::optional<std::span<const std::optional<WirePosition>>> positions) {
  std::string output;
  try {
    if (version<10) { return output; }
    require(positions.has_value());
    require(positions->size()<=maximumBodyBytes,ErrorCode::Limit);
    if (version>=15) {
      bool found=false;
      for (const auto& position:*positions) { require(position.has_value());if (position->type==1) { found=true;break; } }
      const bool fallback=!found && version<51;
      byte(output,static_cast<std::uint8_t>(positions->size()+static_cast<std::size_t>(fallback)));
      for (const auto& position:*positions) {
        require(position.has_value());byte(output,std::bit_cast<std::uint8_t>(position->type));
        byte(output,static_cast<std::uint8_t>(position->declaredSize));bytes(output,position->bytes);
      }
      if (fallback) {
        const auto position=vivaldiV1({0,0,0,0x7fc00000});byte(output,1);byte(output,16);bytes(output,position.bytes);
      }
    } else {
      bool found=false;
      for (const auto& position:*positions) {
        require(position.has_value());if (position->type==1) { bytes(output,position->bytes);found=true;break; }
      }
      require(found);
    }
    return output;
  } catch (const Failure& failure) { return std::unexpected(Error{failure.code,0,0,std::move(output)}); }
}
std::expected<Decoded,Error> decodePositions(std::int8_t version,bool v1Registered,std::string_view input) {
  if (input.size()>maximumBodyBytes) { return std::unexpected(Error{ErrorCode::Limit,0,0,{}}); }
  Reader reader(input);Decoded result;
  try {
    if (version>=15) {
      const auto count=reader.byte();result.positions.reserve(count);
      for (unsigned i=0;i<count;++i) {
        const auto type=std::bit_cast<std::int8_t>(reader.byte()),size=std::bit_cast<std::int8_t>(reader.byte());
        auto position=reader.provider(v1Registered,type);
        if (position) { result.positions.push_back(std::move(position)); }
        else { for (int j=0;j<size;++j) { reader.byte(); } } // Java's signed size, including negative -> no skip.
      }
    } else if (version>=10) { result.positions.push_back(reader.provider(v1Registered,1)); }
    result.consumed=reader.position();result.providerFailures=reader.failures;result.trailing=input.substr(result.consumed);return result;
  } catch (const Failure& failure) { return std::unexpected(Error{failure.code,reader.position(),reader.failures,{}}); }
}
}
