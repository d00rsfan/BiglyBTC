#include "biglybtc/state_cache.hpp"

namespace biglybtc::core {
namespace {
struct Failure { CacheError error; };
std::optional<std::string> text(const Node& node, std::string_view key, std::size_t limit) {
  const auto* value = node.find(key);
  if (!value) { return std::nullopt; }
  if (value->type != Node::Type::Bytes || value->bytes.size() > limit) { throw Failure{CacheError::Text}; }
  return std::string(value->bytes);
}
std::optional<std::int64_t> integer(const Node& node, std::string_view key, std::int64_t minimum) {
  const auto* value = node.find(key);
  if (!value) { return std::nullopt; }
  if (value->type != Node::Type::Integer || value->integer < minimum) { throw Failure{CacheError::Integer}; }
  return value->integer;
}
} // namespace

std::expected<StateCache, CacheError> readStateCache(const Document& document,
    std::size_t maximumEntries, std::size_t maximumNameBytes) {
  if (document.root().type != Node::Type::Dictionary) { return std::unexpected(CacheError::Root); }
  const auto* entries = document.find("state");
  StateCache result;
  if (!entries) { return result; }
  if (entries->type != Node::Type::List) { return std::unexpected(CacheError::State); }
  if (entries->children.size() > maximumEntries) { return std::unexpected(CacheError::Limit); }
  for (std::size_t index = 0; index < entries->children.size(); ++index) {
    try {
      const auto& entry = entries->children[index];
      if (entry.type != Node::Type::Dictionary) { throw Failure{CacheError::Entry}; }
      const auto* hash = entry.find("hash");
      if (!hash || hash->type != Node::Type::Bytes) { throw Failure{CacheError::Hash}; }
      auto identity = TorrentHash::parse(hash->bytes);
      if (!identity) { throw Failure{CacheError::Hash}; }
      CachedTorrent value{.hash = *identity, .name = text(entry, "name", maximumNameBytes),
          .utf8Name = text(entry, "utf8name", maximumNameBytes), .displayName = std::nullopt,
          .bytes = integer(entry, "size", 0), .files = integer(entry, "fc", 1),
          .type = integer(entry, "tt", 1)};
      if (value.type && *value.type > 3) { throw Failure{CacheError::Integer}; }
      if (const auto* privateFlag = entry.find("priv")) {
        if (privateFlag->type != Node::Type::Integer) { throw Failure{CacheError::Integer}; }
        value.privateTorrent = privateFlag->integer != 0;
      }
      if (const auto* attributes = entry.find("attributes")) {
        if (attributes->type != Node::Type::Dictionary) { throw Failure{CacheError::Entry}; }
        value.displayName = text(*attributes, "displayname", maximumNameBytes);
      }
      value.savedDnd = readSavedDndFacts(entry.find("attributes"));
      result.torrents.insert_or_assign(identity->hex(), std::move(value));
    } catch (const Failure& failure) { result.issues.push_back({index, failure.error}); }
  }
  return result;
}
} // namespace biglybtc::core
