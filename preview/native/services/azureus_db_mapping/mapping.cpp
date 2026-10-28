// SPDX-License-Identifier: GPL-2.0-or-later
#include "mapping.hpp"
#include <algorithm>
#include <bit>
#include <exception>
#include <limits>
#include <new>
#include <utility>

namespace biglybtc::services::azureus_db_mapping {
namespace {
std::int32_t hashBytes(std::string_view bytes) noexcept {
  std::uint32_t h = 0;
  for (unsigned char c : bytes) {
    const auto signedByte = std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(c));
    h = 31U * h + static_cast<std::uint32_t>(static_cast<std::int32_t>(signedByte));
  }
  return std::bit_cast<std::int32_t>(h);
}
std::int64_t wrappedAdd(std::int64_t a, std::uint64_t b) noexcept {
  return std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(a) + b);
}
std::int32_t signedCount(std::uint32_t v) noexcept { return std::bit_cast<std::int32_t>(v); }
}
std::expected<Key, Error> Key::copy(std::string_view bytes) {
  if (bytes.size() < 6) { return std::unexpected(Error::Input); }
  if (bytes.size() > 65536) { return std::unexpected(Error::Limit); }
  try { Key result; result.bytes_ = std::make_shared<const std::string>(bytes); return result; }
  catch (const std::bad_alloc&) { return std::unexpected(Error::Memory); }
}
std::string_view Key::bytes() const noexcept { return bytes_ ? std::string_view(*bytes_) : std::string_view{}; }
ShortKey Key::shortKey() const noexcept {
  ShortKey result;
  if (!bytes_) { return result; }
  for (std::size_t i = 0; i < result.bytes.size(); ++i) { result.bytes[i] = static_cast<std::uint8_t>((*bytes_)[i]); }
  result.hash = hashBytes(std::string_view(*bytes_).substr(0, 6)); return result;
}
std::int32_t Key::hash() const noexcept { return hashBytes(bytes()); }
struct StoragePort::Identity {};
StoragePort::StoragePort(bool available) : identity_(std::make_shared<const Identity>()), available_(available) {}
StoragePort::~StoragePort() = default;

