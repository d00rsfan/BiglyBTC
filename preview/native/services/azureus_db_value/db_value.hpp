// SPDX-License-Identifier: GPL-2.0-or-later
// Original DHT DB value lifecycle: Copyright (C) Azureus Software, Inc.
#pragma once
#include "nodes.hpp"
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace biglybtc::services::azureus_db_value {
namespace nodes=biglybtc::services::azureus_nodes;
inline constexpr std::size_t maximumPayloadBytes=65'536;
enum class ErrorCode { Limit };
struct Error { ErrorCode code; };

// A synchronous borrowed provider; no clock or caller reference is retained.
class WallClock {
public:
  virtual ~WallClock()=default;
  virtual std::int64_t nowMilliseconds() noexcept=0;
};

class Payload final {
public:
  Payload()=default; // Null differs from present empty.
  static std::expected<Payload,Error> copy(std::optional<std::string_view>);
  static Payload empty();
  std::optional<std::string_view> view() const noexcept;
  bool sharesStorageWith(const Payload& other) const noexcept;
private:
  explicit Payload(std::shared_ptr<const std::string> bytes) noexcept;
  std::shared_ptr<const std::string> bytes_;
};

class ContactHandle final {
public:
  ContactHandle()=default;
  static ContactHandle copy(const nodes::Contact&);
  const nodes::Contact* get() const noexcept;
  bool sharesStorageWith(const ContactHandle& other) const noexcept;
private:
  explicit ContactHandle(std::shared_ptr<const nodes::Contact> contact) noexcept;
  std::shared_ptr<const nodes::Contact> contact_;
};

struct Fields {
  std::int64_t created=0;
  Payload payload;
  std::int32_t version=0;
  ContactHandle originator,sender;
  bool local=false;
  std::int32_t flags=0,lifeHours=0;
  std::int8_t replication=-1;
};

class DbValue final {
public:
  static DbValue create(Fields,WallClock&);
  static DbValue receive(const DbValue&,ContactHandle newSender,bool local,WallClock&);
  DbValue relay(ContactHandle newSender,WallClock&) const;
  DbValue deletion(std::int32_t newVersion,WallClock&) const;
  DbValue(const DbValue&)=default; // Snapshot copy, no clock read.
  DbValue& operator=(const DbValue&)=default;
  DbValue(DbValue&&) noexcept;
  DbValue& operator=(DbValue&&) noexcept;
  // Moved-from: times/version/flags/lifetime zero, local false, null handles,
  // replication default -1. Self move preserves the original object.

  void reset(WallClock&) noexcept;
  void setCreationTime(WallClock&) noexcept;
  void setStoreTime(std::int64_t) noexcept;
  void setOriginatorAndSender(ContactHandle) noexcept;
  void setFlags(std::int8_t) noexcept;
  std::int64_t creationTime() const noexcept;
  std::int64_t storeTime() const noexcept;
  bool isLocal() const noexcept;
  std::int32_t version() const noexcept;
  Payload payload() const noexcept; // Owning handles keep borrowed views alive.
  ContactHandle originator() const noexcept;
  ContactHandle sender() const noexcept;
  std::uint8_t flags() const noexcept;
  std::uint8_t lifeTimeHours() const noexcept;
  std::int8_t replicationControl() const noexcept;
  std::int8_t replicationFactor() const noexcept;
  std::int8_t replicationFrequencyHours() const noexcept;
private:
  explicit DbValue(Fields) noexcept;
  void clearMovedFrom() noexcept;
  Fields fields_;
  std::int64_t storeTime_=0;
};
}
