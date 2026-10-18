#include "biglybtc/saved_services.hpp"

#include <limits>
#include <new>
#include <unordered_set>

namespace biglybtc::core {
namespace {
// Source field contracts: BiglyBT cb8c6cb9bb97916cbc7e80338a017200ffc3e71f,
// TRHostConfigImpl.loadConfig/recoverStats and ShareResource*/ShareItemImpl
// serialization. This is a native saved-data projection, not manager recovery.
struct Failure { SavedServiceError error; };

void validateLimits(const SavedServiceLimits& limits) {
  const SavedServiceLimits maximum;
  if (limits.document.inputBytes > maximum.document.inputBytes
      || limits.document.stringBytes > maximum.document.stringBytes
      || limits.document.nodes > maximum.document.nodes
      || limits.document.depth > maximum.document.depth || limits.rows > maximum.rows
      || limits.textBytes > maximum.textBytes || limits.fieldBytes > maximum.fieldBytes
      || limits.issues > maximum.issues) { throw Failure{SavedServiceError::Limit}; }
}
void validateNode(const Node& node, const Limits& limits, std::size_t depth, std::size_t& nodes) {
  if (depth > limits.depth || nodes == limits.nodes) { throw Failure{SavedServiceError::Limit}; }
  ++nodes;
  if (node.changed || node.origin) { throw Failure{SavedServiceError::EditedDocument}; }
  if (node.type == Node::Type::Bytes && node.bytes.size() > limits.stringBytes) {
    throw Failure{SavedServiceError::Limit};
  }
  for (const auto& child : node.children) { validateNode(child, limits, depth + 1, nodes); }
}
void validateDocument(const Document& document, const SavedServiceLimits& limits) {
  validateLimits(limits);
  if (document.originalBytes().size() > limits.document.inputBytes) { throw Failure{SavedServiceError::Limit}; }
  std::size_t nodes = 0;
  validateNode(document.root(), limits.document, 0, nodes);
  if (document.root().type != Node::Type::Dictionary) { throw Failure{SavedServiceError::Root}; }
}
std::int32_t signed32(std::int64_t value) {
  const auto low = static_cast<std::uint32_t>(value);
  if (low <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
    return static_cast<std::int32_t>(low);
  }
  // Both casts are representable; no implementation-defined unsigned->signed narrowing.
  return -1 - static_cast<std::int32_t>(std::numeric_limits<std::uint32_t>::max() - low);
}
bool utf8(std::string_view text) {
  for (std::size_t i = 0; i < text.size();) {
    const auto first = static_cast<unsigned char>(text[i]);
    if (first < 0x80) { ++i; continue; }
    const std::size_t length = first >= 0xc2 && first <= 0xdf ? 2
        : first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
    if (length == 0 || length > text.size() - i) { return false; }
    for (std::size_t j = 1; j < length; ++j) {
      const auto byte = static_cast<unsigned char>(text[i + j]);
      if (byte < 0x80 || byte > 0xbf) { return false; }
    }
    const auto second = static_cast<unsigned char>(text[i + 1]);
    if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0)
        || (first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90)) { return false; }
    i += length;
  }
  return true;
}
bool asciiTrue(std::string_view text) {
  constexpr std::string_view expected = "true";
  if (text.size() != expected.size()) { return false; }
  for (std::size_t i = 0; i < text.size(); ++i) {
    auto byte = static_cast<unsigned char>(text[i]);
    if (byte >= 'A' && byte <= 'Z') { byte += 'a' - 'A'; }
    if (byte != static_cast<unsigned char>(expected[i])) { return false; }
  }
  return true;
}

