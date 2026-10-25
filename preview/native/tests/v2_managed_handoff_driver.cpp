#include "persistent_session.hpp"
#include "managed_payload_counters.hpp"
#include "original_torrent_identity_lt.hpp"
#include "original_layout_projection.hpp"
#include "network_policy.hpp"
#include "profile.hpp"
#include "biglybtc/library.hpp"
#include "owned_engine_fixture.hpp"
#include "biglybtc/torrent_layout.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <algorithm>
#include <chrono>
#include <charconv>
#include <cstring>
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
  if (argc != 8) {
    std::cerr << "Usage: v2_managed_handoff_driver WORK_ROOT PROFILE METADATA EXPECTED OUTPUT partial|complete|layout MISSING_REAL_PIECE\n";
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
    require(mode == "partial" || mode == "complete" || mode == "layout", "Invalid managed handoff mode");
    require(inside(root, output) && output.lexically_normal() == output && !fs::exists(output)
        && !fs::is_symlink(fs::symlink_status(output)), "Fresh output under owned root required");
    ownedPath(root, output.parent_path(), true);
    require(fs::create_directory(output), "Cannot allocate owned output directory");
    fs::permissions(output, fs::perms::owner_all);
    const bool partial = mode == "partial";
    int omitted=-1; const std::string omittedText(argv[7]);
    const auto integer=std::from_chars(omittedText.data(),omittedText.data()+omittedText.size(),omitted);
    require(integer.ec==std::errc{}&&integer.ptr==omittedText.data()+omittedText.size(),"Invalid explicit missing real piece");
    const auto metadataBytes = test::read(text(metadataPath));
    auto document = core::Document::parse(metadataBytes.toStdString()); require(document.has_value(), "Cannot parse generated metadata");
    auto layout = compat::originalLayout(*document); require(layout.has_value(), "Invalid original generated geometry");
    require(layout->bytes>0&&layout->bytes<=4*1024*1024&&layout->pieceLength==16384&&layout->files.size()<=32,
        "This bounded handoff covers small actual generated v2/hybrid metadata");
    ownedPath(root, expected, layout->multifile);
    lt::error_code error;
    auto metadata = std::make_shared<lt::torrent_info>(lt::span<const char>(metadataBytes.constData(), metadataBytes.size()), error, lt::from_span);
    require(!error&&metadata->info_hashes().has_v2()&&metadata->num_pieces()>1&&metadata->num_pieces()<=1024,
        "Actual bounded v2/hybrid fixture metadata required");
    const auto exactIdentity=storage::originalTorrentIdentityFromLibtorrent(*document,*metadata);
    require(exactIdentity.has_value(),"Actual LT hashes do not bind to exact original raw info");
    const auto infoOrder=storage::originalTorrentInfoOrder(*document);
    require(infoOrder.has_value(),"Metadata is neither byte canonical nor original UTF16 file tree");
    const auto projection=engine::detail::projectOriginalLayout(*layout,*metadata);
    require(projection.has_value(),"Original/engine geometry differs beyond a proven synthetic final pad");
    const int totalPieces=metadata->num_pieces();
    require(omitted>=0&&omitted<totalPieces,"Missing piece is outside actual metadata");
    bool omittedHasRealBytes=false;
    for(const auto& file:layout->files){if(!file.padding&&file.offset<(omitted+1)*layout->pieceLength
        &&file.offset+file.bytes>omitted*layout->pieceLength){omittedHasRealBytes=true;}}
    require(omittedHasRealBytes,"Partial seed hole must contain actual real payload");
    metadata->clear_trackers();

    QJsonArray originalFacts,engineFacts;
    const auto hexFact=[](std::string_view bytes){return QString::fromLatin1(QByteArray(bytes.data(),static_cast<qsizetype>(bytes.size())).toHex().toUpper());};
    for(std::size_t index=0;index<layout->files.size();++index){const auto& file=layout->files[index];const auto i=lt::file_index_t{static_cast<int>(index)};
      originalFacts.append(QJsonObject{{"index",static_cast<int>(index)},{"offset",QString::number(file.offset)},{"bytes",QString::number(file.bytes)},{"pad",file.padding},{"path",QString::fromStdString(file.path)}});
      engineFacts.append(QJsonObject{{"index",static_cast<int>(index)},{"offset",QString::number(metadata->files().file_offset(i))},{"bytes",QString::number(metadata->files().file_size(i))},{"pad",metadata->files().pad_file_at(i)},{"path",QString::fromStdString(metadata->files().file_path(i))}});
    }
    if(projection->trailingPad){const auto i=lt::file_index_t{static_cast<int>(*projection->trailingPad)};
      engineFacts.append(QJsonObject{{"index",static_cast<int>(*projection->trailingPad)},{"offset",QString::number(metadata->files().file_offset(i))},
        {"bytes",QString::number(metadata->files().file_size(i))},{"pad",metadata->files().pad_file_at(i)},{"path",QString::fromStdString(metadata->files().file_path(i))}});}
    if(mode=="layout"){
      const auto hashes=metadata->info_hashes();QJsonObject facts{{"status","OBSERVED_LAYOUT_ONLY"},{"original_files",originalFacts},{"engine_files",engineFacts},
        {"info_order",*infoOrder==storage::OriginalInfoOrder::ByteSorted?"bytes":"java-utf16-file-tree"},
        {"selected_hash",QString::fromStdString(exactIdentity->originalKey.hex())},
        {"engine_trailing_pad",projection->trailingPad.has_value()},{"original_total_bytes",QString::number(layout->bytes)},
        {"engine_total_bytes",QString::number(metadata->total_size())},
        {"full_v1",hashes.has_v1()?hexFact(hashes.v1.to_string()):QStringLiteral("-")},{"full_v2",hexFact(hashes.v2.to_string())},{"total_pieces",totalPieces}};
      QFile report(text(output/"result.json"));require(report.open(QIODevice::WriteOnly|QIODevice::NewOnly),"Cannot publish layout facts");
      const auto raw=QJsonDocument(facts).toJson(QJsonDocument::Indented);require(report.write(raw)==raw.size()&&report.flush(),"Cannot write layout facts");return 0;
    }
    const auto swarmBridge=engine::detail::managedPayloadCounterBridge();
    require(swarmBridge!=nullptr,"Actual handoff peers require the same approved counter/swarm provider");
    // Actual owner tests retain the exact original-to-engine range gate.
    for(std::size_t index=0;index<layout->files.size();++index){const auto& file=layout->files[index];const auto i=lt::file_index_t{static_cast<int>(index)};
      require(file.offset==metadata->files().file_offset(i)&&file.bytes==metadata->files().file_size(i)&&file.padding==metadata->files().pad_file_at(i),"Original/LT ranges differ before fixture writes");}
    const auto seedRoot = output / "seed";
    require(fs::create_directory(seedRoot), "Cannot allocate owned seed directory");
    for (std::size_t index = 0; index < layout->files.size(); ++index) {
      const auto& file = layout->files[index];
      if(file.padding){continue;} // Virtual original pads are never materialized.
      const fs::path relative = layout->multifile ? fs::path(layout->name) / file.path : fs::path(layout->name);
      require(!relative.is_absolute() && relative.lexically_normal() == relative, "Unsafe generated relative path");
      const auto wanted = layout->multifile ? expected / file.path : expected;
      ownedPath(root, wanted, false);
      auto data = test::read(text(wanted)); require(data.size() == file.bytes, "Generated expected length mismatch");
      if (partial) {
        const auto first = std::max(file.offset, omitted*layout->pieceLength);
        const auto last = std::min(file.offset + file.bytes, (omitted+1)*layout->pieceLength);
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
    require(engine::detail::selectManagedSwarm(seedOptions,*exactIdentity,swarmBridge),
        "Cannot bind actual seed to the original selected swarm");
    const auto seedHandle = seed->add_torrent(seedOptions, error);
    require(!error && seedHandle.is_valid(), "Cannot admit owned seed");
    const auto seedDeadline = Clock::now() + 30s;
    bool seedReady = false;
    do {
      const auto sampled = seedHandle.status();
      seedReady = seed->listen_port() != 0 && (partial
          ? sampled.state != lt::torrent_status::checking_files && sampled.state != lt::torrent_status::checking_resume_data
              && sampled.num_pieces == totalPieces-1 && !sampled.is_seeding
          : sampled.is_seeding);
      if (seedReady) { break; }
      std::this_thread::sleep_for(20ms);
    } while (Clock::now() < seedDeadline);
    require(seedReady, "Owned seed did not reach its actual piece boundary");
    auto lease = storage::ProfileLease::acquire(profile); require(lease.has_value(), "Cannot acquire generated profile lease");
    auto inventory=storage::ConfigFile::load(*lease,"downloads.config");require(inventory.has_value(),"Cannot read actual original inventory");
    auto library=core::readLibrary(inventory->document());require(library&&library->downloads.size()==1&&library->downloads.front().hash,
        "Exactly one saved original selected identity required");
    auto hash=library->downloads.front().hash;
    const auto key=hash->bytes();const auto hashes=metadata->info_hashes();
    require(key.size()==20&&((hashes.has_v1()&&std::memcmp(key.data(),hashes.v1.data(),20)==0)
        ||(hashes.has_v2()&&std::memcmp(key.data(),hashes.v2.data(),20)==0)),"Actual original selected key differs from metainfo");
    auto created = engine::PersistentSession::create({71, 72, partial ? 73U : 74U});
    require(created.has_value(), "Managed handoff requires the approved source-bound runtime");
    auto owner = std::move(*created);
    require(owner->configureNetwork(engine::NetworkPolicy{}).has_value(), "Cannot configure isolated managed owner");
    require(owner->startManaged(*lease, {.hash = *hash, .ownedLoopbackPeerPort = seed->listen_port(), .forceStart = true}).has_value(),
        "Managed admission failed");
    const auto reached = until(*owner, [partial,totalPieces](const auto& value) {
      return value.received > 0 && (partial ? value.pieces == totalPieces-1 && !value.complete : value.complete&&value.retained);
    }, 60s);
    std::int64_t uploadedToActualLeecher=0;
    if(!partial){
      const auto port=owner->tcpListenPort();require(port&&*port,"Retained owner has no actual numeric listener");
      const auto receiveRoot=output/"retained-leecher";require(fs::create_directory(receiveRoot),"Cannot create owned leecher root");
      // Actual zero-length entries stay present; no data or verified bits are
      // synthesized for nonempty files in this independent real receiver.
      for(const auto& file:layout->files){if(!file.padding&&file.bytes==0){const fs::path relative=layout->multifile?fs::path(layout->name)/file.path:fs::path(layout->name);test::write(text(receiveRoot/relative),{});}}
      lt::session leecher(test::localSettings());lt::add_torrent_params receive;receive.ti=metadata;receive.save_path=receiveRoot.string();
      receive.flags&=~(lt::torrent_flags::paused|lt::torrent_flags::auto_managed);
      receive.flags|=lt::torrent_flags::disable_dht|lt::torrent_flags::disable_lsd|lt::torrent_flags::disable_pex;
      require(engine::detail::selectManagedSwarm(receive,*exactIdentity,swarmBridge),
          "Cannot bind actual retained receiver to the original selected swarm");
      auto receiver=leecher.add_torrent(receive,error);require(!error&&receiver.is_valid(),"Cannot admit actual owned leecher");
      receiver.connect_peer(lt::tcp::endpoint(lt::address_v4::loopback(),*port));
      const auto deadline=Clock::now()+60s;bool downloaded=false;
      do{
        require(owner->poll().has_value(),"Retained seed owner poll failed");const auto current=status(*owner);const auto observed=receiver.status();
        if(observed.is_seeding&&observed.total_payload_download>0&&current.sent>reached.sent&&current.retained){uploadedToActualLeecher=observed.total_payload_download;downloaded=true;break;}
        std::this_thread::sleep_for(20ms);
      }while(Clock::now()<deadline);
      require(downloaded,"Retained actual native owner did not seed to independent owned receiver");
      for(const auto& file:layout->files){if(file.padding){continue;}const fs::path relative=layout->multifile?fs::path(layout->name)/file.path:fs::path(layout->name);
        const auto wanted=layout->multifile?expected/file.path:expected;require(test::read(text(receiveRoot/relative))==test::read(text(wanted)),"Actual retained leecher payload differs");}
    }
    require(owner->stopManaged(*hash).has_value(), "Cannot request managed retirement");
    const auto stopped = until(*owner, [](const auto& value) { return value.phase == engine::ManagedPhase::Stopped; }, 10s);
    require(stopped.pieces == (partial ? totalPieces-1 : totalPieces) && stopped.complete == !partial && stopped.received >= reached.received,
        "Stopped checkpoint differs from the actual reached boundary");
    const auto closed = owner->close(Clock::now() + 5s);
    require(closed.sessionJoined && closed.retirement.storageReleased && closed.retirement.peerCounters
        && !closed.retirement.accountingFailure, "Managed close has no joined/storage/counter receipt");
    require(closed.retirement.peerCounters->counters().payloadReceived >= stopped.received,
        "Absolute owner counters lost admission payload");
    owner.reset(); seed.reset(); // All actual owners are joined before Java reopens the profile.
    const auto hex=[](std::string_view bytes){return QString::fromLatin1(QByteArray(bytes.data(),static_cast<qsizetype>(bytes.size())).toHex().toUpper());};
    QJsonArray originalFiles,engineFiles;
    for(std::size_t index=0;index<layout->files.size();++index){const auto& file=layout->files[index];
      originalFiles.append(QJsonObject{{"index",static_cast<int>(index)},{"offset",QString::number(file.offset)},{"bytes",QString::number(file.bytes)},{"pad",file.padding},{"path",QString::fromStdString(file.path)}});
      const auto i=lt::file_index_t{static_cast<int>(index)};engineFiles.append(QJsonObject{{"index",static_cast<int>(index)},{"offset",QString::number(metadata->files().file_offset(i))},{"bytes",QString::number(metadata->files().file_size(i))},{"pad",metadata->files().pad_file_at(i)},{"path",QString::fromStdString(metadata->files().file_path(i))}});
    }
    if(projection->trailingPad){const auto i=lt::file_index_t{static_cast<int>(*projection->trailingPad)};
      engineFiles.append(QJsonObject{{"index",static_cast<int>(*projection->trailingPad)},{"offset",QString::number(metadata->files().file_offset(i))},
        {"bytes",QString::number(metadata->files().file_size(i))},{"pad",metadata->files().pad_file_at(i)},{"path",QString::fromStdString(metadata->files().file_path(i))}});}
    QJsonObject receipt{{QStringLiteral("schema"), 1},
        {"engine_trailing_pad",projection->trailingPad.has_value()},{"original_total_bytes",QString::number(layout->bytes)},
        {"engine_total_bytes",QString::number(metadata->total_size())},{"bytes",QString::number(stopped.bytes)},
        {"omitted_piece",omitted},{"original_files",originalFiles},{"engine_files",engineFiles},
        {"full_v1",hashes.has_v1()?hex(hashes.v1.to_string()):QStringLiteral("-")},{"full_v2",hex(hashes.v2.to_string())},
        {"retained_seed_received",QString::number(uploadedToActualLeecher)}, {QStringLiteral("mode"), QString::fromStdString(mode)},
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
