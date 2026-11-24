// SPDX-License-Identifier: GPL-2.0-or-later
#include "plugin_activation_state.hpp"
#include "plugin_update_workflow.hpp"
#include "plugin_release_test_tls.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryFile>
#include <QMap>
#include <QNetworkProxy>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <QTemporaryDir>
#include <functional>
#include <iostream>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

using namespace biglybtc::update;
using namespace biglybtc::update::test;

namespace {
VerifiedPluginRelease releaseFor(const QByteArray& data, const SigningFixture& signer) {
  const PluginReleaseTarget target{QStringLiteral("fixture"), QStringLiteral("owner/fixture"),
      QStringLiteral("linux-amd64"), QStringLiteral("6.12.0"), 1, 1};
  const QJsonObject entry{{"id", "fixture"}, {"version", "0.1.0"}, {"platform", "linux-amd64"},
      {"qt", "6.12.0"}, {"minHostApi", 1}, {"maxHostApi", 1}, {"minHostAbi", 1}, {"maxHostAbi", 1},
      {"bytes", data.size()}, {"sha256", QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex())},
      {"nativeCommit", QString(40, u'a')}, {"upstreamCommit", QString(40, u'b')},
      {"url", "https://github.com/owner/fixture/releases/download/v0.1.0/fixture.tar.zst"}};
  const auto now = QDateTime::currentSecsSinceEpoch();
  const auto [payload, signature] = signedCatalogue(signer, 2, now + 3600, {entry});
  auto release = verifyPluginReleaseCatalogue(payload, signature, signer.publicKey(), target, 1, now);
  require(bool(release), "Own a valid signed release with a known byte count");
  return std::move(*release);
}
void write(const QString& path, QByteArrayView data) {
  QFile file(path);
  require(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(data.data(), data.size()) == data.size()
      && file.flush(), "Write the exact generated updater fixture");
}

