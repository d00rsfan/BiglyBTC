// SPDX-License-Identifier: GPL-2.0-or-later
#include "db_value.hpp"
#include <utility>

namespace biglybtc::services::azureus_db_value {
Payload::Payload(std::shared_ptr<const std::string> bytes) noexcept:bytes_(std::move(bytes)) {}
std::expected<Payload,Error> Payload::copy(std::optional<std::string_view> bytes) {
  if (!bytes) { return Payload{}; }
  if (bytes->size()>maximumPayloadBytes) { return std::unexpected(Error{ErrorCode::Limit}); }
  if (bytes->empty()) { return empty(); }
  return Payload{std::make_shared<const std::string>(*bytes)};
}
Payload Payload::empty() {
  static const auto emptyBytes=std::make_shared<const std::string>();
  return Payload{emptyBytes};
}
std::optional<std::string_view> Payload::view() const noexcept {
  if (!bytes_) { return {}; }return std::string_view{*bytes_};
}
bool Payload::sharesStorageWith(const Payload& other) const noexcept { return bytes_==other.bytes_; }
ContactHandle::ContactHandle(std::shared_ptr<const nodes::Contact> contact) noexcept:contact_(std::move(contact)) {}
ContactHandle ContactHandle::copy(const nodes::Contact& contact) {
  return ContactHandle{std::make_shared<const nodes::Contact>(contact)};
}
const nodes::Contact* ContactHandle::get() const noexcept { return contact_.get(); }
bool ContactHandle::sharesStorageWith(const ContactHandle& other) const noexcept { return contact_==other.contact_; }

DbValue::DbValue(Fields fields) noexcept:fields_(std::move(fields)) {
  fields_.flags=static_cast<std::uint8_t>(fields_.flags);
  fields_.lifeHours=static_cast<std::uint8_t>(fields_.lifeHours);
}
DbValue DbValue::create(Fields fields,WallClock& clock) {
  DbValue result(std::move(fields));result.reset(clock);return result;
}
DbValue DbValue::receive(const DbValue& other,ContactHandle newSender,bool local,WallClock& clock) {
  auto fields=other.fields_;fields.sender=std::move(newSender);fields.local=local;
  return create(std::move(fields),clock);
}
DbValue DbValue::relay(ContactHandle newSender,WallClock& clock) const {
  return receive(*this,std::move(newSender),fields_.local,clock);
}
DbValue DbValue::deletion(std::int32_t newVersion,WallClock& clock) const {
  auto result=receive(*this,fields_.originator,fields_.local,clock);
  result.fields_.payload=Payload::empty();result.setCreationTime(clock);
  result.fields_.version=newVersion;return result;
}
DbValue::DbValue(DbValue&& other) noexcept:fields_(std::move(other.fields_)),storeTime_(other.storeTime_) {
  other.clearMovedFrom();
}
DbValue& DbValue::operator=(DbValue&& other) noexcept {
  if (this!=&other) { fields_=std::move(other.fields_);storeTime_=other.storeTime_;other.clearMovedFrom(); }
  return *this;
}
void DbValue::clearMovedFrom() noexcept { fields_=Fields{};storeTime_=0; }
void DbValue::reset(WallClock& clock) noexcept {
  storeTime_=clock.nowMilliseconds();
  if (fields_.created>storeTime_) { fields_.created=storeTime_; }
}
void DbValue::setCreationTime(WallClock& clock) noexcept { fields_.created=clock.nowMilliseconds(); }
void DbValue::setStoreTime(std::int64_t time) noexcept { storeTime_=time; }
void DbValue::setOriginatorAndSender(ContactHandle contact) noexcept { fields_.originator=contact;fields_.sender=std::move(contact); }
void DbValue::setFlags(std::int8_t flags) noexcept { fields_.flags=static_cast<std::uint8_t>(flags); }
std::int64_t DbValue::creationTime() const noexcept { return fields_.created; }
std::int64_t DbValue::storeTime() const noexcept { return storeTime_; }
bool DbValue::isLocal() const noexcept { return fields_.local; }
std::int32_t DbValue::version() const noexcept { return fields_.version; }
Payload DbValue::payload() const noexcept { return fields_.payload; }
ContactHandle DbValue::originator() const noexcept { return fields_.originator; }
ContactHandle DbValue::sender() const noexcept { return fields_.sender; }
std::uint8_t DbValue::flags() const noexcept { return static_cast<std::uint8_t>(fields_.flags); }
std::uint8_t DbValue::lifeTimeHours() const noexcept { return static_cast<std::uint8_t>(fields_.lifeHours); }
std::int8_t DbValue::replicationControl() const noexcept { return fields_.replication; }
std::int8_t DbValue::replicationFactor() const noexcept {
  return fields_.replication==-1?-1:static_cast<std::int8_t>(static_cast<std::uint8_t>(fields_.replication)&15);
}
std::int8_t DbValue::replicationFrequencyHours() const noexcept {
  return fields_.replication==-1?-1:static_cast<std::int8_t>((static_cast<std::uint8_t>(fields_.replication)&240)>>4);
}
}
