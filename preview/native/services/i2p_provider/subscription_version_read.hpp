// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "../i2p_dht/actor.hpp"
#include "subscription_seed_values.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace biglybtc::services::i2p::provider {

// Composes one source-authorized version lookup and signed DDB metadata read.
// All calls stay on the owning Actor's QObject thread. The owner cancels when
// its signed profile selection or route changes.
class SubscriptionVersionRead final {
public:
  using Clock=std::chrono::steady_clock;
  struct Outcome {
    search::SignedSubscriptionPresence presence;
    std::string metainfo;
  };

  explicit SubscriptionVersionRead(dht::Actor& actor) noexcept : actor_(actor) {}
  ~SubscriptionVersionRead();
  SubscriptionVersionRead(const SubscriptionVersionRead&)=delete;
  SubscriptionVersionRead& operator=(const SubscriptionVersionRead&)=delete;

  bool startFromAssociation(std::string publicKey,std::uint32_t currentVersion,
      const dht::Id& associationKey,Clock::time_point deadline);
  void advance();
  void cancel() noexcept;
  bool active()const noexcept { return operation_!=0; }
  std::optional<Outcome> take() noexcept { return std::exchange(outcome_,{}); }

private:
  struct Candidate {
    dht::Contact reporter;
    search::SignedSubscriptionPresence presence;
  };
  enum class Phase { Association, Presence, TorrentContacts, Metainfo };

  bool beginLookup(const dht::Id&,std::uint8_t maximum,Phase);
  bool beginMetainfo();
  bool beginTorrentContactLookup();
  void rememberReporter(const dht::Contact&,const search::SignedSubscriptionPresence&);
  void selectReporters(const dht::Snapshot&);
  void selectTorrentReporters(const dht::Snapshot&);
  bool selectVersion(const dht::Snapshot&);

  dht::Actor& actor_;
  std::uint64_t operation_=0;
  std::uint32_t version_=0;
  Clock::time_point deadline_{};
  Clock::time_point reporterDeadline_{};
  std::string publicKey_;
  dht::Id key_{};
  std::vector<Candidate> reporters_;
  std::vector<Candidate> attempted_;
  std::vector<search::SignedSubscriptionPresence> signedVersions_;
  std::size_t nextReporter_=0;
  std::size_t nextTorrentLookup_=0;
  Phase phase_=Phase::Presence;
  std::optional<Outcome> outcome_;
};

} // namespace biglybtc::services::i2p::provider
