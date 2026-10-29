// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Original DHTControlImpl.exportState / DHTPluginStorageManager, pinned cb8c6cb9.
#include "profile.hpp"
#include <array>
#include <chrono>
#include <memory>
#include <span>
#include <stop_token>
#include <vector>
namespace biglybtc::storage {
struct AzureusSavedContact {
  std::int64_t timeAlive=0;
  std::int8_t protocol=55;
  std::uint8_t length=4;
  std::array<std::uint8_t,16> address{};
  std::uint16_t port=0;
  bool operator==(const AzureusSavedContact&)const=default;
};
inline constexpr std::size_t MaximumAzureusSavedContacts=32,MaximumAzureusContactFile=932;
std::expected<std::vector<AzureusSavedContact>,Error> decodeAzureusContactState(std::string_view);
std::expected<std::string,Error> encodeAzureusContactState(std::span<const AzureusSavedContact>);
enum class AzureusContactScope {OwnedLoopback,PublicBound};
struct AzureusContactsSnapshot {std::vector<AzureusSavedContact> contacts;bool writable=false;};
// Explicit opaque-compatible file owner. Calls perform synchronous filesystem
// IO and belong only to external initialization or the existing profile worker.
// It retains SAME OFD; no actor/router, timer or network permission lives here.
class AzureusContacts final {
public:
  static std::expected<std::shared_ptr<AzureusContacts>,Error> open(const ProfileLease&,std::int32_t network,std::uint8_t family,AzureusContactScope=AzureusContactScope::OwnedLoopback);
  ~AzureusContacts();
  std::int32_t network()const noexcept;
  std::uint8_t family()const noexcept;
  AzureusContactScope scope()const noexcept;
  std::shared_ptr<const ProfileLease> profileOwnership()const noexcept;
  std::expected<void,Error> validate()const;
  std::expected<std::shared_ptr<const AzureusContactsSnapshot>,Error> snapshot()const;
  std::expected<void,Error> save(std::span<const AzureusSavedContact>,std::stop_token={},std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::time_point::max());
private:
  struct Impl;explicit AzureusContacts(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};
}
