// SPDX-License-Identifier: GPL-2.0-or-later
// Original core behavior: cb8c6cb9bb97916cbc7e80338a017200ffc3e71f.
#include "plugin_attributes.hpp"
#include "plugin_keys.hpp"
namespace biglybtc::storage {
std::expected<std::int64_t, AttributeError> readOriginalPluginLongMap(
    const core::Node* attributes, std::string_view runtimeId, std::string_view attributeName) {
  if (runtimeId.empty() || runtimeId.size() > 64 || attributeName.size() > 4096) {
    return std::unexpected(AttributeError::Binding);
  }
  if (!attributes) { return 0; }
  if (attributes->type != core::Node::Type::Dictionary) { return std::unexpected(AttributeError::AttributeMapType); }
  const auto key = "Plugin." + std::string(runtimeId) + '.' + std::string(attributeName);
  const auto* value = detail::findOriginalKey(*attributes, key);
  if (!value) { return 0; }
  if (value->type != core::Node::Type::Integer) { return std::unexpected(AttributeError::LongType); }
  return value->integer;
}
std::expected<OriginalLongAttribute, AttributeError> readOriginalPluginLongAttribute(
    const core::Document& primaryState, const core::Node* cachedEntry,
    std::string_view runtimeId, std::string_view attributeName) {
  if (primaryState.root().type != core::Node::Type::Dictionary) { return std::unexpected(AttributeError::Root); }
  if (runtimeId.empty() || runtimeId.size() > 64 || attributeName.size() > 4096) {
    return std::unexpected(AttributeError::Binding);
  }
  const core::Node* attributes = nullptr;
  auto source = AttributeSource::Default;
  if (cachedEntry) {
    if (cachedEntry->type != core::Node::Type::Dictionary) { return std::unexpected(AttributeError::CacheRoot); }
    attributes = cachedEntry->find("attributes");
    if (attributes) {
      if (attributes->type != core::Node::Type::Dictionary) { return std::unexpected(AttributeError::CachedMapType); }
      source = AttributeSource::Cache;
    }
  }
  if (!attributes) {
    attributes = primaryState.find("attributes");
    if (attributes && attributes->type == core::Node::Type::Dictionary) { source = AttributeSource::Primary; }
    else { attributes = nullptr; }
  }
  const auto value = readOriginalPluginLongMap(attributes, runtimeId, attributeName);
  if (!value) { return std::unexpected(value.error()); }
  return OriginalLongAttribute{*value, source};
}
}
