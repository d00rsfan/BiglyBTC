// SPDX-License-Identifier: GPL-2.0-or-later
#include "mainline_checkpoint.hpp"
#ifdef BIGLYBTC_TEST_MAINLINE_IDENTITY
#include "mainline_identity.hpp"
#endif
#include "../platform/utf8_path.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
using namespace biglybtc;
namespace s = storage;
constexpr auto Checkpoint = "BiglyBTC-mainline.config";

void check(bool passed, const char* message) {
  if (!passed) { throw std::runtime_error(message); }
}

template<class T> T take(std::expected<T, s::Error> result) {
  if (!result) { throw std::runtime_error(result.error().message); }
  return std::move(*result);
}

void take(std::expected<void, s::Error> result) {
  if (!result) { throw std::runtime_error(result.error().message); }
}

struct Fixture {
  fs::path path;
  std::optional<s::ProfileLease> lease;
  Fixture() {
    const auto root = fs::canonical(fs::temp_directory_path()).make_preferred();
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    unsigned attempt = 0;
    do {
      path = root / platform::pathFromUtf8(
          "Mainline профиль " + std::to_string(tick) + '-' + std::to_string(++attempt));
    } while (!fs::create_directory(path));
    lease.emplace(take(s::ProfileLease::acquire(path)));
  }
  ~Fixture() {
    lease.reset();
    std::error_code ignored;
    fs::remove_all(path, ignored);
  }
};

void write(const fs::path& path, std::string_view bytes) {
  std::ofstream stream(path, std::ios::binary);
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  stream.close();
  check(bool(stream), "Could not write Mainline test fixture");
}

std::string read(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  check(bool(stream), "Could not read Mainline test fixture");
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void publishAndReopen() {
  Fixture profile;
  auto first = take(s::MainlineCheckpoint::load(*profile.lease, "mldht"));
  check(!first.state() && !fs::exists(profile.path / Checkpoint), "Empty load wrote a native checkpoint");
  take(first.save(*profile.lease, "state-A"));
  const auto old = read(profile.path / Checkpoint);
  auto stale = take(s::MainlineCheckpoint::load(*profile.lease, "mldht"));
  take(first.save(*profile.lease, "state-B"));
  check(read(profile.path / (std::string(Checkpoint) + ".bak")) == old, "Backup differs from the original primary");
  const auto current = read(profile.path / Checkpoint);
  const auto refused = stale.save(*profile.lease, "state-C");
  check(!refused && refused.error().code == s::ErrorCode::Conflict && read(profile.path / Checkpoint) == current,
      "Stale checkpoint replaced a newer publication");
  const auto loaded = take(s::MainlineCheckpoint::load(*profile.lease, "mldht"));
  check(loaded.state() == std::optional<std::string>{"state-B"}, "Published state could not be reopened");
}

void readLegacyWithoutModification() {
  Fixture profile;
  const auto absent = take(s::readLegacyMldhtHints(*profile.lease, "mldht", 1000));
  check(absent.ipv4Missing && absent.ipv6Missing && !fs::exists(profile.path / "plugins"),
      "Missing routing tables changed the profile");
  const auto plugin = profile.path / "plugins" / "mldht";
  fs::create_directories(plugin);
  const std::string cache = "d11:mainEntriesld4:addr6:" + std::string("\x7f\x00\x00\x01\xa0\x29", 6)
      + "7:createdi800e11:failedCounti0e2:id20:" + std::string(20, 'A')
      + "8:lastSeeni995e8:lastSendi990e8:verifiedi1eee9:timestampi1000ee";
  const auto table = plugin / "IPv4-table.cache";
  write(table, cache);
  const auto before = fs::last_write_time(table);
  const auto hints = take(s::readLegacyMldhtHints(*profile.lease, "mldht", 1000));
  check(hints.endpoints.size() == 1 && hints.endpoints.front().address == "127.0.0.1"
      && hints.endpoints.front().port == 41001 && !hints.ipv4Missing && hints.ipv6Missing,
      "Legacy routing table did not produce the expected endpoint");
  check(fs::last_write_time(table) == before && read(table) == cache && !fs::exists(profile.path / Checkpoint),
      "Loading Mainline hints modified original routing metadata");

  fs::create_hard_link(table, profile.path / "legacy-link");
  check(!s::readLegacyMldhtHints(*profile.lease, "mldht", 1000) && read(table) == cache,
      "A shared routing table was accepted or modified");
}
#ifdef BIGLYBTC_TEST_MAINLINE_IDENTITY
void interruptAfterStaging(s::OriginalMainlineIdentityCreateStage stage) {
  if (stage == s::OriginalMainlineIdentityCreateStage::PendingSynced) { throw std::bad_alloc(); }
}

void discoverAndRecoverOriginalIdentity() {
  Fixture profile;
  const auto plugin = profile.path / "plugins" / "mldht";
  fs::create_directories(plugin);
#ifndef _WIN32
  for (const auto& directory : {profile.path, plugin.parent_path(), plugin}) {
    fs::permissions(directory, fs::perms::owner_all, fs::perm_options::replace);
  }
#endif
  write(plugin / "plugin.properties", "# generated fixture\nplugin.id=mldht\n");
  const auto discovered = take(s::discoverOriginalMainlineDirectory(*profile.lease));
  check(discovered == "mldht", "Original Mainline plugin directory was not discovered");
  const auto original = plugin / "baseID.config";
  const auto pending = plugin / "baseID.config.BiglyBTC-pending";
  const auto interrupted = s::OriginalMainlineIdentity::createMissing(*profile.lease, discovered, true,
      interruptAfterStaging);
  if (!fs::exists(pending) && !interrupted) { throw std::runtime_error(interrupted.error().message); }
  check(!interrupted && fs::exists(pending) && !fs::exists(original),
      "Original Mainline identity published before staged interruption");
  const auto bytes = read(pending);
  profile.lease.reset();
  profile.lease.emplace(take(s::ProfileLease::acquire(profile.path)));
  const auto restored = take(s::OriginalMainlineIdentity::createMissing(*profile.lease, discovered));
  check(restored.snapshot().originalBytes == bytes && read(original) == bytes && !fs::exists(pending),
      "Original Mainline identity recovery changed its staged key");
  take(restored.validate(*profile.lease));
  const auto duplicate = s::OriginalMainlineIdentity::createMissing(*profile.lease, discovered);
  check(!duplicate && duplicate.error().code == s::ErrorCode::Conflict, "Original Mainline identity was replaced");
}
#endif

}

int main() {
  try {
    publishAndReopen();
    readLegacyWithoutModification();
#ifdef BIGLYBTC_TEST_MAINLINE_IDENTITY
    discoverAndRecoverOriginalIdentity();
#endif
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
