// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHT identity/wire: Copyright (C) Azureus Software, Inc.
#include "nodes.hpp"
#include <openssl/evp.h>
#include <algorithm>
#include <bit>
#include <charconv>
namespace biglybtc::services::azureus_nodes {
namespace {
struct Failure { ErrorCode code; };
void require(bool value,ErrorCode code=ErrorCode::Input) { if (!value) { throw Failure{code}; } }
void number(std::string& text,std::uint64_t value,int base=10) {
  std::array<char,32> buffer{};const auto result=std::to_chars(buffer.data(),buffer.data()+buffer.size(),value,base);
  require(result.ec==std::errc{});text.append(buffer.data(),result.ptr);
}
std::string host(const ResolvedEndpoint& endpoint) {
  const auto& address=endpoint.address;std::string value;
  if (address.length==4) {
    for (unsigned i=0;i<4;++i) { if (i!=0) { value+='.'; }number(value,address.bytes[i]); }
  } else {
    for (unsigned i=0;i<16;i+=2) {
      if (i!=0) { value+=':'; }number(value,(static_cast<unsigned>(address.bytes[i])<<8)|address.bytes[i+1],16);
    }
    if (endpoint.numericScope && *endpoint.numericScope>=0) { value+='%';number(value,static_cast<std::uint32_t>(*endpoint.numericScope)); }
  }return value;
}
std::expected<NodeId,Error> digest(std::string_view key) {
  std::array<unsigned char,EVP_MAX_MD_SIZE> output{};std::size_t length=0;
  if (EVP_Q_digest(nullptr,"SHA1",nullptr,key.data(),key.size(),output.data(),&length)!=1 || length!=20) {
    return std::unexpected(Error{ErrorCode::Crypto,0,{}});
  }
  NodeId result{};std::copy_n(output.begin(),result.size(),result.begin());return result;
}
void byte(std::string& output,std::uint8_t value) { require(output.size()<maximumBodyBytes,ErrorCode::Limit);output+=static_cast<char>(value); }
void shortValue(std::string& output,std::uint16_t value) { byte(output,static_cast<std::uint8_t>(value>>8));byte(output,static_cast<std::uint8_t>(value)); }
void address(std::string& output,const std::optional<udp::Address>& value) {
  if (!value) {
    require(7<=maximumBodyBytes-output.size(),ErrorCode::Limit);output.append("\4\0\0\0\0\0\0",7);return;
  }
  require(value->length==4 || value->length==16);byte(output,value->length);
  require(value->length<=maximumBodyBytes-output.size(),ErrorCode::Limit);
  output.append(reinterpret_cast<const char*>(value->bytes.data()),value->length);shortValue(output,value->port);
}
void writeContact(std::string& output,const Snapshot& contact) {
  require(contact.udp);byte(output,1);byte(output,std::bit_cast<std::uint8_t>(contact.protocol));address(output,contact.address);
}
class Reader {
public:
  explicit Reader(std::string_view input):input_(input) {}
  std::uint8_t byte() { require(position_<input_.size());return static_cast<std::uint8_t>(input_[position_++]); }
  std::uint16_t shortValue() { const auto high=byte();return static_cast<std::uint16_t>((high<<8)|byte()); }
  udp::Address address() {
    udp::Address value;value.length=byte();require(value.length<=16);
    if (value.length>input_.size()-position_) { position_=input_.size();throw Failure{ErrorCode::Input}; }
    for (unsigned i=0;i<value.length;++i) { value.bytes[i]=byte(); }value.port=shortValue();
    // As in Java, width validation follows address bytes and port consumption.
    require(value.length==4 || value.length==16);
    if (value.length==16 && std::ranges::all_of(value.bytes.begin(),value.bytes.begin()+10,[](auto b){return b==0;})
        && value.bytes[10]==255 && value.bytes[11]==255) {
      const auto original=value.bytes;value.bytes={};std::copy(original.begin()+12,original.end(),value.bytes.begin());value.length=4;
    }return value;
  }
  Contact contact(NodeCache& cache) {
    require(byte()==1);Contact value;value.protocol=std::bit_cast<std::int8_t>(byte());value.address=address();
    const auto identity=cache.lookup({value.address,{}},value.protocol);if (!identity) { throw Failure{identity.error().code}; }value.id=identity->id;return value;
  }
  std::size_t position() const { return position_; }
private:
  std::string_view input_;std::size_t position_=0;
};
}
std::expected<std::string,Error> identityKey(const ResolvedEndpoint& endpoint,std::int8_t protocol) {
  try {
    const auto& address=endpoint.address;require(address.length==4 || address.length==16);
    std::string key;
    if (protocol>=50 && address.length==4) {
      std::uint64_t value=address.port%5;
      value=((static_cast<std::uint64_t>(address.bytes[3])<<8)|value)%50;
      value=((static_cast<std::uint64_t>(address.bytes[2])<<16)|value)%2500;
      value=(static_cast<std::uint64_t>(address.bytes[1])<<24)|value;
      value=(static_cast<std::uint64_t>(address.bytes[0])<<32)|value;number(key,value);
    } else {
      key=host(endpoint);key+=':';
      number(key,protocol>=33?address.port%8:protocol>=32?address.port%1999:address.port);
    }return key;
  } catch (const Failure& failure) { return std::unexpected(Error{failure.code,0,{}}); }
}
std::expected<std::string,Error> unresolvedIdentityKey(std::string_view hostname) {
  if (hostname!="dht6.biglybt.com") { return std::unexpected(Error{ErrorCode::Input,0,{}}); }
  return "IPv6SeedHack";
}
NodeCache::NodeCache():owner_(std::this_thread::get_id()) {}
std::expected<Lookup,Error> NodeCache::lookup(const ResolvedEndpoint& endpoint,std::int8_t protocol) {
  if (std::this_thread::get_id()!=owner_) { return std::unexpected(Error{ErrorCode::Thread,0,{}}); }
  auto key=identityKey(endpoint,protocol);if (!key) { return std::unexpected(key.error()); }return lookupKey(std::move(*key));
}
std::expected<Lookup,Error> NodeCache::lookupUnresolved(std::string_view hostname) {
  if (std::this_thread::get_id()!=owner_) { return std::unexpected(Error{ErrorCode::Thread,0,{}}); }
  auto key=unresolvedIdentityKey(hostname);if (!key) { return std::unexpected(key.error()); }return lookupKey(std::move(*key));
}
std::expected<Lookup,Error> NodeCache::lookupKey(std::string key) {
  const auto found=index_.find(key);
  if (found!=index_.end()) { entries_.splice(entries_.begin(),entries_,found->second);return Lookup{found->second->id,true}; }
  auto id=digest(key);if (!id) { return std::unexpected(id.error()); }
  entries_.push_front({std::move(key),*id});
  try { index_.emplace(entries_.front().key,entries_.begin()); }catch (...) { entries_.pop_front();throw; }
  if (entries_.size()>128) { index_.erase(entries_.back().key);entries_.pop_back(); }
  return Lookup{*id,false};
}
bool NodeCache::clear() {
  if (std::this_thread::get_id()!=owner_) { return false; }index_.clear();entries_.clear();return true;
}
std::expected<std::string,Error> encodeContact(const Snapshot& contact) {
  std::string output;
  try { writeContact(output,contact);return output; }
  catch (const Failure& failure) { return std::unexpected(Error{failure.code,0,std::move(output)}); }
}
std::expected<DecodedContact,Error> decodeContact(std::string_view input,NodeCache& cache) {
  if (input.size()>maximumBodyBytes) { return std::unexpected(Error{ErrorCode::Limit,0,{}}); }Reader reader(input);
  try { auto contact=reader.contact(cache);const auto consumed=reader.position();return DecodedContact{std::move(contact),consumed}; }
  catch (const Failure& failure) { return std::unexpected(Error{failure.code,reader.position(),{}}); }
}
std::expected<std::string,Error> encodeContacts(std::span<const Snapshot> contacts) {
  std::string output;
  try {
    require(contacts.size()<=65535);shortValue(output,static_cast<std::uint16_t>(contacts.size()));
    for (const auto& contact:contacts) {
      writeContact(output,contact);
    }return output;
  } catch (const Failure& failure) { return std::unexpected(Error{failure.code,0,std::move(output)}); }
}
std::expected<Decoded,Error> decodeContacts(std::string_view input,NodeCache& cache) {
  if (input.size()>maximumBodyBytes) { return std::unexpected(Error{ErrorCode::Limit,0,{}}); }
  Reader reader(input);Decoded result;
  try {
    const auto count=reader.shortValue();result.contacts.reserve(std::min<std::size_t>(count,(input.size()-2)/9));
    for (unsigned i=0;i<count;++i) {
      auto contact=reader.contact(cache);
      // Source constructs/hashes the contact before filtering a port0 record.
      if (contact.address.port>0) { result.contacts.push_back(std::move(contact)); }
    }
    result.consumed=reader.position();result.trailing=input.substr(result.consumed);return result;
  } catch (const Failure& failure) { return std::unexpected(Error{failure.code,reader.position(),{}}); }
}
}
