// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "datagram.hpp"
#include <array>

namespace biglybtc::services::i2p {
inline constexpr std::size_t MaximumOnlineClientIdentityBytes=679;
inline constexpr std::size_t MaximumClientIdentityInputBytes=1024;
inline constexpr std::size_t DefaultClientEncryptionPrivateKeyBytes=256;

// Original online I2PClient/I2PSession key stream, not a connected session.
// Only default EncType0 with signing0/7 is supported. The default encryption
// key is opaque/unused (I2PClientImpl deliberately creates unrelated random
// public/private bytes); signing-key possession is validated separately.
// Owns bounded secrets, is move-only, and clears them on move/clear/destruction.
// All returned views/pointers are borrowed and invalidated by those operations.
class ClientIdentity {
public:
  ClientIdentity() noexcept=default;
  ~ClientIdentity();
  ClientIdentity(const ClientIdentity&)=delete;
  ClientIdentity& operator=(const ClientIdentity&)=delete;
  ClientIdentity(ClientIdentity&&) noexcept;
  ClientIdentity& operator=(ClientIdentity&&) noexcept;

  [[nodiscard]] bool valid() const noexcept{return size_!=0;}
  [[nodiscard]] const Destination* destination() const noexcept{return valid()?&destination_:nullptr;}
  // Exact private-key-file serialization. Never log/publish this view; the
  // owner controls any explicit export. No filesystem or generation policy.
  [[nodiscard]] View keyStream() const noexcept{return View(wire_).first(size_);}
  [[nodiscard]] View encryptionPrivateKey() const noexcept;
  [[nodiscard]] View signingPrivateKey() const noexcept;
  [[nodiscard]] Result<Bytes> makeDatagram2(const Hash& recipient,View payload) const;
  void clear() noexcept;

private:
  Destination destination_{};
  std::array<Byte,MaximumOnlineClientIdentityBytes> wire_{};
  std::size_t size_=0;
  ClientIdentity(Destination,View encryptionPrivateKey,View signingPrivateKey) noexcept;
  friend Result<ClientIdentity> makeClientIdentity(View,View,View);
  friend Result<ClientIdentity> decodeClientIdentity(View);
};

// Neither function changes the caller's borrowed bytes. Callers retain their
// own cleansing responsibility. No automatic identity creation or replacement.
[[nodiscard]] Result<ClientIdentity> makeClientIdentity(View destination,View encryptionPrivateKey,View signingPrivateKey);
[[nodiscard]] Result<ClientIdentity> decodeClientIdentity(View keyStream);
} // namespace biglybtc::services::i2p
