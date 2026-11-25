// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/plugin_host.hpp"
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <unistd.h>

extern "C" const bt_plugin_v1* sdk_builtin_entry_0(std::uint32_t);
extern "C" const bt_plugin_v1* sdk_builtin_entry_2(std::uint32_t);
extern "C" const bt_plugin_v1* sdk_builtin_entry_5(std::uint32_t);
extern "C" const bt_plugin_v1* sdk_builtin_entry_6(std::uint32_t);

namespace {
void require(bool condition, const char* message) {
  if (!condition) { throw std::runtime_error(message); }
}
} // namespace

int main() {
  namespace fs = std::filesystem;
  using biglybtc::plugins::Module;
  auto pattern = (fs::temp_directory_path() / "biglybtc-builtin-host-XXXXXX").string();
  require(::mkdtemp(pattern.data()) != nullptr, "Create fixture directory");
  const auto cleanup = [](const char* path) noexcept {
    std::error_code ignored;
    fs::remove_all(path, ignored);
  };
  const std::unique_ptr<const char, decltype(cleanup)> temporary(pattern.c_str(), cleanup);
  const int directory = ::open(temporary.get(), O_RDONLY | O_CLOEXEC | O_DIRECTORY);
  require(directory >= 0, "Open fixture directory");
  const auto events = fs::path(temporary.get()) / "events";
  const auto checkEvents = [&](std::string expected) {
    std::ifstream stream(events);
    const std::string actual{std::istreambuf_iterator<char>{stream}, {}};
    require(actual == expected, "Module lifecycle calls are out of order");
    fs::remove(events);
  };
  const bt_plugin_options_v1 options{sizeof(options), BT_PLUGIN_ABI_V1, directory, 0, "0.6.9", nullptr, nullptr};
  const auto load = [&](bt_plugin_entry_fn entry, std::string_view version = "0.1.0") {
    return Module::loadBuiltin(entry, "fixture", BT_CAP_LOCATION, options, version);
  };
  require(!load(nullptr) && !load(sdk_builtin_entry_2) && !load(sdk_builtin_entry_0, "0.2.0"),
      "Invalid entry, ABI or version was accepted");
  checkEvents("");
  require(!load(sdk_builtin_entry_5), "Create failure was accepted");
  checkEvents("CD");
  require(!load(sdk_builtin_entry_6), "Start failure was accepted");
  checkEvents("CASD");
  {
    auto module = load(sdk_builtin_entry_0);
    require(module.has_value(), "Built-in module did not start");
    auto moved = std::move(*module);
    require(moved.call(0, nullptr, 0, nullptr, 0) == BT_UNSUPPORTED, "Moved module lost its instance");
    require(module->call(0, nullptr, 0, nullptr, 0) == BT_INVALID, "Old module retained its instance");
  }
  checkEvents("CAQSD");
  ::close(directory);
}
