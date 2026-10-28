// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHTPluginStorageManager storageKey statistics, Copyright Azureus.
#pragma once
#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
namespace biglybtc::services::azureus_storage_stats {
enum class Error {Invalid,Limit};
struct Stats {std::int32_t entries=0,bytes=0,readsPerMinute=0;std::int8_t diversification=1;bool operator==(const Stats&)const=default;};
struct Decoded {Stats value;std::uint8_t wireVersion=0;std::string trailing;};
std::string encode(const Stats&); // Always original version0, exactly14 bytes.
std::expected<Decoded,Error> decode(std::string_view); // Original ignores version; consumes14, retains tail; wire bound512.
// Exact volatile source storageKey branch while manager.suspendDivs()==true.
// No frequency/size diversification, persistence, randomness or timer. Read
// clock is the genuine supplied wall sample; IP bloom is original AddOnly301,
// NOT a distinct-address set, raw read count, or BEP33 bloom.
class Key final {
public:
  void changed(std::int32_t entries,std::int32_t bytes)noexcept;
  std::expected<void,Error> read(std::span<const std::uint8_t> rawIp,std::int64_t now)noexcept;
  Stats stats()const noexcept{return {entries_,bytes_,reads_,1};}
private:
  std::int32_t entries_=0,bytes_=0;std::int16_t reads_=0;
  std::int64_t start_=0;std::array<std::uint8_t,38> bloom_{};std::int32_t bloomEntries_=0;bool active_=false;
};
}
