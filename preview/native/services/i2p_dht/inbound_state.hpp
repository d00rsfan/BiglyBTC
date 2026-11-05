// SPDX-License-Identifier: GPL-2.0-or-later
// Original policy: Paul Gardner, Copyright (C) Azureus Software, Inc.
// Derived policy: original Azureus DHTControl/DB and I2PHelper33d97 transport.
// SOURCE-CANDIDATE ONLY: detached state; no source, packet or provider authority.
#pragma once
#include "dht.hpp"
#include "../azureus_auth/authority.hpp"
#include <list>
#include <memory>
#include <thread>
namespace biglybtc::services::i2p::dht::inbound {
enum class StateError {Invalid,Thread,Closed,Capacity,Crypto,Resource,Clock};
template<class T>using StateResult=std::expected<T,StateError>;
inline constexpr std::size_t MaximumTokenBindings=512;
inline constexpr std::int64_t RepublishMillis=90*60*1000;
inline constexpr std::int64_t PeerLifetimeMillis=RepublishMillis+RepublishMillis/4;
inline constexpr std::int64_t ExpiryScanMillis=60*1000;
// Own this independently per actual I2P identity/session. Optional supplied key
// is an explicit test context; default creates the existing Authority CSPRNG key.
// Call remember only for an actually verified signed get_peers contact. A token
// map is not proof of source permission or a provider peer grant.
class TokenBindings final {
public:
  static StateResult<std::unique_ptr<TokenBindings>> create(std::optional<azureus_auth::Key> supplied={});
  ~TokenBindings();
  TokenBindings(const TokenBindings&)=delete;TokenBindings& operator=(const TokenBindings&)=delete;
  StateResult<azureus_auth::Token2> remember(const Contact&);
  StateResult<std::optional<Contact>> resolve(View token); // Owning exact original full contact; touches LRU.
  StateResult<std::size_t> size()const noexcept;
  void close()noexcept;
private:
  struct Entry {azureus_auth::Token2 token;Contact contact;};
  explicit TokenBindings(std::unique_ptr<azureus_auth::Authority>);
  StateResult<void> check()const noexcept;
  const std::thread::id thread_=std::this_thread::get_id();bool closed_=false;
  std::unique_ptr<azureus_auth::Authority> authority_;std::list<Entry> bindings_;
};
struct StoreLimits {std::size_t keys=32,valuesPerKey=64;};
enum class StoreResult {Inserted,StoreTimeRefreshed};
struct Read {bool keyExists=false;std::vector<Peer> peers;};
struct Counts {std::size_t keys=0,values=0;};
// Only direct remote version0 announcements. Original defaults/expiry/order are
// preserved; explicit32x64 capacity is a bounded Native subset. No local values,
// persistence, forwarding, diversification, routing decision or implicit clock.
class PeerStore final {
public:
  static StateResult<std::unique_ptr<PeerStore>> create(StoreLimits={});
  ~PeerStore()=default;
  PeerStore(const PeerStore&)=delete;PeerStore& operator=(const PeerStore&)=delete;
  StateResult<StoreResult> store(const Id& key,const Contact& actualOriginator,bool seed,std::int64_t wallMillis);
  StateResult<bool> contains(const Id& key)const noexcept; // Original hasKey: no expiry/cycling.
  StateResult<Read> read(const Id& key,std::int64_t wallMillis,unsigned maximum=16);
  StateResult<std::size_t> expire(std::int64_t wallMillis,bool force=false);
  StateResult<Counts> counts()const noexcept;
  void close()noexcept;
private:
  struct Value {Id originator;Peer peer;std::int64_t created=0,stored=0;};
  struct Record {Id key;std::vector<Value> values;};
  explicit PeerStore(StoreLimits);
  StateResult<void> check()const noexcept;
  const std::thread::id thread_=std::this_thread::get_id();StoreLimits limits_;bool closed_=false;
  std::int64_t lastScan_=0;std::vector<Record> records_;
};
} // namespace biglybtc::services::i2p::dht::inbound
