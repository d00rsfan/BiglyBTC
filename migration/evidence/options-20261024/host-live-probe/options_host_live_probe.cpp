#include "session_bridge.hpp"
#include "profile.hpp"
#include "biglybtc/bencode.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

#include <arpa/inet.h>
#include <cerrno>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
using namespace biglybtc;
using Id = core::SettingId;
namespace fs = std::filesystem;
void require(bool value, const char* reason) { if (!value) { throw std::runtime_error(reason); } }
QString text(const fs::path& path) { return QString::fromStdString(path.native()); }
QByteArray read(const fs::path& path) {
  QFile file(text(path)); require(file.open(QIODevice::ReadOnly), "Cannot read owned fixture"); return file.readAll();
}
void write(const fs::path& path, const QByteArray& bytes) {
  QFile file(text(path)); require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Cannot write owned fixture");
  require(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
      && file.write(bytes) == bytes.size() && file.flush(), "Cannot finish owned fixture write");
}
QString sha(const QByteArray& bytes) { return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()); }
std::string string(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
struct BoundPeerPort {
  int descriptor = -1;
  unsigned short port = 0;
  BoundPeerPort() {
    descriptor = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    require(descriptor >= 0, "Cannot allocate owned numeric loopback socket");
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t size = sizeof(address);
    if (::bind(descriptor, reinterpret_cast<const sockaddr*>(&address), size) != 0 || ::listen(descriptor, 1) != 0
        || ::getsockname(descriptor, reinterpret_cast<sockaddr*>(&address), &size) != 0) {
      ::close(descriptor); descriptor = -1; throw std::runtime_error("Cannot bind owned numeric loopback socket");
    }
    port = ntohs(address.sin_port); require(port != 0, "Owned listener has no actual port");
  }
  ~BoundPeerPort() { if (descriptor >= 0) { ::close(descriptor); } }
  void requireNoConnection() const {
    const int accepted = ::accept4(descriptor, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (accepted >= 0) { ::close(accepted); throw std::runtime_error("Empty-profile probe unexpectedly connected to its dummy peer"); }
    require(errno == EAGAIN || errno == EWOULDBLOCK, "Cannot inspect owned listener queue");
  }
};
void wait(const std::function<bool()>& complete, int milliseconds, const char* reason) {
  QElapsedTimer clock; clock.start();
  while (!complete()) {
    require(clock.elapsed() < milliseconds, reason);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1); // Bounded scheduling wait, never an input clock or invented sample.
  }
}
std::string opaque(const QByteArray& bytes) {
  auto document = core::Document::parse(bytes.toStdString()); require(document.has_value(), "Saved configuration is not a complete dictionary");
  const auto* value = document->find("unknown"); require(value, "Unknown original configuration field disappeared");
  return std::string(document->originalBytes().substr(value->begin, value->end - value->begin));
}
const core::SettingValue& value(const ui::OptionsPresentation& packet, Id id) {
  require(packet && packet->snapshot && ui::validOptionsValues(*packet->snapshot), "Incomplete actual Options snapshot");
  for (const auto& field : packet->snapshot->fields) {
    if (field.id == id && field.value) { return *field.value; }
  }
  throw std::runtime_error("Actual Options snapshot omitted the requested setting");
}
void requireRates(const ui::NativeSessionPresentation& packet, quint64 session, qint64 upload, qint64 download) {
  require(packet && packet->session == session && packet->sample && packet->epoch && packet->applicationRun,
      "No actual source-qualified native owner observation");
  require(packet->rateLimits.upload.state == core::RateLimitState::Limited
      && packet->rateLimits.upload.bytesPerSecond == upload
      && packet->rateLimits.download.state == core::RateLimitState::Limited
      && packet->rateLimits.download.bytesPerSecond == download,
      "Actual get_settings-derived rates differ from saved live limits");
  require(packet->rateLimits.connections.proxy == core::SessionProxyMode::Disabled,
      "Pending proxy settings unexpectedly changed the isolated active owner");
}
QJsonObject observed(const ui::NativeSessionPresentation& packet) {
  require(packet && packet->sample, "Cannot report an unobserved native sample");
  return {{"sequence", QString::number(packet->sequence)}, {"epoch", QString::number(packet->epoch)},
      {"upload_bytes_per_second", QString::number(packet->rateLimits.upload.bytesPerSecond)},
      {"download_bytes_per_second", QString::number(packet->rateLimits.download.bytesPerSecond)},
      {"proxy_mode", static_cast<int>(packet->rateLimits.connections.proxy)},
      {"owner_elapsed_ns", QString::number(packet->ownerElapsedNanoseconds)},
      {"actual_payload_received", QString::number(packet->sample->counters.payloadReceived)},
      {"actual_payload_sent", QString::number(packet->sample->counters.payloadSent)}};
}
}

