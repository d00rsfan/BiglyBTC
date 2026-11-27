// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_backup_relocation.hpp"
#include "biglybtc/library.hpp"

#include <set>
#include <vector>

namespace biglybtc::storage::detail {
namespace {

struct Replacement {
  std::size_t begin;
  std::size_t end;
  std::string bytes;
};

bool windowsPath(std::string_view path) {
  return path.starts_with("\\\\") || path.starts_with("//") || (path.size() > 1 && path[1] == ':');
}

class ConfigurationPaths {
public:
  ConfigurationPaths(std::string_view previous, std::string_view next)
      : previous_(previous), next_(next), windows_(windowsPath(previous)), targetWindows_(windowsPath(next)) {
    while (previous_.size() > 1 && separator(previous_.back(), windows_)) { previous_.remove_suffix(1); }
  }

  void read(const core::Node& node) {
    switch (node.type) {
      case core::Node::Type::Dictionary: {
        std::set<std::string> keys;
        for (std::size_t index = 0; index < node.children.size(); index += 2) {
          const auto& key = node.children[index];
          auto relocated = path(key.bytes);
          const auto name = relocated ? std::string_view(*relocated) : key.bytes;
          if (!keys.emplace(name).second) {
            throw Error{ErrorCode::Conflict, "Backup configuration keys would overlap at the new profile location"};
          }
          if (relocated) { record(key, std::move(*relocated)); }
          read(node.children[index + 1]);
        }
        break;
      }
      case core::Node::Type::List:
        for (const auto& child : node.children) { read(child); }
        break;
      case core::Node::Type::Bytes:
        if (auto relocated = path(node.bytes)) { record(node, std::move(*relocated)); }
        break;
      case core::Node::Type::Integer: break;
    }
  }

  std::string write(const std::string& original, const core::Limits& bounds) const {
    if (replacements_.empty()) { return original; }
    auto size = original.size();
    for (const auto& change : replacements_) {
      size -= change.end - change.begin;
      if (change.bytes.size() > bounds.inputBytes - size) {
        throw Error{ErrorCode::Unsupported, "Relocated backup configuration exceeds its byte limit"};
      }
      size += change.bytes.size();
    }
    std::string output;
    output.reserve(size);
    std::size_t cursor = 0;
    for (const auto& change : replacements_) {
      output.append(original, cursor, change.begin - cursor);
      output += change.bytes;
      cursor = change.end;
    }
    output.append(original, cursor, original.size() - cursor);
    return output;
  }

private:
  static bool separator(char value, bool windows) noexcept { return value == '/' || (windows && value == '\\'); }

  std::optional<std::string> path(std::string_view value) const {
    if (!value.starts_with(previous_)) { return {}; }
    auto tail = value.substr(previous_.size());
    const bool previousEndsInSeparator = separator(previous_.back(), windows_);
    const bool targetEndsInSeparator = separator(next_.back(), targetWindows_);
    const char targetSeparator = targetWindows_ ? '\\' : '/';
    if (!tail.empty() && !separator(tail.front(), windows_) && !previousEndsInSeparator) { return {}; }
    auto mapped = std::string(next_);
    if (!tail.empty()) {
      if (separator(tail.front(), windows_) && targetEndsInSeparator) { tail.remove_prefix(1); }
      if (!tail.empty() && !separator(tail.front(), windows_) && !targetEndsInSeparator && previousEndsInSeparator) {
        mapped += targetSeparator;
      }
      if (windows_ == targetWindows_) { mapped += tail; }
      else { for (const char value : tail) { mapped += separator(value, windows_) ? targetSeparator : value; } }
    }
    return mapped == value ? std::nullopt : std::optional{std::move(mapped)};
  }

  void record(const core::Node& node, std::string value) {
    if (value.size() > compatibleConfigLimits().stringBytes) {
      throw Error{ErrorCode::Unsupported, "Relocated profile path exceeds the configuration limit"};
    }
    replacements_.push_back({node.begin, node.end, std::to_string(value.size()) + ':' + value});
  }

