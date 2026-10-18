#include "profile.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <string_view>

#include <unistd.h>

namespace {
using namespace biglybtc::storage;
SaveStage failAt = SaveStage::TemporarySynced;
bool crashEnabled = false;
void observer(SaveStage stage) {
  if (crashEnabled && stage == failAt) {
    ::_exit(66);
  }
}
int failed(const Error& error) {
  std::cerr << error.message << '\n';
  return error.code == ErrorCode::Busy ? 3 : 2;
}
}

int main(int argc, char** argv) {
  using namespace biglybtc::storage;
  if (argc < 3) {
    return 1;
  }
  auto lease = ProfileLease::acquire(argv[2]);
  if (!lease) {
    return failed(lease.error());
  }
  const std::string_view command(argv[1]);
  if (command == "try-lock") {
    std::cout << "ACQUIRED\n";
    return 0;
  }
  if (command == "duplicate-lock" && argc == 4) {
    const auto second = ProfileLease::acquire(argv[2]);
    if (second || second.error().code != ErrorCode::Busy) {
      return 4;
    }
    // Verify the failed second acquisition did not release the first lease.
    std::ofstream(argv[3]) << "READY\n";
    std::this_thread::sleep_for(std::chrono::seconds(120));
    return 0;
  }
  if (command == "hold-lock" && argc == 4) {
    std::ofstream(argv[3]) << "READY\n";
    std::this_thread::sleep_for(std::chrono::seconds(120));
    return 0;
  }
  if (argc < 4) {
    return 1;
  }
  auto config = ConfigFile::load(*lease, argv[3]);
  if (!config) {
    return failed(config.error());
  }
  if (command == "read") {
    const auto* port = config->document().find("TCP.Listen.Port");
    if (!port || port->type != biglybtc::core::Node::Type::Integer) {
      return 4;
    }
    std::cout << "PORT " << port->integer << " SOURCE " << static_cast<int>(config->recoverySource()) << '\n';
    return 0;
  }
  if (command == "conflict") {
    std::ofstream(std::filesystem::path(argv[2]) / argv[3], std::ios::binary) << "d3:newi1ee";
    const auto saved = config->save(*lease);
    return !saved && saved.error().code == ErrorCode::Conflict ? 0 : 4;
  }
  if (command == "set-port" && argc >= 5) {
    if (!config->document().setInteger("TCP.Listen.Port", 51414)) {
      return 4;
    }
    if (std::string_view(argv[4]) != "normal") {
      crashEnabled = true;
      const std::string_view stage(argv[4]);
      failAt = stage == "temp" ? SaveStage::TemporarySynced
          : stage == "backup" ? SaveStage::BackupRenamed : SaveStage::PrimaryRenamed;
    }
    const auto saved = config->save(*lease, observer);
    return saved ? 0 : failed(saved.error());
  }
  return 1;
}
