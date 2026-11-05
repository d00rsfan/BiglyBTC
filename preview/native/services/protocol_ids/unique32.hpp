// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <expected>
#include <memory>

namespace biglybtc::services::protocol_ids {
enum class Error {Closed,Thread,Exhausted,Resource,Crypto,Failed};
enum class Zero {Allow,Skip};
inline constexpr std::uint64_t MaximumInputs=std::uint64_t{1}<<31;
inline constexpr unsigned Rounds=1024;
struct Statistics {std::uint64_t reservedInputs=0;bool initialized=false,failed=false;};

// SOURCE-ONLY PROTOTYPE: not connected to a channel, packet or authority.
// One object is one non-resettable, thread-affine allocation lifetime. Its
// factory accepts neither a secret nor an initial counter. Moves retain both
// the counter and original thread; destruction must not race a method call.
class Unique32 final {
  struct Impl;
  std::unique_ptr<Impl> impl_;
  explicit Unique32(std::unique_ptr<Impl>) noexcept;
public:
  static std::expected<Unique32,Error> create(Zero=Zero::Skip) noexcept;
  ~Unique32();
  Unique32(Unique32&&) noexcept;
  Unique32& operator=(Unique32&&) noexcept;
  Unique32(const Unique32&)=delete;
  Unique32& operator=(const Unique32&)=delete;
  // Lazy CSPRNG/key setup. Each permutation input is consumed before its
  // evaluation. A crypto/resource failure permanently poisons this lifetime;
  // skipped zero consumes an input too. No clear/reset/rekey operation exists.
  std::expected<std::uint32_t,Error> next() noexcept;
  std::expected<Statistics,Error> statistics() const noexcept;
};
} // namespace biglybtc::services::protocol_ids
