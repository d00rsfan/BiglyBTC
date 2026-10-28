#include "mainline_checkpoint.hpp"
#include "file_io.hpp"
#include "mldht_cache.hpp"
#include "plugin_settings.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <new>
#include <set>
#include <utility>

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace biglybtc::storage {
namespace {
constexpr std::array<const char*, 3> Names{
    "BiglyBTC-mainline.config", "BiglyBTC-mainline.config.bak", "BiglyBTC-mainline.config.saving"};
constexpr std::size_t MaximumLegacyBytes = 50 * 1024 * 1024; // Existing original decoder bound.
constexpr std::size_t MaximumHints = 64;
constexpr core::Limits Limits{.inputBytes = MainlineCheckpoint::MaximumWrapperBytes,
    .stringBytes = MainlineCheckpoint::MaximumWrapperBytes, .nodes = 100000, .depth = 64};
std::unexpected<Error> error(ErrorCode code, const char* message) {
  return std::unexpected(Error{code, message});
}
bool same(const struct stat& a, const struct stat& b) {
  return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_mode == b.st_mode
      && a.st_uid == b.st_uid && a.st_gid == b.st_gid && a.st_nlink == b.st_nlink && a.st_size == b.st_size
      && a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec
      && a.st_ctim.tv_sec == b.st_ctim.tv_sec && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
}
struct Candidate { std::optional<std::string> bytes; std::optional<struct stat> stamp; };
using Snapshot = std::array<Candidate, 3>;
std::expected<Candidate, Error> capture(int directory, const std::string& name, std::size_t maximum) {
  Candidate result;
  struct stat before{};
  if (::fstatat(directory, name.c_str(), &before, AT_SYMLINK_NOFOLLOW) == 0) {
    if (!S_ISREG(before.st_mode) || before.st_uid != ::geteuid() || before.st_nlink != 1) {
      return error(ErrorCode::UnsafePath, "Mainline input must be an owned unshared regular file");
    }
    if (before.st_size < 0 || static_cast<std::uint64_t>(before.st_size) > maximum) {
      return error(ErrorCode::Unsupported, "Mainline input exceeds its bounded format");
    }
    result.stamp = before;
  } else if (errno != ENOENT) { return error(ErrorCode::Io, "Unable to identify Mainline input safely"); }
  auto read = detail::readFile(directory, name, maximum);
  if (!read) { return std::unexpected(read.error()); }
  struct stat after{};
  const bool present = ::fstatat(directory, name.c_str(), &after, AT_SYMLINK_NOFOLLOW) == 0;
  if (!present && errno != ENOENT) { return error(ErrorCode::Io, "Unable to recheck Mainline input"); }
  if (present != result.stamp.has_value() || present != read->has_value()
      || (present && !same(*result.stamp, after))) {
    return error(ErrorCode::Conflict, "Mainline input changed while being read");
  }
  result.bytes = std::move(*read);
  return result;
}
std::expected<Snapshot, Error> capture(const ProfileLease& lease) {
  Snapshot result;
  for (std::size_t i = 0; i < Names.size(); ++i) {
    auto value = capture(lease.directoryHandle(), Names[i], MainlineCheckpoint::MaximumWrapperBytes);
    if (!value) { return std::unexpected(value.error()); }
    result[i] = std::move(*value);
  }
  return result;
}
bool same(const Snapshot& a, const Snapshot& b) {
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i].bytes != b[i].bytes || a[i].stamp.has_value() != b[i].stamp.has_value()
        || (a[i].stamp && !same(*a[i].stamp, *b[i].stamp))) { return false; }
  }
  return true;
}
std::expected<struct stat, Error> profile(const ProfileLease& lease) {
  struct stat value{};
  if (!lease.valid() || ::fstat(lease.directoryHandle(), &value) != 0
      || !S_ISDIR(value.st_mode) || value.st_uid != ::geteuid()) {
    return error(ErrorCode::UnsafePath, "Mainline access requires an owned held profile lease");
  }
  return value;
}
std::expected<std::string, Error> wrapper(const core::Document& document, const std::string& directory) {
  const auto& root = document.root();
  if (root.type != core::Node::Type::Dictionary) { return error(ErrorCode::Unsupported, "Unsupported native Mainline checkpoint"); }
  std::set<std::string_view> keys;
  for (std::size_t i = 0; i < root.children.size(); i += 2) {
    if (!keys.insert(root.children[i].bytes).second) { return error(ErrorCode::Unsupported, "Duplicate native Mainline checkpoint key"); }
  }
  const auto* version = document.find("version");
  const auto* binding = document.find("pluginDirectory");
  const auto* state = document.find("state");
  const auto* timestamp = document.find("timestamp");
  if (!version || version->type != core::Node::Type::Integer || version->integer != 1
      || !binding || binding->type != core::Node::Type::Bytes || binding->bytes != directory
      || !state || state->type != core::Node::Type::Bytes || state->bytes.size() > MainlineCheckpoint::MaximumStateBytes
      || (timestamp && timestamp->type != core::Node::Type::Integer)) {
    return error(ErrorCode::Unsupported, "Unsupported native Mainline checkpoint version, binding or state");
  }
  return std::string(state->bytes);
}
std::string bytes(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
std::expected<detail::Descriptor, Error> openDirectory(int parent, const std::string& name) {
  detail::Descriptor directory(::openat(parent, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  if (directory.get() < 0) {
    if (errno == ENOENT) { return detail::Descriptor{-1}; }
    return error(errno == ELOOP || errno == ENOTDIR ? ErrorCode::UnsafePath : ErrorCode::Io,
        "Unable to open original Mainline directory safely");
  }
  struct stat held{}, named{};
  if (::fstat(directory.get(), &held) != 0 || ::fstatat(parent, name.c_str(), &named, AT_SYMLINK_NOFOLLOW) != 0
      || !S_ISDIR(held.st_mode) || !S_ISDIR(named.st_mode) || held.st_uid != ::geteuid()
      || held.st_dev != named.st_dev || held.st_ino != named.st_ino) {
    return error(ErrorCode::UnsafePath, "Original Mainline directory ownership changed");
  }
  return directory;
}
bool currentDirectory(int parent, const std::string& name, int descriptor) {
  struct stat held{}, named{};
  return ::fstat(descriptor, &held) == 0 && ::fstatat(parent, name.c_str(), &named, AT_SYMLINK_NOFOLLOW) == 0
      && S_ISDIR(held.st_mode) && S_ISDIR(named.st_mode) && held.st_uid == ::geteuid()
      && held.st_dev == named.st_dev && held.st_ino == named.st_ino;
}
bool preferred(const MainlineLegacyHint& a, const MainlineLegacyHint& b) {
  if (a.replacement != b.replacement) { return !a.replacement; }
  if (a.verified != b.verified) { return a.verified; }
  return a.lastSeen > b.lastSeen;
}
std::optional<std::string> numeric(const MldhtRoutingEntry& entry) {
  if (entry.port == 0 || (entry.family != 4 && entry.family != 6)) { return std::nullopt; }
  const auto length = entry.family == 4 ? 4u : 16u;
  if (std::all_of(entry.address.begin(), entry.address.begin() + length, [](auto c) { return c == 0; })
      || (entry.family == 4 && entry.address[0] >= 224 && entry.address[0] <= 239)
      || (entry.family == 6 && entry.address[0] == 255)
      || (entry.family == 4 && std::all_of(entry.address.begin(), entry.address.begin() + 4,
          [](auto c) { return c == 255; }))) { return std::nullopt; }
  char output[INET6_ADDRSTRLEN]{};
  if (!::inet_ntop(entry.family == 4 ? AF_INET : AF_INET6, entry.address.data(), output, sizeof(output))) { return std::nullopt; }
  return std::string(output);
}
} // namespace

struct MainlineCheckpoint::Impl {
  std::string directory;
  std::optional<ConfigFile> file;
  std::optional<std::string> state;
  Snapshot baseline;
  struct stat profile{};
  bool fenced = false;
};
MainlineCheckpoint::MainlineCheckpoint(std::unique_ptr<Impl> value) : impl_(std::move(value)) {}
MainlineCheckpoint::MainlineCheckpoint(MainlineCheckpoint&&) noexcept = default;
MainlineCheckpoint& MainlineCheckpoint::operator=(MainlineCheckpoint&&) noexcept = default;
MainlineCheckpoint::~MainlineCheckpoint() = default;
const std::optional<std::string>& MainlineCheckpoint::state() const noexcept { return impl_->state; }

std::expected<MainlineCheckpoint, Error> MainlineCheckpoint::load(const ProfileLease& lease, std::string directory) {
  try {
    auto binding = originalPluginBinding("mldht", directory);
    if (!binding) { return error(ErrorCode::UnsafePath, "Exact original Mainline plugin basename required"); }
    auto identity = profile(lease); if (!identity) { return std::unexpected(identity.error()); }
    auto baseline = capture(lease); if (!baseline) { return std::unexpected(baseline.error()); }
    bool missing = true;
    for (const auto& candidate : *baseline) {
      if (!candidate.bytes) { continue; }
      missing = false;
      auto document = core::Document::parse(*candidate.bytes, Limits);
      if (!document) { return error(ErrorCode::Unsupported, "Malformed native Mainline checkpoint preserved"); }
      auto accepted = wrapper(*document, directory);
      if (!accepted) { return std::unexpected(accepted.error()); }
    }
    auto impl = std::make_unique<Impl>();
    impl->directory = std::move(directory); impl->baseline = std::move(*baseline); impl->profile = *identity;
    if (!missing) {
      auto file = ConfigFile::load(lease, Names[0], Limits);
      if (!file) { return std::unexpected(file.error()); }
      const auto source = file->recoverySource();
      const auto index = source == RecoverySource::Primary ? 0u : source == RecoverySource::Backup ? 1u : 2u;
      if (!impl->baseline[index].bytes || file->document().originalBytes() != *impl->baseline[index].bytes) {
        return error(ErrorCode::Conflict, "Native Mainline checkpoint recovery source changed");
      }
      auto state = wrapper(file->document(), impl->directory); if (!state) { return std::unexpected(state.error()); }
      impl->state = std::move(*state); impl->file = std::move(*file);
    }
    auto current = capture(lease); if (!current) { return std::unexpected(current.error()); }
    if (!same(impl->baseline, *current)) { return error(ErrorCode::Conflict, "Native Mainline checkpoint changed while loading"); }
    return MainlineCheckpoint(std::move(impl));
  } catch (...) { return error(ErrorCode::Io, "Unable to load native Mainline checkpoint"); }
}
std::expected<void, Error> MainlineCheckpoint::save(const ProfileLease& lease, std::string_view state) {
  try {
    if (impl_->fenced) { return error(ErrorCode::Conflict, "Native Mainline checkpoint needs explicit reload after failed publication"); }
    auto identity = profile(lease); if (!identity) { return std::unexpected(identity.error()); }
    if (identity->st_dev != impl_->profile.st_dev || identity->st_ino != impl_->profile.st_ino) {
      return error(ErrorCode::UnsafePath, "Native Mainline checkpoint belongs to another profile");
    }
    if (state.size() > MaximumStateBytes) { return error(ErrorCode::Unsupported, "Native Mainline state exceeds1MiB"); }
    auto current = capture(lease); if (!current) { return std::unexpected(current.error()); }
    if (!same(impl_->baseline, *current)) {
      impl_->fenced = true;
      return error(ErrorCode::Conflict, "Native Mainline checkpoint changed since loading");
    }
    if (impl_->state && *impl_->state == state) { return {}; }
    std::optional<std::string> nextState{std::string(state)};
    std::optional<ConfigFile> prepared = impl_->file;
    std::string encoded;
    const bool primary = prepared && prepared->recoverySource() == RecoverySource::Primary;
    if (prepared) {
      if (!prepared->document().replaceValue("state", bytes(state))) {
        return error(ErrorCode::Unsupported, "Native Mainline checkpoint edit exceeds its bounds");
      }
      encoded = prepared->document().serialize();
    } else {
      encoded = "d15:pluginDirectory" + bytes(impl_->directory) + "5:state" + bytes(state) + "7:versioni1ee";
    }
    if (encoded.size() > MaximumWrapperBytes) { return error(ErrorCode::Unsupported, "Native Mainline wrapper exceeds4MiB"); }
    impl_->fenced = true; // ConfigFile may publish before reporting a sync failure.
    if (prepared) {
      auto saved = prepared->save(lease); if (!saved) { return std::unexpected(saved.error()); }
    } else {
      auto saved = ConfigFile::createMissing(lease, Names[0], encoded, Limits);
      if (!saved) { return std::unexpected(saved.error()); }
      prepared = std::move(*saved);
    }
    auto after = capture(lease); if (!after) { return std::unexpected(after.error()); }
    const auto& expectedBackup = primary ? impl_->baseline[0].bytes : impl_->baseline[1].bytes;
    if ((*after)[0].bytes != std::optional<std::string>{encoded} || (*after)[1].bytes != expectedBackup || (*after)[2].bytes) {
      return error(ErrorCode::Conflict, "Native Mainline checkpoint publication could not be reconciled");
    }
    impl_->file = std::move(prepared); impl_->state = std::move(nextState); impl_->baseline = std::move(*after);
    impl_->fenced = false;
    return {};
  } catch (...) { return error(ErrorCode::Io, "Native Mainline checkpoint save failed; reload before retry"); }
}

std::expected<MainlineLegacyHints, Error> readLegacyMldhtHints(
    const ProfileLease& lease, std::string directory, std::int64_t nowMillis) {
  try {
    if (!originalPluginBinding("mldht", directory)) { return error(ErrorCode::UnsafePath, "Exact original Mainline plugin basename required"); }
    auto identity = profile(lease); if (!identity) { return std::unexpected(identity.error()); }
    MainlineLegacyHints result;
    auto plugins = openDirectory(lease.directoryHandle(), "plugins"); if (!plugins) { return std::unexpected(plugins.error()); }
    if (plugins->get() < 0) { result.ipv4Missing = result.ipv6Missing = true; return result; }
    auto plugin = openDirectory(plugins->get(), directory); if (!plugin) { return std::unexpected(plugin.error()); }
    if (plugin->get() < 0) { result.ipv4Missing = result.ipv6Missing = true; return result; }
    for (const auto family : {4, 6}) {
      const auto name = family == 4 ? "IPv4-table.cache" : "IPv6-table.cache";
      auto file = capture(plugin->get(), name, MaximumLegacyBytes); if (!file) { return std::unexpected(file.error()); }
      if (!file->bytes) {
        (family == 4 ? result.ipv4Missing : result.ipv6Missing) = true;
        continue;
      }
      auto cache = OriginalMldhtCache::parse(std::move(*file->bytes), nowMillis);
      if (!cache) { return error(ErrorCode::InvalidFormat, "Corrupt original Mainline routing cache preserved"); }
      result.decoded += cache->entries().size();
      for (const auto& entry : cache->entries()) {
        auto address = numeric(entry); if (!address) { continue; }
        MainlineLegacyHint hint{std::move(*address), entry.port, entry.verified, entry.replacement, entry.lastSeen};
        auto duplicate = std::ranges::find_if(result.endpoints, [&](const auto& old) { return old.address == hint.address && old.port == hint.port; });
        if (duplicate != result.endpoints.end()) {
          if (!preferred(hint, *duplicate)) { continue; }
          result.endpoints.erase(duplicate);
        }
        const auto place = std::lower_bound(result.endpoints.begin(), result.endpoints.end(), hint, preferred);
        if (place == result.endpoints.end() && result.endpoints.size() == MaximumHints) { continue; }
        result.endpoints.insert(place, std::move(hint));
        if (result.endpoints.size() > MaximumHints) { result.endpoints.pop_back(); }
      }
    }
    if (!currentDirectory(lease.directoryHandle(), "plugins", plugins->get())
        || !currentDirectory(plugins->get(), directory, plugin->get())) {
      return error(ErrorCode::Conflict, "Original Mainline directory chain changed during read");
    }
    result.rejected = result.decoded - result.endpoints.size();
    return result;
  } catch (...) { return error(ErrorCode::Io, "Unable to read original Mainline routing hints"); }
}

} // namespace biglybtc::storage
