// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHT framing: Copyright (C) Azureus Software, Inc.
#pragma once
#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace biglybtc::services::azureus_positions {
inline constexpr std::size_t maximumBodyBytes=8192;
// Trusted outgoing provider result, borrowed by encoder only. Opaque provider
// bytes do not imply a native implementation of its coordinate algorithm.
struct WirePosition { std::int8_t type;std::int32_t declaredSize;std::string bytes; };
using Positions=std::vector<std::optional<WirePosition>>;
WirePosition vivaldiV1(const std::array<std::uint32_t,4>& ieeeBits);
enum class ErrorCode { Input,Limit };
struct Error { ErrorCode code;std::size_t consumed=0,providerFailures=0;std::string partial; };
struct Decoded {
  Positions positions;
  std::size_t consumed=0,providerFailures=0;
  std::string trailing;
};
std::expected<std::string,Error> encodePositions(std::int8_t version,std::optional<std::span<const std::optional<WirePosition>>>);
// Supplied registration context only. No manager, global registration,
// listeners, storage or coordinate update is initialized by this pure codec.
std::expected<Decoded,Error> decodePositions(std::int8_t version,bool v1Registered,std::string_view);
}
