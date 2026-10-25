#pragma once

#include "../adapters/original_torrent_identity_lt.hpp"
#include "../adapters/torrent_intake.hpp"

#include <libtorrent/bencode.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/settings_pack.hpp>

#include <array>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace biglybtc::test::selected_swarm {
namespace fs = std::filesystem;
namespace lt = libtorrent;
inline void require(bool good, const char* message) {
  if (!good) { throw std::runtime_error(message); }
}
template<class T> T take(std::expected<T, storage::Error> result) {
  if (!result) { throw std::runtime_error(result.error().message); }
  return std::move(*result);
}
inline void write(const fs::path& path, std::string_view bytes) {
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
  require(fd >= 0, "Cannot create owned selected-swarm fixture");
  std::size_t at = 0;
  while (at < bytes.size()) {
    const auto count = ::write(fd, bytes.data() + at, bytes.size() - at);
    if (count < 0 && errno == EINTR) { continue; }
    if (count <= 0) { ::close(fd); throw std::runtime_error("Fixture write failed"); }
    at += static_cast<std::size_t>(count);
  }
  require(::close(fd) == 0, "Fixture close failed");
}
inline std::string read(const fs::path& path) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  require(fd >= 0, "Cannot read owned selected-swarm fixture");
  std::string bytes;
  std::array<char, 4096> buffer{};
  for (;;) {
    const auto count = ::read(fd, buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) { continue; }
    if (count < 0 || bytes.size() > 1024 * 1024) {
      ::close(fd); throw std::runtime_error("Fixture read failed or exceeded bound");
    }
    if (!count) { break; }
    bytes.append(buffer.data(), static_cast<std::size_t>(count));
  }
  require(::close(fd) == 0, "Fixture read close failed");
  return bytes;
}
inline lt::settings_pack isolated() {
  lt::settings_pack settings;
  settings.set_str(lt::settings_pack::listen_interfaces, "");
  settings.set_str(lt::settings_pack::outgoing_interfaces, "127.0.0.1");
  for (const auto key : {lt::settings_pack::enable_dht, lt::settings_pack::enable_lsd,
      lt::settings_pack::enable_upnp, lt::settings_pack::enable_natpmp,
      lt::settings_pack::enable_incoming_tcp, lt::settings_pack::enable_outgoing_tcp,
      lt::settings_pack::enable_incoming_utp, lt::settings_pack::enable_outgoing_utp}) {
    settings.set_bool(key, false);
  }
  return settings;
}
// Real create_torrent hashes, generated data, no network or borrowed profile.
// Both formats share an aligned real file, so geometry cannot hide a swarm bug.
inline std::string metainfo(bool v2) {
  const std::string payload(16384, 's');
  lt::file_storage files; files.add_file("selected.bin", payload.size());
  lt::create_torrent creator(files, 16384);
  creator.set_hash(lt::piece_index_t{0}, lt::hasher(payload.data(), static_cast<int>(payload.size())).final());
  creator.set_hash2(lt::file_index_t{0}, lt::piece_index_t::diff_type{0},
      lt::hasher256(payload.data(), static_cast<int>(payload.size())).final());
  auto entry = creator.generate();
  entry["azureus_private_properties"]["hybrid_hash_v2"] = v2 ? 1 : 0;
  entry["future.root"] = "opaque-selected-swarm";
  std::vector<char> bytes; lt::bencode(std::back_inserter(bytes), entry);
  return {bytes.begin(), bytes.end()};
}
struct Fixture final {
  fs::path root, profile, input, destination, state;
  std::optional<storage::ProfileLease> lease;
  std::optional<core::TorrentHash> key;
  std::optional<storage::OriginalTorrentIdentity> identity;
  std::string source, rawInfo, initialState, initialInventory;
  explicit Fixture(bool v2) {
    char pattern[] = "/tmp/biglybtc-selected-admission-XXXXXX";
    const auto* made = ::mkdtemp(pattern); require(made, "Cannot create private fixture root");
    root = made; profile = root / "profile"; input = root / "input.torrent";
    destination = root / "payload";
    require(::mkdir(profile.c_str(), 0700) == 0, "Cannot create private profile");
    lease.emplace(take(storage::ProfileLease::acquire(profile)));
    source = metainfo(v2); write(input, source);
    auto parsed = core::Document::parse(source, storage::compatibleConfigLimits());
    require(parsed.has_value(), "Generated source is not bencode");
    lt::error_code error;
    const lt::torrent_info metadata(lt::span<const char>(source.data(), source.size()), error, lt::from_span);
    require(!error, "Generated actual hybrid metadata was rejected");
    identity.emplace(take(storage::originalTorrentIdentityFromLibtorrent(*parsed, metadata)));
    rawInfo = std::string(take(storage::originalTorrentInfoSubject(*parsed)));
    key.emplace(take(storage::importTorrent(*lease, input, destination, true)));
    require(*key == identity->originalKey, "Intake changed original selected key");
    state = profile / "active" / (key->hex() + ".dat");
    initialState = read(state); initialInventory = read(profile / "downloads.config");
    require(!fs::exists(destination), "Fixture unexpectedly prepared payload");
  }
  ~Fixture() {
    lease.reset(); std::error_code ignored; fs::remove_all(root, ignored);
  }
  bool untouched() const {
    return read(state) == initialState && read(profile / "downloads.config") == initialInventory
        && !fs::exists(destination) && !fs::exists(state.string() + ".saving")
        && !fs::exists(profile / "downloads.config.saving");
  }
};
} // namespace biglybtc::test::selected_swarm
