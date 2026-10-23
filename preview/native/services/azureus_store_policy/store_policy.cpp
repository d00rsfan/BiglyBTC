// SPDX-License-Identifier: GPL-2.0-or-later
#include "store_policy.hpp"
#include <bit>
namespace biglybtc::services::azureus_store_policy {
namespace {
int compare(const NodeId& first,const NodeId& second,const NodeId& pivot) {
  for (std::size_t i=0;i<first.size();++i) {
    const auto diff=static_cast<int>(first[i]^pivot[i])-static_cast<int>(second[i]^pivot[i]);if (diff!=0) { return diff; }
  }return 0;
}
std::int64_t elapsed(std::int64_t now,std::int64_t then) { return std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(now)-std::bit_cast<std::uint64_t>(then)); }
}
std::expected<State,ErrorCode> Admission::state() const {
  if (std::this_thread::get_id()!=owner_) { return std::unexpected(ErrorCode::Thread); }return state_;
}
std::expected<Reply,Error> Admission::admit(const Input& input,const Verify& verify) {
  Reply result;
  if (std::this_thread::get_id()!=owner_) { return std::unexpected(Error{ErrorCode::Thread,std::move(result)}); }
  if (active_) { return std::unexpected(Error{ErrorCode::Reentry,std::move(result)}); }
  if (input.keys.size()>255 || input.valueOriginators.size()>255) { return std::unexpected(Error{ErrorCode::Limit,std::move(result)}); }
  std::size_t records=0;
  for (const auto& values:input.valueOriginators) {
    if (values.size()>8192-records) { return std::unexpected(Error{ErrorCode::Limit,std::move(result)}); }records+=values.size();
  }
  for (const auto& key:input.keys) {
    if (key.bytes.size()>255 || (key.block && ((key.block->request && key.block->request->size()>8192) || (key.block->signature && key.block->signature->size()>8192)))) { return std::unexpected(Error{ErrorCode::Limit,std::move(result)}); }
  }
  active_=true;struct Reset { bool& value;~Reset() { value=false; } }reset{active_};
  const auto event=[&](EventType type,std::size_t index=0,std::int32_t factor=0,bool direct=false) { result.events.push_back({type,index,factor,direct}); };
  event(EventType::Alive);result.diversification=std::vector<std::int8_t>(input.keys.size(),1);
  if (input.keys.size()!=input.valueOriginators.size()) { event(EventType::Mismatch);return result; }
  if (input.keys.empty()) { return result; }
  bool forward=false;
  for (const auto& values:input.valueOriginators) { for (const auto& originator:values) { if (originator!=input.sender) { forward=true;break; } }if (forward) { break; } }
  if (forward && !input.seeded) { return result; }
  event(EventType::Verify,0,0,!forward);
  if (!verify) { return std::unexpected(Error{ErrorCode::Authority,std::move(result)}); }
  std::expected<bool,ErrorCode> valid;
  try { valid=verify(!forward); }catch (...) { return std::unexpected(Error{ErrorCode::Callback,std::move(result)}); }
  if (!valid) { return std::unexpected(Error{valid.error(),std::move(result)}); }
  if (!*valid) { event(EventType::VerifyRejected);return result; }
  auto factor=input.k;
  if (input.storageNetwork && *input.storageNetwork==0) { factor=std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(factor)+std::bit_cast<std::uint32_t>(factor/2)); }
  if (forward) {
    if (elapsed(input.now,state_.rejectedAt)<10000 && state_.rejectedSender && *state_.rejectedSender==input.sender) { return result; }
    event(EventType::Closest,0,factor);
    if (!input.furthest) { return std::unexpected(Error{ErrorCode::Input,std::move(result)}); }
    if (compare(*input.furthest,input.sender,input.local)<0) { state_.rejectedSender=input.sender;state_.rejectedAt=input.now;return result; }
  }
  std::optional<Block> blocked;
  for (std::size_t i=0;i<input.keys.size();++i) {
    const auto& key=input.keys[i];event(EventType::HasKey,i);
    if (!key.exists) { event(EventType::IsClosest,i,factor);if (!key.closest) { continue; } }
    event(EventType::Store,i);(*result.diversification)[i]=key.diversification;
    if (!blocked) { event(EventType::BlockDetails,i);blocked=key.block; }
  }
  if (blocked) { result.diversification.reset();result.blockRequest=std::move(blocked->request);result.blockSignature=std::move(blocked->signature); }return result;
}
}
