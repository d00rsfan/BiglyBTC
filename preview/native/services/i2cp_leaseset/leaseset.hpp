// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "session.hpp"
#include <array>

namespace biglybtc::services::i2p::i2cp {
inline constexpr Byte RequestVariableLeaseSetType=37,CreateLeaseSet2Type=41,StandardLeaseSet2Type=3;
inline constexpr std::size_t MaximumLeaseCount=16,LeaseEncryptionBytes=256,MaximumLeaseSetBytes=2048,MaximumSecretLeaseFrameBytes=4096;
struct RequestedLease {Hash gateway{};std::uint32_t tunnelId=0;std::uint64_t expiresMilliseconds=0;};
struct LeaseRequest {std::uint16_t sessionId=0;std::vector<RequestedLease> leases;};
enum class LeaseEncryptionType:std::uint16_t {ElGamal2048=0,X25519=4};
[[nodiscard]] constexpr std::size_t leaseEncryptionWidth(LeaseEncryptionType type)noexcept{return type==LeaseEncryptionType::ElGamal2048?256:type==LeaseEncryptionType::X25519?32:0;}
struct LeaseSetPolicy {bool unpublished=false;LeaseEncryptionType encryptionType=LeaseEncryptionType::ElGamal2048;};
// Exactly one independent Enc0 or Enc4 key, standard online LS2, empty options.
// This never treats the Destination's deliberately opaque Enc0 bytes as a pair.
class LeaseEncryptionKey final {
public:
  LeaseEncryptionKey() noexcept=default;~LeaseEncryptionKey();
  LeaseEncryptionKey(const LeaseEncryptionKey&)=delete;LeaseEncryptionKey& operator=(const LeaseEncryptionKey&)=delete;
  LeaseEncryptionKey(LeaseEncryptionKey&&) noexcept;LeaseEncryptionKey& operator=(LeaseEncryptionKey&&) noexcept;
  [[nodiscard]] static ConfigResult<LeaseEncryptionKey> generate(LeaseEncryptionType=LeaseEncryptionType::ElGamal2048);
  [[nodiscard]] static ConfigResult<LeaseEncryptionKey> fromPrivateKey(View,LeaseEncryptionType=LeaseEncryptionType::ElGamal2048);
  [[nodiscard]] bool valid()const noexcept{return valid_;}
  [[nodiscard]] View publicKey()const noexcept{return valid_?View(public_).first(leaseEncryptionWidth(type_)):View{};}
  // Secret borrowed view: explicit I2CP transfer only, never log/persist it.
  [[nodiscard]] View privateKey()const noexcept{return valid_?View(private_).first(leaseEncryptionWidth(type_)):View{};}
  [[nodiscard]] LeaseEncryptionType type()const noexcept{return type_;}
  void clear() noexcept;
private:
  std::array<Byte,LeaseEncryptionBytes> private_{},public_{};bool valid_=false;LeaseEncryptionType type_=LeaseEncryptionType::ElGamal2048;
};
struct SignedLeaseSet2 {
  Destination destination;std::uint64_t publishedMilliseconds=0,expiresMilliseconds=0;
  bool unpublished=false;std::array<Byte,LeaseEncryptionBytes> encryptionKey{};
  std::vector<RequestedLease> leases;Bytes signature;LeaseEncryptionType encryptionType=LeaseEncryptionType::ElGamal2048;
};
// A fixed owned secret buffer: wipe on move/clear/destruction, never implicit
// copy. Qt's internal TCP write buffer is outside this object's ownership.
class SecretLeaseFrame final {
public:
  SecretLeaseFrame() noexcept=default;~SecretLeaseFrame();
  SecretLeaseFrame(const SecretLeaseFrame&)=delete;SecretLeaseFrame& operator=(const SecretLeaseFrame&)=delete;
  SecretLeaseFrame(SecretLeaseFrame&&) noexcept;SecretLeaseFrame& operator=(SecretLeaseFrame&&) noexcept;
  [[nodiscard]] View bytes()const noexcept{return View(bytes_).first(size_);}
  void clear() noexcept;
private:
  std::array<Byte,MaximumSecretLeaseFrameBytes> bytes_{};std::size_t size_=0;
  friend ConfigResult<SecretLeaseFrame> encodeCreateLeaseSet2(std::uint16_t,const SignedLeaseSet2&,const LeaseEncryptionKey&);
};
struct DecodedCreateLeaseSet2 {std::uint16_t sessionId=0;SignedLeaseSet2 leaseSet;LeaseEncryptionKey key;};
[[nodiscard]] ConfigResult<LeaseSetPolicy> leaseSetPolicy(const SessionOptions&);
[[nodiscard]] ConfigResult<LeaseRequest> decodeLeaseRequest(const Frame&);
[[nodiscard]] ConfigResult<Bytes> encodeLeaseRequest(const LeaseRequest&);
// Matches original max(now,lastRaw+1000), nearest-second published rounding;
// no clock read and no implicit identity/key generation in the codec.
[[nodiscard]] ConfigResult<SignedLeaseSet2> signLeaseSet2(const ClientIdentity&,const LeaseEncryptionKey&,const LeaseRequest&,LeaseSetPolicy,std::uint64_t nowMilliseconds,std::uint64_t previousRawPublished=0);
[[nodiscard]] ConfigResult<void> verifyLeaseSet2(const SignedLeaseSet2&);
[[nodiscard]] ConfigResult<Bytes> encodeLeaseSet2(const SignedLeaseSet2&);
[[nodiscard]] ConfigResult<SignedLeaseSet2> decodeLeaseSet2(View);
[[nodiscard]] ConfigResult<SecretLeaseFrame> encodeCreateLeaseSet2(std::uint16_t,const SignedLeaseSet2&,const LeaseEncryptionKey&);
// Explicit offline/import diagnostic only; the borrowed Frame contains secret
// bytes, so its owner remains responsible for erasing that source buffer.
[[nodiscard]] ConfigResult<DecodedCreateLeaseSet2> decodeCreateLeaseSet2(const Frame&);
} // namespace biglybtc::services::i2p::i2cp
