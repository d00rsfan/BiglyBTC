// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "contact_store.hpp"
#include "profile_source.hpp"
#include "../i2p_dht/actor.hpp"
#include <atomic>
#include <thread>

namespace biglybtc::services::i2p::provider {
// An owning handoff only. It owns neither a network nor a filesystem worker.
class ContactOwner;
class ContactExchange final {
public:
  using Clock=std::chrono::steady_clock;
  void tick(Clock::time_point)noexcept; // Existing Bridge timer, delivered15s ticks.
  std::optional<Clock::time_point> nextCheck()const noexcept;
  bool pending()const noexcept;
  // Checks the actual channel/source/Actor held grant; a DTO cannot grant it.
  bool capture(dht::Actor&,const i2cp::InitialChannel&,std::shared_ptr<ProfileSource>);
  void stop()noexcept; // No fresh capture after withdrawal; last owning rows survive.
private:
  friend class ContactOwner;
  struct Export {
    std::vector<dht::SavedContact> rows;
    std::shared_ptr<ProfileSource> source;
    std::uint64_t sequence=0,revision=0,scope=0;
    Hash identity{};bool periodic=false;
  };
  ContactExchange(std::shared_ptr<const storage::ProfileLease>,unsigned);
  std::shared_ptr<const storage::ProfileLease> lease_;
  unsigned index_=0,ticks_=0;
  Clock::time_point next_=Clock::now()+std::chrono::seconds(15);
  std::atomic<std::uint64_t> wanted_{1},completed_{0};
  std::atomic<bool> stopped_{false},failed_{false};std::stop_source stop_;
  std::atomic<std::shared_ptr<const Export>> latest_{};
};

// Affine to the EXISTING external Host worker. No Qt dispatch or callback is
// made while its source gate is held; all cache I/O stays on this caller.
class ContactOwner final {
public:
  static std::expected<std::unique_ptr<ContactOwner>,storage::Error> open(
      const storage::ProfileLease&,unsigned,std::optional<dht::Actor::Options>&);
  ~ContactOwner();
  std::shared_ptr<ContactExchange> exchange()const noexcept{return exchange_;}
  void poll(); // Save only a completed periodic capture; not every Host poll.
  void finish(); // AFTER existing close/revoke/join; no request/wait/new grant.
  std::optional<storage::Error> diagnostic()const;
private:
  explicit ContactOwner(std::shared_ptr<const storage::ProfileLease>);
  void save(bool final);
  void report(storage::Error);
  const std::thread::id worker_=std::this_thread::get_id();
  std::shared_ptr<const storage::ProfileLease> lease_;
  std::shared_ptr<storage::I2pContacts> store_;
  std::shared_ptr<ContactExchange> exchange_;
  std::optional<storage::Error> failure_;
  std::uint64_t handled_=0;
  bool writable_=false,finished_=false;
};
}
