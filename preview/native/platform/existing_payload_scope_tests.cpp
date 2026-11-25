// SPDX-License-Identifier: GPL-2.0-or-later
#include "intake_destination_snapshot.hpp"
#include "original_payload_io.hpp"
#include "selected_existing_scope.hpp"
#include "state_file_io.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
namespace storage = biglybtc::storage;
namespace compat = biglybtc::compat;
namespace core = biglybtc::core;
namespace io = storage::detail;

void require(bool ok, const char* message) {
  if (!ok) { throw std::runtime_error(message); }
}

template<class T> T take(std::expected<T, storage::Error> result) {
  if (!result) { throw std::runtime_error(result.error().message); }
  return std::move(*result);
}

struct Fixture {
  fs::path root;

  Fixture() {
    fs::path name(u8"biglybtc-scope-\u03bb-");
    name += std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    root = fs::temp_directory_path() / name;
    require(fs::create_directory(root), "Cannot create generated scope directory");
    root = fs::canonical(root);
  }

  ~Fixture() {
    std::error_code error;
    fs::remove_all(root, error);
  }
};

void write(const fs::path& path, std::string_view bytes) {
  std::ofstream output(path, std::ios::binary);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  require(output.good(), "Cannot write generated payload or control");
}

void exerciseScopes() {
  Fixture fixture;
  const auto profile = fixture.root / "profile";
  require(fs::create_directory(profile), "Cannot create generated profile");
  auto lease = take(storage::ProfileLease::acquire(profile));
  const auto destination = fixture.root / fs::path(u8"\u03bb-storage");
  const auto leaf = std::string("payload-\xce\xbb");
  const auto path = destination / fs::path(u8"payload-\u03bb");
  const std::array paths{path};
  const auto torrent = core::Document::parse("d4:infod4:name7:payload6:pieces20:" + std::string(20, 'A') + "ee");
  require(torrent.has_value(), "Cannot decode generated metadata");
  const compat::OriginalLayout layout{leaf, false, 16384, 7, {{leaf, 0, 7, false}}};
  const auto hash = core::TorrentHash::parse(std::string(20, 'A'));
  require(hash.has_value(), "Cannot decode generated selected key");
  std::array<std::byte, 20> full;
  full.fill(std::byte{0x41});
  const storage::OriginalTorrentIdentity identity{*hash, storage::OriginalSelectedHash::V1, {full, {}}};

  const auto snapshot = [&] {
    return take(storage::captureIntakeDestinationSnapshot(*torrent, layout, *hash, paths));
  };
  const auto absentAncestor = snapshot();
  require(absentAncestor == snapshot() && !fs::exists(destination), "Observing missing paths created a directory");
  require(fs::create_directory(destination), "Cannot create generated destination directory");
  const auto absentLeaf = snapshot();
  require(absentAncestor != absentLeaf && !fs::exists(path), "The first missing component did not affect the snapshot");
  write(path, "payload");
  const auto observed = snapshot();
  require(observed != absentLeaf && observed == snapshot(), "Existing leaf was not observed consistently");

  const auto scope = take(storage::VerifiedExistingScope::capture(lease, *torrent, layout, identity, destination, leaf));
  const auto encoded = core::Document::parse(scope.encoded());
  require(encoded.has_value(), "Cannot decode saved full scope");
  const auto restored = take(storage::VerifiedExistingScope::decode(encoded->root()));
  require(restored.validate(lease, *torrent, layout, identity, destination, leaf).has_value(),
      "Restored scope did not accept the same payload");
  auto parent = take(io::openStatePathDirectory(destination));
  {
    const auto file = take(io::openOriginalPayloadAt(parent.get(), path.filename(), io::OriginalPayloadAccess::ReadWrite));
    require(!restored.bind(0, path, file.get()), "Read-only scope accepted a writable descriptor");
  }
  {
    const auto file = take(io::openOriginalPayloadAt(parent.get(), path.filename(), io::OriginalPayloadAccess::ReadOnly));
    const auto proof = take(restored.bind(0, path, file.get()));
    require(proof().has_value(), "Validated read-only descriptor failed its captured proof");
  }

  const std::array policies{compat::FilePolicy{false, 0}};
  const std::array kinds{storage::OriginalStorageKind::Compact};
  const auto control = profile / "active" / hash->hex() / "fmfile0.dat";
  require(fs::create_directories(control.parent_path()), "Cannot create generated control directory");
  constexpr std::string_view controlBytes = "d6:lengthi7e7:versioni0ee";
  write(control, controlBytes);
  const auto captureSelected = [&] {
    return take(storage::SelectedExistingScope::capture(lease, *torrent, layout, identity, policies, kinds, paths));
  };
  const auto validateSelected = [&](const storage::SelectedExistingScope& selected) {
    return selected.validate(lease, *torrent, layout, identity, policies, kinds, paths);
  };
  const auto selected = captureSelected();
  require(validateSelected(selected).has_value(), "Selected scope did not accept its original compact control");
  fs::rename(control, control.parent_path() / "detached-control");
  write(control, controlBytes);
  require(!validateSelected(selected), "Selected scope accepted a same-byte replacement control");
  const auto replacementControl = captureSelected();

  fs::rename(path, destination / "detached-payload");
  write(path, "payload");
  require(!restored.validate(lease, *torrent, layout, identity, destination, leaf)
      && !validateSelected(replacementControl) && observed != snapshot(),
      "Selected and full scopes must reject same-byte replacement payloads");
}
} // namespace

int main() {
  try {
    exerciseScopes();
    std::cout << "Unicode scope paths, read-only proof and payload/control replacements passed\n";
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
