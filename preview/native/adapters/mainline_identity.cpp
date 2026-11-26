#include "mainline_identity.hpp"
#include "file_operation_io.hpp"
#include "profile_file_snapshot.hpp"
#include "state_directory_listing.hpp"
#include "../platform/system_entropy.hpp"
#include "../platform/utf8_path.hpp"
#include "plugin_settings.hpp"
#include "identity.hpp" // Existing source-pinned mlDHT plugin target; private dependency.

#include <new>
#include <utility>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace biglybtc::storage {
namespace {
using detail::StateDescriptor;
using detail::StateHandle;
using detail::StateStamp;
constexpr auto Leaf = "baseID.config";
constexpr auto Pending = "baseID.config.BiglyBTC-pending";
constexpr std::size_t IdentityBytes = 41;
std::unexpected<Error> error(ErrorCode code, const char* text) {
  return std::unexpected(Error{code, text});
}
bool sameDirectory(const StateStamp& a, const StateStamp& b) {
  return b.owned && detail::sameStateDirectory(a, b);
}

bool safeFile(const StateStamp& stamp) { return stamp.regular && stamp.owned && stamp.links == 1; }

std::expected<StateStamp, Error> profile(const ProfileLease& lease) {
  if (!lease.valid()) { return error(ErrorCode::UnsafePath, "Original Mainline identity requires an owned held profile"); }
  const auto value = detail::stateStamp(lease.directoryHandle());
  if (!value || !value->directory || !value->owned) {
    return error(ErrorCode::UnsafePath, "Original Mainline identity requires an owned held profile");
  }
  return *value;
}

std::expected<bool, Error> absent(StateHandle parent, const char* name) {
  const auto value = detail::stateStampAt(parent, name);
  if (!value) { return std::unexpected(value.error()); }
  return !*value;
}

bool currentDirectory(StateHandle parent, const std::filesystem::path& name, StateHandle fd,
    const StateStamp& approved) {
  const auto held = detail::stateStamp(fd);
  const auto named = detail::stateStampAt(parent, name);
  return held && named && *named && sameDirectory(approved, *held) && sameDirectory(*held, **named);
}

std::expected<StateDescriptor, Error> directory(StateHandle parent, const std::filesystem::path& name,
    StateStamp& stamp) {
  auto fd = detail::openDirectoryAt(parent, name);
  if (!fd && fd.error().code == ErrorCode::Missing) { return StateDescriptor{}; }
  if (!fd) { return fd; }
  const auto opened = detail::stateStamp(fd->get());
  if (!opened || !currentDirectory(parent, name, fd->get(), *opened)) {
    return error(ErrorCode::UnsafePath, "Original Mainline identity directory is not owned and bound");
  }
  stamp = *opened;
  return fd;
}

bool currentFile(StateHandle parent, const char* name, StateHandle fd, const StateStamp& approved) {
  const auto held = detail::stateStamp(fd);
  const auto named = detail::stateStampAt(parent, name);
  return held && named && *named && safeFile(*held) && detail::sameStateSnapshot(approved, *held)
      && detail::sameStateSnapshot(*held, **named);
}

std::expected<std::string, Error> contents(StateHandle fd, const StateStamp& stamp) {
  std::string result(static_cast<std::size_t>(stamp.size), '\0');
  for (std::size_t offset = 0; offset < result.size();) {
    const auto count = detail::readStateAt(fd, result.data() + offset, result.size() - offset, offset);
    if (count <= 0) { return error(ErrorCode::Io, "Cannot read complete original Mainline identity"); }
    offset += static_cast<std::size_t>(count);
  }
  return result;
}

bool privatePendingFile(const detail::ProfileFileSnapshot& pending) {
  if (!pending.stamp || !pending.bytes || pending.bytes->size() != IdentityBytes) { return false; }
#ifdef _WIN32
  try { return fileop::privateEntry(pending.descriptor.get()); }
  catch (const fileop::Failure&) { return false; }
#else
  return (pending.stamp->mode & 07777) == 0600;
#endif
}

std::expected<void, Error> syncPending(StateHandle parent, const detail::ProfileFileSnapshot& pending) {
#ifdef _WIN32
  auto writer = detail::openWritableStateFile(parent, Pending, false);
  if (!writer) { return std::unexpected(writer.error()); }
  if (!currentFile(parent, Pending, writer->get(), *pending.stamp)) {
    return error(ErrorCode::Conflict, "Original Mainline pending source changed before synchronization");
  }
  const auto sync = detail::syncStateFile(writer->get());
#else
  const auto sync = detail::syncStateFile(pending.descriptor.get());
#endif
  if (!sync) { return sync; }
  return detail::syncStateDirectory(parent);
}
} // namespace

