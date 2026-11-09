// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/bencode.hpp"
#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace biglybtc::services::msgsync {
enum class Error {Input,Limit,Version,Signature,Clock,Resources,Crypto,Compression};
template<class T>using Result=std::expected<T,Error>;
inline constexpr std::uint32_t MaximumBloomBits=8193;
// Original BloomFilterAddOnly, not BEP33's unrelated bloom format. Exactly
// Java's 32-bit overflow/hash/modulo and least-significant-bit map placement.
class AddOnlyBloom final {
public:
  static Result<AddOnlyBloom> create(std::uint32_t requestedBits);
  static Result<AddOnlyBloom> decode(const core::Node&);
  bool contains(std::string_view bytes)const noexcept;
  void add(std::string_view bytes)noexcept;
  Result<std::string> encode()const;
  std::uint32_t bits()const noexcept{return bits_;}
  std::uint32_t count()const noexcept{return count_;}
  std::string_view map()const noexcept{return map_;}
private:
  AddOnlyBloom(std::uint32_t,std::uint32_t,std::string);
  std::array<std::uint32_t,5> indices(std::string_view)const noexcept;
  std::uint32_t bits_,count_;
  std::string map_;
};
// A bounded set of already selected original signature / public-key+contact /
// inverted tombstone bytes. This optimization confers no sender/contact rights.
struct SyncBloom {std::array<std::uint8_t,8> random;AddOnlyBloom filter;};
Result<SyncBloom> buildSyncBloom(std::span<const std::string> distinctRawKeys);
Result<std::string> maskBloomKey(std::string_view,std::span<const std::uint8_t,8> random);
}
