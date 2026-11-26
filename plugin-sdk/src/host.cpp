// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/plugin_host.hpp"
#include <cstring>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <bit>
#include <memory>
#include <optional>
#include <vector>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <utility>

namespace biglybtc::plugins {
namespace {
#ifdef _WIN32
#include "host_windows.inc"
#endif

void closeLibrary(void* library) noexcept {
  if (!library) { return; }
#ifdef _WIN32
  ::FreeLibrary(static_cast<HMODULE>(library));
#else
  ::dlclose(library);
#endif
}

void closeFile(ModuleFileHandle file) noexcept {
  if (file == InvalidModuleFile) { return; }
#ifdef _WIN32
  ::CloseHandle(file);
#else
  ::close(file);
#endif
}

bool validOptions(std::string_view expectedId, const bt_plugin_options_v1& options) {
  return !expectedId.empty() && expectedId.size() <= 64 && options.size == sizeof(options)
      && options.abi == BT_PLUGIN_ABI_V1 && options.reserved == 0;
}
} // namespace

std::expected<Module, LoadError> Module::load(const std::filesystem::path& artifact,
    std::string_view expectedId, std::uint64_t capabilities, const bt_plugin_options_v1& options,
    std::string_view expectedVersion) {
  if (!validOptions(expectedId, options)) {
    return std::unexpected(LoadError{"Invalid host options"});
  }
#ifdef _WIN32
  const auto file = openFile(artifact).release();
#else
  const int file = ::open(artifact.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
#endif
  return loadOwnedDescriptor(file, expectedId, capabilities, options, expectedVersion, false);
}
std::expected<Module, LoadError> Module::loadDescriptor(ModuleFileHandle authenticatedDescriptor,
    std::string_view expectedId, std::uint64_t capabilities, const bt_plugin_options_v1& options,
    std::string_view expectedVersion, bool allowSystemOwner) {
  if (!validOptions(expectedId, options)) {
    return std::unexpected(LoadError{"Invalid host options"});
  }
#ifdef _WIN32
  const auto raw = ::ReOpenFile(authenticatedDescriptor, GENERIC_READ | READ_CONTROL, FILE_SHARE_READ,
      FILE_FLAG_OPEN_REPARSE_POINT);
  const auto file = raw == INVALID_HANDLE_VALUE ? InvalidModuleFile : raw;
#else
  const int file = ::fcntl(authenticatedDescriptor, F_DUPFD_CLOEXEC, 3);
#endif
  return loadOwnedDescriptor(file, expectedId, capabilities, options, expectedVersion, allowSystemOwner);
}

std::expected<Module, LoadError> Module::loadBuiltin(bt_plugin_entry_fn entry,
    std::string_view expectedId, std::uint64_t capabilities, const bt_plugin_options_v1& options,
    std::string_view expectedVersion) {
  if (!validOptions(expectedId, options) || expectedVersion.empty()) {
    return std::unexpected(LoadError{"Invalid built-in module options"});
  }
  return start(nullptr, InvalidModuleFile, entry, expectedId, capabilities, options, expectedVersion);
}

std::expected<Module, LoadError> Module::loadOwnedDescriptor(ModuleFileHandle file,
    std::string_view expectedId, std::uint64_t capabilities, const bt_plugin_options_v1& options,
    std::string_view expectedVersion, bool allowSystemOwner) {
#ifdef _WIN32
  HeldFile retained(file, ::CloseHandle);
  if (!safeModuleFile(retained.get(), allowSystemOwner)) {
    return std::unexpected(LoadError{"Unsafe native module"});
  }
  const auto path = resolvedPath(file);
  if (path.empty()) { return std::unexpected(LoadError{"Cannot resolve the Windows module path"}); }
  const auto parents = holdModuleParents(path);
  if (!parents || !sameFile(file, openFile(path).get())) {
    return std::unexpected(LoadError{"Cannot authenticate the Windows module path"});
  }
  const auto library = ::LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!library) { return std::unexpected(LoadError{"Module dependencies unavailable"}); }
  const auto address = ::GetProcAddress(library, "biglybtc_plugin_entry");
  HMODULE entryOwner = nullptr;
  constexpr DWORD imageFlags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
  if (!address || !::GetModuleHandleExW(imageFlags, std::bit_cast<LPCWSTR>(address), &entryOwner)
      || entryOwner != library) {
    closeLibrary(library);
    return std::unexpected(LoadError{"Missing entry point in the selected module"});
  }
  return start(library, retained.release(), std::bit_cast<bt_plugin_entry_fn>(address),
      expectedId, capabilities, options, expectedVersion);
#else
  struct stat info{};
  if (file < 0) { return std::unexpected(LoadError{"Cannot open native module"}); }
  if (::fstat(file, &info) != 0 || !S_ISREG(info.st_mode) || info.st_nlink != 1
      || (info.st_uid != ::geteuid() && !(allowSystemOwner && info.st_uid == 0))
      || (info.st_mode & 0022) != 0) {
    ::close(file);
    return std::unexpected(LoadError{"Unsafe native module"});
  }
#ifdef __APPLE__
  (void)expectedId;
  (void)capabilities;
  (void)options;
  (void)expectedVersion;
  ::close(file);
  return std::unexpected(LoadError{"macOS loads the bundled components from the application image"});
#else
  // Retain this descriptor for module lifetime. A replaced pathname never changes
  // the selected inode. The installer never edits an installed inode in place.
  const auto path = "/proc/self/fd/" + std::to_string(file);
  void* library = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!library) { ::close(file); return std::unexpected(LoadError{"Module dependencies unavailable"}); }
  const auto entry = reinterpret_cast<bt_plugin_entry_fn>(::dlsym(library, "biglybtc_plugin_entry"));
  return start(library, file, entry, expectedId, capabilities, options, expectedVersion);
#endif
#endif
}

std::expected<Module, LoadError> Module::start(void* library, ModuleFileHandle file, bt_plugin_entry_fn entry,
    std::string_view expectedId, std::uint64_t capabilities, const bt_plugin_options_v1& options,
    std::string_view expectedVersion) {
  auto reject = [&](const char* reason) -> std::expected<Module, LoadError> {
    closeLibrary(library);
    closeFile(file);
    return std::unexpected(LoadError{reason});
  };
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
      instance_(std::exchange(other.instance_, nullptr)), file_(std::exchange(other.file_, InvalidModuleFile)) {}
Module::~Module() {
  if (instance_) { api_->stop(instance_); api_->destroy(instance_); }
  closeLibrary(library_);
  closeFile(file_);
}
bt_status Module::call(std::uint32_t op, const void* request, std::uint32_t requestSize,
    void* response, std::uint32_t responseSize) {
  if (!instance_) { return BT_INVALID; }
  return api_->call(instance_, op, request, requestSize, response, responseSize);
}
}