class Reader final {
public:
  Reader(const SavedServiceLimits& limits, std::vector<SavedServiceIssue>& issues)
      : limits_(limits), issues_(issues) {}
  void row(std::size_t index) { index_ = index; }
  void issue(SavedIssueCode code, SavedField field) {
    if (issues_.size() == limits_.issues) { throw Failure{SavedServiceError::Limit}; }
    issues_.push_back({code, index_, field});
  }
  const Node* member(const Node& node, std::string_view key, SavedField field, bool required = false) {
    const auto* value = node.find(key);
    if (!value && required) { issue(SavedIssueCode::Missing, field); }
    return value;
  }
  std::optional<std::int64_t> integer(const Node& node, std::string_view key,
      SavedField field, bool required = false) {
    const auto* value = member(node, key, field, required);
    if (!value) { return std::nullopt; }
    if (value->type != Node::Type::Integer) { issue(SavedIssueCode::Type, field); return std::nullopt; }
    return value->integer;
  }
  std::optional<bool> flag(const Node& node, std::string_view key, SavedField field,
      std::optional<bool> absent) {
    if (!node.find(key)) { return absent; }
    const auto value = integer(node, key, field);
    return value ? std::optional<bool>{*value == 1} : std::nullopt;
  }
  std::optional<std::string_view> view(const Node& node, std::string_view key,
      SavedField field, bool required = false) {
    const auto* value = member(node, key, field, required);
    if (!value) { return std::nullopt; }
    if (value->type != Node::Type::Bytes) { issue(SavedIssueCode::Type, field); return std::nullopt; }
    if (value->bytes.size() > limits_.fieldBytes) { throw Failure{SavedServiceError::Limit}; }
    return value->bytes;
  }
  void charge(std::size_t size) {
    if (size > limits_.textBytes - textBytes_) { throw Failure{SavedServiceError::Limit}; }
    textBytes_ += size;
  }
  std::optional<std::string> binary(const Node& node, std::string_view key,
      SavedField field, bool required = false) {
    const auto value = view(node, key, field, required);
    if (!value) { return std::nullopt; }
    charge(value->size());
    return std::string(*value);
  }
  bool textValid(std::string_view bytes, SavedField field, SavedTextEncoding encoding) {
    const bool valid = encoding == SavedTextEncoding::Latin1 || utf8(bytes);
    if (!valid) { issue(SavedIssueCode::Encoding, field); }
    if (bytes.find('\0') != std::string_view::npos) { issue(SavedIssueCode::Nul, field); }
    return valid;
  }
  std::optional<SavedText> text(const Node& node, std::string_view key, SavedField field,
      bool required = false, SavedTextEncoding encoding = SavedTextEncoding::Utf8) {
    auto value = binary(node, key, field, required);
    if (!value) { return std::nullopt; }
    const bool valid = textValid(*value, field, encoding);
    const bool nul = value->find('\0') != std::string::npos;
    return SavedText{std::move(*value), encoding, valid, nul};
  }
private:
  const SavedServiceLimits& limits_;
  std::vector<SavedServiceIssue>& issues_;
  std::size_t index_ = SavedRootIndex;
  std::size_t textBytes_ = 0;
};

const Node* records(const Document& document, std::string_view key,
    const SavedServiceLimits& limits, Reader& reader) {
  const auto* list = document.find(key);
  if (!list) { reader.issue(SavedIssueCode::Missing, SavedField::Records); return nullptr; }
  if (list->type != Node::Type::List) { throw Failure{SavedServiceError::List}; }
  if (list->children.size() > limits.rows) { throw Failure{SavedServiceError::Limit}; }
  return list;
}
SavedHostingRecord hosting(const Node& node, std::size_t index, Reader& reader) {
  SavedHostingRecord result;
  result.sourceIndex = index;
  if (node.type != Node::Type::Dictionary) {
    reader.issue(SavedIssueCode::Type, SavedField::Record);
    return result;
  }
  result.hash = reader.binary(node, "hash", SavedField::Hash, true);
  if (result.hash && result.hash->size() != 20 && result.hash->size() != 32) {
    reader.issue(SavedIssueCode::HashLength, SavedField::Hash);
  }
  result.persistent = reader.flag(node, "persistent", SavedField::Persistent, true);
  result.passive = reader.flag(node, "passive", SavedField::Passive, false);
  if (result.persistent) {
    result.disposition = *result.persistent ? SavedHostingDisposition::PersistentHost : SavedHostingDisposition::StatsBackup;
  }
  result.rawStatus = reader.integer(node, "status", SavedField::Status, result.persistent.value_or(false));
  if (result.rawStatus) {
    result.status = signed32(*result.rawStatus);
    if (*result.status < 0 || *result.status > 3) { reader.issue(SavedIssueCode::Unknown, SavedField::Status); }
  }
  result.dateAdded = reader.integer(node, "dateadded", SavedField::DateAdded);
  result.backupTime = reader.integer(node, "backup_time", SavedField::BackupTime);
  result.torrentFile = reader.text(node, "torrent_file", SavedField::TorrentFile, false, SavedTextEncoding::Latin1);
  if (const auto* stats = node.find("stats")) {
    if (stats->type != Node::Type::Dictionary) { reader.issue(SavedIssueCode::Type, SavedField::Stats); }
    else {
      SavedHostingStats value;
      value.completed = reader.integer(*stats, "completed", SavedField::Completed, true);
      value.announces = reader.integer(*stats, "announces", SavedField::Announces, true);
      value.uploaded = reader.integer(*stats, "uploaded", SavedField::Uploaded, true);
      value.downloaded = reader.integer(*stats, "downloaded", SavedField::Downloaded, true);
      value.scrapes = reader.integer(*stats, "scrapes", SavedField::Scrapes);
      value.bytesIn = reader.integer(*stats, "bytesin", SavedField::BytesIn);
      value.bytesOut = reader.integer(*stats, "bytesout", SavedField::BytesOut);
      result.stats = value;
    }
  }
  return result;
}
SavedShareRecord share(const Node& node, std::size_t index, Reader& reader) {
  SavedShareRecord result;
  result.sourceIndex = index;
  if (node.type != Node::Type::Dictionary) {
    reader.issue(SavedIssueCode::Type, SavedField::Record);
    return result;
  }
  result.rawType = reader.integer(node, "type", SavedField::ResourceType, true);
  if (result.rawType) {
    result.type = signed32(*result.rawType);
    switch (*result.type) {
      case 1: result.kind = SavedShareKind::File; break;
      case 2: result.kind = SavedShareKind::Directory; break;
      case 3: result.kind = SavedShareKind::DirectoryContents; break;
      default: reader.issue(SavedIssueCode::Unknown, SavedField::ResourceType); break;
    }
  }
  result.file = reader.text(node, "file", SavedField::File, true);
  if (const auto* attributes = node.find("attributes")) {
    if (attributes->type != Node::Type::Dictionary) { reader.issue(SavedIssueCode::Type, SavedField::Attributes); }
    else { result.category = reader.text(*attributes, "Category", SavedField::Category); }
  }
  result.persistent = false;
  if (const auto* properties = node.find("props")) {
    if (properties->type != Node::Type::Dictionary) {
      reader.issue(SavedIssueCode::Type, SavedField::Properties);
      result.persistent.reset();
    } else {
      if (properties->find("persistent")) {
        const auto persistent = reader.view(*properties, "persistent", SavedField::Persistent);
        result.persistent.reset();
        if (persistent) {
          const bool valid = reader.textValid(*persistent, SavedField::Persistent, SavedTextEncoding::Utf8);
          if (valid && persistent->find('\0') == std::string_view::npos) { result.persistent = asciiTrue(*persistent); }
        }
      }
      result.tagUids = reader.text(*properties, "tags", SavedField::Tags);
    }
  }
  if (result.kind == SavedShareKind::DirectoryContents) {
    const auto recursive = reader.integer(node, "recursive", SavedField::Recursive, true);
    if (recursive) { result.recursive = *recursive == 1; }
  } else if (result.kind == SavedShareKind::File || result.kind == SavedShareKind::Directory) {
    result.fingerprint = reader.binary(node, "ihash", SavedField::Fingerprint, true);
    result.cacheFile = reader.text(node, "ifile", SavedField::CacheFile, true);
  }
  return result;
}
} // namespace

