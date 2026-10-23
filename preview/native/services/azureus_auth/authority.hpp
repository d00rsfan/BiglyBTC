// SPDX-License-Identifier: GPL-2.0-or-later
// Original anti-spoof behavior: Copyright (C) Azureus Software, Inc.
#pragma once
#include "../azureus_dht/headers.hpp"
#include <memory>
#include <span>
namespace biglybtc::services::azureus_auth {
using Key=std::array<std::uint8_t,16>;
using Token2=std::array<std::uint8_t,8>;
using Address=biglybtc::services::azureus_dht::Address;
enum class Error { Input,Crypto,Thread,Limit };
template<class T> struct Issued { T value;bool cached=false; };
struct Statistics { std::size_t ipEntries=0,idEntries=0;std::uint64_t digests=0; };
// One explicit owner thread. The key belongs to this instance only; no persistence.
class Authority final {
public:
  // Supplied generated/test context; null disables storage's digest as in source.
  // Callers retain responsibility for cleansing their supplied key copy.
  explicit Authority(const std::optional<Key>&);
  static std::expected<std::unique_ptr<Authority>,Error> create(bool supportsStorage=true);
  ~Authority();
  Authority(const Authority&)=delete;
  Authority& operator=(const Authority&)=delete;
  Authority(Authority&&)=delete;
  Authority& operator=(Authority&&)=delete;
  std::expected<Issued<std::int32_t>,Error> issueIp(const std::optional<Address>&);
  std::expected<Issued<Token2>,Error> issueId(std::span<const std::uint8_t>);
  std::expected<bool,Error> verifyIp(const std::optional<Address>&,std::int32_t);
  // Null differs from empty; issuance still occurs before token comparison.
  std::expected<bool,Error> verifyId(std::span<const std::uint8_t>,std::optional<std::span<const std::uint8_t>>);
  std::expected<Statistics,Error> statistics() const;
private:
  struct Impl;std::unique_ptr<Impl> impl_;
};
}
