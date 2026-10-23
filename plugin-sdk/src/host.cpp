// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/plugin_host.hpp"
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace biglybtc::plugins {
std::expected<Module, LoadError> Module::load(const std::filesystem::path& artifact,
    std::string_view expectedId, std::uint64_t capabilities, const bt_plugin_options_v1& options,
    std::string_view expectedVersion) {
  if (expectedId.empty() || expectedId.size() > 64 || options.size != sizeof(options)
      || options.abi != BT_PLUGIN_ABI_V1 || options.reserved != 0) {
    return std::unexpected(LoadError{"Invalid host options"});
  }
  const int file = ::open(artifact.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  return loadOwnedDescriptor(file, expectedId, capabilities, options, expectedVersion);
}
std::expected<Module, LoadError> Module::loadDescriptor(int authenticatedDescriptor,
    std::string_view expectedId, std::uint64_t capabilities, const bt_plugin_options_v1& options,
    std::string_view expectedVersion) {
  if (expectedId.empty() || expectedId.size() > 64 || options.size != sizeof(options)
      || options.abi != BT_PLUGIN_ABI_V1 || options.reserved != 0) {
    return std::unexpected(LoadError{"Invalid host options"});
  }
  const int file = ::fcntl(authenticatedDescriptor, F_DUPFD_CLOEXEC, 3);
  return loadOwnedDescriptor(file, expectedId, capabilities, options, expectedVersion);
}
std::expected<Module, LoadError> Module::loadOwnedDescriptor(int file,
    std::string_view expectedId, std::uint64_t capabilities, const bt_plugin_options_v1& options,
    std::string_view expectedVersion) {
  struct stat info{};
  if (file < 0) { return std::unexpected(LoadError{"Cannot open native module"}); }
  if (::fstat(file, &info) != 0 || !S_ISREG(info.st_mode) || info.st_nlink != 1
      || info.st_uid != ::geteuid() || (info.st_mode & 0022) != 0) {
    ::close(file);
    return std::unexpected(LoadError{"Unsafe native module"});
  }
  // Retain this descriptor for module lifetime. A replaced pathname never changes
  // the selected inode. The installer never edits an installed inode in place.
  const auto path = "/proc/self/fd/" + std::to_string(file);
  void* library = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!library) { ::close(file); return std::unexpected(LoadError{"Module dependencies unavailable"}); }
  auto reject = [&](const char* reason) -> std::expected<Module, LoadError> {
    ::dlclose(library); ::close(file); return std::unexpected(LoadError{reason});
  };
  auto entry = reinterpret_cast<bt_plugin_entry_fn>(::dlsym(library, "biglybtc_plugin_entry"));
  if (!entry) { return reject("Missing plugin entry point"); }
  const auto* api = entry(BT_PLUGIN_ABI_V1);
  if (!api || api->size != sizeof(bt_plugin_v1) || api->abi != BT_PLUGIN_ABI_V1
      || !api->id || !api->version || ::strnlen(api->id, 65) != expectedId.size()
      || expectedId != api->id || ::strnlen(api->version, 65) > 64 || !api->version[0]
      || (!expectedVersion.empty() && expectedVersion != api->version)
      || (api->capabilities & capabilities) != capabilities
      || !api->create || !api->start || !api->call || !api->stop || !api->destroy) {
    return reject("Incompatible plugin descriptor");
  }
  void* instance = nullptr;
  const auto created = api->create(&options, &instance);
  if (created != BT_OK || !instance) {
    if (instance) { api->destroy(instance); }
    return reject("Plugin creation failed");
  }
  if (api->start(instance) != BT_OK) {
    api->stop(instance); api->destroy(instance); return reject("Plugin start failed");
  }
  return Module(library, api, instance, file);
}
Module::Module(Module&& other) noexcept
    : library_(std::exchange(other.library_, nullptr)), api_(other.api_),
      instance_(std::exchange(other.instance_, nullptr)), file_(std::exchange(other.file_, -1)) {}
Module::~Module() {
  if (library_) {
    api_->stop(instance_); api_->destroy(instance_); ::dlclose(library_);
  }
  if (file_ >= 0) { ::close(file_); }
}
bt_status Module::call(std::uint32_t op, const void* request, std::uint32_t requestSize,
    void* response, std::uint32_t responseSize) {
  if (!library_) { return BT_INVALID; }
  return api_->call(instance_, op, request, requestSize, response, responseSize);
}
}
