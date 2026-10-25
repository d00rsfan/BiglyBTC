#include "persistent_session.hpp"
#include "network_policy.hpp"
#include "owned_engine_fixture.hpp"
#include "biglybtc/torrent_layout.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
#include <unistd.h>

namespace {
using namespace biglybtc;
using namespace std::chrono_literals;
namespace fs = std::filesystem;
namespace lt = libtorrent;
using Clock = std::chrono::steady_clock;
void require(bool value, const char* reason) { if (!value) { throw std::runtime_error(reason); } }
QString text(const fs::path& path) { return QString::fromStdString(path.string()); }
bool inside(const fs::path& root, const fs::path& path) {
  auto a = root.begin(), b = path.begin();
  for (; a != root.end(); ++a, ++b) { if (b == path.end() || *a != *b) { return false; } }
  return b != path.end();
}
void ownedPath(const fs::path& root, const fs::path& path, bool directory) {
  require(path.is_absolute() && fs::canonical(path) == path && inside(root, path), "Only canonical owned test inputs are accepted");
  struct stat info{};
  require(::lstat(path.c_str(), &info) == 0 && info.st_uid == ::geteuid()
      && (directory ? S_ISDIR(info.st_mode) : S_ISREG(info.st_mode) && info.st_nlink == 1 && info.st_size >= 0 && info.st_size <= 4 * 1024 * 1024), "Unsafe generated input type/owner");
}
engine::ManagedDownloadStatus status(engine::PersistentSession& owner) {
  const auto values = owner.managedDownloads();
  require(values.size() == 1, "Expected exactly one managed admission");
  if (values.front().failure) { throw std::runtime_error(values.front().failure->message); }
  return values.front();
}
template<class Predicate> engine::ManagedDownloadStatus until(engine::PersistentSession& owner, Predicate reached,
    std::chrono::seconds budget) {
  const auto deadline = Clock::now() + budget;
  do {
    require(owner.poll().has_value(), "Managed owner poll failed");
    const auto value = status(owner);
    if (reached(value)) { return value; }
    std::this_thread::sleep_for(20ms);
  } while (Clock::now() < deadline);
  throw std::runtime_error("Managed generated handoff deadline expired");
}
} // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  if (argc != 7) {
    std::cerr << "Usage: managed_handoff_driver WORK_ROOT PROFILE METADATA EXPECTED OUTPUT partial|complete\n";
    return 1;
  }
  try {
    const fs::path root(argv[1]), profile(argv[2]), metadataPath(argv[3]), expected(argv[4]), output(argv[5]);
    const std::string mode(argv[6]);
    require(root.parent_path() == fs::path("/tmp") && root.filename().string().starts_with("biglybtc-managed-handoff-")
        && fs::canonical(root) == root, "Fresh generated /tmp handoff root required");
    struct stat rootInfo{};
    require(::lstat(root.c_str(), &rootInfo) == 0 && S_ISDIR(rootInfo.st_mode) && rootInfo.st_uid == ::geteuid()
        && (rootInfo.st_mode & 077) == 0, "Owned private test root required");
    ownedPath(root, profile, true); ownedPath(root, metadataPath, false);
    require(mode == "partial" || mode == "complete", "Invalid managed handoff mode");
    require(inside(root, output) && output.lexically_normal() == output && !fs::exists(output)
        && !fs::is_symlink(fs::symlink_status(output)), "Fresh output under owned root required");
    ownedPath(root, output.parent_path(), true);
    require(fs::create_directory(output), "Cannot allocate owned output directory");
    fs::permissions(output, fs::perms::owner_all);
    const bool partial = mode == "partial";
    const auto metadataBytes = test::read(text(metadataPath));
    auto document = core::Document::parse(metadataBytes.toStdString()); require(document.has_value(), "Cannot parse generated metadata");
    auto layout = compat::originalLayout(*document); require(layout.has_value(), "Invalid original generated geometry");
    require(layout->bytes == 131072 && layout->pieceLength == 16384 && layout->files.size() <= 8,
        "This bounded handoff covers the existing 128KiB v1 fixtures only");
    ownedPath(root, expected, layout->multifile);
    lt::error_code error;
    auto metadata = std::make_shared<lt::torrent_info>(lt::span<const char>(metadataBytes.constData(), metadataBytes.size()), error, lt::from_span);
    require(!error && metadata->info_hashes().has_v1() && !metadata->info_hashes().has_v2() && metadata->num_pieces() == 8, "V1 fixture metadata required");
    metadata->clear_trackers();
    auto hash = core::TorrentHash::parse(metadata->info_hashes().v1.to_string()); require(hash.has_value(), "Invalid generated hash");
    const auto seedRoot = output / "seed";
    require(fs::create_directory(seedRoot), "Cannot allocate owned seed directory");
    for (std::size_t index = 0; index < layout->files.size(); ++index) {
      const auto& file = layout->files[index];
      require(!file.padding, "V1 linear generated fixtures must not create physical padding");
      const fs::path relative = layout->multifile ? fs::path(layout->name) / file.path : fs::path(layout->name);
      require(!relative.is_absolute() && relative.lexically_normal() == relative, "Unsafe generated relative path");
      const auto wanted = layout->multifile ? expected / file.path : expected;
      ownedPath(root, wanted, false);
      auto data = test::read(text(wanted)); require(data.size() == file.bytes, "Generated expected length mismatch");
      if (partial) {
        const auto first = std::max(file.offset, layout->bytes - layout->pieceLength);
        const auto last = std::min(file.offset + file.bytes, layout->bytes);
        if (last > first) {
          std::fill(data.begin() + static_cast<qsizetype>(first - file.offset),
              data.begin() + static_cast<qsizetype>(last - file.offset), '\0');
        }
      }
      test::write(text(seedRoot / relative), data);
    }
    auto seed = std::make_unique<lt::session>(test::localSettings());
    lt::add_torrent_params seedOptions; seedOptions.ti = metadata; seedOptions.save_path = seedRoot.string();
    seedOptions.flags &= ~lt::torrent_flags::paused; seedOptions.flags &= ~lt::torrent_flags::auto_managed;
    seedOptions.flags |= lt::torrent_flags::disable_dht | lt::torrent_flags::disable_lsd | lt::torrent_flags::disable_pex;
    const auto seedHandle = seed->add_torrent(seedOptions, error);
    require(!error && seedHandle.is_valid(), "Cannot admit owned seed");
    const auto seedDeadline = Clock::now() + 30s;
    bool seedReady = false;
    do {
      const auto sampled = seedHandle.status();
      seedReady = seed->listen_port() != 0 && (partial
          ? sampled.state != lt::torrent_status::checking_files && sampled.state != lt::torrent_status::checking_resume_data
              && sampled.num_pieces == 7 && !sampled.is_seeding
          : sampled.is_seeding);
      if (seedReady) { break; }
      std::this_thread::sleep_for(20ms);
    } while (Clock::now() < seedDeadline);
    require(seedReady, "Owned seed did not reach its actual piece boundary");
    auto lease = storage::ProfileLease::acquire(profile); require(lease.has_value(), "Cannot acquire generated profile lease");
    auto created = engine::PersistentSession::create({71, 72, partial ? 73U : 74U});
    require(created.has_value(), "Managed handoff requires the approved source-bound runtime");
    auto owner = std::move(*created);
    require(owner->configureNetwork(engine::NetworkPolicy{}).has_value(), "Cannot configure isolated managed owner");
    require(owner->startManaged(*lease, {.hash = *hash, .ownedLoopbackPeerPort = seed->listen_port(), .forceStart = true}).has_value(),
        "Managed admission failed");
    const auto reached = until(*owner, [partial](const auto& value) {
      return value.received > 0 && (partial ? value.pieces == 7 && !value.complete : value.complete);
    }, 60s);
    require(owner->stopManaged(*hash).has_value(), "Cannot request managed retirement");
    const auto stopped = until(*owner, [](const auto& value) { return value.phase == engine::ManagedPhase::Stopped; }, 10s);
    require(stopped.pieces == (partial ? 7 : 8) && stopped.complete == !partial && stopped.received >= reached.received,
        "Stopped checkpoint differs from the actual reached boundary");
    const auto closed = owner->close(Clock::now() + 5s);
    require(closed.sessionJoined && closed.retirement.storageReleased && closed.retirement.peerCounters
        && !closed.retirement.accountingFailure, "Managed close has no joined/storage/counter receipt");
    require(closed.retirement.peerCounters->counters().payloadReceived >= stopped.received,
        "Absolute owner counters lost admission payload");
    owner.reset(); seed.reset(); // All actual owners are joined before Java reopens the profile.
    QJsonObject receipt{{QStringLiteral("schema"), 1}, {QStringLiteral("mode"), QString::fromStdString(mode)},
        {QStringLiteral("hash"), QString::fromStdString(hash->hex())}, {QStringLiteral("pieces"), stopped.pieces},
        {QStringLiteral("total_pieces"), stopped.totalPieces}, {QStringLiteral("complete"), stopped.complete},
        {QStringLiteral("received"), QString::number(stopped.received)}, {QStringLiteral("sent"), QString::number(stopped.sent)},
        {QStringLiteral("completed"), QString::number(stopped.completed)},
        {QStringLiteral("session_joined"), closed.sessionJoined}, {QStringLiteral("storage_released"), closed.retirement.storageReleased},
        {QStringLiteral("settled_payload_received"), QString::number(closed.retirement.peerCounters->counters().payloadReceived)}};
    QFile report(text(output / "result.json"));
    require(report.open(QIODevice::WriteOnly | QIODevice::NewOnly), "Cannot publish fresh handoff receipt");
    const auto data = QJsonDocument(receipt).toJson(QJsonDocument::Indented);
    require(report.write(data) == data.size() && report.flush(), "Cannot write handoff receipt");
    std::cout << "MANAGED_HANDOFF_STOPPED " << mode << " pieces=" << stopped.pieces << " received=" << stopped.received << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 2;
  }
}
