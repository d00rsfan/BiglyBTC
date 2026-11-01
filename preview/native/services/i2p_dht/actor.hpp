// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "dht.hpp"
#include "channel.hpp"
#include <QObject>
#include <memory>
namespace biglybtc::services::i2p::dht {
// Explicit finite client on one actual current source-bound I2CP channel.
// Source/known-torrent/metadata grants belong to the integrating owner.
class Actor final:public QObject {
  Q_OBJECT
public:
  struct Options {Id localId{};std::uint16_t queryPort=0;std::vector<Contact> seeds;unsigned parallelism=3;};
  explicit Actor(i2cp::InitialChannel&,QObject* parent=nullptr);
  ~Actor()override;
  bool begin(const Options&); // Exact local secure NID and explicit seed policy.
  Result<std::uint64_t> lookupPeers(const Id&,bool seeding,unsigned timeoutMilliseconds=120000);
  Result<std::uint64_t> announceSelf(const Id&,bool seeding,unsigned timeoutMilliseconds=120000);
  bool cancel(std::uint64_t);
  void close();
  Snapshot observation()const; // Owning hints; no filesystem/source authority.
signals:
  void changed(); // Queued/coalesced; synchronous close/deletion is safe.
private:
  struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::services::i2p::dht
