#include "../adapters/torrent_intake.hpp"
#include "../adapters/torrent_state.hpp"
#include "biglybtc/file_policy.hpp"

#include <libtorrent/hasher.hpp>

#include <array>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

// Owned generated profiles only. Crash rows terminate only their own fork child.
// No engine, Java, live profile, payload allocation or network is involved.
namespace {
using namespace biglybtc;
namespace fs = std::filesystem;
using Code = storage::ErrorCode;
using Stage = storage::TorrentIntakeStage;
std::size_t assertions = 0, families = 0;
void check(bool value, const char* message) { ++assertions; if (!value) { throw std::runtime_error(message); } }
#define CHECK(...) check(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__)
template<class Function> void run(std::string_view name, Function&& function) {
  try { function(); ++families; }
  catch (const std::exception& error) { throw std::runtime_error(std::string(name) + ": " + error.what()); }
}
std::string b(std::string_view text) { return std::to_string(text.size()) + ':' + std::string(text); }
std::string info(bool multi, char pieces = 'h', std::string part = "second.bin") {
  std::string value = "d";
  if (multi) {
    value += "5:filesld6:lengthi1e4:pathl9:first.bineed6:lengthi1e4:pathl3:Sub" + b(part) + "eee";
  } else { value += "6:lengthi1e"; }
  return value + "4:name" + b(multi ? "bundle" : "sample.bin") + "12:piece lengthi16384e6:pieces" + b(std::string(20, pieces)) + 'e';
}
std::string torrent(bool multi = false, char variant = 'h', std::string part = "second.bin") {
  return "d10:attributesd6:future3:raw10:storetypesl1:Ce8:networksl3:Tore14:resumecompletei2ee"
      "4:info" + info(multi, variant, std::move(part)) + "6:opaque3:" + std::string("\0\xffQ", 3)
      + "6:resumed6:future3:raw4:datad6:futurei9e5:validi1eee" + 'e';
}
core::TorrentHash identity(bool multi = false, char variant = 'h') {
  const auto raw = info(multi, variant);
  const auto digest = libtorrent::hasher(raw.data(), static_cast<int>(raw.size())).final();
  auto hash = core::TorrentHash::parse(std::string_view(digest.data(), 20));
  CHECK(hash); return *hash;
}
void write(const fs::path& path, std::string_view bytes, mode_t mode = 0600) {
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, mode);
  CHECK(fd >= 0);
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto count = ::write(fd, bytes.data() + offset, bytes.size() - offset);
    if (count < 0 && errno == EINTR) { continue; }
    if (count <= 0) { ::close(fd); throw std::runtime_error("generated fixture write failed"); }
    offset += static_cast<std::size_t>(count);
  }
  CHECK(::close(fd) == 0);
}
std::string read(const fs::path& path) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  CHECK(fd >= 0);
  std::array<char, 8192> buffer{};
  std::string result;
  for (;;) {
    const auto count = ::read(fd, buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) { continue; }
    if (count < 0) { ::close(fd); throw std::runtime_error("generated fixture read failed"); }
    if (count == 0) { break; }
    result.append(buffer.data(), static_cast<std::size_t>(count));
  }
  CHECK(::close(fd) == 0); return result;
}
core::Document document(const fs::path& path) {
  auto result = core::Document::parse(read(path), storage::compatibleConfigLimits());
  CHECK(result); return std::move(*result);
}
std::string raw(const core::Document& source, const core::Node* value) {
  CHECK(value); return source.originalBytes().substr(value->begin, value->end - value->begin);
}
struct Area {
  fs::path root, profile, metadata, destination;
  std::optional<storage::ProfileLease> lease;
  explicit Area(bool multi = false) {
    char name[] = "/tmp/biglybtc-torrent-intake-XXXXXX";
    const auto* created = ::mkdtemp(name); CHECK(created);
    root = created; profile = root / "profile"; metadata = root / "input.torrent"; destination = root / "new" / "payload";
    CHECK(::mkdir(profile.c_str(), 0700) == 0);
    write(metadata, torrent(multi)); acquire();
  }
  ~Area() { lease.reset(); std::error_code ignored; fs::remove_all(root, ignored); }
  Area(const Area&) = delete;
  Area& operator=(const Area&) = delete;
  void acquire() { auto result = storage::ProfileLease::acquire(profile); CHECK(result); lease.emplace(std::move(*result)); }
  void reopen() { lease.reset(); acquire(); }
  fs::path state(bool multi = false, char variant = 'h') const { return profile / "active" / (identity(multi, variant).hex() + ".dat"); }
  fs::path archive(bool multi = false, char variant = 'h') const { return profile / "torrents" / (identity(multi, variant).hex() + ".torrent"); }
  void noPayload() const { CHECK(!fs::exists(root / "new")); }
  void noMutation() const {
    noPayload();
    CHECK(!fs::exists(profile / "active") && !fs::exists(profile / "torrents"));
    CHECK(!fs::exists(profile / "BiglyBTC.torrent-import") && !fs::exists(profile / "BiglyBTC.torrent-import.saving"));
  }
  core::LibrarySnapshot saved() const {
    auto source = document(profile / "downloads.config"); auto saved = core::readLibrary(source);
    CHECK(saved && saved->issues.empty() && !saved->legacyMap); return std::move(*saved);
  }
};
Stage crashStage = Stage::JournalSynced;
void crashAt(Stage stage) { if (stage == crashStage) { ::_exit(37); } }
fs::path editPath;
void editInventoryAtJournal(Stage stage) {
  if (stage == Stage::JournalSynced) { write(editPath, "d9:downloadsle6:futurei55ee"); throw std::runtime_error("intentional interruption"); }
}
void editStateAtPublished(Stage stage) {
  if (stage == Stage::StatePublished) { write(editPath, "d6:foreigni1ee"); throw std::runtime_error("intentional interruption"); }
}
} // namespace