  std::string_view previous_, next_;
  bool windows_, targetWindows_;
  std::vector<Replacement> replacements_;
};
} // namespace

std::expected<std::string, Error> relocateBackupConfiguration(std::string original,
    std::string_view previousDirectory, std::string_view newDirectory) try {
  if (previousDirectory.empty() || newDirectory.empty() || previousDirectory.contains('\0') || newDirectory.contains('\0')) {
    return std::unexpected(Error{ErrorCode::UnsafePath, "Backup configuration requires two profile directories"});
  }
  constexpr auto bounds = compatibleConfigLimits();
  const auto parsed = core::Document::parse(std::move(original), bounds);
  if (!parsed || parsed->root().type != core::Node::Type::Dictionary) {
    return std::unexpected(Error{ErrorCode::InvalidFormat, "Backup configuration is unreadable"});
  }
  ConfigurationPaths paths(previousDirectory, newDirectory);
  paths.read(parsed->root());
  return paths.write(parsed->originalBytes(), bounds);
} catch (const Error& error) {
  return std::unexpected(error);
} catch (const std::exception&) {
  return std::unexpected(Error{ErrorCode::Io, "Cannot relocate the backup configuration"});
}

std::expected<std::string, Error> pauseRestoredProfileDownloads(std::string original, std::stop_token stop) try {
  const auto invalid=[] { return Error{ErrorCode::InvalidFormat, "Cannot pause downloads in the saved library"}; };
  auto document=core::Document::parse(std::move(original),compatibleConfigLimits());
  if (!document) { return std::unexpected(invalid()); }
  const auto library=core::readLibrary(*document);
  if (!library) { return std::unexpected(invalid()); }
  const auto& before=document->originalBytes();
  const auto raw=[&](const core::Node& node) { return before.substr(node.begin,node.end-node.begin); };
  for (const auto& issue : library->issues) {
    const bool legacyPauseKey=library->legacyMap && !issue.pauseEntry
        && document->root().children[2*issue.sourceIndex].bytes=="pause_data";
    if (!legacyPauseKey) { return std::unexpected(invalid()); }
  }

  const auto* savedPauses=document->find("pause_data");
  std::string pauses=savedPauses ? raw(*savedPauses) : "le";
  pauses.pop_back();
  std::set<std::string> paused;
  for (const auto& item : library->paused) { paused.emplace(item.hash.hex()); }
  const auto* downloads=document->find("downloads");
  std::vector<std::pair<std::size_t,std::string>> replacements;
  std::set<std::string> identities;
  for (const auto& item : library->downloads) {
    if (stop.stop_requested()) {
      return std::unexpected(Error{ErrorCode::Cancelled,"Backup restoration cancelled"});
    }
    if (item.hash && !identities.emplace(item.hash->hex()).second) { return std::unexpected(invalid()); }
    if (!item.persistent || item.startup==core::StartupState::Stopped) { continue; }
    if (!item.hash) { return std::unexpected(invalid()); }
    const auto& source=downloads ? downloads->children[item.sourceIndex]
        : document->root().children[2*item.sourceIndex+1];
    auto row=core::Document::parse(raw(source),compatibleConfigLimits());
    if (!row || !row->replaceValue("state","i70e") || !row->eraseValue("forceStart")
        || !row->eraseValue("startStopLocked")) { return std::unexpected(invalid()); }
    replacements.emplace_back(item.sourceIndex,row->serialize());
    if (paused.emplace(item.hash->hex()).second) {
      const auto hash=item.hash->bytes();
      const auto* savedForce=source.find("forceStart");
      const bool force=savedForce ? static_cast<std::uint32_t>(savedForce->integer)!=0 : item.forceStart;
      pauses+="d5:forcei";
      pauses+=force ? '1' : '0';
      pauses+="e4:hash"+std::to_string(hash.size())+':';
      pauses.append(reinterpret_cast<const char*>(hash.data()),hash.size());
      pauses+='e';
    }
  }
  if (replacements.empty()) { return before; }
  if (downloads) {
    std::string encoded="l";
    auto next=replacements.begin();
    for (std::size_t index=0;index<downloads->children.size();++index) {
      if (next!=replacements.end() && next->first==index) { encoded+=std::move(next++->second); }
      else { encoded+=raw(downloads->children[index]); }
    }
    encoded+='e';
    if (!document->replaceValue("downloads",std::move(encoded))) { return std::unexpected(invalid()); }
  } else {
    for (auto& [index,value] : replacements) {
      const auto key=std::string(document->root().children[2*index].bytes);
      if (!document->replaceValue(key,std::move(value))) { return std::unexpected(invalid()); }
    }
  }
  pauses+='e';
  if (!document->replaceValue("pause_data",std::move(pauses))) { return std::unexpected(invalid()); }
  auto bytes=document->serialize();
  if (!core::Document::parse(bytes,compatibleConfigLimits())) { return std::unexpected(invalid()); }
  return bytes;
} catch (const std::exception&) {
  return std::unexpected(Error{ErrorCode::Io,"Cannot preserve the paused download state in the restored backup"});
}

} // namespace biglybtc::storage::detail
