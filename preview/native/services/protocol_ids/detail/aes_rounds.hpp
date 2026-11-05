// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../unique32.hpp"
#include <array>
#include <span>

namespace biglybtc::services::protocol_ids::detail {
// Pure cryptographic implementation detail. A known key here enables offline
// conformance tests; it cannot construct/seed/reset the public ID allocator.
class AesRounds final {
  struct Impl;
  std::unique_ptr<Impl> impl_;
  explicit AesRounds(std::unique_ptr<Impl>) noexcept;
public:
  using Key=std::array<unsigned char,32>;
  static std::expected<AesRounds,Error> create(const Key&,unsigned width=32) noexcept;
  ~AesRounds();AesRounds(AesRounds&&) noexcept;AesRounds& operator=(AesRounds&&) noexcept;
  AesRounds(const AesRounds&)=delete;AesRounds& operator=(const AesRounds&)=delete;
  std::expected<std::uint32_t,Error> apply(std::uint32_t,bool inverse=false) noexcept;
};
} // namespace biglybtc::services::protocol_ids::detail
