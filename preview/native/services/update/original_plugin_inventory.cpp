// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_plugin_inventory.hpp"
#include "original_plugin_jar.hpp"
#include "../../platform/utf8_path.hpp"
#include "java_properties.hpp"
#include "plugin_settings.hpp"
#include "state_directory_listing.hpp"
#ifdef _WIN32
#include "../../platform/windows_file_owner.hpp"
#else
#include <sys/stat.h>
#endif

namespace biglybtc::update {
namespace {
namespace files = storage::detail;
using storage::Error;
using storage::ErrorCode;

std::unexpected<Error> error(ErrorCode code, const char* message) {
  return std::unexpected(Error{code, message});
}

bool heldAt(files::StateHandle parent, const std::filesystem::path& leaf, files::StateHandle file) {
  const auto held = files::stateStamp(file);
  const auto named = files::stateStampAt(parent, leaf);
  return held && held->owned && named && *named && files::sameStateDirectory(*held, **named);
}

bool indirection(const files::StateStamp& info) {
#ifdef _WIN32
  return (info.mode & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
  return S_ISLNK(info.mode);
#endif
}
} // namespace

namespace detail {
std::expected<OriginalDescriptor, Error> originalDescriptor(storage::detail::StateHandle parent, std::string_view directoryName) {
  auto loose = storage::detail::readStateFile(parent, "plugin.properties", 65536);
  if (!loose) { return std::unexpected(loose.error()); }
  std::string properties, identity;
  std::optional<std::string> fallbackId;
  if (*loose) {
    properties = std::move(**loose);
    identity = "loose";
  } else {
    auto jar = readOriginalJarDescriptor(parent, directoryName);
    if (!jar) { return std::unexpected(jar.error()); }
    if (!*jar) { return error(ErrorCode::Missing, "Installed Java plugin descriptor is missing"); }
    properties = std::move((**jar).properties);
    identity = std::move((**jar).identity);
    fallbackId = std::move((**jar).fallbackId);
  }
  const auto parsed = services::readJavaProperties(QString::fromLatin1(properties), 4096,
      services::JavaPropertiesDuplicates::Reject);
  if (!parsed) { return error(ErrorCode::Conflict, "Installed Java plugin descriptor has invalid or duplicate properties"); }

  const auto id = parsed->constFind(QStringLiteral("plugin.id"));
  if (id == parsed->cend() && *loose) {
    auto derived = originalPluginIdFallback(parent, directoryName);
    if (!derived) { return std::unexpected(derived.error()); }
    fallbackId = std::move(*derived);
  }
  if (id == parsed->cend() && !fallbackId) {
    return error(ErrorCode::Conflict, "Installed Java plugin descriptor has an ambiguous runtime ID");
  }
  auto runtimeId = id == parsed->cend() ? QString::fromLatin1(*fallbackId) : *id;
  return OriginalDescriptor{std::move(runtimeId), identity + '\0' + properties};
}

} // namespace detail

std::expected<std::vector<InstalledJavaPluginDirectory>, Error> installedJavaPluginDirectories(
    const storage::ProfileLease& lease, std::stop_token stop) {
  const auto access = lease.sourceAccess();
  if (!access) { return std::unexpected(access.error()); }
  const auto plugins = files::openStateDirectory(lease.directoryHandle(), "plugins");
  if (!plugins && plugins.error().code == ErrorCode::Missing) { return std::vector<InstalledJavaPluginDirectory>{}; }
  if (!plugins) { return std::unexpected(plugins.error()); }
  if (!heldAt(lease.directoryHandle(), "plugins", plugins->get())) {
    return error(ErrorCode::UnsafePath, "The original plugins directory is unsafe");
  }
  const auto names = files::listStateDirectoryNames(plugins->get(), 514, stop);
  if (!names) {
    if (names.error().code == ErrorCode::Unsupported) {
      return error(ErrorCode::InvalidFormat, "The plugin directory contains too many entries");
    }
    return std::unexpected(names.error());
  }
  std::vector<InstalledJavaPluginDirectory> entries;
  for (const auto& path : *names) {
    if (stop.stop_requested()) { return error(ErrorCode::Cancelled, "Java plugin inventory reading was cancelled"); }
    if (path == "." || path == ".." || path == "CVS") { continue; }
    const auto info = files::stateStampAt(plugins->get(), path);
    if (!info || !*info) { return error(ErrorCode::Conflict, "The original plugins inventory changed during discovery"); }
    if (indirection(**info)) { return error(ErrorCode::UnsafePath, "The original plugins inventory contains a symbolic link"); }
    if (!(**info).directory) { continue; }
    const auto name = platform::pathToUtf8(path);
    if (!storage::originalPluginBinding("plugins", name)) {
      return error(ErrorCode::UnsafePath, "An original plugin directory is unsafe");
    }
    const auto original = files::openDirectoryAt(plugins->get(), path);
    if (!original) { return std::unexpected(original.error()); }
    if (!heldAt(plugins->get(), path, original->get())) {
      return error(ErrorCode::UnsafePath, "The original plugin directory owner or name changed");
    }
    const auto descriptor = detail::originalDescriptor(original->get(), name);
    if (!descriptor && descriptor.error().code == ErrorCode::Missing) { continue; }
    if (!descriptor) { return std::unexpected(descriptor.error()); }
    if (!heldAt(plugins->get(), path, original->get())) {
      return error(ErrorCode::Conflict, "An original plugin directory changed during discovery");
    }
    entries.push_back({descriptor->runtimeId, name});
  }
  if (!heldAt(lease.directoryHandle(), "plugins", plugins->get())) {
    return error(ErrorCode::Conflict, "The original plugins directory changed during discovery");
  }
  return entries;
}
} // namespace biglybtc::update