std::expected<SavedHosting, SavedServiceError> readSavedHosting(
    const Document& document, SavedServiceLimits limits) {
  try {
    validateDocument(document, limits);
    SavedHosting result;
    Reader reader(limits, result.issues);
    const auto* list = records(document, "torrents", limits, reader);
    result.listPresent = list != nullptr;
    if (list) {
      result.records.reserve(list->children.size());
      std::unordered_set<std::string_view> identities;
      for (std::size_t i = 0; i < list->children.size(); ++i) {
        reader.row(i);
        result.records.push_back(hosting(list->children[i], i, reader));
        auto& record = result.records.back();
        if (record.hash && !identities.insert(*record.hash).second) {
          record.duplicateIdentity = true;
          reader.issue(SavedIssueCode::DuplicateIdentity, SavedField::Hash);
        }
      }
    }
    result.source = std::make_shared<const Document>(document);
    return result;
  } catch (const Failure& failure) { return std::unexpected(failure.error); }
  catch (const std::bad_alloc&) { return std::unexpected(SavedServiceError::Memory); }
}
std::expected<SavedShares, SavedServiceError> readSavedShares(
    const Document& document, SavedServiceLimits limits) {
  try {
    validateDocument(document, limits);
    SavedShares result;
    Reader reader(limits, result.issues);
    const auto* list = records(document, "resources", limits, reader);
    result.listPresent = list != nullptr;
    if (list) {
      result.records.reserve(list->children.size());
      std::unordered_set<std::string_view> identities;
      for (std::size_t i = 0; i < list->children.size(); ++i) {
        reader.row(i);
        result.records.push_back(share(list->children[i], i, reader));
        auto& record = result.records.back();
        if (record.file && !identities.insert(record.file->bytes).second) {
          record.duplicateIdentity = true;
          reader.issue(SavedIssueCode::DuplicateIdentity, SavedField::File);
        }
      }
    }
    result.source = std::make_shared<const Document>(document);
    return result;
  } catch (const Failure& failure) { return std::unexpected(failure.error); }
  catch (const std::bad_alloc&) { return std::unexpected(SavedServiceError::Memory); }
}

} // namespace biglybtc::core