std::expected<std::string,Error> discoverOriginalMainlineDirectory(const ProfileLease& lease) {
  try {
    auto root=profile(lease);if(!root){return std::unexpected(root.error());}
    auto access=lease.sourceAccess();if(!access){return std::unexpected(access.error());}
    StateStamp pluginsStamp{};
    auto plugins = directory(lease.directoryHandle(), "plugins", pluginsStamp);
    if (!plugins) { return std::unexpected(plugins.error()); }
    if (plugins->get() == detail::InvalidStateHandle) {
      return error(ErrorCode::Missing, "No installed Mainline plugin directory");
    }
    const auto entries = detail::listStateDirectoryNames(plugins->get(), 1026);
    if (!entries) { return std::unexpected(entries.error()); }
    std::optional<std::string> selected;
    std::size_t count = 0;
    for (const auto& leaf : *entries) {
      if (leaf == "." || leaf == "..") { continue; }
      if (++count > 1024) { return error(ErrorCode::Unsupported, "Installed plugin directory limit"); }
      const auto name = platform::pathToUtf8(leaf);
      auto folded = name;
      for (auto& ch : folded) { if (ch >= 'A' && ch <= 'Z') { ch = static_cast<char>(ch - 'A' + 'a'); } }
      const auto label = detail::stateStampAt(plugins->get(), leaf);
      if (!label || !*label) { return error(ErrorCode::Conflict, "Installed plugin directory changed"); }
      if (!(*label)->directory) {
        if (folded == "mldht") {
          return error(ErrorCode::UnsafePath, "Mainline plugin directory is not a direct owned directory");
        }
        continue;
      }
      StateStamp childStamp{};
      auto child = directory(plugins->get(), leaf, childStamp);
      if (!child) { return std::unexpected(child.error()); }
      if (child->get() == detail::InvalidStateHandle || !sameDirectory(**label, childStamp)) {
        return error(ErrorCode::Conflict, "Installed plugin directory changed while opening");
      }
      const auto props = detail::captureProfileFile(child->get(), "plugin.properties", 65536);
      if (!props) { return std::unexpected(props.error()); }
      if (!props->bytes) {
        if (folded == "mldht") { return error(ErrorCode::Missing, "Mainline plugin identity metadata is missing"); }
        continue;
      }
      const auto* data = &*props->bytes;
      std::optional<std::string_view> id;std::size_t at=0,lines=0;
      const auto trim=[](std::string_view text){while(!text.empty()&&(text.front()==' '||text.front()=='\t'||text.front()=='\f'||text.front()=='\r')){text.remove_prefix(1);}while(!text.empty()&&(text.back()==' '||text.back()=='\t'||text.back()=='\f'||text.back()=='\r')){text.remove_suffix(1);}return text;};
      while(at<data->size()) {
        if(++lines>4096){return error(ErrorCode::Unsupported,"Installed plugin identity line limit");}
        const auto end=data->find('\n',at);auto line=trim(std::string_view(*data).substr(at,end==data->npos?end:end-at));at=end==data->npos?data->size():end+1;
        if(line.empty()||line.front()=='#'||line.front()=='!'){continue;}
        const auto split=line.find_first_of("=: \t\f");if(split==line.npos||line.substr(0,split)!="plugin.id"){continue;}
        if(id){return error(ErrorCode::Conflict,"Installed plugin has duplicate runtime identity");}
        auto value=trim(line.substr(split));if(!value.empty()&&(value.front()=='='||value.front()==':')){value=trim(value.substr(1));}
        if(value.find('\\')!=value.npos||value.find('\0')!=value.npos){return error(ErrorCode::Unsupported,"Escaped installed plugin runtime identity requires review");}id=value;
      }
      if (!currentFile(child->get(), "plugin.properties", props->descriptor.get(), *props->stamp)
          || !currentDirectory(plugins->get(), leaf, child->get(), childStamp)) {
        return error(ErrorCode::Conflict, "Installed plugin identity changed while reading");
      }
      if (id && *id == "mldht") {
        if (selected) { return error(ErrorCode::Conflict, "Multiple installed Mainline plugin directories"); }
        selected = name;
      } else if (folded == "mldht") {
        return error(ErrorCode::Conflict, "Mainline directory runtime identity does not match");
      }
    }
    const auto after = detail::stateStamp(plugins->get());
    if (!after || !currentDirectory(lease.directoryHandle(), "plugins", plugins->get(), pluginsStamp)
        || !detail::sameStateTimestamp(after->modified, pluginsStamp.modified)
        || !detail::sameStateTimestamp(after->changed, pluginsStamp.changed)) {
      return error(ErrorCode::Conflict, "Installed plugin set changed while discovering Mainline");
    }
    if(!selected){return error(ErrorCode::Missing,"No installed Mainline runtime identity");}return *selected;
  } catch (const std::filesystem::filesystem_error&) {
    return error(ErrorCode::UnsafePath, "Cannot decode installed plugin directory name");
  } catch (const std::bad_alloc&) {
    return error(ErrorCode::Io, "Mainline plugin discovery memory limit");
  }
}

