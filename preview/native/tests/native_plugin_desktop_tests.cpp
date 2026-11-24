// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_plugin_updates.hpp"
#include "session_bridge.hpp"
#include "library_window.hpp"
#include "existing_controls_fixture.hpp"
#include "plugin_release_test_bundle.hpp"
#include "plugin_release_test_tls.hpp"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

#include <iostream>

using namespace biglybtc;
using namespace biglybtc::update;
using namespace biglybtc::update::test;
namespace f = existing_controls_fixture;

namespace {
using app::NativePluginAction;
using app::NativePluginStatus;
using app::NativePluginUpdateChannel;
using app::NativePluginVersion;

struct Desktop {
  ui::LibraryWindow window;
  app::SessionBridge bridge;
  QSignalSpy status{&bridge, &app::SessionBridge::nativePluginUpdateReady};
  QSignalSpy opened{&bridge, &app::SessionBridge::columnLayoutSessionReady};
  QSignalSpy closed{&bridge, &app::SessionBridge::sessionClosed};
  QAction* action;
  QString config;

  Desktop() {
    app::bindNativePluginUpdateControls(window, bridge);
    action = window.findChild<QAction*>(QStringLiteral("classic-native-plugin-updates"));
    require(action && !action->isEnabled(), "Updater requires a profile session");
    window.show();
  }

  NativePluginStatus last() const {
    return status.empty() ? NativePluginStatus{} : qvariant_cast<NativePluginStatus>(status.last().at(0));
  }

  NativePluginStatus open(const QString& profile, bool network) {
    config = profile + "/biglybt.config";
    require(network ? bridge.openNormal(profile.toStdString()) : bridge.open(profile.toStdString(), 0),
        "Open the generated profile with the real worker");
    require(QTest::qWaitFor([&] { return opened.size() == 1 && action->isEnabled(); }, 12000),
        "Expose the update menu after the profile is ready");
    const auto before = f::read(config);
    action->trigger();
    require(QTest::qWaitFor([&] { return status.size() == 1 && !bridge.busy(); }, 12000),
        "Read the update state through the actual menu");
    require(f::read(config) == before, "Reading the update state preserves all original config bytes");
    return last();
  }

  QPushButton& button(const char* objectName) {
    auto* dialog = window.findChild<QDialog*>("native-plugin-updates-dialog");
    auto* control = dialog ? dialog->findChild<QPushButton*>(QString::fromLatin1(objectName)) : nullptr;
    require(control != nullptr, "Find the update dialog control");
    return *control;
  }

  NativePluginStatus click(const char* objectName) {
    const auto before = status.size();
    auto& control = button(objectName);
    require(control.isEnabled(), objectName);
    const auto saved = f::read(config);
    control.click();
    require(QTest::qWaitFor([&] { return status.size() == before + 1 && !bridge.busy(); }, 12000),
        "Finish the update worker command");
    require(f::read(config) == saved, "An update command preserves all original config bytes");
    return last();
  }

  QString value(const char* objectName) const {
    auto* field = window.findChild<QLabel*>(QString::fromLatin1(objectName));
    require(field != nullptr, "Find the update dialog field");
    return field->text();
  }

  void close() {
    const auto session = opened.first().at(0).toULongLong();
    require(bridge.closeSession(), "Request normal profile close");
    require(QTest::qWaitFor([&] { return closed.size() == 1 && !bridge.busy(); }, 12000),
        "Wait for the worker to close and release storage");
    require(closed.first().at(1).toBool() && closed.first().at(2).toBool() && !action->isEnabled() &&
        !bridge.nativePluginUpdate({session, NativePluginAction::Status, {}}),
        "Close releases the profile and invalidates menu requests");
  }
};

void isolatedStartup(const QString& profile, const NativePluginUpdateChannel& channel, const QString& expected) {
  QProcess process;
  process.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--startup-probe"), profile,
      QString::fromStdString(channel.countryDatabase.string()), QString::fromLatin1(channel.publicKey.toHex()), expected});
  require(process.waitForFinished(18000), "Wait for a fresh desktop process to open and close the profile");
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    std::cerr << process.readAllStandardError().constData();
    throw std::runtime_error("Fresh desktop process did not start the selected signed version");
  }
}

void serve(OwnedConnectProxy& proxy, const VerifiedPluginRelease& release, const QByteArray& bundle, bool valid) {
  const auto request = [](const QUrl& url) { return "GET " + url.path().toLatin1() + " HTTP/1.1"; };
  auto catalogue = *pluginReleaseCatalogueUrl(release.target(), NativePluginUpdateChannel::catalogueName);
  proxy.documents[request(catalogue)] = release.catalogue();
  catalogue.setPath(catalogue.path() + QStringLiteral(".sig"));
  proxy.documents[request(catalogue)] = valid ? release.signature() : QByteArray(64, '!');
  proxy.documents[request(release.asset().url)] = bundle;
}