int main(int argc, char** argv) {
  QApplication application(argc, argv);
  fs::path work;
  bool created = false;
  try {
    require(argc == 2, "Usage: options-host-live-probe /tmp/biglybtc-options-host-live-UNIQUE");
    work = fs::path(argv[1]);
    require(work.is_absolute() && work.parent_path() == fs::path("/tmp") && work.lexically_normal() == work
        && work.filename().native().starts_with("biglybtc-options-host-live-")
        && !fs::exists(work) && !fs::is_symlink(fs::symlink_status(work)), "A fresh owned /tmp probe root is required");
    require(::mkdir(work.c_str(), 0700) == 0, "Cannot create owned probe root"); created = true;
    const auto profile = work / "profile"; require(::mkdir(profile.c_str(), 0700) == 0, "Cannot create owned profile");
    struct stat identity{}; require(::stat(profile.c_str(), &identity) == 0, "Cannot identify owned profile");
    const QByteArray inventory("d9:downloadslee"); write(profile / "downloads.config", inventory);
    std::string configuration = "d" + string("unknown") + "li7e3:rawe"
        + string("Max Upload Speed KBs") + "i17e" + string("Max Download Speed KBs") + "i19e"
        + string("max active torrents") + "i4e" + string("max downloads") + "i4e"
        + string("long.term.stats.enable") + "i0e"
        + string("TCP.Listen.Port") + "i18001e" + string("UDP.Listen.Port.Enable") + "i0e"
        + string("peercontrol.udp.public.enable") + "i0e"
        + string("Peer Source Selection Default.Tracker") + "i0e"
        + string("Tracker Client Enable TCP") + "i0e" + string("Server Enable UDP") + "i0e"
        + string("Peer Source Selection Default.DHT") + "i0e" + string("Plugin.DHT.dht.enabled") + "i0e"
        + string("Peer Source Selection Default.PeerExchange") + "i0e"
        + string("Bind IP") + string("127.0.0.1") + 'e';
    const auto config = profile / "biglybt.config";
    write(config, QByteArray::fromStdString(configuration)); const auto original = read(config);
    require(opaque(original) == "li7e3:rawe", "Generated opaque fixture is malformed");
    BoundPeerPort endpoint;
    quint64 session = 0;
    bool closed = false, joined = false, released = false, closeFailed = false;
    quint64 closedSession = 0;
    std::size_t progress = 0;
    std::vector<ui::OptionsPresentation> reads;
    std::vector<ui::OptionsSavePresentation> saves;
    ui::NativeSessionPresentation latest;
    QString packetFailure;
    QJsonArray actual;
    app::SessionBridge bridge; // Destroy/join before every captured signal destination.
    QObject::connect(&bridge, &app::SessionBridge::columnLayoutSessionReady, &application, [&](quint64 value) { session = value; });
    QObject::connect(&bridge, &app::SessionBridge::optionsReady, &application, [&](auto packet) { if (reads.size() < 16) { reads.push_back(std::move(packet)); } });
    QObject::connect(&bridge, &app::SessionBridge::optionsSaved, &application, [&](auto packet) { if (saves.size() < 16) { saves.push_back(std::move(packet)); } });
    QObject::connect(&bridge, &app::SessionBridge::nativeSessionReady, &application, [&](auto packet) {
      if (!packet || !ui::acceptNativeSession(latest, packet, packet->session)) { packetFailure = QStringLiteral("Invalid/regressing actual native owner packet"); return; }
      if (packet->state == ui::NativeSessionState::Uncertified) { packetFailure = QStringLiteral("Actual owner became uncertified"); }
      latest = std::move(packet);
    });
    QObject::connect(&bridge, &app::SessionBridge::progressReady, &application, [&](auto) { ++progress; });
    QObject::connect(&bridge, &app::SessionBridge::sessionCloseFailed, &application, [&] { closeFailed = true; });
    QObject::connect(&bridge, &app::SessionBridge::sessionClosed, &application, [&](quint64 value, bool didJoin, bool didRelease) {
      closedSession = value; joined = didJoin; released = didRelease; closed = true;
    });
    require(bridge.open(profile, endpoint.port), "Bridge rejected owned idle profile admission");
    wait([&] { return session && !bridge.busy() && latest && latest->sample && latest->state == ui::NativeSessionState::Idle; },
        15000, "No actual idle sample: approved source-bound runtime is mandatory; unbound fallback is not a pass");
    require(packetFailure.isEmpty(), "Actual native packet admission failed");
    const auto applicationRun = latest->applicationRun, epoch = latest->epoch;
    require(latest->rateLimits.connections.proxy == core::SessionProxyMode::Disabled, "Initial isolated owner unexpectedly uses a proxy");
    actual.append(observed(latest));
    require(read(config) == original && read(profile / "downloads.config") == inventory, "Opening the owned idle profile changed configuration/inventory");
    quint64 request = 0;
    const auto load = [&]() -> ui::OptionsPresentation {
      wait([&] { return !bridge.busy(); }, 10000, "Bridge did not become idle before Options read");
      const auto count = reads.size(); const auto key = ++request;
      require(bridge.loadOptions({session, key}), "Bridge rejected actual Options read");
      wait([&] { return reads.size() == count + 1 && !bridge.busy(); }, 10000, "Actual Options read did not finish");
      const auto packet = reads.back();
      require(packet && packet->session == session && packet->request == key && packet->revision
          && packet->snapshot && packet->failure.isEmpty() && ui::validOptionsValues(*packet->snapshot), "Actual Options read failed");
      return packet;
    };
    const auto save = [&](const ui::OptionsPresentation& baseline, std::vector<core::SettingChange> changes) -> ui::OptionsSavePresentation {
      wait([&] { return !bridge.busy(); }, 10000, "Bridge did not become idle before Options save");
      const auto count = saves.size(); const auto key = ++request;
      require(bridge.saveOptions({session, key, baseline->revision, std::move(changes)}), "Bridge rejected actual Options save dispatch");
      wait([&] { return saves.size() == count + 1 && !bridge.busy(); }, 10000, "Actual Options save did not finish");
      const auto packet = saves.back();
      require(packet && packet->session == session && packet->request == key && packet->revision == baseline->revision,
          "Actual Options acknowledgement lost original request/revision identity");
      return packet;
    };
    const auto checkSaved = [&](const ui::OptionsSavePresentation& result) {
      if (!result->saved) { throw std::runtime_error("Actual Options save failed: " + result->failure.toStdString()); }
      require(result->snapshot && result->snapshot->session == session && result->snapshot->request == result->request
          && result->snapshot->revision > result->revision && result->snapshot->snapshot
          && ui::validOptionsValues(*result->snapshot->snapshot), "Save has no fresh actual committed snapshot");
      require(opaque(read(config)) == "li7e3:rawe", "Sparse save changed an opaque original field");
    };
    const auto awaitRates = [&](quint64 after, qint64 upload, qint64 download) {
      wait([&] { return latest && latest->sequence > after && latest->sample
          && latest->rateLimits.upload.bytesPerSecond == upload && latest->rateLimits.download.bytesPerSecond == download; },
          10000, "Actual owner did not publish the applied live rates");
      require(packetFailure.isEmpty(), "Actual native packet became invalid/uncertified");
      require(latest->applicationRun == applicationRun && latest->epoch == epoch && latest->session == session,
          "Options replaced the actual engine owner instead of updating it");
      requireRates(latest, session, upload, download); actual.append(observed(latest));
    };
    auto baseline = load();
    require(std::get<std::int64_t>(value(baseline, Id::UploadLimit)) == 17, "Original desired rate was not read through actual Options");
    require(read(config) == original, "Options read materialized defaults");
    auto sequence = latest->sequence;
    auto first = save(baseline, {{Id::UploadLimit, std::int64_t{17}}, {Id::DownloadLimit, std::int64_t{19}},
        {Id::MaxActive, std::int64_t{5}}, {Id::MaxDownloads, std::int64_t{3}}});
    checkSaved(first); awaitRates(sequence, 17 * 1024, 19 * 1024);
    baseline = first->snapshot; sequence = latest->sequence;
    auto pending = save(baseline, {{Id::UploadLimit, std::int64_t{23}}, {Id::DownloadLimit, std::int64_t{29}},
        {Id::MaxActive, std::int64_t{6}}, {Id::MaxDownloads, std::int64_t{4}},
        {Id::BindIp, std::string{}}, {Id::TcpPort, std::int64_t{18002}},
        {Id::ProxyEnabled, true}, {Id::ProxySocks, true}, {Id::ProxyHost, std::string("127.0.0.1")},
        {Id::ProxyPort, std::string("9")}, {Id::PeerProxyVersion, std::string("V5")},
        {Id::PeerProxyEnabled, true}, {Id::PeerProxySame, true}});
    checkSaved(pending); require(pending->restartRequired, "Pending bind/proxy change lost its restart requirement");
    awaitRates(sequence, 23 * 1024, 29 * 1024);
    require(std::get<std::int64_t>(value(pending->snapshot, Id::MaxActive)) == 6
        && std::get<std::int64_t>(value(pending->snapshot, Id::MaxDownloads)) == 4, "Saved queue caps differ from actual committed Options snapshot");
    baseline = load(); require(baseline->restartRequired, "Reload lost saved-versus-active routing restart state");
    require(std::get<std::string>(value(baseline, Id::BindIp)).empty()
        && std::get<bool>(value(baseline, Id::PeerProxyEnabled)), "Reload lost pending bind/proxy settings");
    sequence = latest->sequence;
    auto cumulative = save(baseline, {{Id::MaxActive, std::int64_t{7}}}); checkSaved(cumulative);
    require(cumulative->restartRequired, "Later live-only save incorrectly cleared the pending routing restart requirement");
    awaitRates(sequence, 23 * 1024, 29 * 1024);
    require(std::get<std::int64_t>(value(cumulative->snapshot, Id::MaxActive)) == 7, "Later saved queue cap is missing");
    baseline = cumulative->snapshot;
    const auto beforeConflict = read(config); auto external = beforeConflict;
    require(external.count("7:unknownli7e3:rawe") == 1, "Owned CAS fixture lost its exact opaque marker");
    external.replace("7:unknownli7e3:rawe", "7:unknownli9e3:rawe"); write(config, external);
    sequence = latest->sequence;
    auto conflict = save(baseline, {{Id::UploadLimit, std::int64_t{99}}});
    require(!conflict->saved && !conflict->failure.isEmpty(), "Changed original baseline did not reject the save");
    require(read(config) == external && opaque(read(config)) == "li9e3:rawe", "Rejected save overwrote external configuration bytes");
    awaitRates(sequence, 23 * 1024, 29 * 1024);
    require(progress == 0 && read(profile / "downloads.config") == inventory, "Options created payload progress or changed empty inventory");
    for (const auto* name : {"active", "torrents", "payload", "stats"}) {
      require(!fs::exists(profile / name), "Empty-profile probe created torrent/payload/history storage");
    }
    endpoint.requireNoConnection();
    require(bridge.closeSession(), "Bridge rejected explicit idle owner close");
    wait([&] { return closed || closeFailed; }, 10000, "No actual joined-close acknowledgement");
    require(!closeFailed && closedSession == session && joined && released, "Explicit close did not join/release the real owner");
    require(latest && latest->state == ui::NativeSessionState::Closed && latest->sample
        && packetFailure.isEmpty(), "Joined close has no actual closed native observation");
    requireRates(latest, session, 23 * 1024, 29 * 1024);
    actual.append(observed(latest));
    auto held = storage::ProfileLease::acquire(profile); require(held && held->valid(), "Joined close retained the profile lease");
    struct stat after{}; require(::fstat(held->directoryHandle(), &after) == 0
        && after.st_dev == identity.st_dev && after.st_ino == identity.st_ino, "Joined close changed profile identity");
    require(read(config) == external && read(profile / "downloads.config") == inventory, "Close rewrote configuration or empty inventory");
    endpoint.requireNoConnection();
    QJsonObject report{{"status", "PASS_ACTUAL_OPTIONS_HOST_LIVE"}, {"source_commit", "c191e73cb0feffbd4a61f1bef67df7f5dcb8d621"},
        {"profile", text(profile)}, {"owner_epoch", QString::number(epoch)}, {"application_run", QString::number(applicationRun)},
        {"actual_observations", actual}, {"options_reads", static_cast<int>(reads.size())}, {"options_saves", static_cast<int>(saves.size())},
        {"pending_restart_retained", true}, {"cas_rejection_preserved_effective_rates", true}, {"original_unknown_retained_on_success", true},
        {"original_config_sha256", sha(original)}, {"external_config_sha256", sha(external)},
        {"joined", joined}, {"storage_released", released}, {"lease_reacquired", true}, {"payload_operations", 0},
        {"scope", "Actual Bridge source-qualified idle owner, effective rate/proxy observations and saved queue/restart state; no transfer/public network"},
        {"limits", "Bridge packet exposes actual rates/proxy, not get_settings queue caps or bind interfaces; saved cap/bind values are not claimed as active readback"}};
    const auto encoded = QJsonDocument(report).toJson(QJsonDocument::Indented); write(work / "result.json", encoded);
    std::cout << encoded.toStdString(); return 0;
  } catch (const std::exception& failure) {
    QJsonObject report{{"status", "FAIL_ACTUAL_OPTIONS_HOST_LIVE"}, {"reason", QString::fromUtf8(failure.what())}, {"work", text(work)}};
    const auto encoded = QJsonDocument(report).toJson(QJsonDocument::Indented);
    if (created) { try { write(work / "failure.json", encoded); } catch (...) {} }
    std::cerr << encoded.toStdString(); return 2;
  }
}
