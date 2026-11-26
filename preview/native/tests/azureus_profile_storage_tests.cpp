// SPDX-License-Identifier: GPL-2.0-or-later
#include "azureus_contacts.hpp"
#include "azureus_key_block_area.hpp"
#include "azureus_local_diversification.hpp"
#include "azureus_recent_address_area.hpp"
#include "azureus_remote_diversification.hpp"
#include "azureus_value_versions.hpp"
#include "../platform/utf8_path.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <utility>

namespace {
using namespace biglybtc;
namespace fs = std::filesystem;
namespace s = storage;

void check(bool condition, const char* message) {
  if (!condition) { throw std::runtime_error(message); }
}

template <class T>
T take(std::expected<T, s::Error> result) {
  if (!result) { throw std::runtime_error(result.error().message); }
  return std::move(*result);
}

void take(std::expected<void, s::Error> result) {
  if (!result) { throw std::runtime_error(result.error().message); }
}

void write(const fs::path& path, std::string_view bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  out.close();
  check(bool(out), "Cannot write test profile file");
}

std::string read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  check(bool(in), "Cannot read test profile file");
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

struct Profile {
  fs::path path, network;
  std::optional<s::ProfileLease> lease;

  Profile() {
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto parent = fs::canonical(fs::temp_directory_path()).make_preferred();
    unsigned attempt = 0;
    do {
      path = parent / platform::pathFromUtf8(
          "BiglyBTC-DHT-тест-" + std::to_string(tick) + '-' + std::to_string(++attempt));
    } while (!fs::create_directory(path));
    network = path / "dht" / "net4";
    fs::create_directories(network);
    lease.emplace(take(s::ProfileLease::acquire(path)));
  }

  ~Profile() {
    lease.reset();
    std::error_code error;
    fs::remove_all(path, error);
  }
};

void contactsRecoverAndReplace() {
  Profile profile;
  s::AzureusSavedContact contact{1234, 54, 4, {127, 0, 0, 1}, 4100};
  const auto pending = profile.network / "contacts.saving";
  const auto primary = profile.network / "contacts.dat";
  const auto original = take(s::encodeAzureusContactState(std::span(&contact, 1)));
  write(pending, original);
  auto contacts = take(s::AzureusContacts::open(*profile.lease, 4, 4));
  const auto recovered = take(contacts->snapshot());
  check(recovered->writable && recovered->contacts == std::vector{contact}
      && !fs::exists(pending) && read(primary) == original, "Pending contacts were not recovered exactly");

  contact.port = 4200;
  take(contacts->save(std::span(&contact, 1)));
  check(!fs::exists(pending) && take(s::decodeAzureusContactState(read(primary))) == std::vector{contact},
      "Contacts did not publish a replacement");
  contacts.reset();
  fs::create_hard_link(primary, pending);
  check(!s::AzureusContacts::open(*profile.lease, 4, 4) && fs::equivalent(primary, pending),
      "A shared contact candidate was accepted or retired");
}

void versionsRecoverAndReserve() {
  Profile profile;
  const auto primary = profile.network / "version.dat";
  const auto pending = profile.network / "version.saving";
  constexpr auto original = "d4:nexti2000e6:future3:rawe";
  write(pending, original);
  auto versions = take(s::AzureusValueVersions::open(*profile.lease, 4));
  check(!fs::exists(pending) && read(primary) == original, "Pending version map was not recovered");
  const auto reservation = s::originalAzureusVersionReservation(2000, 0);
  check(take(versions->next(0)) == reservation.first, "Unexpected first reserved version");
  const auto published = read(primary);
  check(published.find("6:future3:raw") != published.npos && !fs::exists(pending),
      "Version publication dropped unknown data");
  versions.reset();
  versions = take(s::AzureusValueVersions::open(*profile.lease, 4));
  check(take(versions->next(0)) == reservation.next, "Restart reused a reserved version");
  write(pending, original);
  check(!versions->next(0) && read(pending) == original, "Existing pending version candidate was overwritten");
}

void diversificationPreservesOtherRecords() {
  Profile profile;
  const auto versions = take(s::AzureusValueVersions::open(*profile.lease, 4));
  const auto primary = profile.network / "diverse.dat";
  write(primary, "d7:unknown4:keepe");
  core::AzureusRemoteDiversification remote;
  remote.key.fill(0xFA);
  remote.type = 3;
  remote.expiry = 12000;
  remote.offsets = {2, 7};
  take(s::saveAzureusRemoteDiversifications(*versions, std::span(&remote, 1)));
  core::AzureusLocalDiversification local;
  local.key.fill(0xAB);
  local.type = 2;
  local.expiry = 18000;
  take(s::saveAzureusLocalDiversifications(*versions, std::span(&local, 1)));
  check(take(s::readAzureusRemoteDiversifications(*versions, 6000)) == std::vector{remote} &&
      take(s::readAzureusLocalDiversifications(*versions, 6000)) == std::vector{local} &&
      read(primary).find("7:unknown4:keep") != std::string::npos, "Diversification merge lost original records");
}

void keyBlocksAndAddressesKeepIndependentPreimages() {
  Profile profile;
  const auto versions = take(s::AzureusValueVersions::open(*profile.lease, 4));
  const auto block = take(s::readAzureusKeyBlockArea(*versions));
  const auto addresses = take(s::readAzureusRecentAddressArea(*versions));
  constexpr auto map = "d7:entrieslee";
  const auto saved = take(s::saveAzureusKeyBlockArea(*versions, block, map));
  const auto old = take(s::saveAzureusRecentAddressArea(*versions, addresses, "de"));
  const auto newer = take(s::saveAzureusRecentAddressArea(*versions, old, "d1:ai1ee"));
  check(saved.primary() == std::optional<std::string>{map} && newer.primary() == std::optional<std::string>{"d1:ai1ee"},
      "Azureus storage areas did not publish expected files");
  check(!s::saveAzureusKeyBlockArea(*versions, block, "de") &&
      !s::saveAzureusRecentAddressArea(*versions, old, "de") &&
      read(profile.network / "block.dat") == map && read(profile.network / "addresses.dat") == "d1:ai1ee",
      "A stale Azureus storage capture replaced a more recent publication");
}
} // namespace

int main() {
  try {
    contactsRecoverAndReplace();
    versionsRecoverAndReserve();
    diversificationPreservesOtherRecords();
    keyBlocksAndAddressesKeepIndependentPreimages();
    std::cout << "4 Azureus profile storage families passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
