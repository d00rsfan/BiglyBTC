#include "biglybtc/library.hpp"

#include <limits>

namespace biglybtc::core {
namespace {

std::expected<std::optional<std::int64_t>, LibraryError> integer(const Node& entry, std::string_view key) {
  const auto* value = entry.find(key);
  if (!value) { return std::nullopt; }
  if (value->type != Node::Type::Integer) { return std::unexpected(LibraryError::Integer); }
  return value->integer;
}

std::expected<std::optional<std::string>, LibraryError> path(const Node& entry, std::string_view key) {
  const auto* value = entry.find(key);
  if (!value) { return std::nullopt; }
  if (value->type != Node::Type::Bytes || value->bytes.find('\0') != std::string_view::npos) {
    return std::unexpected(LibraryError::Path);
  }
  return std::string(value->bytes);
}

std::expected<SavedDownload, LibraryError> download(const Node& entry) {
  if (entry.type != Node::Type::Dictionary) { return std::unexpected(LibraryError::EntryType); }
  SavedDownload result;
  const auto torrent = path(entry, "torrent");
  if (!torrent || !torrent->has_value() || (**torrent).empty()) { return std::unexpected(LibraryError::Path); }
  result.torrentFile = **torrent;
  const auto directory = path(entry, "save_dir");
  if (!directory) { return std::unexpected(directory.error()); }
  if (directory->has_value()) {
    result.saveDirectory = **directory;
    auto file = path(entry, "save_file");
    if (!file) { return std::unexpected(file.error()); }
    result.saveFile = std::move(*file);
  } else {
    const auto legacy = path(entry, "path");
    if (!legacy || !legacy->has_value()) { return std::unexpected(LibraryError::Path); }
    result.saveDirectory = **legacy;
    result.legacySavePath = true;
  }
  if (const auto* hash = entry.find("torrent_hash")) {
    if (hash->type != Node::Type::Bytes) { return std::unexpected(LibraryError::Hash); }
    auto decoded = TorrentHash::parse(hash->bytes);
    if (!decoded) { return std::unexpected(decoded.error()); }
    result.hash = std::move(*decoded);
  }
  const auto state = integer(entry, "state");
  if (!state) { return std::unexpected(state.error()); }
  if (*state) {
    result.originalState = **state;
    if (result.originalState < std::numeric_limits<int>::min() || result.originalState > std::numeric_limits<int>::max()) {
      return std::unexpected(LibraryError::Integer);
    }
    result.startup = result.originalState == 0 ? StartupState::Waiting
        : result.originalState == 70 ? StartupState::Stopped : StartupState::Queued;
  } else {
    const auto stopped = integer(entry, "stopped");
    if (!stopped || !stopped->has_value()) { return std::unexpected(LibraryError::Integer); }
    result.startup = *stopped && **stopped == 1 ? StartupState::Stopped : StartupState::Waiting;
    result.originalState = static_cast<int>(result.startup);
  }
  const auto persistent = integer(entry, "persistent");
  auto forced = integer(entry, "forceStart");
  if (!persistent || !forced) { return std::unexpected(LibraryError::Integer); }
  result.persistent = !*persistent || **persistent == 1;
  if (!*forced) { forced = integer(entry, "startStopLocked"); }
  if (!forced) { return std::unexpected(forced.error()); }
  result.forceStart = *forced && **forced == 1;
  for (auto [key, target] : {std::pair{"position", &result.position}, {"downloaded", &result.downloaded},
      {"uploaded", &result.uploaded}, {"completedbytes", &result.completedBytes}}) {
    auto value = integer(entry, key);
    if (!value) { return std::unexpected(value.error()); }
    *target = *value;
  }
  result.hasFilePriorities = entry.find("file_priorities_c") || entry.find("file_priorities");
  return result;
}

} // namespace

std::expected<TorrentHash, LibraryError> TorrentHash::parse(std::string_view value) {
  if (value.size() != 20 && value.size() != 32) { return std::unexpected(LibraryError::Hash); }
  TorrentHash result;
  result.length_ = value.size();
  for (std::size_t i = 0; i < value.size(); ++i) { result.value_[i] = static_cast<std::byte>(value[i]); }
  return result;
}

std::string TorrentHash::hex() const {
  constexpr char alphabet[] = "0123456789ABCDEF";
  std::string result;
  result.reserve(length_ * 2);
  for (auto byte : bytes()) {
    const auto value = std::to_integer<unsigned>(byte);
    result += alphabet[value >> 4];
    result += alphabet[value & 15];
  }
  return result;
}

std::expected<LibrarySnapshot, LibraryError> readLibrary(const Document& document, LibraryLimits limits) {
  if (document.root().type != Node::Type::Dictionary) { return std::unexpected(LibraryError::Root); }
  const auto* entries = document.find("downloads");
  if (entries && entries->type != Node::Type::List) { return std::unexpected(LibraryError::DownloadsType); }
  LibrarySnapshot result;
  result.legacyMap = !entries;
  const auto size = entries ? entries->children.size() : document.root().children.size() / 2;
  if (size > limits.downloads) { return std::unexpected(LibraryError::Limit); }
  result.downloads.reserve(size);
  for (std::size_t i = 0; i < size; ++i) {
    const auto& node = entries ? entries->children[i] : document.root().children[i * 2 + 1];
    auto value = download(node);
    if (!value) {
      result.issues.push_back({value.error(), i, false});
      continue;
    }
    value->sourceIndex = i;
    if (!entries) { value->legacyKey = document.root().children[i * 2].bytes; }
    result.downloads.push_back(std::move(*value));
  }
  const auto* paused = document.find("pause_data");
  if (!paused) { return result; }
  if (paused->type != Node::Type::List) {
    result.issues.push_back({LibraryError::PauseType, 0, true});
    return result;
  }
  if (paused->children.size() > limits.paused) { return std::unexpected(LibraryError::Limit); }
  for (std::size_t i = 0; i < paused->children.size(); ++i) {
    const auto& node = paused->children[i];
    const auto* hash = node.type == Node::Type::Bytes ? &node : node.find("hash");
    if (!hash || hash->type != Node::Type::Bytes) {
      result.issues.push_back({LibraryError::Hash, i, true});
      continue;
    }
    auto decoded = TorrentHash::parse(hash->bytes);
    auto force = integer(node, "force");
    auto owner = integer(node, "owner");
    if (node.type == Node::Type::Dictionary && force && !force->has_value()) {
      result.issues.push_back({LibraryError::PauseType, i, true});
      continue;
    }
    if (!decoded || !force || !owner) {
      result.issues.push_back({!decoded ? decoded.error() : LibraryError::Integer, i, true});
      continue;
    }
    result.paused.push_back({std::move(*decoded), *force && **force == 1, *owner ? **owner : 1});
  }
  return result;
}

} // namespace biglybtc::core