namespace detail {
struct ContactData { std::uint64_t id; db::ContactHandle transport; };
struct ValueData {
  std::uint64_t id = 0;
  std::optional<db::DbValue> lifecycle;
  Contact originator, sender;
  std::size_t payloadBytes = 0;
};
struct Entry { NodeId key; Value value; };
struct State : std::enable_shared_from_this<State> {
  Limits limits;
  std::vector<std::weak_ptr<const ContactData>> contacts;
  std::vector<std::weak_ptr<ValueData>> values;
  std::vector<Entry> direct, forwarded;
  std::vector<NodeId> removals;
  Key key;
  std::shared_ptr<const StoragePort::Identity> portIdentity;
  std::uint64_t epoch = 1, nextContact = 0, nextValue = 0;
  std::uint32_t directBytes = 0, indirectBytes = 0, localBytes = 0, hits = 0;
  std::int8_t div = 1;
  bool opened = false, closed = false, busy = false, directAllocated = false, adapterPresent = false, adapterKey = false;
  explicit State(Limits l) : limits(l) {
    contacts.reserve(l.contacts); values.reserve(l.values);
    direct.reserve(l.entries); forwarded.reserve(l.entries); removals.reserve(l.entries);
  }
  struct Guard {
    State& state;
    explicit Guard(State& s) : state(s) { state.busy = true; }
    ~Guard() { state.busy = false; }
  };
  std::expected<void, Error> ready(bool requireOpen = true, const StoragePort* port = nullptr) const {
    if (closed) { return std::unexpected(Error::Closed); }
    if (busy) { return std::unexpected(Error::Busy); }
    if (requireOpen && !opened) { return std::unexpected(Error::NotOpen); }
    if (port && opened && portIdentity != port->identity_) { return std::unexpected(Error::Foreign); }
    return {};
  }
  std::expected<void, Error> validate(const Contact& c, bool nullable = false) const {
    if (!c.data_) { if (nullable) { return {}; } return std::unexpected(Error::Input); }
    const auto owner = c.owner_.lock();
    if (!owner) { return std::unexpected(Error::Expired); }
    if (owner.get() != this) { return std::unexpected(Error::Foreign); }
    if (c.epoch_ != epoch) { return std::unexpected(Error::Expired); }
    return {};
  }
  std::expected<void, Error> validate(const Value& v) const {
    if (!v.data_ || !v.data_->lifecycle) { return std::unexpected(Error::Input); }
    const auto owner = v.owner_.lock();
    if (!owner) { return std::unexpected(Error::Expired); }
    if (owner.get() != this) { return std::unexpected(Error::Foreign); }
    if (v.epoch_ != epoch) { return std::unexpected(Error::Expired); }
    return {};
  }
  std::size_t liveContacts() const {
    return static_cast<std::size_t>(std::ranges::count_if(contacts, [](const auto& p) { return !p.expired(); }));
  }
  std::pair<std::size_t, std::size_t> liveValues() const {
    std::size_t count = 0, bytes = 0;
    for (const auto& weak : values) { if (auto v = weak.lock()) { ++count; bytes += v->payloadBytes; } }
    return {count, bytes};
  }
  ValueView view(const Value& v) const noexcept { return {v.id(), v.snapshot(), v.data_->originator, v.data_->sender}; }
  std::expected<Receipt, Error> reserve(std::size_t events) const {
    if (events > limits.receiptEvents) { return std::unexpected(Error::Limit); }
    try { Receipt r; r.events.reserve(events); return r; }
    catch (const std::bad_alloc&) { return std::unexpected(Error::Memory); }
  }
  void emit(Receipt& receipt, Event e) const noexcept {
    if (receipt.events.size() == receipt.events.capacity()) { std::terminate(); }
    receipt.events.push_back(std::move(e));
  }
  const NodeId& rawId(const Contact& c) const noexcept { return c.data_->transport.get()->id; }
  const NodeId& id(const Contact& c, Receipt& r) const noexcept {
    Event e; e.kind = EventKind::Id; e.contact = c; emit(r, std::move(e)); return rawId(c);
  }
  bool isDirect(const Value& v, Receipt& r) const noexcept {
    const auto& origin = id(v.data_->originator, r);
    const auto& sender = id(v.data_->sender, r);
    return origin == sender;
  }
  bool bloomDirect(const Value& v, Receipt& r) const noexcept {
    return !v.data_->lifecycle->isLocal() && isDirect(v, r);
  }
  void valueEvent(Receipt& r, EventKind kind, const Value& v, const std::optional<Value>& old = {}) const noexcept {
    Event e; e.kind = kind; e.value = view(v); if (old) { e.oldValue = view(*old); } emit(r, std::move(e));
  }
  void failure(Receipt& r, BoundaryOperation op, BoundaryStage stage) const noexcept {
    Event e; e.kind = EventKind::BoundaryFailure; e.operation = op; e.stage = stage; emit(r, std::move(e));
  }
  void refresh(StoragePort& port, Receipt& r, BoundaryOperation op) noexcept {
    const auto response = port.diversification();
    if (!response) { failure(r, op, BoundaryStage::Diversification); return; }
    div = *response; Event e; e.kind = EventKind::Diversification; e.scalar = div; emit(r, std::move(e));
  }
  void added(const Value& value, StoragePort& port, Receipt& r) noexcept {
    if (bloomDirect(value, r)) { valueEvent(r, EventKind::BloomAdd, value); }
    if (adapterKey) {
      valueEvent(r, EventKind::Add, value);
      if (!port.valueAdded(view(value))) { failure(r, BoundaryOperation::Add, BoundaryStage::Callback); return; }
      refresh(port, r, BoundaryOperation::Add);
    }
  }
  void updated(const Value& old, const Value& value, StoragePort& port, Receipt& r) noexcept {
    const bool oldDirect = bloomDirect(old, r);
    const bool newDirect = bloomDirect(value, r);
    if (newDirect && !oldDirect) { valueEvent(r, EventKind::BloomAdd, value); }
    if (adapterKey) {
      valueEvent(r, EventKind::Update, value, old);
      if (!port.valueUpdated(view(old), view(value))) { failure(r, BoundaryOperation::Update, BoundaryStage::Callback); return; }
      refresh(port, r, BoundaryOperation::Update);
    }
  }
  void deleted(const Value& value, StoragePort& port, Receipt& r) noexcept {
    if (bloomDirect(value, r)) { valueEvent(r, EventKind::BloomRemove, value); }
    if (adapterKey) {
      valueEvent(r, EventKind::Delete, value);
      if (!port.valueDeleted(view(value))) { failure(r, BoundaryOperation::Delete, BoundaryStage::Callback); return; }
      refresh(port, r, BoundaryOperation::Delete);
    }
  }
  void readBy(const std::optional<Contact>& reader, StoragePort& port, Receipt& r) noexcept {
    if (adapterKey && reader && reader->data_) {
      Event e; e.kind = EventKind::Read; e.contact = *reader; emit(r, std::move(e));
      if (!port.keyRead(*reader)) { failure(r, BoundaryOperation::Read, BoundaryStage::Callback); return; }
      refresh(port, r, BoundaryOperation::Read);
    }
  }
  struct Clock final : db::WallClock {
    State& state; db::WallClock& source; Receipt& receipt;
    Clock(State& s, db::WallClock& c, Receipt& r) : state(s), source(c), receipt(r) {}
    std::int64_t nowMilliseconds() noexcept override {
      const auto now = source.nowMilliseconds(); Event e; e.kind = EventKind::Clock; e.scalar = now;
      state.emit(receipt, std::move(e)); return now;
    }
  };
  using Position = std::vector<Entry>::iterator;
  Position find(std::vector<Entry>& entries, const NodeId& keyValue) noexcept { return std::ranges::find(entries, keyValue, &Entry::key); }
  std::optional<Value> lookup(std::vector<Entry>& entries, const NodeId& keyValue, bool promote) noexcept {
    auto it = find(entries, keyValue); if (it == entries.end()) { return {}; }
    Value result = it->value;
    if (promote) { std::rotate(it, it + 1, entries.end()); }
    return result;
  }
  std::optional<Value> erase(std::vector<Entry>& entries, const NodeId& keyValue, bool directMap, StoragePort& port, Receipt& r) noexcept {
    auto it = find(entries, keyValue); if (it == entries.end()) { return {}; }
    Value old = it->value; entries.erase(it);
    const auto bytes = static_cast<std::uint32_t>(old.data_->payloadBytes);
    (directMap ? directBytes : indirectBytes) -= bytes;
    if (old.data_->lifecycle->isLocal()) { localBytes -= bytes; }
    deleted(old, port, r); return old;
  }
  void put(std::vector<Entry>& entries, const NodeId& keyValue, const Value& value, bool directMap,
           db::WallClock& clock, StoragePort& port, Receipt& r) noexcept {
    auto it = find(entries, keyValue); std::optional<Value> old;
    if (it != entries.end()) {
      old = it->value; it->value = value; std::rotate(it, it + 1, entries.end());
    } else { entries.push_back({keyValue, value}); }
    if (old) {
      const auto oldVersion = old->data_->lifecycle->version(); const auto version = value.data_->lifecycle->version();
      if (oldVersion != -1 && version != -1 && oldVersion >= version) {
        if (oldVersion == version) { Clock recorded(*this, clock, r); old->data_->lifecycle->reset(recorded); }
        entries.back().value = *old; return;
      }
      if (!directMap && (oldVersion == -1 || version == -1) &&
          old->data_->lifecycle->creationTime() > wrappedAdd(value.data_->lifecycle->creationTime(), 30000)) {
        entries.back().value = *old; return;
      }
      const auto bytes = static_cast<std::uint32_t>(old->data_->payloadBytes);
      (directMap ? directBytes : indirectBytes) -= bytes;
      if (old->data_->lifecycle->isLocal()) { localBytes -= bytes; }
    }
    const auto bytes = static_cast<std::uint32_t>(value.data_->payloadBytes);
    (directMap ? directBytes : indirectBytes) += bytes;
    if (value.data_->lifecycle->isLocal()) { localBytes += bytes; }
    if (old) { updated(*old, value, port, r); } else { added(value, port, r); }
  }
  std::expected<void, Error> valueRoom(std::size_t bytes) {
    const auto [count, retained] = liveValues();
    if (count == limits.values || bytes > limits.retainedPayloadBytes - retained || nextValue == std::numeric_limits<std::uint64_t>::max()) {
      return std::unexpected(Error::Limit);
    }
    return {};
  }
  Value publish(std::shared_ptr<ValueData> data) noexcept {
    std::erase_if(values, [](const auto& p) { return p.expired(); });
    data->id = ++nextValue; values.push_back(data);
    Value result; result.owner_ = weak_from_this(); result.data_ = std::move(data); result.epoch_ = epoch; return result;
  }
};
}

