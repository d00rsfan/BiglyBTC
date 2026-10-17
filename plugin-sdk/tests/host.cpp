#include "biglybtc/plugin_host.hpp"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
namespace fs = std::filesystem;
void require(bool value) { if (!value) { throw std::runtime_error("Host lifecycle contract failed"); } }
int main(int argc, char** argv) {
  require(argc == 8);
  char pattern[] = "/tmp/biglybtc-sdk-XXXXXX";
  const auto* temp = ::mkdtemp(pattern);
  require(temp != nullptr);
  const fs::path root(temp);
  const int directory = ::open(temp, O_RDONLY | O_CLOEXEC | O_DIRECTORY);
  require(directory >= 0);
  const bt_plugin_options_v1 options{sizeof(options), 1, directory, 0, "0.6.9", nullptr, nullptr};
  for (int mode = 0; mode <= 6; ++mode) {
    const auto artifact = root / (std::to_string(mode) + ".so");
    fs::copy_file(argv[mode + 1], artifact);
    require(::chmod(artifact.c_str(), 0400) == 0);
    fs::remove(root / "events");
    {
      auto module = biglybtc::plugins::Module::load(artifact, "fixture", BT_CAP_LOCATION, options);
      require(module.has_value() == (mode == 0));
      if (module) {
        require(module->id() == "fixture" && module->version() == "0.1.0");
        auto moved = std::move(*module);
        require(moved.call(0, nullptr, 0, nullptr, 0) == BT_UNSUPPORTED);
        require(module->call(0, nullptr, 0, nullptr, 0) == BT_INVALID);
      }
    }
    std::ifstream stream(root / "events");
    const std::string events((std::istreambuf_iterator<char>(stream)), {});
    require(events == (mode == 0 ? "CAQSD" : mode == 5 ? "CD" : mode == 6 ? "CASD" : ""));
  }
  const auto artifact = root / "0.so";
  require(::chmod(artifact.c_str(), 0660) == 0);
  require(!biglybtc::plugins::Module::load(artifact, "fixture", BT_CAP_LOCATION, options));
  fs::create_symlink(artifact, root / "link.so");
  require(!biglybtc::plugins::Module::load(root / "link.so", "fixture", BT_CAP_LOCATION, options));
  ::close(directory);
  std::cout << "PASS native descriptor, lifecycle, failed start/create, move, capability, mode and symlink checks\n";
}
