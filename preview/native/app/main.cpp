#include "local_session.hpp"
#include "../platform/utf8_path.hpp"

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

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
  using namespace biglybtc;
  const auto argument = [argv](int index) {
#ifdef _WIN32
    return platform::pathToUtf8(std::filesystem::path(argv[index]));
#else
    return std::string_view(argv[index]);
#endif
  };
  if (!std::setlocale(LC_CTYPE, "")) {
    std::cerr << "Unable to initialize the filesystem character locale\n";
    return 2;
  }
  if (argc != 5 && argc != 7) {
    std::cerr << "Usage: BiglyBTC-headless --experimental-local GENERATED_PROFILE HASH OWNED_PEER_PORT [--crash STAGE]\n";
    return 2;
  }
  namespace fs = std::filesystem;
  const auto profile = fs::path(argv[2]).make_preferred();
  const auto root = profile.lexically_normal();
  std::error_code pathError;
#ifdef _WIN32
  const auto temp = fs::temp_directory_path(pathError);
  const auto generatedRoot = pathError ? fs::path{} : fs::canonical(temp, pathError).make_preferred();
#else
  const fs::path generatedRoot("/tmp");
#endif
  const auto relative = root.lexically_relative(generatedRoot);
  const auto first = relative.begin();
  const bool generated = !pathError && !generatedRoot.empty() && !relative.has_root_path()
      && first != relative.end() && platform::pathToUtf8(*first).starts_with("biglybtc-");
  const auto resolved = fs::canonical(profile, pathError).make_preferred();
  if (argument(1) != "--experimental-local" || !generated || profile != root
      || pathError || resolved != profile) {
    std::cerr << "This experimental runner accepts generated biglybtc-* profiles in its temporary directory only\n";
    return 2;
  }
  const auto hex = argument(3);
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
  const auto portText = argument(4);
  const auto parsed = std::from_chars(portText.data(), portText.data() + portText.size(), port);
  if (!hash || parsed.ec != std::errc{} || parsed.ptr != portText.data() + portText.size() || port == 0 || port > 65535) {
    std::cerr << "Invalid torrent identity or owned endpoint\n";
    return 2;
  }
  if (argc == 7) {
    const auto stage = argument(6);
    const auto value = std::from_chars(stage.data(), stage.data() + stage.size(), crashStage);
    if (argument(5) != "--crash" || value.ec != std::errc{}
        || value.ptr != stage.data() + stage.size() || crashStage < 0 || crashStage > 5) { return 2; }
  }
  auto result = engine::continueLocal({.profile = profile, .hash = *hash,
      .peerPort = static_cast<unsigned short>(port), .observer = checkpoint});
  if (!result) { std::cerr << result.error().message << '\n'; return 1; }
  std::cout << "LOCAL_COMPLETE reused=" << result->reusedPieces << " received=" << result->received
      << " bytes=" << result->bytes << " path=" << result->payload << '\n';
  return 0;
}
