// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "plugin.h"
#include <expected>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace biglybtc::plugins {
struct LoadError { std::string message; };
// Owner-thread-only RAII. Input must already be authenticated by the installer;
// dlopen executes native code and is not a sandbox or signature verifier.
class Module final {
public:
  static std::expected<Module, LoadError> load(const std::filesystem::path& artifact,
      std::string_view expectedId, std::uint64_t requiredCapabilities,
      const bt_plugin_options_v1& options, std::string_view expectedVersion = {});
  // Input descriptor must already be authenticated. Duplicate and retain that
  // exact inode, avoiding a second path lookup between authentication and load.
  // Installed module inodes must remain immutable for their loaded lifetime.
  static std::expected<Module, LoadError> loadDescriptor(int authenticatedDescriptor,
      std::string_view expectedId, std::uint64_t requiredCapabilities,
      const bt_plugin_options_v1& options, std::string_view expectedVersion = {});
  Module(const Module&) = delete;
  Module& operator=(const Module&) = delete;
  Module(Module&&) noexcept;
  Module& operator=(Module&&) = delete;
  ~Module();
  bt_status call(std::uint32_t operation, const void* request, std::uint32_t requestSize,
      void* response, std::uint32_t responseSize);
  std::string_view id() const { return api_->id; }
  std::string_view version() const { return api_->version; }
private:
  static std::expected<Module, LoadError> loadOwnedDescriptor(int file,
      std::string_view expectedId, std::uint64_t requiredCapabilities,
      const bt_plugin_options_v1& options, std::string_view expectedVersion);
  Module(void* library, const bt_plugin_v1* api, void* instance, int file)
      : library_(library), api_(api), instance_(instance), file_(file) {}
  void* library_ = nullptr;
  const bt_plugin_v1* api_ = nullptr;
  void* instance_ = nullptr;
  int file_ = -1;
};
}