void qualifyInstalledWorkflow(OwnedConnectProxy& proxy, const VerifiedPluginRelease& release,
    const SigningFixture& signer, const QByteArray& archive, const PluginDownloadOptions& options) {
  using Intent = PluginUpdateIntent;
  using Trigger = UpdateCheckTrigger;
  QTemporaryDir area;
  require(area.isValid(), "Create an isolated original profile");
  const auto plugin = area.filePath(QStringLiteral("plugins/GeneratedOriginalCompanion"));
  const auto descriptor = plugin + QStringLiteral("/plugin.properties");
  const QByteArray descriptorBytes = "plugin.id=fixture\nplugin.version=4.2\n";
  require(QDir().mkpath(plugin), "Install an original generated Java companion");
  write(descriptor, descriptorBytes);
  const auto originalJar = plugin + QStringLiteral("/fixture.jar");
  const QByteArray jar = "generated original archive bytes";
  write(originalJar, jar);
  const auto unrelated = area.filePath(QStringLiteral("plugins/unrelated"));
  const auto legacy = area.filePath(QStringLiteral("plugins/CVS"));
  const auto nativeOnly = area.filePath(QStringLiteral("plugins/fixture/biglybtc"));
  require(QDir().mkpath(unrelated) && QDir().mkpath(legacy) && QDir().mkpath(nativeOnly),
      "Generate unrelated Java, ignored CVS and native-only neighbors");
  write(unrelated + QStringLiteral("/plugin.properties"), "plugin.id=elsewhere\n");
  write(legacy + QStringLiteral("/plugin.properties"), descriptorBytes);
  write(nativeOnly + QStringLiteral("/sentinel"), "unchanged");

  auto config = biglybtc::core::Document::parse("de");
  const auto port = QByteArray::number(proxy.serverPort()).toStdString();
  require(config && config->replaceValue("Enable.Proxy", "i1e") && config->replaceValue("Proxy.Host", "9:127.0.0.1") &&
      config->replaceValue("Proxy.Port", std::to_string(port.size()) + ":" + port) &&
      config->replaceValue("update.periodic", "i0e"), "Save the original route and opt out of the periodic check");
  const auto path = area.filePath(QStringLiteral("biglybt.config"));
  write(path, QByteArray::fromStdString(config->serialize()));
  auto lease = biglybtc::storage::ProfileLease::acquire(area.path().toStdString());
  require(bool(lease), "Take the generated original shared-profile lease");
  const auto check = [&](Trigger trigger, Intent intent, std::string directory = {},
                         const PluginDownloadOptions* callOptions = nullptr) {
    return checkInstalledPluginUpdate(*lease, std::move(directory), release.target(), u"catalogue.json",
        release.signingKey(), trigger, intent, callOptions ? *callOptions : options);
  };
  const auto duplicate = area.filePath(QStringLiteral("plugins/AnotherOriginalCompanion"));
  const auto installDuplicate = [&] {
    require(QDir().mkpath(duplicate), "Create a generated duplicate installation");
    write(duplicate + QStringLiteral("/plugin.properties"), descriptorBytes);
  };
  const auto retireDuplicate = [&] {
    require(QFile::remove(duplicate + QStringLiteral("/plugin.properties")) && QDir().rmdir(duplicate),
        "Remove only the generated duplicate installation");
  };
  const auto conflict = [](const auto& outcome) {
    const auto* storage = outcome ? nullptr : std::get_if<biglybtc::storage::Error>(&outcome.error());
    return storage && storage->code == biglybtc::storage::ErrorCode::Conflict;
  };
  const auto setOriginal = [&](const char* key, const char* value) {
    auto settings = biglybtc::storage::ConfigFile::load(*lease, "biglybt.config");
    require(settings && settings->document().replaceValue(key, value) && settings->save(*lease),
        "Change a generated original preference through the held lease");
  };
  const auto count = [&] { return proxy.requests.size(); };
  proxy.requests.clear();
  const auto skipped = check(Trigger::Periodic, Intent::FollowSavedDownload);
  require(skipped && !skipped->release && !skipped->staged && count() == 0,
      "A disabled saved schedule opens no network connection");
  auto denied = check(Trigger::Startup, Intent::ExplicitDownload);
  require(!denied && std::get_if<PluginDownloadError>(&denied.error()) && count() == 0,
      "The background trigger cannot take the explicit download path");

  setOriginal("PluginInfo.fixture.enabled", "i0e");
  denied = check(Trigger::Explicit, Intent::ExplicitDownload);
  require(!denied && std::get_if<biglybtc::storage::Error>(&denied.error()) && count() == 0,
      "A disabled original plugin is rejected before a catalogue request");
  setOriginal("PluginInfo.fixture.enabled", "i1e");

  const auto discovered = findInstalledJavaPluginDirectory(*lease, release.target());
  require(discovered && *discovered == std::optional<std::string>("GeneratedOriginalCompanion"),
      "Discover the exact Java basename by runtime ID, independently of saved config and native artifacts");
  installDuplicate();
  const auto duplicated = check(Trigger::Explicit, Intent::ExplicitDownload);
  require(conflict(duplicated) && count() == 0, "Ambiguous original identities refuse discovery before network access");
  retireDuplicate();

  const auto available = check(Trigger::Explicit, Intent::FollowSavedDownload);
  require(available && available->release && !available->staged && count() == 2 &&
      !QFile::exists(plugin + QStringLiteral("/biglybtc")), "A manual-download policy only checks the verified catalogue");
  setOriginal("update.autodownload", "i1e");
  proxy.requests.clear();
  const auto listed = check(Trigger::Explicit, Intent::AvailabilityOnly);
  require(listed && listed->release && !listed->staged && count() == 2 &&
      !QFile::exists(plugin + QStringLiteral("/biglybtc")), "An availability-only check never stages even with automatic download");

  const auto wasCancelled = [](const auto& outcome) {
    const auto* failure = outcome ? nullptr : std::get_if<PluginDownloadError>(&outcome.error());
    return failure && *failure == PluginDownloadError::Cancelled;
  };
  std::stop_source stop;
  auto cancelledOptions = options;
  cancelledOptions.cancellation = stop.get_token();
  proxy.requests.clear();
  proxy.requestObserved = [&](QByteArrayView request) {
    if (request.startsWith("GET /owner/fixture/releases/download/v0.1.0/")) { stop.request_stop(); }
  };
  const auto duringAsset = check(Trigger::Explicit, Intent::ExplicitDownload, {}, &cancelledOptions);
  proxy.requestObserved = {};
  require(wasCancelled(duringAsset) && count() == 3 && !QFile::exists(plugin + QStringLiteral("/biglybtc")),
      "A cancelled authenticated asset request does not stage any pending files");
  proxy.requests.clear();
  require(wasCancelled(check(Trigger::Explicit, Intent::ExplicitDownload, {}, &cancelledOptions)) && count() == 0,
      "An already cancelled update exits before network access");

  proxy.requestObserved = [&](QByteArrayView request) {
    if (request.startsWith("GET /owner/fixture/releases/download/v0.1.0/")) {
      setOriginal("update.autodownload", "i0e");
    }
  };
  const auto changedDuringDownload = check(Trigger::Startup, Intent::FollowSavedDownload);
  proxy.requestObserved = {};
  require(!changedDuringDownload && std::get_if<biglybtc::storage::Error>(&changedDuringDownload.error()) &&
      count() == 3 && !QFile::exists(plugin + QStringLiteral("/biglybtc")),
      "Revalidate the saved policy after transfer and before writing any pending artifact");
  setOriginal("update.autodownload", "i1e");
  proxy.requests.clear();
  proxy.requestObserved = [&](QByteArrayView request) {
    if (request.startsWith("GET /owner/fixture/releases/download/v0.1.0/")) { installDuplicate(); }
  };
  const auto duplicateDuringDownload = check(Trigger::Startup, Intent::FollowSavedDownload);
  proxy.requestObserved = {};
  require(conflict(duplicateDuringDownload) && count() == 3 &&
      !QFile::exists(plugin + QStringLiteral("/biglybtc")),
      "Recheck the unique installed original identity after transport and before staging");
  retireDuplicate();
  proxy.requests.clear();
  const auto staged = check(Trigger::Startup, Intent::FollowSavedDownload);
  QFile pending(plugin + QStringLiteral("/biglybtc/pending/linux-amd64/1/0.1.0/release.asset"));
  require(staged && staged->release && staged->staged == PluginReleaseStage::Published && count() == 3 &&
      pending.open(QIODevice::ReadOnly) && pending.readAll() == archive,
      "Fetch and durably stage exact downloaded bytes using the actual original settings and proxy");
  const auto retried = check(Trigger::Explicit, Intent::ExplicitDownload, "GeneratedOriginalCompanion");
  require(retried && retried->staged == PluginReleaseStage::AlreadyPresent,
      "Retry the same downloaded release without replacing the pending artifact");
  const auto pointer = plugin + QStringLiteral("/biglybtc/installed.json");
  require(!QFile::exists(pointer), "The profile update job leaves activation to the installer");

  const auto activated = selectPluginActivation({}, release, QDateTime::currentSecsSinceEpoch());
  require(bool(activated), "Generate an authenticated installed-pointer fixture");
  write(pointer, encodePluginActivationState(*activated));
  require(QFile::setPermissions(pointer, QFileDevice::ReadOwner), "Lock the generated activation record");
  proxy.requests.clear();
  const auto current = check(Trigger::Explicit, Intent::ExplicitDownload);
  require(current && current->release && current->alreadyAccepted && !current->staged && count() == 2,
      "The exact authenticated installed proof reports current before downloading an artifact");
  const QByteArray catalog = "GET /owner/fixture/releases/latest/download/catalogue.json HTTP/1.1";
  const QByteArray signature = "GET /owner/fixture/releases/latest/download/catalogue.json.sig HTTP/1.1";
  auto altered = QJsonDocument::fromJson(release.catalogue()).object();
  altered.insert(QStringLiteral("fixtureExtra"), true);
  const auto expectReplay = [&] {
    const auto bytes = QJsonDocument(altered).toJson(QJsonDocument::Compact);
    proxy.documents[catalog] = bytes;
    proxy.documents[signature] = signer.sign(bytes);
    proxy.requests.clear();
    const auto replay = check(Trigger::Explicit, Intent::ExplicitDownload);
    require(!replay && std::get_if<CatalogueError>(&replay.error()) &&
        std::get<CatalogueError>(replay.error()) == CatalogueError::Replay && count() == 2,
        "Reject a different signed proof without fetching its artifact");
  };
  expectReplay();
  altered.insert(QStringLiteral("sequence"), release.sequence() - 1);
  expectReplay();
  proxy.documents[catalog] = release.catalogue();
  proxy.documents[signature] = release.signature();
  std::cout << "PASS exact installed catalogue reports current and other signed replays are refused\n";

  const auto now = QDateTime::currentSecsSinceEpoch();
  const auto entries = QJsonDocument::fromJson(release.catalogue()).object().value("releases").toArray();
  require(entries.size() == 1 && entries.first().isObject(), "Read the generated signed catalogue entry");
  auto previousEntry = entries.first().toObject();
  previousEntry.insert("version", "0.0.9");
  previousEntry.insert("url", "https://github.com/owner/fixture/releases/download/v0.0.9/fixture.tar.zst");
  const auto [previousBytes, previousSignature] = signedCatalogue(signer, 1, now + 3600, {previousEntry});
  const auto previousRelease = verifyPluginReleaseCatalogue(previousBytes, previousSignature,
      release.signingKey(), release.target(), 0, now);
  require(bool(previousRelease), "Authenticate the generated previous release");
  const auto prior = selectPluginActivation({}, *previousRelease, now);
  require(bool(prior), "Select the generated prior installation");
  const auto installed = selectPluginActivation(*prior, release, now);
  require(bool(installed), "Select two authenticated versions before the next catalogue");
  const auto rolledBack = selectPluginRollback(*installed);
  require(rolledBack && rolledBack->highestSequence == installed->highestSequence && rolledBack->active &&
      rolledBack->active->release.asset().version == u"0.0.9",
      "Keep the installed replay floor when the active and previous releases switch");
  const auto publishFixture = [&](const PluginActivationState& state) {
    const auto bytes = encodePluginActivationState(state);
    QTemporaryFile file(pointer + QStringLiteral(".fixture-XXXXXX"));
    require(file.open() && file.write(bytes) == bytes.size() && file.flush() &&
        file.setPermissions(QFileDevice::ReadOwner), "Write the complete generated activation receipt");
    const auto staged = QFile::encodeName(file.fileName());
    const auto current = QFile::encodeName(pointer);
    file.close();
    require(::rename(staged.constData(), current.constData()) == 0,
        "Atomically replace only the generated installed pointer with an authenticated state");
  };
  const auto [nextBytes, nextSignature] = signedCatalogue(signer, 3, now + 3600, entries);
  proxy.documents[catalog] = nextBytes;
  proxy.documents[signature] = nextSignature;
  const auto proof = plugin + QStringLiteral("/biglybtc/pending/linux-amd64/1/0.1.0/proof-3.signed");
  for (const bool atAsset : {false, true}) {
    publishFixture(*installed);
    proxy.requests.clear();
    bool switched = false;
    proxy.requestObserved = [&](QByteArrayView request) {
      if ((!atAsset && request == signature) ||
          (atAsset && request.startsWith("GET /owner/fixture/releases/download/v0.1.0/"))) {
        publishFixture(*rolledBack);
        switched = true;
      }
    };
    const auto raced = check(Trigger::Explicit, Intent::ExplicitDownload);
    proxy.requestObserved = {};
    require(switched && conflict(raced) && count() == (atAsset ? 3 : 2) && !QFile::exists(proof),
        "Reject a same-sequence active version switch before stage publication");
  }
  proxy.documents[catalog] = release.catalogue();
  proxy.documents[signature] = release.signature();
  std::cout << "PASS detect authenticated rollback with unchanged replay floor during catalogue and asset transfer\n";
  QFile originalDescriptor(descriptor), originalArchive(originalJar);
  require(originalDescriptor.open(QIODevice::ReadOnly) && originalDescriptor.readAll() == descriptorBytes &&
      originalArchive.open(QIODevice::ReadOnly) && originalArchive.readAll() == jar,
      "Preserve the original Java descriptor and companion bytes");
  std::cout << "PASS owned profile update scheduling, HTTPS catalogue, pending asset, and replay floor\n";
  require(QDir().rename(plugin, area.filePath(QStringLiteral("uninstalled-fixture"))),
      "Retire only the held generated Java installation");
  proxy.requests.clear();
  const auto disappeared = check(Trigger::Explicit, Intent::ExplicitDownload);
  const auto* absence = disappeared ? nullptr : std::get_if<biglybtc::storage::Error>(&disappeared.error());
  require(absence && absence->code == biglybtc::storage::ErrorCode::Missing && count() == 0 &&
      !QFile::exists(plugin) && QFile::exists(nativeOnly + QStringLiteral("/sentinel")),
      "A stale enabled setting and native-only subtree do not recreate a removed installation");
  std::cout << "PASS unique original plugin discovery, duplicate update recheck and uninstalled refusal\n";
}
} // namespace

