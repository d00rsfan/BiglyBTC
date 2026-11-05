// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "profile.hpp"
#include "saved_contacts.hpp"
#include <chrono>
#include <memory>
#include <span>
#include <stop_token>

namespace biglybtc::storage {
struct I2pContactsSnapshot {
  std::vector<services::i2p::dht::SavedContact> contacts;
  bool writable=false;
};
// Synchronous filesystem worker only. Retains the actual original profile OFD;
// parsing/restoring a saved hint never grants Alive, routing or network effects.
// Existing plugins/azneti2phelper must be owned real directories. Only the
// selected original dhtdata[1][_s] cache directory may be created here.
class I2pContacts final {
public:
  static std::expected<std::shared_ptr<I2pContacts>,Error> open(
      const ProfileLease&,unsigned index,bool secondary=false);
  ~I2pContacts();
  unsigned index()const noexcept;
  bool secondary()const noexcept;
  std::shared_ptr<const ProfileLease> profileOwnership()const noexcept;
  std::expected<void,Error> validate()const;
  std::expected<std::shared_ptr<const I2pContactsSnapshot>,Error> snapshot()const;
  // Ambiguous dat+saving is read-only. A valid saving-only file is recovered at
  // open using its held inode, NOREPLACE and directory fsync. A failed write may
  // leave recovery inputs and permanently poisons this owner until reopened.
  std::expected<void,Error> save(std::span<const services::i2p::dht::SavedContact>,
      std::stop_token={},std::chrono::steady_clock::time_point=
          std::chrono::steady_clock::time_point::max());
private:
  struct Impl;
  explicit I2pContacts(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};
} // namespace biglybtc::storage
