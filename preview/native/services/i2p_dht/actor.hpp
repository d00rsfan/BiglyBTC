// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "dht.hpp"
#include "saved_contacts.hpp"
#include "channel.hpp"
#include <QObject>
#include <memory>
namespace biglybtc::services::i2p::provider {class ContactExchange;}
namespace biglybtc::services::i2p::dht {
// Explicit finite client on one actual current source-bound I2CP channel.
// Source/known-torrent/metadata grants belong to the integrating owner.
class Actor final:public QObject {
  Q_OBJECT
public:
  struct Options {Id localId{};std::uint16_t queryPort=0;std::vector<Contact> seeds;unsigned parallelism=3;bool inbound=false,inboundSleeping=false,inboundSuspended=false,routingMaintenance=false;std::vector<SavedContact> savedContacts{};};
  explicit Actor(i2cp::InitialChannel&,QObject* parent=nullptr);
  ~Actor()override;
  bool begin(const Options&); // Exact local secure NID and explicit seed policy.
  Result<std::uint64_t> lookupPeers(const Id&,bool seeding,unsigned timeoutMilliseconds=120000);
  Result<std::uint64_t> announceSelf(const Id&,bool seeding,unsigned timeoutMilliseconds=120000);
  bool cancel(std::uint64_t);
  bool enableInbound(); // Accept/defer an explicit source-bound request; no grant setter.
  void stopInbound(); // Server state/replies only; client operation is preserved.
  bool inboundEnabled()const noexcept; // Active only after actual held-grant initialization.
  bool resumeAuthorizedInbound(); // Only actual worker-held channel grant succeeds.
  // Owning original-priority cache rows, only during an actual held source
  // grant. Saved options are untrusted; restore never imports Alive/time.
  Result<std::vector<SavedContact>> exportSavedContacts()const;
  void close();
  Snapshot observation()const; // Owning hints; no filesystem/source authority.
  struct InboundObservation {
    bool requested=false,ready=false,retained=false,sourceCurrent=false,grantActive=false;
    std::uint64_t scope=0,requests=0,replyPrepared=0,tokenRejected=0;
    std::size_t tokenBindings=0,peerKeys=0,peerValues=0;
  };
  InboundObservation inboundObservation()const noexcept; // Passive owning hints; never an effect grant.
signals:
  void changed(); // Queued/coalesced; synchronous close/deletion is safe.
private:
  friend class provider::ContactExchange;
  bool boundChannel(const i2cp::InitialChannel&)const noexcept;
  struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::services::i2p::dht
