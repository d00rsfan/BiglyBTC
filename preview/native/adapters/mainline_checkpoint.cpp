#include "mainline_checkpoint.hpp"
#include "mldht_cache.hpp"
#include "plugin_settings.hpp"
#include "profile_file_snapshot.hpp"

#include <algorithm>
#include <array>
#include <new>
#include <set>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

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
using detail::StateDescriptor;
using detail::StateHandle;
using detail::StateStamp;
using Candidate = detail::ProfileFileSnapshot;
using Snapshot = std::array<Candidate, 3>;

std::expected<Candidate, Error> capture(StateHandle directory, const std::string& name, std::size_t maximum) {
  auto candidate = detail::captureProfileFile(directory, name, maximum);
  if (!candidate && candidate.error().code == ErrorCode::InvalidFormat) {
    return error(ErrorCode::Unsupported, "Mainline input exceeds its bounded format");
  }
  // Baselines retain identity and bytes. A Windows replacement needs the old
  // target to be closed before publication.
  if (candidate) { candidate->releaseWitness(); }
  return candidate;
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
  return std::ranges::equal(a, b,
      [](const auto& before, const auto& current) { return before.matches(current); });
}

std::expected<StateStamp, Error> profile(const ProfileLease& lease) {
  if (!lease.valid()) {
    return error(ErrorCode::UnsafePath, "Mainline access requires an owned held profile lease");
  }
  const auto held = detail::stateStamp(lease.directoryHandle());
  if (!held || !held->directory || !held->owned) {
    return error(ErrorCode::UnsafePath, "Mainline access requires an owned held profile lease");
  }
  return *held;
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
bool currentDirectory(StateHandle parent, const std::string& name, StateHandle descriptor) {
  const auto held = detail::stateStamp(descriptor);
  const auto named = detail::stateStampAt(parent, name);
  return held && held->owned && named && *named && detail::sameStateDirectory(*held, **named);
}

std::expected<StateDescriptor, Error> openDirectory(StateHandle parent, const std::string& name) {
  auto directory = detail::openStateDirectory(parent, name);
  if (!directory && directory.error().code == ErrorCode::Missing) { return StateDescriptor{}; }
  if (!directory) { return directory; }
  if (!currentDirectory(parent, name, directory->get())) {
    return error(ErrorCode::UnsafePath, "Original Mainline directory ownership changed");
  }
  return directory;
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
  StateStamp profile{};
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
    if (!detail::sameStateIdentity(*identity, impl_->profile)) {
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
    if (plugins->get() == detail::InvalidStateHandle) { result.ipv4Missing = result.ipv6Missing = true; return result; }
    auto plugin = openDirectory(plugins->get(), directory); if (!plugin) { return std::unexpected(plugin.error()); }
    if (plugin->get() == detail::InvalidStateHandle) { result.ipv4Missing = result.ipv6Missing = true; return result; }
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
