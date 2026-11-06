#include "../engine/magnet_intake.hpp"
#include "../adapters/torrent_intake.hpp"
#include "../adapters/library_commands.hpp"

#include <libtorrent/hasher.hpp>
#include <libtorrent/torrent_flags.hpp>

#include <array>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

// Generated config/metainfo/owned temporary paths only. No session/network/Java.
namespace {
using namespace biglybtc;
namespace fs = std::filesystem;
namespace lt = libtorrent;
using Code = storage::ErrorCode;
std::size_t assertions = 0, families = 0;
void check(bool value, const char* message) { ++assertions; if (!value) { throw std::runtime_error(message); } }
#define CHECK(...) check(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__)
template<class Function> void run(std::string_view name, Function&& function) {
  try { function(); ++families; }
  catch (const std::exception& error) { throw std::runtime_error(std::string(name) + ": " + error.what()); }
}
std::string b(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
std::string metainfo(char variant = 'h') {
  return "d4:infod6:lengthi1e4:name8:magnet.x12:piece lengthi16384e6:pieces" + b(std::string(20, variant)) + "ee";
}
std::string hex(std::string_view bytes) {
  constexpr char alphabet[] = "0123456789abcdef";
  std::string result;
  for (const unsigned char c : bytes) { result += alphabet[c >> 4]; result += alphabet[c & 15]; }
  return result;
}
std::string uri() {
  const auto data = metainfo();
  const auto source = core::Document::parse(data); CHECK(source);
  const auto* info = source->find("info"); CHECK(info);
  const auto raw = data.substr(info->begin, info->end - info->begin);
  const auto hash = lt::hasher(raw.data(), static_cast<int>(raw.size())).final();
  return "magnet:?xt=urn:btih:" + hex(std::string_view(hash.data(), 20));
}
engine::NetworkPolicy normal() {
  auto value = engine::readNetworkPolicy(nullptr, {engine::NetworkMode::Normal}); CHECK(value); return *value;
}
void write(const fs::path& path, std::string_view bytes) {
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600); CHECK(fd >= 0);
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto count = ::write(fd, bytes.data() + offset, bytes.size() - offset);
    if (count < 0 && errno == EINTR) { continue; }
    if (count <= 0) { ::close(fd); throw std::runtime_error("fixture write failed"); }
    offset += static_cast<std::size_t>(count);
  }
  CHECK(::close(fd) == 0);
}
std::string read(const fs::path& path) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK); CHECK(fd >= 0);
  std::string result; std::array<char, 4096> buffer{};
  for (;;) {
    const auto count = ::read(fd, buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) { continue; }
    if (count < 0) { ::close(fd); throw std::runtime_error("fixture read failed"); }
    if (count == 0) { break; } result.append(buffer.data(), static_cast<std::size_t>(count));
  }
  CHECK(::close(fd) == 0); return result;
}
core::Document document(const fs::path& path) {
  auto result = core::Document::parse(read(path)); CHECK(result); return std::move(*result);
}
std::string raw(const core::Document& source, const core::Node* value) {
  CHECK(value); return source.originalBytes().substr(value->begin, value->end - value->begin);
}
struct Area {
  fs::path root, profile, destination, actual;
  std::optional<storage::ProfileLease> lease;
  Area() {
    char name[] = "/tmp/biglybtc-magnet-intake-XXXXXX";
    const auto* created = ::mkdtemp(name); CHECK(created); root = created;
    profile = root / "profile"; destination = root / "payload"; actual = root / "actual.torrent";
    CHECK(::mkdir(profile.c_str(), 0700) == 0); write(actual, metainfo());
    auto result = storage::ProfileLease::acquire(profile); CHECK(result); lease.emplace(std::move(*result));
  }
  ~Area() { lease.reset(); std::error_code ignored; fs::remove_all(root, ignored); }
  Area(const Area&) = delete; Area& operator=(const Area&) = delete;
  engine::PendingMagnet intent(std::string value = uri(), bool stopped = false) {
    auto result = engine::parseMagnetIntent(value, destination, stopped, normal()); CHECK(result); return std::move(*result);
  }
  engine::PendingMagnet queue(std::string value = uri(), bool stopped = false) {
    auto result = engine::queuePendingMagnet(*lease, intent(std::move(value), stopped), normal()); CHECK(result); return std::move(*result);
  }
  void noNormalDownload() {
    CHECK(!fs::exists(profile / "downloads.config") && !fs::exists(profile / "active") && !fs::exists(profile / "torrents"));
    CHECK(!fs::exists(destination));
  }
};
void stopAfterJournal(storage::TorrentIntakeStage stage) {
  if (stage == storage::TorrentIntakeStage::JournalSynced) { ::_exit(39); }
}
} // namespace

