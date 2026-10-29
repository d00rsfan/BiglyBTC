// SPDX-License-Identifier: GPL-2.0-or-later
// DHTPluginStorageManager / DHTDBImpl, Azureus Software, Inc., pinned cb8c6cb9.
#pragma once
#include "profile.hpp"
#include <bit>
#include <cstdint>
#include <memory>
#include <optional>
namespace biglybtc::storage {
struct AzureusVersionReservation {std::int32_t first=0,next=0;};
// Java Long.intValue(), signed comparison and int+128 wrap. Pure projection;
// calling it alone neither reserves nor authorises a published value.
inline AzureusVersionReservation originalAzureusVersionReservation(std::optional<std::int64_t> saved,std::int64_t nowMillis) noexcept {
  const auto narrow=[](std::int64_t n){return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(n));};
  const auto now=narrow(nowMillis/1000);auto first=saved?narrow(*saved):now;if(first<now){first=now;}
  return {first,std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(first)+128U)};
}
// Explicit Linux profile owner; retains the SAME acquired OFD. Only version.dat
// and its canonical .saving can be published. No contacts/value DB snapshot or
// public service activation. Methods serialize their small synchronous IO.
class AzureusValueVersions final {
public:
  static std::expected<std::shared_ptr<AzureusValueVersions>,Error> open(const ProfileLease&,std::int32_t network);
  ~AzureusValueVersions();
  AzureusValueVersions(const AzureusValueVersions&)=delete;
  AzureusValueVersions& operator=(const AzureusValueVersions&)=delete;
  std::int32_t network()const noexcept;
  std::expected<void,Error> validate() const;
  // Explicit retained alias of this SAME profile OFD, after current version
  // validation. Consumers own the alias; no borrowed caller lease is exposed.
  std::expected<std::shared_ptr<const ProfileLease>,Error> retainProfile() const;
  // A version is returned only AFTER its128-value chunk's durable reservation.
  // Indeterminate publication poisons this owner; never replay after an error.
  std::expected<std::int32_t,Error> next(std::int64_t nowMillis);
private:
  struct Impl;explicit AzureusValueVersions(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};
}
