// SPDX-License-Identifier: GPL-2.0-or-later
#include "../engine/torrent_creator.hpp"
#include "../platform/utf8_path.hpp"

#include <libtorrent/torrent_info.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
namespace engine = biglybtc::engine;
namespace platform = biglybtc::platform;
namespace fs = std::filesystem;

void check(bool condition, const char* message) {
  if (!condition) { throw std::runtime_error(message); }
}

struct Fixture {
  fs::path root;

  Fixture() {
    const auto parent = fs::canonical(fs::temp_directory_path()).make_preferred();
    const auto clock = std::chrono::steady_clock::now().time_since_epoch().count();
    unsigned attempt = 0;
    do {
      root = parent / platform::pathFromUtf8(
          "BiglyBTC creator тест " + std::to_string(clock) + '-' + std::to_string(++attempt));
    } while (!fs::create_directory(root));
  }
  ~Fixture() {
    std::error_code ignored;
    fs::remove_all(root, ignored);
  }
  void noStagedOutput() const {
    for (const auto& child : fs::directory_iterator(root)) {
      check(!platform::pathToUtf8(child.path().filename()).starts_with(".biglybtc-create-"),
          "Creator retained a staging entry");
    }
  }
};

void write(const fs::path& path, std::string_view bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  out.close();
  check(bool(out), "Cannot write test fixture");
}

std::string read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  check(bool(in), "Cannot read test output");
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void createFromUnicodeDirectory() {
  Fixture fixture;
  engine::CreateOptions options;
  options.source = fixture.root / platform::pathFromUtf8("папка");
  options.output = fixture.root / platform::pathFromUtf8("результат.torrent");
  const auto child = options.source / platform::pathFromUtf8("дочерняя");
  fs::create_directories(child);
  const auto data = child / platform::pathFromUtf8("данные.bin");
  write(data, std::string(40000, 'd'));
  write(options.source / "empty", "");

  for (const auto format : {engine::CreateFormat::V1, engine::CreateFormat::V2, engine::CreateFormat::Hybrid}) {
    options.format = format;
    const auto result = engine::createTorrent(options);
    if (!result) { throw std::runtime_error(result.error().message); }
    check(result->publication == engine::CreatePublication::Durable && result->fileCount == 2
        && result->totalBytes == 40000, "Creator returned an incomplete publication");
    const auto bytes = read(options.output);
    libtorrent::error_code error;
    const libtorrent::torrent_info torrent(
        libtorrent::span<const char>(bytes.data(), bytes.size()), error, libtorrent::from_span);
    check(!error && torrent.is_valid() && torrent.name() == "папка", "Published torrent did not retain the Unicode name");
    check(read(data) == std::string(40000, 'd'), "Creator changed its source");
    fixture.noStagedOutput();
    fs::remove(options.output);
  }
}

void rejectRacingOutputAndMutation() {
  Fixture fixture;
  engine::CreateOptions options;
  options.source = fixture.root / "payload";
  options.output = fixture.root / "saved.torrent";
  write(options.source, "source");
  const auto conflict = engine::createTorrent(options, [&](const auto& state) {
    if (state.stage == engine::CreateStage::Publishing) { write(options.output, "reserved name"); }
  });
  check(!conflict && conflict.error().code == engine::CreateErrorCode::Conflict
      && read(options.output) == "reserved name", "Creator replaced a competing publication");
  fixture.noStagedOutput();
  fs::remove(options.output);
  const auto changed = engine::createTorrent(options, [&](const auto& state) {
    if (state.stage == engine::CreateStage::Publishing) { write(options.source, "grown source"); }
  });
  check(!changed && changed.error().code == engine::CreateErrorCode::Changed
      && !fs::exists(options.output), "Creator published metadata for stale source bytes");
  fixture.noStagedOutput();
}

}

int main() {
  try {
    createFromUnicodeDirectory();
    rejectRacingOutputAndMutation();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
