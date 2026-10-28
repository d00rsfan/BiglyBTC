// SPDX-License-Identifier: GPL-2.0-or-later
// Derived from DHTDBMapping, Copyright (C) Azureus Software, Inc.
#pragma once
#include "db_value.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace biglybtc::services::azureus_db_mapping {
namespace db = biglybtc::services::azureus_db_value;
namespace nodes = biglybtc::services::azureus_nodes;
namespace detail { struct State; struct ContactData; struct ValueData; }
using NodeId = nodes::NodeId;
enum class Error { Input, Limit, Memory, Foreign, Expired, Closed, Busy, NotOpen, AlreadyOpen, Unsupported };
struct Limits {
  std::size_t contacts = 256, values = 1024, entries = 256;
  std::size_t retainedPayloadBytes = 16U * 1024U * 1024U, receiptEvents = 8192, keyBytes = 65536;
};
struct ShortKey {
  std::array<std::uint8_t, 6> bytes{};
  std::int32_t hash = 0;
  bool operator==(const ShortKey& other) const noexcept { return bytes == other.bytes; }
};
class Key {
public:
  Key() = default;
  static std::expected<Key, Error> copy(std::string_view);
  std::string_view bytes() const noexcept;
  ShortKey shortKey() const noexcept;
  std::int32_t hash() const noexcept;
private:
  std::shared_ptr<const std::string> bytes_;
};
class Contact {
public:
  Contact() = default;
  std::uint64_t id() const noexcept;
  db::ContactHandle transport() const noexcept;
private:
  friend struct detail::State;
  friend class Owner;
  std::weak_ptr<detail::State> owner_;
  std::shared_ptr<const detail::ContactData> data_;
  std::uint64_t epoch_ = 0;
};
class Value {
public:
  Value() = default;
  std::uint64_t id() const noexcept;
  std::optional<db::DbValue> snapshot() const noexcept;
private:
  friend struct detail::State;
  friend class Owner;
  std::weak_ptr<detail::State> owner_;
  std::shared_ptr<detail::ValueData> data_;
  std::uint64_t epoch_ = 0;
};
struct ValueFields {
  std::int64_t created = 0;
  db::Payload payload;
  std::int32_t version = 0;
  Contact originator, sender;
  bool local = false;
  std::int32_t flags = 0, lifeHours = 0;
  std::int8_t replication = -1;
};
struct ValueView {
  std::uint64_t id = 0;
  std::optional<db::DbValue> lifecycle;
  Contact originator, sender;
};
enum class BoundaryOperation { Create, Add, Update, Delete, Read };
enum class BoundaryStage { Callback, Diversification };
enum class BoundaryError { Failed };
// A synchronous borrowed port. The owner stores only its immutable identity,
// never this object/provider. Fallible reentry returns Busy. Destroying, moving
// or closing the owner during this call violates the serialized lifetime contract.
class StoragePort {
public:
  explicit StoragePort(bool available = true);
  virtual ~StoragePort();
  StoragePort(const StoragePort&) = delete;
  StoragePort& operator=(const StoragePort&) = delete;
  virtual std::expected<bool, BoundaryError> keyCreated(const Key&, bool local) noexcept = 0;
  virtual std::expected<void, BoundaryError> valueAdded(const ValueView&) noexcept = 0;
  virtual std::expected<void, BoundaryError> valueUpdated(const ValueView&, const ValueView&) noexcept = 0;
  virtual std::expected<void, BoundaryError> valueDeleted(const ValueView&) noexcept = 0;
  virtual std::expected<void, BoundaryError> keyRead(const Contact&) noexcept = 0;
  virtual std::expected<std::int8_t, BoundaryError> diversification() noexcept = 0;
private:
  friend struct detail::State;
  friend class Owner;
  struct Identity;
  std::shared_ptr<const Identity> identity_;
  bool available_;
};
enum class EventKind { Clock, Id, Create, Diversification, Add, Update, Delete, Read, BloomAdd, BloomRemove, BoundaryFailure };
struct Event {
  EventKind kind{};
  std::int64_t scalar = 0;
  bool flag = false;
  Contact contact;
  Key key;
  std::optional<ValueView> oldValue, value;
  BoundaryOperation operation{};
  BoundaryStage stage{};
};
struct Receipt { std::vector<Event> events; };
struct Unit {};
template<class T> struct Result { T value; Receipt receipt; };
struct EntryView { NodeId key{}; ValueView value; };
struct Snapshot {
  bool opened = false, closed = true, directMapAllocated = false, adapterPresent = false, adapterKey = false;
  Key key;
  std::vector<EntryView> direct, forwarded;
  std::vector<ValueView> values;
  std::int32_t directBytes = 0, indirectBytes = 0, localBytes = 0, hits = 0;
  std::int8_t diversification = 1;
  std::size_t liveContacts = 0, liveValues = 0, retainedPayloadBytes = 0;
};
struct CloseReport { std::size_t direct = 0, forwarded = 0, liveValues = 0, liveContacts = 0; };
class Owner {
public:
  static std::expected<Owner, Error> create(Limits = {});
  ~Owner();
  Owner(const Owner&) = delete;
  Owner& operator=(const Owner&) = delete;
  Owner(Owner&&) noexcept;
  Owner& operator=(Owner&&) noexcept;
  std::expected<Contact, Error> contact(const nodes::Contact&);
  std::expected<Result<Value>, Error> value(ValueFields, db::WallClock&);
  std::expected<Result<Value>, Error> receive(const Value&, Contact sender, bool local, db::WallClock&);
  std::expected<Result<Unit>, Error> open(Key, bool local, StoragePort&);
  std::expected<Result<Unit>, Error> add(const Value&, db::WallClock&, StoragePort&);
  std::expected<Result<std::vector<Value>>, Error> read(std::optional<Contact>, std::int32_t max, std::int16_t flags, StoragePort&);
  std::expected<Result<std::optional<Value>>, Error> local(const Contact&);
  std::expected<Result<std::optional<Value>>, Error> any(const Contact&);
  std::expected<Result<std::vector<Value>>, Error> all();
  std::expected<Result<std::optional<Value>>, Error> remove(const Contact&, StoragePort&);
  std::expected<Result<std::optional<Value>>, Error> removeDirect(const NodeId&, StoragePort&);
  std::expected<Result<std::optional<Value>>, Error> removeForwarded(const NodeId&, StoragePort&);
  std::expected<Result<Unit>, Error> hit();
  Snapshot snapshot() const;
  // Serialized owner teardown, outside active port calls. No storage callback.
  CloseReport close() noexcept;
private:
  explicit Owner(std::shared_ptr<detail::State>);
  std::shared_ptr<detail::State> state_;
};
}