int main() {
  try {
    run("single and multifile import creates only compatible metadata", [] {
      for (const bool multi : {false, true}) {
        Area area(multi);
        const auto original = read(area.metadata);
        struct stat inputBefore{}; CHECK(::stat(area.metadata.c_str(), &inputBefore) == 0);
        const auto result = storage::importTorrent(*area.lease, area.metadata, area.destination, multi);
        CHECK(result && *result == identity(multi));
        const auto saved = area.saved();
        CHECK(saved.downloads.size() == 1);
        const auto& entry = saved.downloads.front();
        // Original legacy encoding:0 means skipped; -1 decodes to selected
        // normal priority0. Check the published row through the actual shared
        // decoder for BOTH a one-file and a multi-file import.
        const auto inventory = document(area.profile / "downloads.config");
        const auto* rows = inventory.find("downloads"); CHECK(rows && rows->children.size() == 1);
        const auto& imported = rows->children.front();
        const std::size_t files = multi ? 2 : 1;
        const auto policies = compat::originalFilePolicy(imported, files);
        CHECK(policies && policies->size() == files);
        for (const auto& policy : *policies) { CHECK(!policy.skipped && policy.priority == 0); }
        const auto* priorities = imported.find("file_priorities");
        CHECK(priorities && priorities->type == core::Node::Type::List && priorities->children.size() == files);
        for (const auto& priority : priorities->children) {
          CHECK(priority.type == core::Node::Type::Integer && priority.integer == -1);
        }
        CHECK(entry.hash && *entry.hash == *result && entry.saveDirectory == area.destination && entry.saveFile == (multi ? "bundle" : "sample.bin"));
        CHECK(entry.torrentFile == area.archive(multi) && entry.persistent && !entry.forceStart);
        CHECK(entry.originalState == (multi ? 70 : 0) && entry.downloaded == 0 && entry.completedBytes == 0);
        CHECK(read(area.archive(multi)) == original && read(area.metadata) == original);
        struct stat inputAfter{}, archiveInfo{}, stateInfo{};
        CHECK(::stat(area.metadata.c_str(), &inputAfter) == 0 && inputBefore.st_ino == inputAfter.st_ino);
        CHECK(::stat(area.archive(multi).c_str(), &archiveInfo) == 0 && (archiveInfo.st_mode & 0777) == 0600 && archiveInfo.st_nlink == 1);
        CHECK(::stat(area.state(multi).c_str(), &stateInfo) == 0 && (stateInfo.st_mode & 0777) == 0600);
        const auto state = document(area.state(multi));
        const auto source = document(area.metadata);
        CHECK(raw(state, state.find("info")) == raw(source, source.find("info")));
        CHECK(raw(state, state.find("opaque")) == raw(source, source.find("opaque")));
        const auto* attributes = state.find("attributes");
        CHECK(attributes && attributes->find("future")->bytes == "raw");
        CHECK(attributes->find("networks")->children[0].bytes == "Public");
        CHECK(attributes->find("storetypes")->children.size() == (multi ? 2 : 1));
        for (const auto& type : attributes->find("storetypes")->children) { CHECK(type.bytes == "L"); }
        CHECK(attributes->find("resumecomplete")->integer == 1 && attributes->find("filelinks2")->children.empty());
        CHECK(state.find("resume")->find("future")->bytes == "raw");
        CHECK(state.find("resume")->find("data")->find("future")->integer == 9);
        CHECK(state.find("resume")->find("data")->find("valid")->integer == 0);
        auto store = storage::TorrentStateStore::open(*area.lease); CHECK(store);
        auto loaded = store->load(*area.lease, *result); CHECK(loaded && !loaded->recoveredBackup());
        CHECK(!fs::exists(area.profile / "downloads.config.bak"));
        CHECK(!fs::exists(area.profile / "BiglyBTC.torrent-import")); area.noPayload();
      }
    });
    run("append preserves original opaque rows roots pauses modes and backup", [] {
      Area area;
      const auto oldRow = "d12:torrent_hash20:" + std::string(20, 'x') + "7:torrent9:/unopened8:save_dir9:/unopened5:statei70e8:positioni3e6:futureli7e3:rawee";
      const std::string before = "d9:downloadsl" + oldRow + "e6:opaque3:" + std::string("\0\xffX", 3) + "10:pause_datalee";
      write(area.profile / "downloads.config", before, 0640);
      const auto old = document(area.profile / "downloads.config");
      CHECK(storage::importTorrent(*area.lease, area.metadata, area.destination, false));
      const auto after = document(area.profile / "downloads.config");
      CHECK(raw(after, &after.find("downloads")->children.front()) == raw(old, &old.find("downloads")->children.front()));
      CHECK(raw(after, after.find("opaque")) == raw(old, old.find("opaque")));
      CHECK(raw(after, after.find("pause_data")) == raw(old, old.find("pause_data")));
      CHECK(read(area.profile / "downloads.config.bak") == before);
      struct stat mode{}; CHECK(::stat((area.profile / "downloads.config").c_str(), &mode) == 0 && (mode.st_mode & 0777) == 0640);
      CHECK(area.saved().downloads.back().position == 4); area.noPayload();
    });
    run("duplicate imports are unchanged conflicts", [] {
      Area area;
      CHECK(storage::importTorrent(*area.lease, area.metadata, area.destination, false));
      const auto before = read(area.profile / "downloads.config");
      const auto state = read(area.state());
      const auto again = storage::importTorrent(*area.lease, area.metadata, area.destination, true);
      CHECK(!again && again.error().code == Code::Conflict);
      CHECK(read(area.profile / "downloads.config") == before && read(area.state()) == state);
      CHECK(!fs::exists(area.profile / "BiglyBTC.torrent-import")); area.noPayload();
    });
    run("all four durable interruption stages recover on a fresh lease", [] {
      for (const auto stage : {Stage::JournalSynced, Stage::StatePublished, Stage::ArchivePublished, Stage::InventoryPublished}) {
        Area area;
        const std::string before = "d9:downloadslee";
        write(area.profile / "downloads.config", before);
        crashStage = stage;
        const auto child = ::fork(); CHECK(child >= 0);
        if (child == 0) {
          const auto result = storage::importTorrent(*area.lease, area.metadata, area.destination, false, crashAt);
          ::_exit(result ? 0 : 90);
        }
        int status = 0;
        while (::waitpid(child, &status, 0) < 0) { if (errno != EINTR) { throw std::runtime_error("owned child wait failed"); } }
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 37);
        CHECK(fs::exists(area.profile / "BiglyBTC.torrent-import"));
        if (stage != Stage::InventoryPublished) { CHECK(read(area.profile / "downloads.config") == before); }
        area.reopen();
        const auto recovered = storage::recoverTorrentIntake(*area.lease);
        CHECK(recovered && recovered->has_value() && **recovered == identity());
        CHECK(area.saved().downloads.size() == 1);
        CHECK(read(area.profile / "downloads.config.bak") == before);
        CHECK(read(area.archive()) == read(area.metadata));
        CHECK(!fs::exists(area.profile / "BiglyBTC.torrent-import"));
        const auto idle = storage::recoverTorrentIntake(*area.lease); CHECK(idle && !idle->has_value()); area.noPayload();
      }
    });
    run("concurrent inventory change blocks recovery without overwriting", [] {
      Area area;
      write(area.profile / "downloads.config", "d9:downloadslee");
      editPath = area.profile / "downloads.config";
      CHECK(!storage::importTorrent(*area.lease, area.metadata, area.destination, false, editInventoryAtJournal));
      const auto foreign = read(editPath);
      area.reopen(); const auto result = storage::recoverTorrentIntake(*area.lease);
      CHECK(!result && result.error().code == Code::Conflict && read(editPath) == foreign);
      CHECK(fs::exists(area.profile / "BiglyBTC.torrent-import") && !fs::exists(area.profile / "active")); area.noPayload();
    });
    run("changed published state never admits a library row", [] {
      Area area;
      write(area.profile / "downloads.config", "d9:downloadslee"); editPath = area.state();
      CHECK(!storage::importTorrent(*area.lease, area.metadata, area.destination, false, editStateAtPublished));
      const auto foreign = read(editPath);
      const auto result = storage::recoverTorrentIntake(*area.lease);
      CHECK(!result && result.error().code == Code::Conflict);
      CHECK(read(editPath) == foreign && area.saved().downloads.empty()); area.noPayload();
    });
    run("invalid metadata paths hash overrides and ambiguous info reject before journal", [] {
      for (int kind = 0; kind < 4; ++kind) {
        Area area;
        if (kind == 0) { write(area.metadata, "d4:infodee"); }
        if (kind == 1) { write(area.metadata, torrent(true, 'h', "..")); }
        if (kind == 2) {
          auto data = torrent(); const auto at = data.find("d6:length"); CHECK(at != std::string::npos);
          data.insert(at + 1, "13:hash-override20:" + std::string(20, 'x')); write(area.metadata, data);
        }
        if (kind == 3) {
          auto data = torrent(); const auto at = data.find("6:lengthi1e"); CHECK(at != std::string::npos);
          data.insert(at, "6:lengthi2e"); write(area.metadata, data);
        }
        CHECK(!storage::importTorrent(*area.lease, area.metadata, area.destination, false)); area.noMutation();
      }
    });
    run("symlink metadata and destination are never followed", [] {
      Area area;
      const auto target = read(area.metadata);
      const auto link = area.root / "linked.torrent";
      CHECK(::symlink(area.metadata.c_str(), link.c_str()) == 0);
      CHECK(!storage::importTorrent(*area.lease, link, area.destination, false)); area.noMutation();
      CHECK(::symlink(area.root.c_str(), (area.root / "dest-link").c_str()) == 0);
      CHECK(!storage::importTorrent(*area.lease, area.metadata, area.root / "dest-link" / "payload", false)); area.noMutation();
      CHECK(read(area.metadata) == target);
    });
    run("existing state candidate regular symlink fifo and directory remain untouched", [] {
      for (int kind = 0; kind < 4; ++kind) {
        Area area; CHECK(::mkdir((area.profile / "active").c_str(), 0700) == 0);
        const auto candidate = area.state();
        if (kind == 0) { write(candidate, "foreign"); }
        if (kind == 1) { CHECK(::symlink(area.metadata.c_str(), candidate.c_str()) == 0); }
        if (kind == 2) { CHECK(::mkfifo(candidate.c_str(), 0600) == 0); }
        if (kind == 3) { CHECK(::mkdir(candidate.c_str(), 0700) == 0); }
        struct stat before{}; CHECK(::lstat(candidate.c_str(), &before) == 0);
        const auto result = storage::importTorrent(*area.lease, area.metadata, area.destination, false);
        CHECK(!result && result.error().code == Code::Conflict);
        struct stat after{}; CHECK(::lstat(candidate.c_str(), &after) == 0 && before.st_ino == after.st_ino && before.st_mode == after.st_mode);
        CHECK(!fs::exists(area.profile / "downloads.config") && !fs::exists(area.profile / "BiglyBTC.torrent-import"));
        if (kind == 0) { CHECK(read(candidate) == "foreign"); } area.noPayload();
      }
    });
    run("recovery candidates and malformed library are not promoted", [] {
      for (int kind = 0; kind < 3; ++kind) {
        Area area;
        const auto candidate = area.profile / (kind == 0 ? "downloads.config.bak" : kind == 1 ? "downloads.config.saving" : "downloads.config");
        write(candidate, kind == 2 ? "d9:downloadsi1ee" : "d9:downloadslee");
        const auto before = read(candidate);
        CHECK(!storage::importTorrent(*area.lease, area.metadata, area.destination, false));
        CHECK(read(candidate) == before); area.noMutation();
      }
    });
    run("no transaction and invalid lease are bounded", [] {
      Area area;
      const auto idle = storage::recoverTorrentIntake(*area.lease);
      CHECK(idle && !idle->has_value());
      auto moved = std::move(*area.lease);
      const auto result = storage::importTorrent(*area.lease, area.metadata, area.destination, false);
      CHECK(!result && result.error().code == Code::UnsafePath); area.noMutation();
      (void)moved;
    });
    run("force start is part of the same journaled row", [] {
      Area area;
      const auto imported = storage::importTorrent(*area.lease, area.metadata, area.destination, true, nullptr, true);
      CHECK(imported && *imported == identity());
      const auto saved = area.saved();
      CHECK(saved.downloads.size() == 1 && saved.downloads.front().forceStart);
      CHECK(saved.downloads.front().originalState == 0 && saved.downloads.front().startup == core::StartupState::Waiting);
      area.noPayload();
    });
    run("old version-one journal defaults missing force to false and rejects unknown extras", [] {
      for (int kind = 0; kind < 3; ++kind) {
        Area area;
        crashStage = Stage::JournalSynced;
        const auto child = ::fork(); CHECK(child >= 0);
        if (child == 0) {
          (void)storage::importTorrent(*area.lease, area.metadata, area.destination, false, crashAt);
          ::_exit(90);
        }
        int status = 0;
        while (::waitpid(child, &status, 0) < 0) { if (errno != EINTR) { throw std::runtime_error("owned child wait failed"); } }
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 37);
        const auto path = area.profile / "BiglyBTC.torrent-import";
        auto journal = read(path);
        const std::string field = "10:forceStarti0e";
        const auto position = journal.find(field); CHECK(position != std::string::npos);
        if (kind < 2) { journal.erase(position, field.size()); }
        if (kind == 1) { journal.insert(journal.size() - 1, "6:futurei1e"); }
        if (kind == 2) { journal.replace(position, field.size(), "10:forceStarti2e"); }
        write(path, journal);
        area.reopen(); const auto result = storage::recoverTorrentIntake(*area.lease);
        if (kind != 0) {
          CHECK(!result && read(path) == journal && !fs::exists(area.profile / "downloads.config"));
        } else {
          CHECK(result && result->has_value());
          CHECK(area.saved().downloads.size() == 1 && !area.saved().downloads.front().forceStart);
        }
        area.noPayload();
      }
    });
    std::cout << families << " torrent-intake families, " << assertions << " assertions passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
