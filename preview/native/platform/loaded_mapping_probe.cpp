// SPDX-License-Identifier: GPL-2.0-or-later
#include "../engine/native_loaded_mappings.hpp"

#include <array>
#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <memory>

namespace {
using biglybtc::engine::detail::matchesMappedFile;
using biglybtc::engine::detail::nativeLoadedMappings;

struct TemporaryDirectory {
  std::filesystem::path path;
  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
};

int check(const std::filesystem::path& fixture) {
  auto pattern = (std::filesystem::temp_directory_path() / "biglybtc-mapping-XXXXXX").string();
  if (!::mkdtemp(pattern.data())) { return 1; }
  const TemporaryDirectory directory{pattern};
  const auto active = directory.path / fixture.filename();
  std::filesystem::copy_file(fixture, active);
  const auto close = [](void* handle) { ::dlclose(handle); };
  const std::unique_ptr<void, decltype(close)> pin(::dlopen(active.c_str(), RTLD_NOW | RTLD_LOCAL), close);
  if (!pin) {
    std::cerr << ::dlerror() << '\n';
    return 2;
  }
  const auto symbol = ::dlsym(pin.get(), "mapped_probe_function");
  if (!symbol || reinterpret_cast<int(*)()>(symbol)() != 17) { return 3; }
  const std::array addresses{reinterpret_cast<std::uintptr_t>(symbol)};
  struct stat original{};
  const auto snapshot = nativeLoadedMappings(addresses);
  if (::stat(active.c_str(), &original) != 0 || !snapshot || !matchesMappedFile((*snapshot)[0], original)) {
    return 4;
  }
  const std::array dataAddress{reinterpret_cast<std::uintptr_t>(&original)};
  if (nativeLoadedMappings(dataAddress)) { return 5; }

  std::filesystem::rename(active, directory.path / "original");
  std::filesystem::copy_file(fixture, active);
  struct stat replacement{};
  if (::stat(active.c_str(), &replacement) != 0
      || matchesMappedFile((*snapshot)[0], replacement)
      || nativeLoadedMappings(addresses) != snapshot) {
    return 6;
  }
  std::cout << "Executable mapping kept the original file identity after pathname replacement\n";
  return 0;
}
} // namespace

int main(int argc, char** argv) {
  if (argc != 2) { return 7; }
  try {
    const auto result = check(argv[1]);
    if (result) { std::cerr << "Loaded image check failed at step " << result << '\n'; }
    return result;
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 8;
  }
}
