#include "local_session.hpp"

#include <charconv>
#include <clocale>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
int crashStage = -1;
void checkpoint(biglybtc::engine::LocalStage stage) {
  std::cout << "LOCAL_STAGE " << static_cast<int>(stage) << std::endl;
  if (static_cast<int>(stage) == crashStage) { std::_Exit(66); }
}
}

int main(int argc, char** argv) {
  using namespace biglybtc;
  if (!std::setlocale(LC_CTYPE, "")) {
    std::cerr << "Unable to initialize the filesystem character locale\n";
    return 2;
  }
  if (argc != 5 && argc != 7) {
    std::cerr << "Usage: BiglyBTC-headless --experimental-local GENERATED_PROFILE HASH OWNED_PEER_PORT [--crash STAGE]\n";
    return 2;
  }
  const std::filesystem::path profile(argv[2]);
  const auto root = profile.lexically_normal();
  auto iterator = root.begin();
  bool generated = iterator != root.end() && *iterator++ == "/";
  generated = generated && iterator != root.end() && *iterator++ == "tmp";
  generated = generated && iterator != root.end() && iterator->string().starts_with("biglybtc-");
  std::error_code pathError;
  const auto resolved = std::filesystem::canonical(profile, pathError);
  if (std::string_view(argv[1]) != "--experimental-local" || !generated || profile != root
      || pathError || resolved != profile) {
    std::cerr << "This experimental runner accepts generated /tmp/biglybtc-* profiles only\n";
    return 2;
  }
  const std::string_view hex(argv[3]);
  std::string bytes;
  if (hex.size() == 40 || hex.size() == 64) {
    for (std::size_t i = 0; i < hex.size(); i += 2) {
      unsigned int byte = 0;
      const auto parsed = std::from_chars(hex.data() + i, hex.data() + i + 2, byte, 16);
      if (parsed.ec != std::errc{} || parsed.ptr != hex.data() + i + 2) { bytes.clear(); break; }
      bytes.push_back(static_cast<char>(byte));
    }
  }
  auto hash = core::TorrentHash::parse(bytes);
  unsigned int port = 0;
  const std::string_view argument(argv[4]);
  const auto parsed = std::from_chars(argument.data(), argument.data() + argument.size(), port);
  if (!hash || parsed.ec != std::errc{} || parsed.ptr != argument.data() + argument.size() || port == 0 || port > 65535) {
    std::cerr << "Invalid torrent identity or owned endpoint\n";
    return 2;
  }
  if (argc == 7) {
    const std::string_view stage(argv[6]);
    const auto value = std::from_chars(stage.data(), stage.data() + stage.size(), crashStage);
    if (std::string_view(argv[5]) != "--crash" || value.ec != std::errc{}
        || value.ptr != stage.data() + stage.size() || crashStage < 0 || crashStage > 5) { return 2; }
  }
  auto result = engine::continueLocal({.profile = profile, .hash = *hash,
      .peerPort = static_cast<unsigned short>(port), .observer = checkpoint});
  if (!result) { std::cerr << result.error().message << '\n'; return 1; }
  std::cout << "LOCAL_COMPLETE reused=" << result->reusedPieces << " received=" << result->received
      << " bytes=" << result->bytes << " path=" << result->payload << '\n';
  return 0;
}
