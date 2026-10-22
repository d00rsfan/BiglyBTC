#include "biglybtc/saved_download_tags.hpp"

#include <array>
#include <charconv>
#include <new>
#include <unordered_map>
#include <utility>

namespace biglybtc::core {
namespace {
struct Failure { SavedTagError error; };
bool digitKey(std::string_view key) { return !key.empty() && key.front() >= '0' && key.front() <= '9'; }
void validate(const SavedTagLimits& value) {
  const SavedTagLimits maximum;
  if (value.document.inputBytes > maximum.document.inputBytes
      || value.document.stringBytes > maximum.document.stringBytes
      || value.document.nodes > maximum.document.nodes || value.document.depth > maximum.document.depth
      || value.tags > maximum.tags || value.members > maximum.members || value.fieldBytes > maximum.fieldBytes
      || value.textBytes > maximum.textBytes || value.issues > maximum.issues) { throw Failure{SavedTagError::Limit}; }
}
void text(std::string_view raw, const SavedTagLimits& limits, std::size_t& charged) {
  if (raw.size() > limits.fieldBytes || raw.size() > limits.textBytes - charged) { throw Failure{SavedTagError::Limit}; }
  charged += raw.size();
}
std::optional<std::int32_t> tagId(std::string_view key) {
  if (!digitKey(key)) { return {}; }
  std::int32_t value = 0;
  const auto result = std::from_chars(key.data(), key.data() + key.size(), value);
  return result.ec == std::errc{} && result.ptr == key.data() + key.size() ? std::optional{value} : std::nullopt;
}
std::optional<TorrentHash> normalHash(std::string_view id) {
  if (id.size() != 32 && id.size() != 52) { return {}; }
  std::array<char, 32> bytes{};
  std::size_t used = 0;
  std::uint32_t buffer = 0;
  unsigned bits = 0;
  for (const auto letter : id) {
    const unsigned value = letter >= 'A' && letter <= 'Z' ? static_cast<unsigned>(letter - 'A')
        : letter >= '2' && letter <= '7' ? static_cast<unsigned>(letter - '2' + 26) : 32;
    if (value == 32) { return {}; }
    buffer = (buffer << 5) | value; bits += 5;
    if (bits >= 8) {
      bits -= 8;
      if (used == bytes.size()) { return {}; }
      bytes[used++] = static_cast<char>((buffer >> bits) & 255);
    }
    buffer &= (std::uint32_t{1} << bits) - 1;
  }
  if (buffer != 0 || (used != 20 && used != 32)) { return {}; }
  auto parsed = TorrentHash::parse({bytes.data(), used});
  return parsed ? std::optional{std::move(*parsed)} : std::nullopt;
}
} // namespace

std::expected<SavedDownloadTags, SavedTagError> readSavedDownloadTags(std::string_view encoded, SavedTagLimits limits) {
  try {
    validate(limits);
    if (encoded.size() > limits.document.inputBytes) { return std::unexpected(SavedTagError::Limit); }
    auto parsed = Document::parse(std::string(encoded), limits.document);
    if (!parsed) { return std::unexpected(parsed.error().code.ends_with("_limit") ? SavedTagError::Limit : SavedTagError::Format); }
    if (parsed->root().type != Node::Type::Dictionary) { return std::unexpected(SavedTagError::Root); }
    const auto* manual = parsed->find("3");
    if (manual && manual->type != Node::Type::Dictionary) { return std::unexpected(SavedTagError::ManualType); }
    std::size_t rows = 0, members = 0, charged = 0;
    if (manual) {
      for (std::size_t i = 0; i < manual->children.size(); i += 2) {
        const auto& key = manual->children[i].bytes; const auto& record = manual->children[i + 1];
        if (!digitKey(key)) { continue; }
        if (rows == limits.tags) { throw Failure{SavedTagError::Limit}; } ++rows;
        text(key, limits, charged);
        if (record.type != Node::Type::Dictionary) { continue; }
        if (const auto* name = record.find("n"); name && name->type == Node::Type::Bytes) { text(name->bytes, limits, charged); }
        if (const auto* objects = record.find("o"); objects && objects->type == Node::Type::List) {
          if (objects->children.size() > limits.members - members) { throw Failure{SavedTagError::Limit}; }
          members += objects->children.size();
          for (const auto& member : objects->children) { if (member.type == Node::Type::Bytes) { text(member.bytes, limits, charged); } }
        }
      }
    }
    SavedDownloadTags result;
    result.source = std::make_shared<const Document>(std::move(*parsed));
    result.manualTypePresent = manual != nullptr;
    // Rebind into the privately owned moved document rather than retain a Node
    // pointer into a caller's or moved-from tree.
    manual = result.source->find("3");
    if (!manual) { return result; }
    result.tags.reserve(rows);
    std::unordered_map<std::int32_t, std::size_t> ids;
    const auto issue = [&](SavedTagIssueCode code, std::size_t tag, std::size_t member = NoSavedTagIndex) {
      if (result.issues.size() == limits.issues) { throw Failure{SavedTagError::Limit}; }
      result.issues.push_back({code, tag, member});
    };
    for (std::size_t i = 0; i < manual->children.size(); i += 2) {
      const auto& key = manual->children[i].bytes; const auto& record = manual->children[i + 1];
      if (!digitKey(key)) { continue; }
      const auto index = result.tags.size();
      result.tags.emplace_back(); auto& tag = result.tags.back(); tag.sourceIndex = i / 2; tag.idKey = key;
      tag.id = tagId(key);
      if (!tag.id) { issue(SavedTagIssueCode::InvalidId, index); }
      if (record.type != Node::Type::Dictionary) { issue(SavedTagIssueCode::Type, index); continue; }
      if (tag.id) {
        tag.uid = (std::int64_t{3} << 32) | *tag.id;
        const auto [prior, inserted] = ids.emplace(*tag.id, index);
        if (!inserted) { tag.duplicateId = true; result.tags[prior->second].duplicateId = true; issue(SavedTagIssueCode::DuplicateId, index); }
      }
      if (const auto* name = record.find("n")) {
        if (name->type == Node::Type::Bytes) { tag.rawName = std::string(name->bytes); tag.nameMayRequireLocalization = name->bytes.starts_with("tag."); }
        else { issue(SavedTagIssueCode::Type, index); }
      }
      if (const auto* config = record.find("c")) {
        if (config->type != Node::Type::Dictionary) {
          tag.visibilityMalformed = true; issue(SavedTagIssueCode::Type, index);
        } else if (const auto* visible = config->find("vis")) {
          if (visible->type == Node::Type::Integer) {
            tag.rawVisibility = visible->integer; tag.visibilityDefaulted = false;
            tag.visible = visible->integer == 1;
          } else { tag.visibilityMalformed = true; issue(SavedTagIssueCode::Type, index); }
        }
      }
      const auto* objects = record.find("o"); tag.membersPresent = objects != nullptr;
      if (!objects) { continue; }
      if (objects->type != Node::Type::List) { tag.membersMalformed = true; issue(SavedTagIssueCode::Type, index); continue; }
      tag.members.reserve(objects->children.size()); tag.uniqueHashes.reserve(objects->children.size());
      std::unordered_map<std::string, std::size_t> hashes;
      for (std::size_t j = 0; j < objects->children.size(); ++j) {
        const auto& source = objects->children[j]; tag.members.emplace_back(); auto& member = tag.members.back(); member.sourceIndex = j;
        if (source.type != Node::Type::Bytes) { issue(SavedTagIssueCode::Type, index, j); continue; }
        member.rawId = std::string(source.bytes); member.hash = normalHash(source.bytes);
        if (!member.hash) { issue(SavedTagIssueCode::InvalidMember, index, j); continue; }
        const auto [prior, inserted] = hashes.emplace(member.hash->hex(), j);
        if (!inserted) { member.duplicate = true; tag.members[prior->second].duplicate = true; issue(SavedTagIssueCode::DuplicateMember, index, j); }
        else { tag.uniqueHashes.push_back(*member.hash); }
      }
    }
    return result;
  } catch (const Failure& failure) { return std::unexpected(failure.error); }
  catch (const std::bad_alloc&) { return std::unexpected(SavedTagError::Memory); }
}
} // namespace biglybtc::core
