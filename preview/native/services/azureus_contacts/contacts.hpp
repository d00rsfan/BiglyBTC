// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHT wire: Copyright (C) Azureus Software, Inc.
#pragma once
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace biglybtc::services::azureus_contacts {
inline constexpr std::size_t maximumBodyBytes=8192,maximumPropertyBytes=65535;
inline constexpr std::size_t maximumExpandedBytes=maximumBodyBytes+maximumPropertyBytes;
struct Snapshot {
  std::int32_t network=0,version=0,age=0;
  // Host-supplied already encoded property dictionary. No lazy map projection
  // or endpoint interpretation is performed by this wire layer.
  std::string properties;
};
enum class ErrorCode { Input,Limit };
struct Error { ErrorCode code;std::size_t consumed=0;std::string partial; };
struct Decoded;
class Contact final {
public:
  Contact(const Contact&)=default;
  Contact& operator=(const Contact&);
  Contact(Contact&&) noexcept;
  Contact& operator=(Contact&&) noexcept;
  std::uint8_t network() const { return network_; }
  std::uint8_t version() const { return version_; }
  std::int32_t id() const { return id_; }
  std::int32_t lastAlive() const { return seen_; }
  std::int32_t age(std::int64_t nowMilliseconds) const;
  std::string_view rawProperties() const { return properties_; }
private:
  Contact(std::uint8_t,std::uint8_t,std::int16_t,std::string,std::int64_t);
  std::uint8_t network_=0,version_=0;
  std::int32_t seen_=0,id_=1;
  std::string properties_;
  friend std::expected<Decoded,Error> decodeContacts(std::string_view,std::int64_t);
};
struct Decoded { std::vector<Contact> contacts;std::size_t consumed=0;std::string trailing; };
std::expected<std::string,Error> encodeContacts(std::optional<std::span<const Snapshot>>);
std::expected<Decoded,Error> decodeContacts(std::string_view,std::int64_t nowMilliseconds);
}
