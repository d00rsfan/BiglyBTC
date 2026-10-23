// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHT wire: Copyright (C) Azureus Software, Inc.
#include "contacts.hpp"
#include <algorithm>
#include <bit>
#include <utility>
namespace biglybtc::services::azureus_contacts {
namespace {
struct Failure { ErrorCode code=ErrorCode::Input; };
void require(bool value,ErrorCode code=ErrorCode::Input) { if (!value) { throw Failure{code}; } }
std::int32_t seconds(std::int64_t milliseconds) {
  return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(milliseconds/1000));
}
std::int32_t subtract(std::int32_t left,std::int32_t right) {
  return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(left)-std::bit_cast<std::uint32_t>(right));
}
std::int32_t identity(std::string_view bytes) {
  std::uint32_t value=1;
  for (unsigned char byte:bytes) { value=value*31+static_cast<std::uint32_t>(static_cast<std::int32_t>(std::bit_cast<std::int8_t>(byte))); }
  return std::bit_cast<std::int32_t>(value);
}
void byte(std::string& output,std::uint8_t value) {
  require(output.size()<maximumBodyBytes,ErrorCode::Limit);output+=static_cast<char>(value);
}
void shortValue(std::string& output,std::uint16_t value) { byte(output,static_cast<std::uint8_t>(value>>8));byte(output,static_cast<std::uint8_t>(value)); }
class Reader {
public:
  explicit Reader(std::string_view input):input_(input) {}
  std::uint8_t byte() { require(position_<input_.size());return static_cast<std::uint8_t>(input_[position_++]); }
  std::uint16_t shortValue() { const auto high=byte();return static_cast<std::uint16_t>((high<<8)|byte()); }
  std::string properties(std::size_t length) {
    require(length<=maximumPropertyBytes);
    const auto read=std::min(length,input_.size()-position_);
    // Original deserialiseByteArray ignores the single read's return count.
    // ByteArrayInputStream consumes the available prefix; unread bytes stay0.
    std::string value(length,'\0');value.replace(0,read,input_.substr(position_,read));position_+=read;return value;
  }
  std::size_t position() const { return position_; }
private:
  std::string_view input_;std::size_t position_=0;
};
}
Contact::Contact(std::uint8_t network,std::uint8_t version,std::int16_t age,std::string properties,std::int64_t now)
    :network_(network),version_(version),seen_(subtract(seconds(now),age<0?32767:age)),
      id_(identity(properties)),properties_(std::move(properties)) {}
Contact::Contact(Contact&& other) noexcept
    :network_(std::exchange(other.network_,0)),version_(std::exchange(other.version_,0)),seen_(std::exchange(other.seen_,0)),
      id_(std::exchange(other.id_,1)),properties_(std::move(other.properties_)) { other.properties_.clear(); }
Contact& Contact::operator=(const Contact& other) {
  if (this!=&other) { auto copy=other;*this=std::move(copy); }return *this;
}
Contact& Contact::operator=(Contact&& other) noexcept {
  if (this!=&other) {
    network_=std::exchange(other.network_,0);version_=std::exchange(other.version_,0);seen_=std::exchange(other.seen_,0);
    id_=std::exchange(other.id_,1);properties_=std::move(other.properties_);other.properties_.clear();
  }return *this;
}
std::int32_t Contact::age(std::int64_t nowMilliseconds) const { return subtract(seconds(nowMilliseconds),seen_); }
std::expected<std::string,Error> encodeContacts(std::optional<std::span<const Snapshot>> contacts) {
  std::string output;
  try {
    const auto count=contacts?contacts->size():0;require(count<=64);byte(output,static_cast<std::uint8_t>(count));
    if (contacts) {
      for (const auto& contact:*contacts) {
        byte(output,static_cast<std::uint8_t>(contact.network));byte(output,static_cast<std::uint8_t>(contact.version));
        shortValue(output,static_cast<std::uint16_t>(contact.age));require(contact.properties.size()<=maximumPropertyBytes);
        shortValue(output,static_cast<std::uint16_t>(contact.properties.size()));
        require(contact.properties.size()<=maximumBodyBytes-output.size(),ErrorCode::Limit);output+=contact.properties;
      }
    }return output;
  } catch (const Failure& failure) { return std::unexpected(Error{failure.code,0,std::move(output)}); }
}
std::expected<Decoded,Error> decodeContacts(std::string_view input,std::int64_t nowMilliseconds) {
  if (input.size()>maximumBodyBytes) { return std::unexpected(Error{ErrorCode::Limit,0,{}}); }
  Reader reader(input);Decoded result;
  try {
    const auto count=reader.byte();require(count<=64);result.contacts.reserve(count);std::size_t expanded=0;
    for (unsigned i=0;i<count;++i) {
      const auto network=reader.byte(),version=reader.byte();const auto age=std::bit_cast<std::int16_t>(reader.shortValue());
      const auto length=reader.shortValue();require(length<=maximumExpandedBytes-expanded,ErrorCode::Limit);expanded+=length;
      auto properties=reader.properties(length);result.contacts.push_back(Contact(network,version,age,std::move(properties),nowMilliseconds));
    }
    result.consumed=reader.position();result.trailing=input.substr(result.consumed);return result;
  } catch (const Failure& failure) { return std::unexpected(Error{failure.code,reader.position(),{}}); }
}
}
