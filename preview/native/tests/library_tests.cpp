#include "biglybtc/library.hpp"

#include <iostream>
#include <stdexcept>

using namespace biglybtc::core;

namespace {
void require(bool condition) {
  if (!condition) { throw std::runtime_error("Saved library check failed"); }
}
std::string bytes(std::string_view value) { return std::to_string(value.size()) + ":" + std::string(value); }
std::string map(std::initializer_list<std::pair<std::string_view, std::string>> values) {
  std::string result = "d";
  for (const auto& [key, value] : values) { result += bytes(key) + value; }
  return result + "e";
}
std::string entry(std::int64_t state = 50) {
  return map({{"torrent", bytes("/tmp/generated.torrent")}, {"save_dir", bytes("/tmp/generated-ש")},
      {"save_file", bytes("generated.bin")}, {"torrent_hash", bytes(std::string(20, '\xff'))},
      {"state", "i" + std::to_string(state) + "e"}, {"forceStart", "i1e"}, {"persistent", "i1e"},
      {"downloaded", "i1099511627776e"}, {"uploaded", "i0e"}, {"completedbytes", "i131072e"},
      {"position", "i3e"}, {"unknown.future.plugin", "l4:keepe"}});
}
LibrarySnapshot read(std::string input) {
  auto document = Document::parse(std::move(input));
  require(document.has_value());
  auto snapshot = readLibrary(*document);
  require(snapshot.has_value());
  return std::move(*snapshot);
}
}

int main() {
  try {
    auto snapshot = read(map({{"downloads", "l" + entry() + "e"}, {"unknown", "4:data"}}));
    require(snapshot.downloads.size() == 1 && snapshot.issues.empty() && !snapshot.legacyMap);
    auto& current = snapshot.downloads.front();
    require(current.torrentFile == "/tmp/generated.torrent" && current.saveDirectory == "/tmp/generated-ש");
    require(current.saveFile == "generated.bin" && !current.legacySavePath);
    require(current.hash && current.hash->hex() == std::string(40, 'F'));
    require(current.startup == StartupState::Queued && current.originalState == 50 && current.forceStart && current.persistent);
    require(current.downloaded == 1099511627776LL && current.uploaded == 0 && current.completedBytes == 131072 && current.position == 3);
    // Snapshot strings/hashes own their values after the source document dies.
    for (const auto state : {0, 70, 75, 100}) {
      auto value = read(map({{"downloads", "l" + entry(state) + "e"}}));
      const auto expected = state == 0 ? StartupState::Waiting : state == 70 ? StartupState::Stopped : StartupState::Queued;
      require(value.downloads.front().startup == expected);
    }
    const auto legacyEntry = map({{"torrent", bytes("/tmp/old.torrent")}, {"path", bytes("/tmp/old.bin")},
        {"stopped", "i1e"}, {"startStopLocked", "i1e"}, {"persistent", "i0e"}});
    auto legacy = read(map({{"old-key", legacyEntry}}));
    require(legacy.legacyMap && legacy.downloads.size() == 1 && legacy.issues.empty());
    require(legacy.downloads.front().legacyKey == "old-key" && legacy.downloads.front().legacySavePath);
    require(!legacy.downloads.front().saveFile && !legacy.downloads.front().hash);
    require(!legacy.downloads.front().persistent && legacy.downloads.front().forceStart);
    require(legacy.downloads.front().startup == StartupState::Stopped);

    const auto firstHash = bytes(std::string(20, '\1'));
    const auto secondHash = bytes(std::string(32, '\2'));
    auto pauses = read(map({{"downloads", "le"}, {"pause_data", "l" + firstHash + map({{"hash", secondHash},
        {"force", "i1e"}, {"owner", "i7e"}}) + "e"}}));
    require(pauses.paused.size() == 2 && pauses.issues.empty());
    require(pauses.paused[0].owner == 1 && !pauses.paused[0].forceStart);
    require(pauses.paused[1].owner == 7 && pauses.paused[1].forceStart && pauses.paused[1].hash.bytes().size() == 32);
    auto brokenPauses = read(map({{"downloads", "le"}, {"pause_data", "li1e" + map({{"hash", firstHash}}) + "e"}}));
    require(brokenPauses.paused.empty() && brokenPauses.issues.size() == 2 && brokenPauses.issues[0].pauseEntry);

    for (const auto& invalid : {std::string("i1e"), map({{"torrent", bytes("/tmp/x")}, {"save_dir", bytes("/tmp")}}),
        map({{"torrent", bytes(std::string("/tmp/\0bad", 9))}, {"save_dir", bytes("/tmp")}, {"state", "i0e"}}),
        map({{"torrent", bytes("/tmp/x")}, {"save_dir", bytes("/tmp")}, {"state", "i0e"}, {"torrent_hash", "1:x"}})}) {
      auto partial = read(map({{"downloads", "l" + std::string(invalid) + entry() + "e"}}));
      require(partial.downloads.size() == 1 && partial.issues.size() == 1);
      require(partial.downloads[0].sourceIndex == 1 && partial.issues[0].sourceIndex == 0 && !partial.issues[0].pauseEntry);
    }
    auto document = Document::parse(map({{"downloads", "l" + entry() + entry() + "e"}}));
    const auto original = document->serialize();
    auto limited = readLibrary(*document, LibraryLimits{.downloads = 1});
    require(!limited && limited.error() == LibraryError::Limit && document->serialize() == original);
    auto pauseDoc = Document::parse(map({{"downloads", "le"}, {"pause_data", "l" + firstHash + "e"}}));
    limited = readLibrary(*pauseDoc, LibraryLimits{.paused = 0});
    require(!limited && limited.error() == LibraryError::Limit);
    auto root = Document::parse("le");
    require(!readLibrary(*root));
    root = Document::parse("d9:downloadsi1ee");
    require(!readLibrary(*root));
    require(!TorrentHash::parse("short"));
    require(TorrentHash::parse(std::string(20, '\0'))->hex() == std::string(40, '0'));
    require(TorrentHash::parse(std::string(32, '\xff'))->hex() == std::string(64, 'F'));
    std::cout << "PASS original/legacy inventory, owned paths/hashes, normalized startup, force/pause owners, issues and bounds\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
