// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHT identity/wire: Copyright (C) Azureus Software, Inc.
#pragma once
#include "headers.hpp"
#include <list>
#include <span>
#include <thread>
#include <unordered_map>
#include <vector>
namespace biglybtc::services::azureus_nodes {
namespace udp=biglybtc::services::azureus_dht;
using NodeId=std::array<std::uint8_t,20>;
inline constexpr std::size_t maximumBodyBytes=8192;
struct ResolvedEndpoint { udp::Address address;std::optional<std::int32_t> numericScope; };
enum class ErrorCode { Input,Crypto,Limit,Thread };
struct Error { ErrorCode code;std::size_t consumed=0;std::string partial; };
std::expected<std::string,Error> identityKey(const ResolvedEndpoint&,std::int8_t protocol);
std::expected<std::string,Error> unresolvedIdentityKey(std::string_view hostname);
struct Lookup { NodeId id;bool cached=false; };
// One explicit owner thread. No process-global map or borrowed hash bytes.
class NodeCache final {
public:
  NodeCache();
  NodeCache(const NodeCache&)=delete;
  NodeCache& operator=(const NodeCache&)=delete;
  NodeCache(NodeCache&&)=delete;
  NodeCache& operator=(NodeCache&&)=delete;
  std::expected<Lookup,Error> lookup(const ResolvedEndpoint&,std::int8_t);
  std::expected<Lookup,Error> lookupUnresolved(std::string_view);
  bool clear();
  std::size_t size() const { return entries_.size(); } // Owner thread only.
private:
  struct Entry { std::string key;NodeId id; };
  std::expected<Lookup,Error> lookupKey(std::string);
  const std::thread::id owner_;
  std::list<Entry> entries_;
  std::unordered_map<std::string,std::list<Entry>::iterator> index_;
};
struct Snapshot { bool udp=true;std::int8_t protocol=55;std::optional<udp::Address> address; };
struct Contact {
  std::int8_t protocol=0;
  udp::Address address;
  NodeId id{};
  std::int32_t instance=0;
  std::int64_t skew=0;
  std::int8_t flags=0;
};
struct DecodedContact { Contact contact;std::size_t consumed=0; };
std::expected<std::string,Error> encodeContact(const Snapshot&);
std::expected<DecodedContact,Error> decodeContact(std::string_view,NodeCache&);
struct Decoded { std::vector<Contact> contacts;std::size_t consumed=0;std::string trailing; };
std::expected<std::string,Error> encodeContacts(std::span<const Snapshot>);
std::expected<Decoded,Error> decodeContacts(std::string_view,NodeCache&);
}