void exercise(const QStringList& args) {
  f::Profile profile(false, false, false);
  const auto original = profile.path + "/plugins/RenamedLocationProvider";
  const auto descriptor = original + "/plugin.properties";
  const auto javaArchive = original + "/LocationProvider_0.6.9.jar";
  const QByteArray descriptorBytes = "plugin.id=azlocprov\nplugin.version=0.6.9\nplugin.class=generated.Main\n";
  const QByteArray javaBytes = "generated adjacent Java artifact";
  f::write(descriptor, descriptorBytes);
  f::write(javaArchive, javaBytes);
  const auto config = profile.path + "/biglybt.config";
  const auto certificate = generatedCertificate();
  auto server = QSslConfiguration::defaultConfiguration();
  server.setLocalCertificate(certificate.certificate);
  server.setPrivateKey(certificate.key);
  server.setPeerVerifyMode(QSslSocket::VerifyNone);
  OwnedConnectProxy proxy(server, {});
  {
    auto lease = storage::ProfileLease::acquire(profile.path.toStdString());
    require(bool(lease), "Hold the generated profile to configure the loopback route");
    auto saved = storage::ConfigFile::load(*lease, "biglybt.config");
    require(saved && saved->document().replaceValue("Enable.Proxy", "i1e") &&
        saved->document().replaceValue("Proxy.Host", "9:127.0.0.1") &&
        saved->document().replaceValue("Proxy.Port", f::b(std::to_string(proxy.serverPort()))) &&
        saved->document().replaceValue("update.periodic", "i0e") && saved->save(*lease),
        "Use only the explicit saved loopback HTTPS proxy");
  }
  QTemporaryDir countryDirectory;
  require(countryDirectory.isValid(), "Create the private country data directory");

  ReleaseFixture signedRelease;
  auto channel = std::make_shared<NativePluginUpdateChannel>();
  channel->publicKey = signedRelease.signer.publicKey();
  channel->trial = {args[2], QCryptographicHash::hash(f::read(args[2]), QCryptographicHash::Sha256)};
  channel->download.tls.setCaCertificates({certificate.certificate});
  channel->countryDatabase = countryDirectory.path().toStdString();
  const auto module1 = f::read(args[1]);
  const auto package1 = archiveFixture({{"manifest.json", signedRelease.manifest(module1)}, {"plugin.so", module1}});
  const auto release1 = signedRelease.select(package1, 12);
  const auto version1 = signedRelease.version;
  signedRelease.version = QStringLiteral("0.2.0");
  const auto module2 = f::read(args[3]);
  const auto package2 = archiveFixture({{"manifest.json", signedRelease.manifest(module2)}, {"plugin.so", module2}});
  const auto release2 = signedRelease.select(package2, 13);
  const auto version2 = signedRelease.version;
  const auto pointer = original + "/biglybtc/installed.json";

  {
    Desktop desktop;
    const auto unconfigured = desktop.open(profile.path, true);
    require(!unconfigured.canDownload && !unconfigured.message.isEmpty() &&
        !desktop.button("native-plugin-download").isEnabled(), "A build without trust exposes no download path");
    desktop.close();
  }

  // Normal startup can initialize the saved swarm identity before any update.
  const auto baselineConfig = f::read(config);

  {
    Desktop desktop;
    require(desktop.bridge.configureNativePluginUpdates(channel), "Bind fixture trust before opening a profile");
    const auto before = desktop.open(profile.path, true);
    require(before.canDownload && !before.staged && !before.active && before.running.isEmpty(), "Initially there is no native release");
    serve(proxy, release1, package1, false);
    const auto invalid = desktop.click("native-plugin-download");
    require(proxy.requests.size() == 2 && !invalid.staged && invalid.message.contains("catalogue") &&
        !QFile::exists(pointer),
        "An invalid signature cannot stage or activate a release from the actual menu");
    serve(proxy, release1, package1, true);
    proxy.mode = OwnedConnectProxy::Mode::Silent;
    const auto attempts = proxy.destinations.size();
    const auto responses = desktop.status.size();
    desktop.button("native-plugin-download").click();
    require(QTest::qWaitFor([&] { return proxy.destinations.size() == attempts + 1; }, 5000),
        "Observe a real pending HTTPS request before cancellation");
    desktop.button("native-plugin-cancel").click();
    require(QTest::qWaitFor([&] { return desktop.status.size() == responses + 1 && !desktop.bridge.busy(); }, 5000) &&
        !desktop.last().staged && desktop.last().message.contains("cancelled"), "Cancel the menu request before staging");
    proxy.mode = OwnedConnectProxy::Mode::Good;
    const auto staged = desktop.click("native-plugin-download");
    if (!staged.staged) { std::cerr << staged.message.toStdString() << '\n'; }
    require(staged.staged == NativePluginVersion{version1, 12} && !staged.active && !QFile::exists(pointer) &&
        desktop.value("native-plugin-staged") == version1, "Authenticated HTTPS download stages exact bytes");
    desktop.close();
  }
  const auto requestsBeforeOffline = proxy.requests.size();
  {
    Desktop desktop;
    require(desktop.bridge.configureNativePluginUpdates(channel), "Restore fixture trust on profile reopen");
    const auto before = desktop.open(profile.path, false);
    require(before.staged == NativePluginVersion{version1, 12} && !desktop.button("native-plugin-download").isEnabled(),
        "Rediscover the staged proof on an offline reopen");
    const auto installed = desktop.click("native-plugin-install");
    require(installed.active == NativePluginVersion{version1, 12} && installed.running.isEmpty() &&
        !installed.staged && !installed.previous && QFile::exists(pointer), "Install by isolated trial for the next start");
    desktop.close();
  }
  require(proxy.requests.size() == requestsBeforeOffline, "Inspect and install the verified slot without network requests");
  isolatedStartup(profile.path, *channel, version1);

  {
    Desktop desktop;
    require(desktop.bridge.configureNativePluginUpdates(channel), "Open the signed location provider configuration again");
    require(desktop.open(profile.path, true).running == version1, "Start the first authenticated descriptor");
    serve(proxy, release2, package2, true);
    require(desktop.click("native-plugin-download").staged == NativePluginVersion{version2, 13},
        "Stage the second signed release through the actual menu");
    const auto installed = desktop.click("native-plugin-install");
    require(installed.active == NativePluginVersion{version2, 13} &&
        installed.previous == NativePluginVersion{version1, 12} && installed.running == version1 &&
        desktop.value("native-plugin-running") == version1, "Stage the new version without replacing the running native image");
    desktop.close();
  }
  isolatedStartup(profile.path, *channel, version2);

  {
    Desktop desktop;
    require(desktop.bridge.configureNativePluginUpdates(channel), "Open the rollback profile with the same channel");
    const auto before = desktop.open(profile.path, false);
    require(before.running == version2 && before.previous == NativePluginVersion{version1, 12},
        "The restarted session shows both the active and rollback versions");
    const auto rolledBack = desktop.click("native-plugin-rollback");
    require(rolledBack.active == NativePluginVersion{version1, 12} && rolledBack.running == version2 &&
        rolledBack.previous == NativePluginVersion{version2, 13}, "Rollback requalifies a signed earlier version for restart");
    desktop.close();
  }
  isolatedStartup(profile.path, *channel, version1);

  require(f::read(descriptor) == descriptorBytes && f::read(javaArchive) == javaBytes,
      "The whole signed lifecycle preserves the original Java descriptor and JAR");
  require(f::read(config) == baselineConfig, "Native updates preserve the config established by normal startup");
}
} // namespace

int main(int argc, char** argv) try {
  QApplication application(argc, argv);
  QApplication::setQuitOnLastWindowClosed(false);
  const auto args = application.arguments();
  if (args.size() == 6 && args[1] == QStringLiteral("--startup-probe")) {
    Desktop desktop;
    auto channel = std::make_shared<NativePluginUpdateChannel>();
    channel->countryDatabase = args[3].toStdString();
    channel->publicKey = QByteArray::fromHex(args[4].toLatin1());
    require(desktop.bridge.configureNativePluginUpdates(channel), "Configure authenticated startup in the fresh process");
    const auto status = desktop.open(args[2], false);
    require(status.running == args[5] && status.startupFailure.isEmpty() &&
        desktop.value("native-plugin-running") == args[5], "Start and display the actually loaded signed module");
    desktop.close();
    return 0;
  }
  require(args.size() == 4, "Supply the two actual test modules and the packaged trial helper");
  exercise(args);
  std::cout << "PASS desktop HTTPS staging, offline install, process restart, rollback and preservation\n";
  return 0;
} catch (const std::exception& failure) {
  std::cerr << failure.what() << '\n';
  return 1;
}
