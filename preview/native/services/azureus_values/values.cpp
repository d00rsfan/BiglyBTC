// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHT values: Copyright (C) Azureus Software, Inc.
#include "values.hpp"
#include <algorithm>
#include <bit>
namespace biglybtc::services::azureus_values {
namespace {
struct Failure { ErrorCode code; };
void require(bool value,ErrorCode code=ErrorCode::Input) { if (!value) { throw Failure{code}; } }
std::int64_t add(std::int64_t a,std::int64_t b) { return std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(a)+std::bit_cast<std::uint64_t>(b)); }
class Writer {
public:
  std::string output;
  void byte(std::uint8_t value) { require(output.size()<maximumBodyBytes,ErrorCode::Limit);output+=static_cast<char>(value); }
  void integer(std::uint64_t value,unsigned width) { require(width<=maximumBodyBytes-output.size(),ErrorCode::Limit);for (unsigned i=width;i>0;--i) { byte(static_cast<std::uint8_t>(value>>((i-1)*8))); } }
  void append(std::string_view bytes) { require(bytes.size()<=maximumBodyBytes-output.size(),ErrorCode::Limit);output+=bytes; }
  void value(std::int8_t protocol,std::int64_t skew,const std::optional<Snapshot>& snapshot) {
    if (protocol>=11) { require(snapshot.has_value());integer(std::bit_cast<std::uint32_t>(snapshot->version),4); }else { integer(0,4); }
    require(snapshot.has_value());integer(std::bit_cast<std::uint64_t>(add(snapshot->created,skew)),8);
    require(snapshot->bytes.has_value() && snapshot->bytes->size()<=maximumValueBytes);integer(snapshot->bytes->size(),2);append(*snapshot->bytes);
    require(snapshot->originator.has_value());const auto contact=nodes::encodeContact(*snapshot->originator);
    if (!contact) { append(contact.error().partial);throw Failure{static_cast<ErrorCode>(contact.error().code)}; }append(*contact);
    byte(static_cast<std::uint8_t>(snapshot->flags));if (protocol>=23) { byte(static_cast<std::uint8_t>(snapshot->lifeHours)); }
    if (protocol>=24) { byte(std::bit_cast<std::uint8_t>(snapshot->replication)); }
  }
};
class Reader {
public:
  explicit Reader(std::string_view input):input_(input) {}
  std::uint8_t byte() { require(position_<input_.size());return static_cast<std::uint8_t>(input_[position_++]); }
  std::uint64_t integer(unsigned width) {
    std::uint64_t value=0;for (unsigned i=0;i<width;++i) { value=(value<<8)|byte(); }return value;
  }
  Value value(std::int8_t protocol,std::int64_t skew,nodes::NodeCache& cache) {
    Value value;
    // Original pre11 reader intentionally does not consume serializer's int0.
    if (protocol>=11) { value.version=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(integer(4))); }
    value.created=add(std::bit_cast<std::int64_t>(integer(8)),skew);const auto length=integer(2);require(length<=maximumValueBytes);
    value.bytes.assign(static_cast<std::size_t>(length),'\0');const auto received=std::min<std::size_t>(length,input_.size()-position_);
    std::copy_n(input_.begin()+static_cast<std::ptrdiff_t>(position_),received,value.bytes.begin());position_+=received;
    auto contact=nodes::decodeContact(input_.substr(position_),cache);if (!contact) { position_+=contact.error().consumed;throw Failure{static_cast<ErrorCode>(contact.error().code)}; }
    position_+=contact->consumed;value.originator=std::move(contact->contact);value.flags=byte();
    if (protocol>=23) { value.lifeHours=byte(); }if (protocol>=24) { value.replication=std::bit_cast<std::int8_t>(byte()); }return value;
  }
  std::size_t position() const { return position_; }
private:
  std::string_view input_;std::size_t position_=0;
};
std::expected<Decoded,Error> read(std::int8_t protocol,std::int64_t skew,std::string_view input,nodes::NodeCache& cache,bool array,bool keepTrailing=true) {
  if (input.size()>maximumBodyBytes) { return std::unexpected(Error{ErrorCode::Limit,0,{}}); }Reader reader(input);Decoded result;
  try {
    const auto count=array?reader.integer(2):1;result.values.reserve(std::min<std::size_t>(count,input.size()/20));
    for (std::uint64_t i=0;i<count;++i) { result.values.push_back(reader.value(protocol,skew,cache)); }
    result.consumed=reader.position();if (keepTrailing) { result.trailing=input.substr(result.consumed); }return result;
  } catch (const Failure& failure) { return std::unexpected(Error{failure.code,reader.position(),{}}); }
}
}
std::expected<std::string,Error> encodeValue(std::int8_t version,std::int64_t skew,const std::optional<Snapshot>& value) {
  Writer writer;try { writer.value(version,skew,value);return std::move(writer.output); }
  catch (const Failure& failure) { return std::unexpected(Error{failure.code,0,std::move(writer.output)}); }
}
std::expected<std::string,Error> encodeValues(std::int8_t version,std::int64_t skew,std::optional<std::span<const std::optional<Snapshot>>> values) {
  Writer writer;try {
    require(values.has_value() && values->size()<=65535);writer.integer(values->size(),2);
    for (const auto& value:*values) { writer.value(version,skew,value); }return std::move(writer.output);
  } catch (const Failure& failure) { return std::unexpected(Error{failure.code,0,std::move(writer.output)}); }
}
std::expected<Decoded,Error> decodeValue(std::int8_t version,std::int64_t skew,std::string_view input,nodes::NodeCache& cache) { return read(version,skew,input,cache,false); }
std::expected<Decoded,Error> decodeValues(std::int8_t version,std::int64_t skew,std::string_view input,nodes::NodeCache& cache) { return read(version,skew,input,cache,true); }
std::expected<Decoded,Error> decodeValuesPrefix(std::int8_t version,std::int64_t skew,std::string_view input,nodes::NodeCache& cache) { return read(version,skew,input,cache,true,false); }
}