struct OriginalMainlineIdentity::Impl {
  OriginalMainlineIdentitySnapshot source;
  StateStamp profile{}, pluginsStamp{}, directoryStamp{}, fileStamp{};
  StateDescriptor plugins, directory, file;
};
OriginalMainlineIdentity::OriginalMainlineIdentity(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
OriginalMainlineIdentity::OriginalMainlineIdentity(OriginalMainlineIdentity&&) noexcept = default;
OriginalMainlineIdentity& OriginalMainlineIdentity::operator=(OriginalMainlineIdentity&&) noexcept = default;
OriginalMainlineIdentity::~OriginalMainlineIdentity() = default;
const OriginalMainlineIdentitySnapshot& OriginalMainlineIdentity::snapshot() const noexcept {
  static const OriginalMainlineIdentitySnapshot empty{};
  return impl_ ? impl_->source : empty;
}
std::expected<OriginalMainlineIdentity, Error> OriginalMainlineIdentity::load(
    const ProfileLease& lease, std::string pluginDirectory, bool restoreIdentity) {
  try {
    auto binding = originalPluginBinding("mldht", std::move(pluginDirectory));
    if (!binding) { return error(ErrorCode::UnsafePath, "Exact original Mainline plugin basename required"); }
    auto held = profile(lease); if (!held) { return std::unexpected(held.error()); }
    auto impl = std::make_unique<Impl>(); impl->profile = *held;
    impl->source.pluginDirectory = std::move(binding->directoryName);
    if (!restoreIdentity) {
      impl->source.state = OriginalMainlineIdentityState::Disabled;
      return OriginalMainlineIdentity(std::move(impl));
    }
    auto plugins = directory(lease.directoryHandle(), "plugins", impl->pluginsStamp);
    if (!plugins) { return std::unexpected(plugins.error()); }
    impl->plugins = std::move(*plugins);
    if (impl->plugins.get() != detail::InvalidStateHandle) {
      auto plugin = directory(impl->plugins.get(), platform::pathFromUtf8(impl->source.pluginDirectory), impl->directoryStamp);
      if (!plugin) { return std::unexpected(plugin.error()); }
      impl->directory = std::move(*plugin);
    }
    if (impl->directory.get() != detail::InvalidStateHandle) {
      auto file = detail::openStateFile(impl->directory.get(), Leaf);
      if (!file && file.error().code != ErrorCode::Missing) { return std::unexpected(file.error()); }
      if (file) {
        const auto stamp = detail::stateStamp(file->get());
        if (!stamp || !safeFile(*stamp)) {
          return error(ErrorCode::UnsafePath, "Original Mainline identity must be an owned unshared regular file");
        }
        if (stamp->size < 0 || static_cast<std::uint64_t>(stamp->size) > MaximumBytes) {
          return error(ErrorCode::Unsupported, "Original Mainline identity exceeds its bounded format");
        }
        auto bytes = contents(file->get(), *stamp);
        if (!bytes) { return std::unexpected(bytes.error()); }
        auto parsed = biglybtc::plugins::mldht::readOriginalIdentity(std::move(*bytes));
        if (!parsed) { return error(ErrorCode::InvalidFormat, "Original Mainline identity cannot be imported unchanged"); }
        impl->file = std::move(*file);
        impl->fileStamp = *stamp;
        impl->source.state = OriginalMainlineIdentityState::Imported;
        impl->source.root = parsed->key;
        impl->source.originalBytes = std::move(parsed->originalBytes);
      }
    }
    OriginalMainlineIdentity result(std::move(impl));
    auto current = result.validate(lease); if (!current) { return std::unexpected(current.error()); }
    return result;
  } catch (const std::bad_alloc&) { return error(ErrorCode::Io, "Cannot allocate original Mainline identity snapshot"); }
}
std::expected<void, Error> OriginalMainlineIdentity::validate(const ProfileLease& lease) const {
  try {
    if (!impl_) { return error(ErrorCode::Conflict, "Original Mainline identity snapshot has retired"); }
    auto held = profile(lease); if (!held) { return std::unexpected(held.error()); }
    if (!sameDirectory(impl_->profile, *held)) { return error(ErrorCode::Conflict, "Original Mainline identity profile changed"); }
    if (impl_->source.state == OriginalMainlineIdentityState::Disabled) { return {}; }
    const auto missing = [](StateHandle fd, const char* name) -> std::expected<void, Error> {
      auto value = absent(fd, name); if (!value) { return std::unexpected(value.error()); }
      if (!*value) { return error(ErrorCode::Conflict, "Original Mainline identity source appeared after capture"); }
      return {};
    };
    if (impl_->plugins.get() == detail::InvalidStateHandle) { return missing(lease.directoryHandle(), "plugins"); }
    if (!currentDirectory(lease.directoryHandle(), "plugins", impl_->plugins.get(), impl_->pluginsStamp)) {
      return error(ErrorCode::Conflict, "Original Mainline plugins directory changed");
    }
    if (impl_->directory.get() == detail::InvalidStateHandle) { return missing(impl_->plugins.get(), impl_->source.pluginDirectory.c_str()); }
    if (!currentDirectory(impl_->plugins.get(), platform::pathFromUtf8(impl_->source.pluginDirectory), impl_->directory.get(), impl_->directoryStamp)) {
      return error(ErrorCode::Conflict, "Original Mainline plugin directory changed");
    }
    if (impl_->file.get() == detail::InvalidStateHandle) { return missing(impl_->directory.get(), Leaf); }
    const auto stillCurrent = [&] {
      return currentFile(impl_->directory.get(), Leaf, impl_->file.get(), impl_->fileStamp);
    };
    if (!stillCurrent()) { return error(ErrorCode::Conflict, "Original Mainline identity file changed"); }
    auto bytes = contents(impl_->file.get(), impl_->fileStamp);
    if (!bytes) { return std::unexpected(bytes.error()); }
    if (*bytes != impl_->source.originalBytes || !stillCurrent()
        || !currentDirectory(lease.directoryHandle(), "plugins", impl_->plugins.get(), impl_->pluginsStamp)
        || !currentDirectory(impl_->plugins.get(), platform::pathFromUtf8(impl_->source.pluginDirectory), impl_->directory.get(), impl_->directoryStamp)) {
      return error(ErrorCode::Conflict, "Original Mainline identity changed while validating");
    }
    return {};
  } catch (const std::bad_alloc&) { return error(ErrorCode::Io, "Cannot validate original Mainline identity snapshot"); }
}
std::expected<OriginalMainlineIdentitySlot, Error> OriginalMainlineIdentity::select(
    std::span<const MainlineIdentityKey> active) const {
  if (!impl_ || !impl_->source.root || impl_->source.state != OriginalMainlineIdentityState::Imported) {
    return error(ErrorCode::Missing, "No imported original Mainline root identity");
  }
  const auto selected = biglybtc::plugins::mldht::selectOriginalIdentity(*impl_->source.root, active);
  if (!selected) { return error(ErrorCode::Unsupported, "Original Mainline active identity bound exceeded"); }
  return OriginalMainlineIdentitySlot{selected->key, selected->index};
}
std::expected<OriginalMainlineIdentity, Error> OriginalMainlineIdentity::createMissing(
    const ProfileLease& lease, std::string pluginDirectory, bool restoreIdentity,
    void (*observer)(OriginalMainlineIdentityCreateStage)) {
  // This is not the original malformed/restore=false replacement branch.
  if (!restoreIdentity) { return error(ErrorCode::Unsupported, "Original Mainline replacement is not enabled"); }
  try {
    auto ownership = lease.retain(); if (!ownership) { return std::unexpected(ownership.error()); }
    const ProfileLease& held = **ownership;
    auto source = load(held, std::move(pluginDirectory), true);
    if (!source) { return std::unexpected(source.error()); }
    if (source->snapshot().state != OriginalMainlineIdentityState::Missing) {
      return error(ErrorCode::Conflict, "Original Mainline first creation requires an absent primary");
    }
    const auto& captured = *source->impl_;
    if (captured.plugins.get() == detail::InvalidStateHandle || captured.directory.get() == detail::InvalidStateHandle) {
      return error(ErrorCode::Missing, "Original Mainline first creation requires existing plugin directories");
    }
    const auto parent = captured.directory.get();
    const bool protectedDirectories = fileop::nonOwnerWritesBlocked(held.directoryHandle())
        && fileop::nonOwnerWritesBlocked(captured.plugins.get()) && fileop::nonOwnerWritesBlocked(parent);
    if (!protectedDirectories) {
      return error(ErrorCode::UnsafePath, "Original Mainline creation directories must not allow other writers");
    }
    const auto chainCurrent = [&]() -> std::expected<void, Error> {
      auto retained = held.retain(); if (!retained) { return std::unexpected(retained.error()); }
      auto currentProfile = profile(held); if (!currentProfile) { return std::unexpected(currentProfile.error()); }
      if (!sameDirectory(captured.profile, *currentProfile)
          || !currentDirectory(held.directoryHandle(), "plugins", captured.plugins.get(), captured.pluginsStamp)
          || !currentDirectory(captured.plugins.get(), platform::pathFromUtf8(captured.source.pluginDirectory), parent, captured.directoryStamp)) {
        return error(ErrorCode::Conflict, "Original Mainline creation directory chain changed");
      }
      return {};
    };
    auto current = chainCurrent(); if (!current) { return std::unexpected(current.error()); }
    current = source->validate(held); if (!current) { return std::unexpected(current.error()); }
    auto pending = detail::captureProfileFile(parent, Pending, IdentityBytes);
    if (!pending) {
      if (pending.error().code == ErrorCode::InvalidFormat) {
        return error(ErrorCode::UnsafePath, "Recovery requires an owned private canonical pending root");
      }
      return std::unexpected(pending.error());
    }
    if (!pending->stamp) {
      MainlineIdentityKey key{};
      if (!platform::fillSystemEntropy(key.data(), key.size(), true)) {
        return error(ErrorCode::Io, "Cannot generate original Mainline root");
      }
      const auto bytes = biglybtc::plugins::mldht::encodeNewIdentity(key);
      current = source->validate(held); if (!current) { return std::unexpected(current.error()); }
      current = chainCurrent(); if (!current) { return std::unexpected(current.error()); }
      auto writer = detail::stageStateFile(parent, Pending, bytes, 0600);
      if (!writer) { return std::unexpected(writer.error()); }
#ifndef _WIN32
      if (::fchmod(writer->get(), 0600) != 0) {
        return error(ErrorCode::Io, "Cannot set private original Mainline pending mode");
      }
#endif
      auto synced = detail::syncStateFile(writer->get());
      if (!synced) { return std::unexpected(synced.error()); }
      const auto created = detail::stateStamp(writer->get());
      if (!created) { return std::unexpected(created.error()); }
      *writer = StateDescriptor{};
      pending = detail::captureProfileFile(parent, Pending, IdentityBytes);
      if (!pending || !pending->stamp || !detail::sameStateIdentity(*created, *pending->stamp)
          || pending->bytes != std::optional{bytes}) {
        return error(ErrorCode::Conflict, "Created original Mainline pending pathname changed");
      }
    }

    if (!privatePendingFile(*pending)) {
      return error(ErrorCode::UnsafePath, "Recovery requires an owned private canonical pending root");
    }
    const auto& bytes = *pending->bytes;
    auto root = biglybtc::plugins::mldht::readOriginalIdentity(bytes);
    if (!root || biglybtc::plugins::mldht::encodeNewIdentity(root->key) != bytes) {
      return error(ErrorCode::InvalidFormat, "Original Mainline pending root is not canonical");
    }
    auto sync = syncPending(parent, *pending);
    if (!sync) { return std::unexpected(sync.error()); }
    if (observer) { observer(OriginalMainlineIdentityCreateStage::PendingSynced); }
    current = source->validate(held); if (!current) { return std::unexpected(current.error()); }
    current = chainCurrent(); if (!current) { return std::unexpected(current.error()); }
    const auto latest = detail::captureProfileFile(parent, Pending, IdentityBytes);
    if (!latest || !pending->matches(*latest) || !privatePendingFile(*latest)) {
      return error(ErrorCode::Conflict, "Original Mainline pending authority changed");
    }
    auto moved = detail::publishStateFile(parent, Pending, Leaf, detail::InvalidStateHandle, false, &*pending->stamp);
    if (!moved) { return std::unexpected(moved.error()); }
    if (observer) { observer(OriginalMainlineIdentityCreateStage::PrimaryPublished); }
    current = chainCurrent(); if (!current) { return std::unexpected(current.error()); }
    sync = detail::syncStateDirectory(parent);
    if (!sync) { return std::unexpected(sync.error()); }

    // Publication can change ctime, so confirm the inode, permissions, mtime and bytes.
    const auto published = detail::captureProfileFile(parent, Leaf, IdentityBytes);
    const auto& approved = *pending->stamp;
    if (!published || !published->stamp || !privatePendingFile(*published)
        || !detail::sameStateIdentity(approved, *published->stamp) || approved.mode != published->stamp->mode
        || approved.owner != published->stamp->owner || approved.group != published->stamp->group
        || !detail::sameStateTimestamp(approved.modified, published->stamp->modified)
        || published->bytes != pending->bytes) {
      return error(ErrorCode::Conflict, "Published original Mainline root lost its owned inode");
    }
    auto reloaded = load(held, captured.source.pluginDirectory, true);
    if (!reloaded) { return std::unexpected(reloaded.error()); }
    if (reloaded->snapshot().state != OriginalMainlineIdentityState::Imported
        || reloaded->snapshot().originalBytes != bytes || !detail::sameStateSnapshot(*published->stamp, reloaded->impl_->fileStamp)
        || !sameDirectory(captured.pluginsStamp, reloaded->impl_->pluginsStamp)
        || !sameDirectory(captured.directoryStamp, reloaded->impl_->directoryStamp)) {
      return error(ErrorCode::Conflict, "Reloaded original Mainline root differs from its publication");
    }
    current = chainCurrent(); if (!current) { return std::unexpected(current.error()); }
    current = reloaded->validate(held); if (!current) { return std::unexpected(current.error()); }
    return reloaded;
  } catch (const std::bad_alloc&) {
    return error(ErrorCode::Io, "Cannot allocate original Mainline first creation; pending or primary may remain");
  }
}
} // namespace biglybtc::storage
