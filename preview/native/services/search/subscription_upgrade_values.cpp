// SPDX-License-Identifier: GPL-2.0-or-later
#include "subscription_upgrade_values.hpp"

#include "biglybtc/bencode.hpp"

#include <new>
#include <optional>
#include <string_view>
#include <utility>

namespace biglybtc::search {
namespace {
using Error=SubscriptionSeedError;
constexpr std::size_t MaximumSourceBytes=4*1024*1024;
constexpr core::Limits SourceLimits{MaximumSourceBytes,MaximumSourceBytes,100000,64};

std::optional<std::string_view> bytes(const core::Node& node,std::string_view key) {
  const auto* field=node.find(key);
  return field&&field->type==core::Node::Type::Bytes?std::optional{field->bytes}:std::nullopt;
}

std::optional<std::int64_t> integer(const core::Node& node,std::string_view key) {
  const auto* field=node.find(key);
  return field&&field->type==core::Node::Type::Integer?std::optional{field->integer}:std::nullopt;
}

const core::Node* signedDetails(const core::Document& document) {
  const auto* vuze=document.find("vuze");
  const auto* components=vuze?vuze->find("components"):nullptr;
  if (!components||components->type!=core::Node::Type::List||components->children.size()!=1) { return nullptr; }
  const auto* content=components->children.front().find("content");
  return content?content->find("details"):nullptr;
}
}

std::expected<SubscriptionUpgradeValues,SubscriptionSeedError> planI2pSubscriptionUpgrade(
    std::string configuration,std::string oldDefinition,std::string newDefinition,
    const I2pSubscriptionUpgrade& update) try {
  const auto candidate=validateSubscriptionSeedPayload(newDefinition,update.presence,update.publicKey,true);
  if (!candidate) { return std::unexpected(candidate.error()); }
  auto catalogue=core::Document::parse(std::move(configuration),SourceLimits);
  if (!catalogue||!core::encodeValue(catalogue->root())) { return std::unexpected(Error::Malformed); }
  const auto* entries=catalogue->find("subs");
  if (!entries||entries->type!=core::Node::Type::List) { return std::unexpected(Error::Malformed); }
  if (entries->children.size()>4096) { return std::unexpected(Error::Limit); }
  const std::string_view previous(update.previous.data(),update.previous.size());
  const core::Node* selected=nullptr;
  for (const auto& row:entries->children) {
    const bool sameKey=bytes(row,"public_key")==update.publicKey;
    const bool sameHash=bytes(row,"hash")==previous;
    if (!sameKey&&!sameHash) { continue; }
    if (selected||!sameKey||!sameHash) { return std::unexpected(Error::Malformed); }
    selected=&row;
  }
  if (!selected||selected->find("private_key")) { return std::unexpected(Error::Ineligible); }
  const auto old=makeSubscriptionSeed(*selected,std::move(oldDefinition),0);
  if (!old||!old->anonymous||!old->presence||old->presence->version>=update.presence.version) {
    return std::unexpected(Error::Ineligible);
  }
  const auto prompted=integer(*selected,"hupv");
  if ((selected->find("hupv")&&!prompted)||prompted.value_or(0)<0||prompted.value_or(0)>update.presence.version) {
    return std::unexpected(Error::Ineligible);
  }

  auto definition=core::Document::parse(std::move(newDefinition),SourceLimits);
  const auto* details=definition?signedDetails(*definition):nullptr;
  auto raw=core::encodeValue(*selected);
  if (!details||!raw) { return std::unexpected(Error::Malformed); }
  auto row=core::Document::parse(std::move(*raw),SourceLimits);
  if (!row) { return std::unexpected(Error::Malformed); }
  for (const auto* key:{"name","version","is_public","is_anonymous"}) {
    const auto* value=details->find(key);
    if (!value) { return std::unexpected(Error::Malformed); }
    const auto encoded=core::encodeValue(*value);
    if (!encoded||!row->replaceValue(key,*encoded)) { return std::unexpected(Error::Malformed); }
  }
  const auto encodedBytes=[](std::string_view value){return std::to_string(value.size())+':'+std::string(value);};
  const auto encodedInteger=[](std::int64_t value){return "i"+std::to_string(value)+"e";};
  if (!row->replaceValue("az_version",encodedInteger(integer(*details,"az_version").value_or(1)))||
      !row->replaceValue("hash",encodedBytes({update.presence.publicationHash.data(),update.presence.publicationHash.size()}))||
      !row->replaceValue("sig",encodedBytes(update.presence.signature))||
      !row->replaceValue("sig_data_size",encodedInteger(update.presence.signedSize))||
      !row->replaceValue("hupv",encodedInteger(update.presence.version))) {
    return std::unexpected(Error::Malformed);
  }
  const auto updated=makeSubscriptionSeed(row->root(),definition->originalBytes(),0);
  if (!updated||!updated->presence||!updated->anonymous) { return std::unexpected(Error::Malformed); }
  auto replacement=std::string("l");
  for (const auto& entry:entries->children) {
    const auto encoded=core::encodeValue(entry);
    if (!encoded) { return std::unexpected(Error::Malformed); }
    replacement+=&entry==selected?row->serialize():*encoded;
    if (replacement.size()>MaximumSourceBytes) { return std::unexpected(Error::Limit); }
  }
  replacement+='e';
  if (!catalogue->replaceValue("subs",replacement)) { return std::unexpected(Error::Malformed); }
  auto changed=catalogue->serialize();
  if (changed.size()>MaximumSourceBytes) { return std::unexpected(Error::Limit); }
  const auto definitionName=updated->fileName.substr(0,updated->fileName.rfind('.'));
  return SubscriptionUpgradeValues{std::move(changed),definition->originalBytes(),definitionName};
} catch (const std::bad_alloc&) {
  return std::unexpected(Error::Limit);
}

} // namespace biglybtc::search
