// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHT wire helpers: Copyright (C) Azureus Software, Inc.
#pragma once
#include <array>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
namespace biglybtc::services::azureus_ping {
inline constexpr std::size_t maximumBodyBytes=8192;
struct CountryRate { std::u16string code;std::int64_t received=0,sent=0; };
// Already ranked cached host arrays, borrowed only for this function call.
// Sampling, ranking, cache refresh and emission throttling belong to the host.
struct UploadSnapshot {
  std::int64_t totalReceived=0,totalSent=0;
  std::span<const CountryRate> received,sent;
};
struct EncodeOptions {
  std::int8_t version=55;
  std::int32_t network=4;
  std::optional<std::span<const std::int32_t>> networks,counts;
  std::optional<UploadSnapshot> upload; // null -> original no-stats sentinel.
};
enum class ErrorCode { Input,Limit };
struct Error {
  ErrorCode code;
  std::size_t consumed=0;
  // Serializer diagnostic only. Never transmit a failed partial packet.
  std::string partial;
};
struct LegacyCountryStat { std::u16string code;std::int64_t received=0,sent=0; };
using LegacyStats=std::vector<LegacyCountryStat>; // original order/duplicates.
using CountryMap=std::map<std::u16string,std::array<std::int64_t,2>>;
using UploadStats=std::variant<LegacyStats,CountryMap>;
struct UploadDecoded { std::optional<UploadStats> stats;std::size_t consumed=0;std::string trailing; };
struct OptionsDecoded {
  std::vector<std::int32_t> networks,counts;
  std::optional<UploadStats> upload;
  std::size_t consumed=0;
  std::string trailing;
};
std::expected<std::string,Error> encodeOptions(const EncodeOptions&);
// Shared upload section for request/reply assembly. The caller applies the
// version/network gates; null produces the original0xff no-stats sentinel.
std::expected<std::string,Error> encodeUpload(std::optional<UploadSnapshot>);
// Optional upload decoding intentionally swallows malformed/truncated stats,
// returning null with the exact consumed offset, as the original helper does.
std::expected<UploadDecoded,Error> decodeUpload(std::string_view);
std::expected<OptionsDecoded,Error> decodeOptions(std::int8_t version,std::int32_t network,std::string_view);
}