int main(int argc, char** argv) try {
  using Error = PluginDownloadError;
  using Mode = OwnedConnectProxy::Mode;
  using namespace std::chrono_literals;
  QCoreApplication application(argc, argv);
  require(QSslSocket::supportsSsl(), "Load the expected Qt TLS runtime");
  const auto fixture = generatedCertificate();
  require(!fixture.certificate.isNull() && !fixture.key.isNull(), "Import the generated private TLS fixture");
  auto server = QSslConfiguration::defaultConfiguration();
  server.setLocalCertificate(fixture.certificate);
  server.setPrivateKey(fixture.key);
  server.setPeerVerifyMode(QSslSocket::VerifyNone);
  server.setProtocol(QSsl::TlsV1_2OrLater);

  QByteArray sample(160000, 'F');
  for (qsizetype i = 0; i < sample.size(); ++i) { sample[i] = static_cast<char>(i % 251); }
  const SigningFixture signer;
  const auto release = releaseFor(sample, signer);
  OwnedConnectProxy proxy(server, sample);
  auto config = biglybtc::core::Document::parse("de");
  require(config && config->replaceValue("Enable.Proxy", "i1e") && config->replaceValue("Proxy.Host", "9:127.0.0.1") &&
      config->replaceValue("Proxy.Port", std::to_string(QByteArray::number(proxy.serverPort()).size()) + ":" +
          QByteArray::number(proxy.serverPort()).toStdString()), "Set the original profile's isolated proxy");
  const auto plan = planPluginUpdate(&*config, UpdateCheckTrigger::Explicit);
  require(plan && plan->route == UpdateNetworkRoute::WebProxy, "Select the saved original proxy for the actual HTTPS client");
  PluginDownloadOptions options;
  options.deliveryHosts = {QStringLiteral("release-assets.fixture")};
  options.deadline = 2s;
  options.tls.setCaCertificates({fixture.certificate});
  const auto get = [&] { return downloadPluginReleaseAsset(release, *plan, options); };

  const auto ordinary = get();
  require(ordinary && *ordinary == sample && proxy.requests.size() == 1 &&
      proxy.destinations == QList<QByteArray>{"github.com:443"}, "Fetch exact signed bytes through actual CONNECT and TLS");
  proxy.mode = Mode::ApprovedRedirect;
  proxy.destinations.clear();
  proxy.requests.clear();
  const auto redirected = get();
  require(redirected && *redirected == sample && proxy.requests.size() == 2 &&
      proxy.destinations == QList<QByteArray>{"github.com:443", "release-assets.fixture:443"} &&
      proxy.requests.back().startsWith("GET /archive?generated=1 "), "Approve the precise HTTPS delivery host and preserve the original proxy");
  proxy.mode = Mode::DeniedRedirect;
  proxy.destinations.clear();
  require(get() == std::unexpected(Error::Redirect) && proxy.destinations == QList<QByteArray>{"github.com:443"},
      "Refuse a foreign destination without opening another proxy connection");
  proxy.mode = Mode::WrongBytes;
  require(get() == std::unexpected(Error::AssetIdentity), "Refuse a changed final artifact of the signed length");
  proxy.mode = Mode::Oversize;
  require(get() == std::unexpected(Error::AssetIdentity), "Abort a body that exceeds the signed bound");
  proxy.mode = Mode::Good;
  auto untrusted = options;
  untrusted.tls = QSslConfiguration::defaultConfiguration();
  require(downloadPluginReleaseAsset(release, *plan, untrusted) == std::unexpected(Error::Tls),
      "Keep TLS verification active even with a signed archive");
  proxy.destinations.clear();
  require(downloadPluginReleaseAsset(release, {UpdateNetworkRoute::NeedsAnonymousProvider, false}, options) ==
      std::unexpected(Error::Policy) && proxy.destinations.isEmpty(), "A missing anonymous provider does not fall back to this proxy or a direct route");
  const QByteArray catalogueRequest = "GET /owner/fixture/releases/latest/download/catalogue.json HTTP/1.1";
  const QByteArray signatureRequest = "GET /owner/fixture/releases/latest/download/catalogue.json.sig HTTP/1.1";
  proxy.documents[catalogueRequest] = release.catalogue();
  proxy.documents[signatureRequest] = release.signature();
  proxy.requests.clear();
  proxy.destinations.clear();
  const auto check = [&](qint64 previous = 1) {
    return checkPluginReleaseCatalogue(release.target(), u"catalogue.json", release.signingKey(), previous, *plan, options);
  };
  const auto checked = check();
  require(checked && checked->sequence() == 2 && checked->catalogue() == release.catalogue() &&
      checked->signature() == release.signature() && checked->asset().sha256 == release.asset().sha256 &&
      proxy.requests == QList<QByteArray>{catalogueRequest, signatureRequest} &&
      proxy.destinations == QList<QByteArray>{"github.com:443", "github.com:443"},
      "Fetch and verify the exact approved channel catalogue and detached signature through the saved proxy");
  const auto checkError = [](const auto& result, PluginCatalogueCheckError expected) {
    return !result && result.error() == expected;
  };
  require(checkError(check(2), CatalogueError::Replay),
      "The retrieved catalogue cannot bypass the committed replay floor");
  proxy.documents[signatureRequest][0] ^= 1;
  require(checkError(check(), CatalogueError::Signature),
      "Reject a channel body and signature that do not match");
  proxy.documents[signatureRequest] = release.signature() + 'x';
  require(checkError(check(), Error::CatalogueSize),
      "Bound the raw detached signature before cryptographic verification");
  proxy.documents[signatureRequest] = release.signature();
  proxy.documents[catalogueRequest] = QByteArray(MaximumReleaseCatalogueBytes + 1, 'X');
  proxy.requests.clear();
  require(checkError(check(), Error::CatalogueSize) &&
      proxy.requests == QList<QByteArray>{catalogueRequest}, "Reject oversized catalogue before requesting its signature");
  proxy.mode = Mode::OtherRepository;
  proxy.requests.clear();
  require(checkError(check(), Error::Redirect) &&
      proxy.requests == QList<QByteArray>{catalogueRequest}, "Keep the channel within its approved same-origin repository");
  proxy.requests.clear();
  require(checkError(checkPluginReleaseCatalogue(release.target(), u"../catalogue.json", release.signingKey(), 1, *plan, options),
      CatalogueError::InvalidInput) && proxy.requests.isEmpty(),
      "Reject an invalid channel name before opening a socket");
  std::cout << "PASS isolated HTTPS release channel, detached signature, replay, and byte limits\n";
  proxy.mode = Mode::Good;
  proxy.documents[catalogueRequest] = release.catalogue();
  qualifyInstalledWorkflow(proxy, release, signer, sample, options);

  OwnedConnectProxy socks(server, sample, true);
  socks.mode = Mode::ApprovedRedirect;
  const auto socksPort = std::to_string(socks.serverPort());
  require(config->replaceValue("Enable.SOCKS", "i1e") &&
      config->replaceValue("Proxy.Port", std::to_string(socksPort.size()) + ":" + socksPort) &&
      config->replaceValue("Proxy.Username", "14:fixture-client") &&
      config->replaceValue("Proxy.Password", "13:fixture-token"), "Supply owned saved SOCKS5 credentials");
  const auto socksPlan = planPluginUpdate(&*config, UpdateCheckTrigger::Explicit);
  require(socksPlan && socksPlan->proxy && socksPlan->proxy->kind == biglybtc::engine::NetworkProxyKind::Socks5,
      "Select the actual saved SOCKS5 proxy");
  const auto routed = downloadPluginReleaseAsset(release, *socksPlan, options);
  const std::pair<QByteArray, QByteArray> credentials{"fixture-client", "fixture-token"};
  require(routed && *routed == sample &&
      socks.destinations == QList<QByteArray>{"github.com:443", "release-assets.fixture:443"} &&
      socks.socksCredentials == QList{credentials, credentials},
      "Authenticate at SOCKS5 and resolve both approved HTTPS destinations there");
  std::cout << "PASS isolated authenticated SOCKS5 update route and both remote DNS requests\n";
  proxy.destinations.clear();
  proxy.mode = Mode::Silent;
  std::stop_source stop;
  auto cancelledOptions = options;
  cancelledOptions.cancellation = stop.get_token();
  proxy.connectObserved = [&] { stop.request_stop(); };
  const auto started = std::chrono::steady_clock::now();
  const auto cancelled = downloadPluginReleaseAsset(release, *plan, cancelledOptions);
  proxy.connectObserved = {};
  require(cancelled == std::unexpected(Error::Cancelled) && proxy.destinations.size() == 1 &&
      std::chrono::steady_clock::now() - started < 1s, "Cancel an in-flight CONNECT instead of waiting for its deadline");
  proxy.destinations.clear();
  std::cout << "PASS cancel an owned update before network, during CONNECT and before staging\n";
  options.deadline = 150ms;
  require(get() == std::unexpected(Error::Deadline) && proxy.destinations.size() == 1, "Bound a silent proxy without retries or direct fallback");
  std::cout << "PASS isolated HTTPS asset, saved proxy, authorized redirect, byte identity and timeout\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