std::uint64_t Contact::id() const noexcept { return data_ ? data_->id : 0; }
db::ContactHandle Contact::transport() const noexcept { return data_ ? data_->transport : db::ContactHandle{}; }
std::uint64_t Value::id() const noexcept { return data_ ? data_->id : 0; }
std::optional<db::DbValue> Value::snapshot() const noexcept { return data_ ? data_->lifecycle : std::optional<db::DbValue>{}; }
Owner::Owner(std::shared_ptr<detail::State> state) : state_(std::move(state)) {}
std::expected<Owner, Error> Owner::create(Limits l) {
  if (!l.contacts || l.contacts > 4096 || !l.values || l.values > 4096 || !l.entries || l.entries > 4096 ||
      l.retainedPayloadBytes > 64U * 1024U * 1024U || l.receiptEvents < 16 || l.receiptEvents > 32768 || l.keyBytes < 6 || l.keyBytes > 65536) {
    return std::unexpected(Error::Limit);
  }
  try { return Owner(std::make_shared<detail::State>(l)); }
  catch (const std::bad_alloc&) { return std::unexpected(Error::Memory); }
}
Owner::~Owner() { close(); }
Owner::Owner(Owner&& other) noexcept : state_(std::move(other.state_)) {}
Owner& Owner::operator=(Owner&& other) noexcept {
  if (this != &other) { close(); state_ = std::move(other.state_); } return *this;
}
std::expected<Contact, Error> Owner::contact(const nodes::Contact& supplied) {
  if (!state_) { return std::unexpected(Error::Expired); }
  auto& s = *state_; if (auto ok = s.ready(false); !ok) { return std::unexpected(ok.error()); }
  if (s.liveContacts() == s.limits.contacts || s.nextContact == std::numeric_limits<std::uint64_t>::max()) { return std::unexpected(Error::Limit); }
  try {
    auto data = std::make_shared<const detail::ContactData>(detail::ContactData{s.nextContact + 1, db::ContactHandle::copy(supplied)});
    std::erase_if(s.contacts, [](const auto& p) { return p.expired(); }); s.contacts.push_back(data); ++s.nextContact;
    Contact result; result.owner_ = state_; result.data_ = std::move(data); result.epoch_ = s.epoch; return result;
  } catch (const std::bad_alloc&) { return std::unexpected(Error::Memory); }
}
std::expected<Result<Value>, Error> Owner::value(ValueFields fields, db::WallClock& clock) {
  if (!state_) { return std::unexpected(Error::Expired); }
  auto& s = *state_; if (auto ok = s.ready(false); !ok) { return std::unexpected(ok.error()); }
  for (const auto* c : {&fields.originator, &fields.sender}) { if (auto ok = s.validate(*c, true); !ok) { return std::unexpected(ok.error()); } }
  const auto bytes = fields.payload.view().value_or(std::string_view{}).size();
  if (auto ok = s.valueRoom(bytes); !ok) { return std::unexpected(ok.error()); }
  auto receipt = s.reserve(1); if (!receipt) { return std::unexpected(receipt.error()); }
  try {
    auto data = std::make_shared<detail::ValueData>(); data->originator = fields.originator; data->sender = fields.sender; data->payloadBytes = bytes;
    db::Fields raw{fields.created, fields.payload, fields.version, fields.originator.transport(), fields.sender.transport(), fields.local, fields.flags, fields.lifeHours, fields.replication};
    detail::State::Guard guard(s); detail::State::Clock recorded(s, clock, *receipt);
    data->lifecycle.emplace(db::DbValue::create(std::move(raw), recorded));
    return Result<Value>{s.publish(std::move(data)), std::move(*receipt)};
  } catch (const std::bad_alloc&) { return std::unexpected(Error::Memory); }
}
std::expected<Result<Value>, Error> Owner::receive(const Value& old, Contact sender, bool localValue, db::WallClock& clock) {
  if (!state_) { return std::unexpected(Error::Expired); }
  auto& s = *state_; if (auto ok = s.ready(false); !ok) { return std::unexpected(ok.error()); }
  if (auto ok = s.validate(old); !ok) { return std::unexpected(ok.error()); }
  if (auto ok = s.validate(sender, true); !ok) { return std::unexpected(ok.error()); }
  if (auto ok = s.valueRoom(old.data_->payloadBytes); !ok) { return std::unexpected(ok.error()); }
  auto receipt = s.reserve(1); if (!receipt) { return std::unexpected(receipt.error()); }
  try {
    auto data = std::make_shared<detail::ValueData>(); data->originator = old.data_->originator; data->sender = sender; data->payloadBytes = old.data_->payloadBytes;
    detail::State::Guard guard(s); detail::State::Clock recorded(s, clock, *receipt);
    data->lifecycle.emplace(db::DbValue::receive(*old.data_->lifecycle, sender.transport(), localValue, recorded));
    return Result<Value>{s.publish(std::move(data)), std::move(*receipt)};
  } catch (const std::bad_alloc&) { return std::unexpected(Error::Memory); }
}
std::expected<Result<Unit>, Error> Owner::open(Key keyValue, bool localValue, StoragePort& port) {
  if (!state_) { return std::unexpected(Error::Expired); }
  auto& s = *state_; if (auto ok = s.ready(false); !ok) { return std::unexpected(ok.error()); }
  if (s.opened) { return std::unexpected(Error::AlreadyOpen); }
  if (keyValue.bytes().size() < 6) { return std::unexpected(Error::Input); }
  if (keyValue.bytes().size() > s.limits.keyBytes) { return std::unexpected(Error::Limit); }
  auto receipt = s.reserve(3); if (!receipt) { return std::unexpected(receipt.error()); }
  detail::State::Guard guard(s); s.key = std::move(keyValue); s.opened = true;
  s.portIdentity = port.identity_; s.adapterPresent = port.available_;
  if (s.adapterPresent) {
    Event e; e.kind = EventKind::Create; e.key = s.key; e.flag = localValue; s.emit(*receipt, std::move(e));
    const auto response = port.keyCreated(s.key, localValue);
    if (!response) { s.failure(*receipt, BoundaryOperation::Create, BoundaryStage::Callback); }
    else if (*response) { s.adapterKey = true; s.refresh(port, *receipt, BoundaryOperation::Create); }
  }
  return Result<Unit>{{}, std::move(*receipt)};
}
std::expected<Result<Unit>, Error> Owner::add(const Value& value, db::WallClock& clock, StoragePort& port) {
  if (!state_) { return std::unexpected(Error::Expired); }
  auto& s = *state_; if (auto ok = s.ready(true, &port); !ok) { return std::unexpected(ok.error()); }
  if (auto ok = s.validate(value); !ok) { return std::unexpected(ok.error()); }
  if (!value.data_->lifecycle->payload().view()) { return std::unexpected(Error::Input); }
  for (const auto* c : {&value.data_->originator, &value.data_->sender}) { if (auto ok = s.validate(*c); !ok) { return std::unexpected(ok.error()); } }
  const NodeId origin = s.rawId(value.data_->originator);
  const bool direct = origin == s.rawId(value.data_->sender);
  const bool directExists = s.find(s.direct, origin) != s.direct.end();
  const bool forwardedExists = s.find(s.forwarded, origin) != s.forwarded.end();
  const bool inserts = direct ? !directExists : !directExists && !forwardedExists && s.div == 1;
  if (inserts && s.direct.size() + s.forwarded.size() == s.limits.entries) { return std::unexpected(Error::Limit); }
  auto receipt = s.reserve(14 + 2 * s.forwarded.size()); if (!receipt) { return std::unexpected(receipt.error()); }
  detail::State::Guard guard(s);
  const NodeId keyValue = s.id(value.data_->originator, *receipt);
  const bool classifiedDirect = s.isDirect(value, *receipt);
  if (classifiedDirect) {
    s.directAllocated = true; s.put(s.direct, keyValue, value, true, clock, port, *receipt);
    s.removals.clear();
    for (const auto& entry : s.forwarded) {
      const auto& existingId = s.id(entry.value.data_->originator, *receipt);
      const auto& incomingId = s.id(value.data_->originator, *receipt);
      if (existingId == incomingId) { s.removals.push_back(entry.key); }
    }
    for (const auto& removal : s.removals) { s.erase(s.forwarded, removal, false, port, *receipt); }
  } else {
    if (s.directAllocated && s.lookup(s.direct, keyValue, true)) { return Result<Unit>{{}, std::move(*receipt)}; }
    // getOriginatorValueID repeats the originator ID read in the source.
    const NodeId forwardedKey = s.id(value.data_->originator, *receipt);
    const auto existing = s.lookup(s.forwarded, forwardedKey, true);
    if (existing || s.div == 1) { s.put(s.forwarded, forwardedKey, value, false, clock, port, *receipt); }
  }
  return Result<Unit>{{}, std::move(*receipt)};
}
std::expected<Result<std::vector<Value>>, Error> Owner::read(std::optional<Contact> reader, std::int32_t max, std::int16_t flags, StoragePort& port) {
  if (!state_) { return std::unexpected(Error::Expired); }
  auto& s = *state_; if (auto ok = s.ready(true, &port); !ok) { return std::unexpected(ok.error()); }
  if ((static_cast<std::uint16_t>(flags) & 8U) != 0) { return std::unexpected(Error::Unsupported); }
  if (reader) { if (auto ok = s.validate(*reader, true); !ok) { return std::unexpected(ok.error()); } }
  auto receipt = s.reserve(3); if (!receipt) { return std::unexpected(receipt.error()); }
  try {
    std::vector<Value> result; std::vector<std::string_view> seen; std::vector<NodeId> used;
    const auto count = s.direct.size() + s.forwarded.size(); result.reserve(count); seen.reserve(count); used.reserve(count);
    detail::State::Guard guard(s);
    for (auto* entries : {&s.direct, &s.forwarded}) {
      used.clear();
      for (const auto& entry : *entries) {
        if (max != 0 && (max < 0 || result.size() >= static_cast<std::size_t>(max))) { break; }
        const auto payload = *entry.value.data_->lifecycle->payload().view();
        if (std::ranges::find(seen, payload) != seen.end()) { continue; }
        seen.push_back(payload);
        if (!payload.empty()) { result.push_back(entry.value); used.push_back(entry.key); }
      }
      for (const auto& key : used) { s.lookup(*entries, key, true); }
    }
    s.readBy(reader, port, *receipt); return Result<std::vector<Value>>{std::move(result), std::move(*receipt)};
  } catch (const std::bad_alloc&) { return std::unexpected(Error::Memory); }
}
std::expected<Result<std::optional<Value>>, Error> Owner::local(const Contact& originator) {
  if (!state_) { return std::unexpected(Error::Expired); }
  auto& s = *state_; if (auto ok = s.ready(); !ok) { return std::unexpected(ok.error()); }
  if (auto ok = s.validate(originator); !ok) { return std::unexpected(ok.error()); }
  auto receipt = s.reserve(1); if (!receipt) { return std::unexpected(receipt.error()); }
  detail::State::Guard guard(s); std::optional<Value> result;
  if (s.directAllocated) { result = s.lookup(s.direct, s.id(originator, *receipt), true); }
  return Result<std::optional<Value>>{std::move(result), std::move(*receipt)};
}
std::expected<Result<std::optional<Value>>, Error> Owner::any(const Contact& originator) {
  auto result = local(originator); if (!result) { return result; }
  if (!result->value && !state_->forwarded.empty()) { result->value = state_->forwarded.front().value; }
  return result;
}
std::expected<Result<std::vector<Value>>, Error> Owner::all() {
  if (!state_) { return std::unexpected(Error::Expired); }
  auto& s = *state_; if (auto ok = s.ready(); !ok) { return std::unexpected(ok.error()); }
  try {
    std::vector<Value> result; std::vector<std::string_view> seen;
    const auto count = s.direct.size() + s.forwarded.size(); result.reserve(count); seen.reserve(count);
    detail::State::Guard guard(s);
    for (const auto* entries : {&s.direct, &s.forwarded}) {
      for (const auto& entry : *entries) {
        const auto payload = *entry.value.data_->lifecycle->payload().view();
        if (std::ranges::find(seen, payload) == seen.end()) {
          seen.push_back(payload); if (!payload.empty()) { result.push_back(entry.value); }
        }
      }
    }
    return Result<std::vector<Value>>{std::move(result), {}};
  } catch (const std::bad_alloc&) { return std::unexpected(Error::Memory); }
}
std::expected<Result<std::optional<Value>>, Error> Owner::remove(const Contact& originator, StoragePort& port) {
  if (!state_) { return std::unexpected(Error::Expired); }
  auto& s = *state_; if (auto ok = s.ready(true, &port); !ok) { return std::unexpected(ok.error()); }
  if (auto ok = s.validate(originator); !ok) { return std::unexpected(ok.error()); }
  auto receipt = s.reserve(8); if (!receipt) { return std::unexpected(receipt.error()); }
  detail::State::Guard guard(s); const NodeId key = s.id(originator, *receipt);
  auto result = s.erase(s.direct, key, true, port, *receipt);
  return Result<std::optional<Value>>{std::move(result), std::move(*receipt)};
}
std::expected<Result<std::optional<Value>>, Error> Owner::removeDirect(const NodeId& key, StoragePort& port) {
  if (!state_) { return std::unexpected(Error::Expired); }
  auto& s = *state_; if (auto ok = s.ready(true, &port); !ok) { return std::unexpected(ok.error()); }
  auto receipt = s.reserve(7); if (!receipt) { return std::unexpected(receipt.error()); }
  detail::State::Guard guard(s); auto result = s.erase(s.direct, key, true, port, *receipt);
  return Result<std::optional<Value>>{std::move(result), std::move(*receipt)};
}
std::expected<Result<std::optional<Value>>, Error> Owner::removeForwarded(const NodeId& key, StoragePort& port) {
  if (!state_) { return std::unexpected(Error::Expired); }
  auto& s = *state_; if (auto ok = s.ready(true, &port); !ok) { return std::unexpected(ok.error()); }
  auto receipt = s.reserve(7); if (!receipt) { return std::unexpected(receipt.error()); }
  detail::State::Guard guard(s); auto result = s.erase(s.forwarded, key, false, port, *receipt);
  return Result<std::optional<Value>>{std::move(result), std::move(*receipt)};
}
std::expected<Result<Unit>, Error> Owner::hit() {
  if (!state_) { return std::unexpected(Error::Expired); }
  auto& s = *state_; if (auto ok = s.ready(); !ok) { return std::unexpected(ok.error()); }
  ++s.hits; return Result<Unit>{{}, {}};
}
Snapshot Owner::snapshot() const {
  if (!state_) { return {}; }
  const auto& s = *state_; Snapshot result;
  result.opened = s.opened; result.closed = s.closed; result.directMapAllocated = s.directAllocated;
  result.adapterPresent = s.adapterPresent; result.adapterKey = s.adapterKey; result.key = s.key;
  result.direct.reserve(s.direct.size()); result.forwarded.reserve(s.forwarded.size()); result.values.reserve(s.values.size());
  for (const auto& e : s.direct) { result.direct.push_back({e.key, s.view(e.value)}); }
  for (const auto& e : s.forwarded) { result.forwarded.push_back({e.key, s.view(e.value)}); }
  for (const auto& weak : s.values) {
    if (auto value = weak.lock()) {
      Value handle; handle.data_ = std::move(value); handle.owner_ = state_; handle.epoch_ = s.epoch;
      result.values.push_back(s.view(handle));
    }
  }
  result.directBytes = signedCount(s.directBytes - s.localBytes); result.indirectBytes = signedCount(s.indirectBytes);
  result.localBytes = signedCount(s.localBytes); result.hits = signedCount(s.hits); result.diversification = s.div;
  result.liveContacts = s.liveContacts();
  const auto [count, retained] = s.liveValues(); result.liveValues = count; result.retainedPayloadBytes = retained;
  return result;
}
CloseReport Owner::close() noexcept {
  if (!state_ || state_->closed) { return {}; }
  auto& s = *state_; if (s.busy) { std::terminate(); } // forbidden reentrant teardown
  CloseReport result{s.direct.size(), s.forwarded.size(), s.liveValues().first, s.liveContacts()};
  s.closed = true; ++s.epoch; s.direct.clear(); s.forwarded.clear(); s.values.clear(); s.contacts.clear();
  s.removals.clear(); s.portIdentity.reset(); s.key = {}; s.opened = false; s.adapterKey = false;
  s.adapterPresent = false; s.directAllocated = false; s.directBytes = s.indirectBytes = s.localBytes = s.hits = 0; s.div = 1;
  return result;
}
}
