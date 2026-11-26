// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_preflight.hpp"
#include "profile.hpp"
#include "../platform/utf8_path.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
using biglybtc::app::checkExistingProfile;
using biglybtc::platform::pathFromUtf8;
using biglybtc::platform::pathToUtf8;
using biglybtc::storage::ProfileLease;

void check(bool passed, const char* message) {
  if (!passed) { throw std::runtime_error(message); }
}

struct Fixture {
  fs::path profile;
  Fixture() {
    const auto temporary = fs::canonical(fs::temp_directory_path()).make_preferred();
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    unsigned attempt = 0;
    do {
      profile = temporary / pathFromUtf8("BiglyBTC профиль " + std::to_string(tick) + '-' + std::to_string(++attempt));
    } while (!fs::create_directory(profile));
    fs::create_directory(profile / "active");
  }
  ~Fixture() {
    std::error_code ignored;
    fs::remove_all(profile, ignored);
  }
};

void write(const fs::path& path, std::string_view bytes) {
  std::ofstream stream(path, std::ios::binary);
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  stream.close();
  check(bool(stream), "Could not write profile fixture");
}

std::string field(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }

std::string run(const fs::path& profile, int expected) {
  std::ostringstream output;
  const auto status = checkExistingProfile(profile, output);
  if (status != expected) { throw std::runtime_error("Unexpected status: " + output.str()); }
  check(output.view().find("\"payload_reads\":0,\"writes\":0,\"profile_lock_acquired\":false") !=
      std::string_view::npos, "Preflight changed its read-only report");
  return std::move(output).str();
}

void readUnderExistingLease() {
  Fixture fixture;
  const auto& profile = fixture.profile;
  const auto cfg = profile / "biglybt.config";
  const auto inventory = profile / "downloads.config";
  std::string hex;
  for (int n = 0; n < 20; ++n) { hex += "61"; }
  const auto active = profile / "active" / (hex + ".dat");
  write(cfg, "d15:TCP.Listen.Porti4123ee");
  const auto payload = profile / "unused";
  write(inventory, "d9:downloadsld8:save_dir" + field(pathToUtf8(payload)) +
      "5:statei70e7:torrent" + field(pathToUtf8(profile / "unused.torrent")) +
      "12:torrent_hash20:" + std::string(20, 'a') + "eee");
  write(active, "d10:attributesd8:networksl6:Public3:I2Pee4:infodee");

  const auto untouched = fs::last_write_time(active);
  const auto read = run(profile, 0);
  check(read.find("\"readable\":1") != read.npos && read.find("\"public_with_inactive_overlays\":1") != read.npos
      && read.find("\"tcp_port\":4123") != read.npos && !fs::exists(profile / ".azlock"),
      "Preflight did not read the profile or created a lock");
  const auto lease = ProfileLease::acquire(profile);
  check(lease.has_value(), "Could not hold the profile test lease");
  check(run(profile, 0) == read, "Preflight could not read a locked profile");
  check(fs::last_write_time(active) == untouched && !fs::exists(payload), "Preflight changed the fixture");

  const auto hardLink = profile / "state-link";
  fs::create_hard_link(active, hardLink);
  const auto refused = run(profile, 1);
  check(refused.find("\"status\":\"REVIEW_REQUIRED\"") != refused.npos
      && refused.find("\"issues\":1") != refused.npos, "Preflight accepted a shared state file");
  fs::remove(hardLink);
  fs::remove(active);
  const auto missing = run(profile, 1);
  check(missing.find("\"readable\":0") != missing.npos && !fs::exists(active),
      "Preflight created a missing state");
}

void refuseUnavailableProfile() {
  Fixture fixture;
  fs::remove(fixture.profile / "active");
  write(fixture.profile / "biglybt.config", "de");
  write(fixture.profile / "downloads.config", "d9:downloadslee");
  const auto missing = run(fixture.profile, 2);
  check(missing.find("\"status\":\"PREFLIGHT_UNAVAILABLE\"") != missing.npos
      && !fs::exists(fixture.profile / ".azlock"), "Preflight modified an incomplete profile");
  static_cast<void>(run("relative-profile", 2));
}
}

int main() {
  try {
    readUnderExistingLease();
    refuseUnavailableProfile();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
