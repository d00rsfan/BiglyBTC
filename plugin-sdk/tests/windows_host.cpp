// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/plugin_host.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
using biglybtc::plugins::Module;

void check(bool success, const char* message) {
  if (!success) { throw std::runtime_error(message); }
}

struct Fixture {
  fs::path root;

  Fixture() {
    const auto temp = fs::canonical(fs::temp_directory_path());
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    unsigned attempt = 0;
    do {
      fs::path name(u8"BiglyBTC модуль ");
      name += std::to_string(tick) + '-' + std::to_string(++attempt);
      root = temp / name;
    } while (!fs::create_directory(root));
  }
  ~Fixture() {
    std::error_code ignored;
    fs::remove_all(root, ignored);
  }
};

void call(Module& module) {
  std::uint32_t value = 0;
  check(module.id() == "fixture" && module.version() == "0.1.0" &&
      module.call(7, nullptr, 0, &value, sizeof(value)) == BT_OK && value == 42,
      "Windows module did not reach its started state");
}

}

int wmain(int argc, wchar_t** argv) {
  try {
    check(argc == 2, "Missing Windows plugin test fixture");
    Fixture fixture;
    const auto path = fixture.root / fs::path(u8"провайдер.dll");
    fs::copy_file(argv[1], path);
    const bt_plugin_options_v1 options{.size = sizeof(bt_plugin_options_v1), .abi = BT_PLUGIN_ABI_V1,
        .plugin_directory_fd = -1, .reserved = 0, .java_plugin_version = nullptr,
        .host_context = nullptr, .country_name = nullptr};
    {
      auto loaded = Module::load(path, "fixture", BT_CAP_LOCATION, options, "0.1.0");
      if (!loaded) { throw std::runtime_error(loaded.error().message); }
      call(*loaded);
      check(!::DeleteFileW(path.c_str()), "A loaded module could lose its retained file");
    }
    {
      const auto file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
          OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
      check(file != INVALID_HANDLE_VALUE, "Cannot open module descriptor");
      auto witness = std::unique_ptr<void, decltype(&::CloseHandle)>(file, ::CloseHandle);
      auto loaded = Module::loadDescriptor(witness.get(), "fixture", BT_CAP_LOCATION, options, "0.1.0");
      if (!loaded) { throw std::runtime_error(loaded.error().message); }
      witness.reset();
      call(*loaded);
      check(!::DeleteFileW(path.c_str()), "Module released the selected file too early");
    }
    check(!Module::load(path, "other", BT_CAP_LOCATION, options, "0.1.0"), "Accepted a mismatched module ID");
    fs::create_hard_link(path, fixture.root / "alias.dll");
    check(!Module::load(path, "fixture", BT_CAP_LOCATION, options), "Accepted a shared module inode");
    check(fs::remove(path), "Module retained its file after unloading");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
