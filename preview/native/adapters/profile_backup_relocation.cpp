// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_backup_relocation.hpp"

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

} // namespace biglybtc::storage::detail