int main() {
  try {
    run("literal hex/base32 topics and exact full v2 binding", [] {
      const auto p = normal(); const fs::path destination("/owned/uncreated");
      const auto hexValue = engine::parseMagnetIntent("magnet:?xt=urn:btih:0123456789012345678901234567890123456789", destination, false, p);
      const auto b32Value = engine::parseMagnetIntent("magnet:?xt=urn:btih:AERUKZ4JAERUKZ4JAERUKZ4JAERUKZ4J", destination, false, p);
      CHECK(hexValue && b32Value && hexValue->requested == b32Value->requested && hexValue->hash == b32Value->hash);
      const auto v2 = engine::parseMagnetIntent("magnet:?xt=urn:btmh:1220" + std::string(64, 'a'), destination, false, p);
      CHECK(v2 && !v2->requested.has_v1() && v2->requested.has_v2() && v2->hash.bytes().size() == 20);
      const auto both = engine::parseMagnetIntent(hexValue->uri + "&xt=urn:btmh:1220" + std::string(64, 'b'), destination, false, p);
      CHECK(both && both->requested.has_v1() && both->requested.has_v2() && both->hash == hexValue->hash);
    });
    run("query sources are bounded and admitted through actual policy", [] {
      Area area;
      const auto intent = area.intent(uri() + "&dn=Actual+name&tr=https%3A%2F%2Ftracker.invalid%2Fa&ws=http%3A%2F%2Fseed.invalid%2Fdata&x.pe=127.0.0.1%3A12345&dht=router.invalid%3A6881");
      CHECK(intent.displayName == "Actual name" && intent.trackers.size() == 1 && intent.webSeeds.size() == 1);
      CHECK(intent.peers.size() == 1 && intent.peers[0].address().is_loopback() && intent.dhtNodes.size() == 1);
      auto params = engine::metadataOnlyParameters(intent, normal(), area.root / "quarantine"); CHECK(params);
      CHECK(!params->ti && params->info_hashes == intent.requested && params->save_path == area.root / "quarantine");
      CHECK(params->flags & lt::torrent_flags::upload_mode);
      CHECK(!(params->flags & lt::torrent_flags::stop_when_ready));
      CHECK(params->flags & lt::torrent_flags::default_dont_download);
      CHECK(!(params->flags & lt::torrent_flags::auto_managed) && !(params->flags & lt::torrent_flags::paused));
      CHECK(!fs::exists(area.root / "quarantine")); area.noNormalDownload();
    });
    run("malformed hashes alternate routes and unsupported selectors reject", [] {
      Area area;
      for (const auto& value : {"magnet:?xt=urn:btih:" + std::string(40, 'z'),
          "magnet:?xt=urn:btih:" + std::string(40, '0'), uri() + "&xt=urn:btih:" + std::string(40, 'a'),
          uri() + "&tr=https%3A%2F%2Fhidden.onion%2Fa", uri() + "&net=Tor", uri() + "&net=I2P",
          uri() + "&dn=%00", uri() + "&x.pe=example.invalid%3A80", uri() + "&so=1-2", uri() + "&xs=https%3A%2F%2Fmetadata.invalid%2Fa"}) {
        CHECK(!engine::parseMagnetIntent(value, area.destination, false, normal()));
      }
      area.noNormalDownload(); CHECK(!fs::exists(area.profile / "biglybt.config"));
    });
    run("Java-compatible pending map preserves opaque config and no normal entry", [] {
      Area area;
      const std::string before = "d6:opaque3:" + std::string("\0\xffX", 3) + 'e'; write(area.profile / "biglybt.config", before);
      auto intent = area.intent(uri() + "&x.pe=127.0.0.1%3A12345", true); intent.timeoutMillis = 432100; intent.addedMillis = 1234567;
      auto queued = engine::queuePendingMagnet(*area.lease, intent, normal()); CHECK(queued && !queued->sourceEntry.empty());
      auto doc = document(area.profile / "biglybt.config");
      const auto* map = doc.find("MagnetPlugin.active.magnets"); CHECK(map && map->children.size() == 2);
      const auto& row = map->children[1];
      CHECK(map->children[0].bytes.size() == 32 && row.find("hash")->bytes.size() == 20);
      CHECK(row.find("args")->bytes == std::string_view(intent.uri).substr(8));
      CHECK(row.find("timeout")->integer == 432100 && row.find("ss")->integer == 3 && row.find("added")->integer == 1234567);
      CHECK(row.find("sources")->children.size() == 1);
      CHECK(row.find("sources")->children[0].find("address")->bytes == std::string("\x7f\0\0\1", 4));
      CHECK(row.find("other_metadata")->find("BiglyBTC.pending.destination")->bytes == area.destination.native());
      auto beforeDoc = core::Document::parse(before); CHECK(beforeDoc);
      CHECK(raw(doc, doc.find("opaque")) == raw(*beforeDoc, beforeDoc->find("opaque")));
      area.lease.reset(); auto lease = storage::ProfileLease::acquire(area.profile); CHECK(lease); area.lease.emplace(std::move(*lease));
      const auto loaded = engine::readPendingMagnets(*area.lease, normal()); CHECK(loaded && loaded->size() == 1);
      CHECK(loaded->front().hash == intent.hash && loaded->front().stopped && loaded->front().timeoutMillis == 432100);
      CHECK(loaded->front().peers.size() == 1); area.noNormalDownload();
    });
    run("original legacy pending row retains force state and unknown fields", [] {
      Area area;
      const std::string key = "AERUKZ4JAERUKZ4JAERUKZ4JAERUKZ4J";
      const std::string legacyUri = "magnet:?xt=urn:btih:0123456789012345678901234567890123456789";
      auto legacyIntent = area.intent(legacyUri);
      const auto value = legacyIntent.hash.bytes();
      const auto row = "d4:hash" + b(std::string_view(reinterpret_cast<const char*>(value.data()), value.size()))
          + "4:args" + b(std::string_view(legacyUri).substr(8)) + "7:sourcesle7:timeouti600000e2:ssi1e4:tagsl5:Alphae6:futurei77ee";
      write(area.profile / "biglybt.config", "d" + b("MagnetPlugin.active.magnets") + "d" + b(key) + row + "ee");
      const auto loaded = engine::readPendingMagnets(*area.lease, normal()); CHECK(loaded && loaded->size() == 1);
      CHECK(!loaded->front().destination && loaded->front().forceStart && loaded->front().sourceEntry == row);
      const auto originalConfig = read(area.profile / "biglybt.config");
      CHECK(!engine::completePendingMagnet(*area.lease, loaded->front(), area.actual, normal()));
      CHECK(read(area.profile / "biglybt.config") == originalConfig);
      area.queue();
      const auto appended = document(area.profile / "biglybt.config");
      CHECK(raw(appended, appended.find("MagnetPlugin.active.magnets")->find(key)) == row);
      area.noNormalDownload();
    });
    run("duplicate pending and stale cancellation preserve actual state", [] {
      Area area; const auto queued = area.queue();
      const auto before = read(area.profile / "biglybt.config");
      CHECK(!engine::queuePendingMagnet(*area.lease, area.intent(), normal()));
      CHECK(read(area.profile / "biglybt.config") == before);
      auto changed = before; const auto at = changed.find("i600000e"); CHECK(at != std::string::npos); changed.replace(at, 8, "i500000e");
      write(area.profile / "biglybt.config", changed);
      const auto cancelled = engine::cancelPendingMagnet(*area.lease, queued);
      CHECK(!cancelled && cancelled.error().code == Code::Conflict && read(area.profile / "biglybt.config") == changed);
      area.noNormalDownload();
    });
    run("actual metadata completion imports once then retires pending", [] {
      Area area; const auto queued = area.queue(uri() + "&tr=https%3A%2F%2Ftracker.invalid%2Fa", true);
      const auto result = engine::completePendingMagnet(*area.lease, queued, area.actual, normal()); CHECK(result && *result == queued.hash);
      const auto loaded = engine::readPendingMagnets(*area.lease, normal()); CHECK(loaded && loaded->empty());
      auto inventory = document(area.profile / "downloads.config"); const auto saved = core::readLibrary(inventory);
      CHECK(saved && saved->issues.empty() && saved->downloads.size() == 1 && saved->downloads[0].hash == queued.hash);
      CHECK(saved->downloads[0].startup == core::StartupState::Stopped && saved->downloads[0].saveDirectory == area.destination);
      const auto archive = document(area.profile / "torrents" / (queued.hash.hex() + ".torrent"));
      CHECK(archive.find("announce")->bytes == "https://tracker.invalid/a");
      CHECK(archive.find("announce-list")->children.size() == 1);
      CHECK(!fs::exists(area.destination));
      CHECK(!engine::completePendingMagnet(*area.lease, queued, area.actual, normal()));
    });
    run("restart after ordinary import removes pending without duplicate row", [] {
      Area area; const auto queued = area.queue();
      CHECK(storage::importTorrent(*area.lease, area.actual, area.destination, false));
      const auto before = read(area.profile / "downloads.config");
      const auto result = engine::completePendingMagnet(*area.lease, queued, area.actual, normal()); CHECK(result && *result == queued.hash);
      CHECK(read(area.profile / "downloads.config") == before);
      const auto pending = engine::readPendingMagnets(*area.lease, normal()); CHECK(pending && pending->empty());
    });
    run("every requested hash must match before any normal publication", [] {
      for (const bool dual : {false, true}) {
        Area area; const auto queued = area.queue(dual ? uri() + "&xt=urn:btmh:1220" + std::string(64, 'a') : uri());
        if (!dual) { write(area.actual, metainfo('z')); }
        const auto before = read(area.profile / "biglybt.config");
        const auto result = engine::completePendingMagnet(*area.lease, queued, area.actual, normal());
        CHECK(!result && result.error().code == Code::Conflict);
        CHECK(read(area.profile / "biglybt.config") == before); area.noNormalDownload();
      }
    });
    run("explicit cancellation changes only pending config", [] {
      Area area; const auto queued = area.queue();
      CHECK(engine::cancelPendingMagnet(*area.lease, queued));
      const auto pending = engine::readPendingMagnets(*area.lease, normal()); CHECK(pending && pending->empty()); area.noNormalDownload();
      CHECK(!engine::cancelPendingMagnet(*area.lease, queued));
    });
    run("recovery disabled is respected and does not erase persisted intents", [] {
      Area area; area.queue();
      auto doc = document(area.profile / "biglybt.config");
      CHECK(doc.replaceValue("Plugin.Magnet URI Handler.MagnetPlugin.recover.magnets", "i0e"));
      write(area.profile / "biglybt.config", doc.serialize()); const auto before = read(area.profile / "biglybt.config");
      const auto pending = engine::readPendingMagnets(*area.lease, normal()); CHECK(pending && pending->empty());
      CHECK(!engine::queuePendingMagnet(*area.lease, area.intent(uri() + "&dn=Other"), normal()));
      CHECK(read(area.profile / "biglybt.config") == before); area.noNormalDownload();
    });
    run("forced pending roundtrip commits force and waiting atomically", [] {
      Area area;
      auto request = area.intent(); request.forceStart = true;
      const auto queued = engine::queuePendingMagnet(*area.lease, request, normal()); CHECK(queued && queued->forceStart && !queued->stopped);
      const auto loaded = engine::readPendingMagnets(*area.lease, normal());
      CHECK(loaded && loaded->size() == 1 && loaded->front().forceStart);
      const auto result = engine::completePendingMagnet(*area.lease, loaded->front(), area.actual, normal()); CHECK(result && *result == queued->hash);
      const auto inventory = document(area.profile / "downloads.config"); const auto saved = core::readLibrary(inventory);
      CHECK(saved && saved->downloads.size() == 1 && saved->downloads.front().forceStart);
      CHECK(saved->downloads.front().startup == core::StartupState::Waiting && saved->downloads.front().originalState == 0);
      const auto pending = engine::readPendingMagnets(*area.lease, normal()); CHECK(pending && pending->empty());
      CHECK(!fs::exists(area.destination));
    });
    run("forced import recovery never reapplies force over a subsequent user Stop", [] {
      Area area;
      auto request = area.intent(); request.forceStart = true;
      const auto queued = engine::queuePendingMagnet(*area.lease, request, normal()); CHECK(queued);
      const auto child = ::fork(); CHECK(child >= 0);
      if (child == 0) {
        (void)storage::importTorrent(*area.lease, area.actual, area.destination, false, stopAfterJournal, true);
        ::_exit(90);
      }
      int status = 0;
      while (::waitpid(child, &status, 0) < 0) { if (errno != EINTR) { throw std::runtime_error("owned child wait failed"); } }
      CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 39);
      CHECK(!fs::exists(area.profile / "downloads.config"));
      area.lease.reset(); auto lease = storage::ProfileLease::acquire(area.profile); CHECK(lease); area.lease.emplace(std::move(*lease));
      const auto recovered = storage::recoverTorrentIntake(*area.lease); CHECK(recovered && recovered->has_value() && **recovered == queued->hash);
      auto inventory = document(area.profile / "downloads.config"); auto saved = core::readLibrary(inventory);
      CHECK(saved && saved->downloads.size() == 1 && saved->downloads.front().forceStart
          && saved->downloads.front().startup == core::StartupState::Waiting);
      const auto stopped = storage::applyLibraryCommand(*area.lease, queued->hash, core::DownloadAction::Stop);
      CHECK(stopped && stopped->changed);
      const auto before = read(area.profile / "downloads.config");
      const auto completed = engine::completePendingMagnet(*area.lease, *queued, area.actual, normal()); CHECK(completed && *completed == queued->hash);
      CHECK(read(area.profile / "downloads.config") == before);
      inventory = document(area.profile / "downloads.config"); saved = core::readLibrary(inventory);
      CHECK(saved && saved->downloads.size() == 1 && !saved->downloads.front().forceStart
          && saved->downloads.front().startup == core::StartupState::Stopped);
      const auto pending = engine::readPendingMagnets(*area.lease, normal()); CHECK(pending && pending->empty());
      CHECK(!fs::exists(area.destination));
    });
    run("legacy destination attachment preserves original intent and raw unknown fields", [] {
      Area area; area.queue(uri(), true);
      auto config = document(area.profile / "biglybt.config");
      const auto* map = config.find("MagnetPlugin.active.magnets"); CHECK(map && map->children.size() == 2);
      const auto key = std::string(map->children[0].bytes);
      auto owned = core::Document::parse(raw(config, &map->children[1])); CHECK(owned);
      const std::string opaque = "d6:opaque3:" + std::string("\0\xffM", 3) + 'e';
      CHECK(owned->replaceValue("other_metadata", opaque));
      CHECK(owned->replaceValue("future", "l3:rawi-7ee"));
      CHECK(config.replaceValue("MagnetPlugin.active.magnets", "d" + b(key) + owned->serialize() + 'e'));
      write(area.profile / "biglybt.config", config.serialize());
      const auto legacy = engine::readPendingMagnets(*area.lease, normal()); CHECK(legacy && legacy->size() == 1);
      CHECK(!legacy->front().destination && legacy->front().stopped && !legacy->front().forceStart);
      const auto beforeRow = core::Document::parse(legacy->front().sourceEntry); CHECK(beforeRow);
      const auto attached = engine::attachPendingMagnetDestination(*area.lease, legacy->front(), area.destination, normal());
      CHECK(attached && attached->destination == area.destination && attached->stopped && !attached->forceStart);
      const auto afterRow = core::Document::parse(attached->sourceEntry); CHECK(afterRow);
      for (const auto& field : {"hash", "args", "sources", "timeout", "ss", "future"}) {
        CHECK(raw(*beforeRow, beforeRow->find(field)) == raw(*afterRow, afterRow->find(field)));
      }
      const auto* afterOther = afterRow->find("other_metadata"); CHECK(afterOther);
      CHECK(raw(*beforeRow, beforeRow->find("other_metadata")->find("opaque")) == raw(*afterRow, afterOther->find("opaque")));
      CHECK(afterOther->find("BiglyBTC.pending.version")->integer == 1);
      const auto primary = read(area.profile / "biglybt.config"), backup = read(area.profile / "biglybt.config.bak");
      CHECK(engine::attachPendingMagnetDestination(*area.lease, *attached, area.destination, normal()));
      CHECK(read(area.profile / "biglybt.config") == primary && read(area.profile / "biglybt.config.bak") == backup);
      const auto conflict = engine::attachPendingMagnetDestination(*area.lease, *attached, area.root / "other", normal());
      CHECK(!conflict && conflict.error().code == Code::Conflict);
      const auto stale = engine::attachPendingMagnetDestination(*area.lease, legacy->front(), area.destination, normal());
      CHECK(!stale && stale.error().code == Code::Conflict);
      CHECK(read(area.profile / "biglybt.config") == primary); area.noNormalDownload();
    });
    run("destination attachment rejects unsafe paths and unsupported metadata version", [] {
      Area area; area.queue();
      auto config = document(area.profile / "biglybt.config");
      const auto* map = config.find("MagnetPlugin.active.magnets"); CHECK(map && map->children.size() == 2);
      const auto key = std::string(map->children[0].bytes);
      auto owned = core::Document::parse(raw(config, &map->children[1])); CHECK(owned);
      CHECK(owned->replaceValue("other_metadata", "d" + b("BiglyBTC.pending.version") + "i2ee"));
      CHECK(config.replaceValue("MagnetPlugin.active.magnets", "d" + b(key) + owned->serialize() + 'e'));
      write(area.profile / "biglybt.config", config.serialize());
      const auto legacy = engine::readPendingMagnets(*area.lease, normal()); CHECK(legacy && legacy->size() == 1);
      const auto before = read(area.profile / "biglybt.config");
      CHECK(!engine::attachPendingMagnetDestination(*area.lease, legacy->front(), "relative", normal()));
      const auto unsupported = engine::attachPendingMagnetDestination(*area.lease, legacy->front(), area.destination, normal());
      CHECK(!unsupported && unsupported.error().code == Code::Unsupported);
      CHECK(read(area.profile / "biglybt.config") == before); area.noNormalDownload();
    });
    run("explicit I2P pending context survives recovery and journaled completion", [] {
      Area area;const auto network=normal();
      auto intent=engine::parseMagnetIntent(uri()+"&net=I2P",area.destination,true,network,engine::MagnetNetwork::I2p);CHECK(intent);
      CHECK(intent->network==engine::MagnetNetwork::I2p&&intent->networks==std::vector<std::string>{"I2P"});
      CHECK(!engine::parseMagnetIntent(intent->uri,area.destination,true,network)); // Ordinary Public entry stays strict.
      auto pending=engine::queuePendingMagnet(*area.lease,*intent,network,engine::MagnetNetwork::I2p);CHECK(pending);
      area.noNormalDownload();
      const auto saved=read(area.profile/"biglybt.config");auto recovered=engine::readPendingMagnets(*area.lease,network,engine::MagnetNetwork::I2p);CHECK(recovered&&recovered->size()==1);
      auto publicOnly=engine::readPendingMagnets(*area.lease,network);CHECK(publicOnly&&publicOnly->empty());CHECK(read(area.profile/"biglybt.config")==saved);
      CHECK(recovered->front().requested==pending->requested&&recovered->front().sourceEntry==pending->sourceEntry);
      auto tampered=*pending;tampered.uri=uri()+"&dn=forged&net=I2P";CHECK(!engine::validatePendingMagnet(*area.lease,tampered,network));
      CHECK(engine::validatePendingMagnet(*area.lease,*pending,network));
      auto parameters=engine::metadataOnlyParameters(*pending,network,area.root/"virtual-metadata");CHECK(parameters&&!parameters->ti&&parameters->peers.empty()&&parameters->url_seeds.empty());
      CHECK(parameters->flags&lt::torrent_flags::upload_mode);CHECK(parameters->flags&lt::torrent_flags::disable_dht);
      CHECK(!engine::parseMagnetIntent(uri()+"&net=I2P&x.pe=127.0.0.1:6881",area.destination,true,network,engine::MagnetNetwork::I2p));
      CHECK(!engine::parseMagnetIntent(uri()+"&net=Tor",area.destination,true,network,engine::MagnetNetwork::I2p));
      const auto completed=engine::completePendingMagnet(*area.lease,*pending,area.actual,network);CHECK(completed&&*completed==pending->hash);
      const auto state=document(area.profile/"active"/(pending->hash.hex()+".dat"));const auto* attrs=state.find("attributes");CHECK(attrs);
      const auto* networks=attrs->find("networks");CHECK(networks&&networks->type==core::Node::Type::List&&networks->children.size()==1&&networks->children.front().bytes=="I2P");
      const auto inventory=document(area.profile/"downloads.config");const auto library=core::readLibrary(inventory);CHECK(library&&library->downloads.size()==1&&library->downloads.front().startup==core::StartupState::Stopped);
      CHECK(!fs::exists(area.destination));
    });
    run("mixed pending original choice order is retained and cannot widen pure context", [] {
      Area area;const auto network=normal();const auto value=uri()+"&net=I2P&net=Public";
      const auto defaultPublic=engine::parseMagnetIntent(uri(),area.destination,false,network,engine::MagnetNetwork::PublicAndI2p);CHECK(defaultPublic&&defaultPublic->network==engine::MagnetNetwork::Public);
      const auto explicitPure=engine::parseMagnetIntent(uri()+"&net=I2P",area.destination,false,network,engine::MagnetNetwork::PublicAndI2p);CHECK(explicitPure&&explicitPure->network==engine::MagnetNetwork::I2p);
      auto intent=engine::parseMagnetIntent(value,area.destination,false,network,engine::MagnetNetwork::PublicAndI2p);CHECK(intent);
      CHECK((intent->networks==std::vector<std::string>{"I2P","Public"}));
      CHECK(!engine::parseMagnetIntent(value,area.destination,false,network,engine::MagnetNetwork::I2p));
      auto pending=engine::queuePendingMagnet(*area.lease,*intent,network,engine::MagnetNetwork::PublicAndI2p);CHECK(pending);
      const auto recovered=engine::readPendingMagnets(*area.lease,network,engine::MagnetNetwork::PublicAndI2p);CHECK(recovered&&recovered->size()==1&&recovered->front().networks==intent->networks);
      CHECK(engine::completePendingMagnet(*area.lease,*pending,area.actual,network));
      const auto state=document(area.profile/"active"/(pending->hash.hex()+".dat"));const auto* networks=state.find("attributes")->find("networks");
      CHECK(networks&&networks->children.size()==2&&networks->children[0].bytes=="Public"&&networks->children[1].bytes=="I2P"); // Original storage policy canonical pair, raw args retain input order.
    });
    run("trackerless private completion retains original decentralized marker and explicit sources", [] {
      for(const auto context:{engine::MagnetNetwork::I2p,engine::MagnetNetwork::PublicAndI2p}){
        for(bool inherited:{false,true}){
          Area area;const auto network=normal();const auto args=context==engine::MagnetNetwork::I2p?"&net=I2P":"&net=Public&net=I2P";
          auto intent=engine::parseMagnetIntent(uri()+args,area.destination,true,network,context);CHECK(intent);
          auto saved=engine::queuePendingMagnet(*area.lease,*intent,network,context);CHECK(saved);
          auto actual=document(area.actual);const auto info=raw(actual,actual.find("info"));
          if(inherited){CHECK(actual.replaceValue("attributes","d11:peersourcesl3:DHTe6:future4:keepe"));}
          CHECK(actual.replaceValue("future",b("untouched")));write(area.actual,actual.serialize());
          CHECK(engine::completePendingMagnet(*area.lease,*saved,area.actual,network));
          const auto state=document(area.profile/"active"/(saved->hash.hex()+".dat"));const auto archive=document(area.profile/"torrents"/(saved->hash.hex()+".torrent"));
          CHECK(raw(state,state.find("info"))==info&&raw(archive,archive.find("info"))==info);
          CHECK(state.find("announce")&&state.find("announce")->bytes=="dht://"+saved->hash.hex()+".dht/announce");
          CHECK(state.find("announce-list")->children.size()==1&&state.find("announce-list")->children.front().children.size()==1);
          CHECK(state.find("future")->bytes=="untouched");const auto* attrs=state.find("attributes");CHECK(attrs);
          const auto* sources=attrs->find("peersources");CHECK(sources&&sources->children.size()==1&&sources->children.front().bytes==(inherited?"DHT":"Tracker"));
          if(inherited){CHECK(attrs->find("future")->bytes=="keep");}CHECK(!fs::exists(area.destination));
        }
      }
    });
    run("I2P pending forced intent commits exact original force in ordinary import", [] {
      Area area;const auto network=normal();auto intent=engine::parseMagnetIntent(uri()+"&net=I2P",area.destination,false,network,engine::MagnetNetwork::I2p);CHECK(intent);intent->forceStart=true;
      auto saved=engine::queuePendingMagnet(*area.lease,*intent,network,engine::MagnetNetwork::I2p);CHECK(saved&&saved->forceStart&&!saved->stopped);
      auto original=core::Document::parse(saved->sourceEntry);CHECK(original&&original->find("ss")->integer==1);
      CHECK(engine::completePendingMagnet(*area.lease,*saved,area.actual,network));const auto inventory=document(area.profile/"downloads.config");const auto library=core::readLibrary(inventory);
      CHECK(library&&library->downloads.size()==1&&library->downloads.front().forceStart&&library->downloads.front().startup==core::StartupState::Waiting);
      const auto state=document(area.profile/"active"/(saved->hash.hex()+".dat"));CHECK(state.find("attributes")->find("networks")->children.front().bytes=="I2P");CHECK(!fs::exists(area.destination));
    });
    run("implicit pure args recover as original I2P without Native network context", [] {
      Area area;const auto network=normal();const auto submitted=uri()+"&dn=Original+name&future=%2Bopaque";
      const std::string opaque="d6:opaque3:"+std::string("\0\xffX",3)+'e';write(area.profile/"biglybt.config",opaque);
      auto input=engine::parseMagnetIntent(submitted,area.destination,false,network,engine::MagnetNetwork::I2p);CHECK(input);input->forceStart=true;
      auto queued=engine::queuePendingMagnet(*area.lease,*input,network,engine::MagnetNetwork::I2p);CHECK(queued);
      CHECK(queued->uri==submitted+"&net=I2P"&&queued->forceStart&&queued->network==engine::MagnetNetwork::I2p);
      CHECK(engine::validatePendingMagnet(*area.lease,*queued,network));
      auto config=document(area.profile/"biglybt.config");const auto* map=config.find("MagnetPlugin.active.magnets");CHECK(map&&map->children.size()==2);
      const auto& row=map->children[1];const auto* other=row.find("other_metadata");CHECK(other);
      CHECK(row.find("args")->bytes==std::string_view(queued->uri).substr(8));CHECK(other->find("BiglyBTC.pending.submitted_uri")->bytes==submitted);
      auto before=core::Document::parse(opaque);CHECK(before);CHECK(raw(config,config.find("opaque"))==raw(*before,before->find("opaque")));
      // Use only actual original-shaped args, with Public as the ordinary
      // default/capability ceiling. No Native context/metadata contributes.
      const auto recovered=engine::parseMagnetIntent("magnet:?"+std::string(row.find("args")->bytes),area.destination,false,network,engine::MagnetNetwork::PublicAndI2p);
      CHECK(recovered&&recovered->network==engine::MagnetNetwork::I2p&&recovered->networks==std::vector<std::string>{"I2P"});
      CHECK(!engine::parseMagnetIntent(recovered->uri,area.destination,false,network));
      const auto key=std::string(map->children[0].bytes);auto originalRow=core::Document::parse(raw(config,&row));CHECK(originalRow);CHECK(originalRow->replaceValue("other_metadata","de"));
      CHECK(config.replaceValue("MagnetPlugin.active.magnets","d"+b(key)+originalRow->serialize()+'e'));write(area.profile/"biglybt.config",config.serialize());
      const auto original=engine::readPendingMagnets(*area.lease,network,engine::MagnetNetwork::PublicAndI2p);CHECK(original&&original->size()==1&&original->front().network==engine::MagnetNetwork::I2p);
      const auto publicRows=engine::readPendingMagnets(*area.lease,network);CHECK(publicRows&&publicRows->empty());area.noNormalDownload();
    });
    run("explicit original network args and mixed no-net default remain byte exact", [] {
      for(int kind=0;kind<3;++kind){
        Area area;const auto network=normal();const auto submitted=uri()+(kind==0?"&net=I2P":kind==1?"&net=I2P&net=Public":"");
        const auto context=kind==0?engine::MagnetNetwork::I2p:engine::MagnetNetwork::PublicAndI2p;
        auto input=engine::parseMagnetIntent(submitted,area.destination,true,network,context);CHECK(input);
        auto queued=engine::queuePendingMagnet(*area.lease,*input,network,context);CHECK(queued&&queued->uri==submitted);
        const auto config=document(area.profile/"biglybt.config");const auto* map=config.find("MagnetPlugin.active.magnets");CHECK(map);const auto& row=map->children[1];
        CHECK(row.find("args")->bytes==std::string_view(submitted).substr(8));CHECK(!row.find("other_metadata")->find("BiglyBTC.pending.submitted_uri"));
        CHECK(queued->network==(kind==0?engine::MagnetNetwork::I2p:kind==1?engine::MagnetNetwork::PublicAndI2p:engine::MagnetNetwork::Public));
      }
    });
    run("legacy implicit pure attachment preserves unknowns and refuses reserved conflicts", [] {
      for(bool conflict:{false,true}){
        Area area;const auto network=normal();const auto submitted=uri()+"&dn=Keep+raw";
        auto input=engine::parseMagnetIntent(submitted,area.destination,true,network,engine::MagnetNetwork::I2p);CHECK(input);
        CHECK(engine::queuePendingMagnet(*area.lease,*input,network,engine::MagnetNetwork::I2p));
        auto config=document(area.profile/"biglybt.config");const auto* map=config.find("MagnetPlugin.active.magnets");CHECK(map);
        const auto key=std::string(map->children[0].bytes);auto row=core::Document::parse(raw(config,&map->children[1]));CHECK(row);
        CHECK(row->replaceValue("args",b(std::string_view(submitted).substr(8))));CHECK(row->replaceValue("future","l3:rawi-7ee"));
        std::string other="d"+b("BiglyBTC.pending.networks")+"l3:I2Pe"+b("opaque")+b(std::string("\0\xffM",3));
        if(conflict){other+=b("BiglyBTC.pending.submitted_uri")+b("foreign reserved bytes");}other+='e';CHECK(row->replaceValue("other_metadata",other));
        CHECK(config.replaceValue("MagnetPlugin.active.magnets","d"+b(key)+row->serialize()+'e'));const auto before=config.serialize();write(area.profile/"biglybt.config",before);
        const auto legacy=engine::readPendingMagnets(*area.lease,network,engine::MagnetNetwork::PublicAndI2p);
        if(conflict){CHECK(!legacy&&legacy.error().code==Code::Conflict);CHECK(read(area.profile/"biglybt.config")==before);continue;}
        CHECK(legacy&&legacy->size()==1&&!legacy->front().destination);
        // Edits own separate Node.origin buffers; raw offsets on the mutated
        // row are not relative to row.originalBytes(). Pin the actual persisted
        // pre-attachment bytes before comparing unknown values below.
        const auto beforeRow=core::Document::parse(legacy->front().sourceEntry);CHECK(beforeRow);
        CHECK(raw(*beforeRow,beforeRow->find("future"))=="l3:rawi-7ee");
        CHECK(raw(*beforeRow,beforeRow->find("other_metadata")->find("opaque"))==b(std::string("\0\xffM",3)));
        auto attached=engine::attachPendingMagnetDestination(*area.lease,legacy->front(),area.destination,network,engine::MagnetNetwork::I2p);CHECK(attached&&attached->uri==submitted+"&net=I2P");
        const auto after=core::Document::parse(attached->sourceEntry);CHECK(after);
        CHECK(raw(*after,after->find("future"))==raw(*beforeRow,beforeRow->find("future")));
        CHECK(raw(*after,after->find("other_metadata")->find("opaque"))==raw(*beforeRow,beforeRow->find("other_metadata")->find("opaque")));
        CHECK(after->find("other_metadata")->find("BiglyBTC.pending.submitted_uri")->bytes==submitted);
        const auto primary=read(area.profile/"biglybt.config"),backup=read(area.profile/"biglybt.config.bak");
        CHECK(engine::attachPendingMagnetDestination(*area.lease,*attached,area.destination,network,engine::MagnetNetwork::I2p));
        CHECK(read(area.profile/"biglybt.config")==primary&&read(area.profile/"biglybt.config.bak")==backup);area.noNormalDownload();
      }
    });
    run("fresh completion causality is false on errors duplicate and already-present recovery", [] {
      {Area area;const auto queued=area.queue(uri(),true);bool created=true;
        write(area.actual,metainfo('z'));CHECK(!engine::completePendingMagnet(*area.lease,queued,area.actual,normal(),&created));CHECK(!created);area.noNormalDownload();
        write(area.actual,metainfo());created=false;CHECK(engine::completePendingMagnet(*area.lease,queued,area.actual,normal(),&created));CHECK(created);
        const auto state=document(area.profile/"active"/(queued.hash.hex()+".dat"));const auto* p=state.find("attributes")->find("parameters");CHECK(p&&p->find("stats.download.added.time")&&p->find("stats.download.added.time")->integer>0);
        created=true;CHECK(!engine::completePendingMagnet(*area.lease,queued,area.actual,normal(),&created));CHECK(!created);
      }
      {Area area;const auto queued=area.queue();CHECK(storage::importTorrent(*area.lease,area.actual,area.destination,false));
        const auto before=read(area.profile/"downloads.config");bool created=true;CHECK(engine::completePendingMagnet(*area.lease,queued,area.actual,normal(),&created));CHECK(!created);CHECK(read(area.profile/"downloads.config")==before);
      }
    });
    std::cout << families << " magnet-intake families, " << assertions << " assertions passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
