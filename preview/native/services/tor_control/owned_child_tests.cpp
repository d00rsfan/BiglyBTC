// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_child.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <cerrno>
#include <csignal>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

using biglybtc::services::tor::OwnedTorChild;
using biglybtc::services::tor::OwnedTorChildOptions;
namespace {
QByteArray read(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool write(const QString& path, const QByteArray& bytes, bool executable = false) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly|QIODevice::NewOnly) || file.write(bytes) != bytes.size()) { return false; }
  file.close();
  auto permissions = QFile::Permissions(QFile::ReadOwner|QFile::WriteOwner);
  if (executable) { permissions |= QFile::ExeOwner; }
  return file.setPermissions(permissions);
}
struct Fixture {
  QTemporaryDir root;
  QString executable;
  bool ready = false;
  explicit Fixture(const QByteArray& mode = "good") {
    if (!root.isValid()) { return; }
    auto script = read(QStringLiteral(BIGLYBTC_TOR_CHILD_FIXTURE));
    if (!script.contains("MODE = 'good'")) { return; }
    script.replace("MODE = 'good'", "MODE = '"+mode+"'");
    executable = root.path()+QStringLiteral("/owned-peer.py");
    ready = write(executable,script,true) && write(root.path()+QStringLiteral("/outsider-cookie"),QByteArray(32,'s'));
  }
  OwnedTorChildOptions options(const QString& leaf = QStringLiteral("run")) const {
    return {executable,root.path()+QStringLiteral("/")+leaf,3000,100};
  }
};
bool reaped(qint64 pid) { errno = 0; return pid > 0 && ::kill(static_cast<pid_t>(pid),0) == -1 && errno == ESRCH; }
QByteArray fileHash(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) { return {}; }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  return hash.addData(&file) ? hash.result().toHex() : QByteArray{};
}
}

class OwnedTorChildTests final : public QObject {
  Q_OBJECT
private slots:
  void actualOwnedChildAuthenticatesStopsJoinsAndRestartsInFreshDirectory() {
    Fixture fixture; QVERIFY(fixture.ready);
    OwnedTorChild child;
    QSignalSpy ready(&child,&OwnedTorChild::ready), failed(&child,&OwnedTorChild::failed), finished(&child,&OwnedTorChild::finished);
    for (const auto& leaf : {QStringLiteral("first"),QStringLiteral("second")}) {
      const auto options = fixture.options(leaf);
      const int count = ready.count();
      QVERIFY(child.start(options));
      QTRY_COMPARE_WITH_TIMEOUT(ready.count(),count+1,5000);
      QCOMPARE(child.state(),OwnedTorChild::State::Ready);
      QVERIFY(!child.joined()); QVERIFY(child.sourceCurrent()); QVERIFY(child.controlPort() > 0);
      QCOMPARE(child.rawVersionInfo(),QByteArray("version=owned-child-fixture"));
      const auto audit = QJsonDocument::fromJson(read(options.workingDirectory+QStringLiteral("/child-audit.json"))).object();
      QCOMPARE(audit["pid"].toInteger(),child.processId());
      QCOMPARE(audit["parent"].toInteger(),QCoreApplication::applicationPid());
      QCOMPARE(audit["port"].toInt(),child.controlPort());
      QCOMPARE(audit["commands"].toArray(),QJsonArray({"AUTHCHALLENGE","AUTHENTICATE","TAKEOWNERSHIP","RESETCONF __OwningControllerProcess","GETINFO version"}));
      const auto pid = child.processId();
      const auto port = child.controlPort();
      const auto cookie = read(options.workingDirectory+QStringLiteral("/data/control_auth_cookie"));
      QCOMPARE(cookie.size(),32);
      child.stop(); child.stop();
      QTRY_VERIFY_WITH_TIMEOUT(child.joined(),5000);
      QCOMPARE(child.state(),OwnedTorChild::State::Stopped); QVERIFY(reaped(pid));
      QCOMPARE(child.controlPort(),quint16(0)); QVERIFY(!child.sourceCurrent()); QVERIFY(port > 0);
      QCOMPARE(read(options.workingDirectory+QStringLiteral("/data/control_auth_cookie")),cookie);
    }
    QCOMPARE(failed.count(),0); QCOMPARE(finished.count(),2);
  }
  void boundedStartupCancellationEscalationAndCrash_data() {
    QTest::addColumn<QByteArray>("mode"); QTest::addColumn<bool>("cancel");
    QTest::newRow("deadline-no-discovery") << QByteArray("hang") << false;
    QTest::newRow("cancel-while-discovering") << QByteArray("hang") << true;
    QTest::newRow("owned-kill-after-term-refusal") << QByteArray("ignore-term") << true;
    QTest::newRow("unexpected-exit-after-ready") << QByteArray("crash") << false;
  }
  void boundedStartupCancellationEscalationAndCrash() {
    QFETCH(QByteArray,mode); QFETCH(bool,cancel);
    Fixture fixture(mode); QVERIFY(fixture.ready);
    OwnedTorChild child; QSignalSpy ready(&child,&OwnedTorChild::ready), failed(&child,&OwnedTorChild::failed);
    auto options = fixture.options(); if (mode == "hang" && !cancel) { options.startupMilliseconds = 150; }
    QVERIFY(child.start(options));
    QTRY_VERIFY_WITH_TIMEOUT(child.processId() > 0,3000);
    const auto pid = child.processId();
    if (mode == "ignore-term" || mode == "crash") { QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,5000); }
    if (cancel) { child.stop(); }
    QTRY_VERIFY_WITH_TIMEOUT(child.joined(),5000); QVERIFY(reaped(pid));
    QCOMPARE(child.state(),cancel ? OwnedTorChild::State::Stopped : OwnedTorChild::State::Failed);
    QCOMPARE(failed.count(),cancel ? 0 : 1);
  }
  void unsafeDiscoveryCannotAuthenticateOrTouchOutsider_data() {
    QTest::addColumn<QByteArray>("mode");
    for (const auto* mode : {"symlink-cookie","hardlink-cookie","bad-address","bad-proof"}) { QTest::newRow(mode) << QByteArray(mode); }
  }
  void unsafeDiscoveryCannotAuthenticateOrTouchOutsider() {
    QFETCH(QByteArray,mode);
    Fixture fixture(mode); QVERIFY(fixture.ready);
    const auto outsider = fixture.root.path()+QStringLiteral("/outsider-cookie");
    struct stat before{}; QVERIFY(!::stat(QFile::encodeName(outsider).constData(),&before));
    OwnedTorChild child; QSignalSpy ready(&child,&OwnedTorChild::ready), failed(&child,&OwnedTorChild::failed);
    QVERIFY(child.start(fixture.options()));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(),1,5000); QVERIFY(child.joined()); QCOMPARE(ready.count(),0);
    QCOMPARE(child.state(),OwnedTorChild::State::Failed); QCOMPARE(read(outsider),QByteArray(32,'s'));
    struct stat after{}; QVERIFY(!::stat(QFile::encodeName(outsider).constData(),&after));
    QCOMPARE(after.st_dev,before.st_dev); QCOMPARE(after.st_ino,before.st_ino); QCOMPARE(after.st_size,before.st_size);
    const auto audit = QJsonDocument::fromJson(read(fixture.options().workingDirectory+QStringLiteral("/child-audit.json"))).object();
    QVERIFY(!audit["commands"].toArray().contains(QStringLiteral("TAKEOWNERSHIP")));
  }
  void partialCookieWaitsWithinSameDeadlineAndSourceReplacementRevokes() {
    Fixture fixture("partial-cookie"); QVERIFY(fixture.ready);
    OwnedTorChild child; QSignalSpy ready(&child,&OwnedTorChild::ready), failed(&child,&OwnedTorChild::failed);
    const auto options = fixture.options(); QVERIFY(child.start(options));
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,5000); QVERIFY(child.sourceCurrent());
    const auto cookie = options.workingDirectory+QStringLiteral("/data/control_auth_cookie");
    QVERIFY(QFile::rename(cookie,cookie+QStringLiteral(".kept")));
    QVERIFY(write(cookie,QByteArray(32,'r'))); QVERIFY(!child.sourceCurrent());
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(),1,5000); QVERIFY(child.joined());
    QCOMPARE(read(cookie),QByteArray(32,'r'));
    QCOMPARE(read(cookie+QStringLiteral(".kept")).size(),32);
  }
  void authenticatedResetMayRepublishOnlyTheSamePrivateEndpointOnce() {
    Fixture fixture("reset-republish"); QVERIFY(fixture.ready);
    const auto options = fixture.options(); OwnedTorChild child;
    QSignalSpy ready(&child,&OwnedTorChild::ready), failed(&child,&OwnedTorChild::failed);
    QVERIFY(child.start(options)); QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,5000);
    QVERIFY(child.sourceCurrent()); QCOMPARE(failed.count(),0);
    const auto audit = QJsonDocument::fromJson(read(options.workingDirectory+QStringLiteral("/child-audit.json"))).object();
    QVERIFY(audit["old_port_inode"].toInteger() > 0);
    QVERIFY(audit["new_port_inode"].toInteger() != audit["old_port_inode"].toInteger());
    QCOMPARE(audit["commands"].toArray(),QJsonArray({"AUTHCHALLENGE","AUTHENTICATE","TAKEOWNERSHIP","RESETCONF __OwningControllerProcess","GETINFO version"}));
    const auto path = options.workingDirectory+QStringLiteral("/control.port");
    const auto selected = read(path); const auto pid = child.processId();
    QCOMPARE(selected,QByteArray("PORT=127.0.0.1:")+QByteArray::number(child.controlPort())+'\n');
    // No reconciliation after Ready, even for byte-identical replacement.
    QVERIFY(QFile::rename(path,path+QStringLiteral(".retained"))); QVERIFY(write(path,selected));
    QVERIFY(!child.sourceCurrent()); QTRY_COMPARE_WITH_TIMEOUT(failed.count(),1,5000);
    QVERIFY(child.joined()); QVERIFY(reaped(pid)); QCOMPARE(child.state(),OwnedTorChild::State::Failed);
    QCOMPARE(ready.count(),1); QCOMPARE(read(path),selected);
  }
  void authenticatedResetCannotRefreshOtherAuthorityOrUnsafePort_data() {
    QTest::addColumn<QByteArray>("mode");
    for (const auto* mode : {"reset-bad-port","reset-cookie","reset-config","reset-symlink-port",
        "reset-hardlink-port","reset-fifo-port","reset-public-port","reset-ancestor"}) {
      QTest::newRow(mode) << QByteArray(mode);
    }
  }
  void authenticatedResetCannotRefreshOtherAuthorityOrUnsafePort() {
    QFETCH(QByteArray,mode); Fixture fixture(mode); QVERIFY(fixture.ready);
    const auto outsider = fixture.root.path()+QStringLiteral("/outsider-port");
    QVERIFY(write(outsider,"unselected-discovery-sentinel\n"));
    struct stat before{}; QVERIFY(!::lstat(QFile::encodeName(outsider).constData(),&before));
    auto options = fixture.options();
    if (mode == "reset-ancestor") {
      const auto ancestor = fixture.root.path()+QStringLiteral("/ancestor");
      QVERIFY(QDir().mkdir(ancestor));
      QVERIFY(QFile::setPermissions(ancestor,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
      options.workingDirectory = ancestor+QStringLiteral("/run");
    }
    OwnedTorChild child; QSignalSpy ready(&child,&OwnedTorChild::ready), failed(&child,&OwnedTorChild::failed);
    QVERIFY(child.start(options)); QTRY_COMPARE_WITH_TIMEOUT(failed.count(),1,5000);
    QCOMPARE(ready.count(),0); QVERIFY(child.joined()); QVERIFY(reaped(child.processId()));
    QCOMPARE(child.state(),OwnedTorChild::State::Failed);
    QCOMPARE(failed.at(0).at(0).toString(),QStringLiteral("Owned Tor authority changed during authentication"));
    const auto audit = QJsonDocument::fromJson(read(options.workingDirectory+QStringLiteral("/child-audit.json"))).object();
    QCOMPARE(audit["commands"].toArray(),QJsonArray({"AUTHCHALLENGE","AUTHENTICATE","TAKEOWNERSHIP","RESETCONF __OwningControllerProcess","GETINFO version"}));
    QCOMPARE(read(outsider),QByteArray("unselected-discovery-sentinel\n"));
    struct stat after{}; QVERIFY(!::lstat(QFile::encodeName(outsider).constData(),&after));
    QCOMPARE(after.st_dev,before.st_dev); QCOMPARE(after.st_ino,before.st_ino); QCOMPARE(after.st_size,before.st_size);
  }
  void preexistingRootAndSymlinkExecutableAreRefusedWithoutSpawn() {
    Fixture fixture; QVERIFY(fixture.ready);
    auto options = fixture.options(); QVERIFY(QDir().mkdir(options.workingDirectory));
    OwnedTorChild child; QVERIFY(!child.start(options)); QVERIFY(child.joined()); QCOMPARE(child.processId(),qint64(0));
    QVERIFY(QFile::link(fixture.executable,fixture.root.path()+QStringLiteral("/linked-peer")));
    options = fixture.options(QStringLiteral("fresh")); options.executable = fixture.root.path()+QStringLiteral("/linked-peer");
    QVERIFY(!child.start(options)); QVERIFY(child.joined()); QCOMPARE(child.processId(),qint64(0));
    QVERIFY(!QFile::exists(options.workingDirectory));
    options = fixture.options(QStringLiteral("fifo-work"));
    options.executable = fixture.root.path()+QStringLiteral("/fifo-executable");
    QVERIFY(!::mkfifo(QFile::encodeName(options.executable).constData(),0600));
    QVERIFY(!child.start(options)); QVERIFY(child.joined()); QCOMPARE(child.processId(),qint64(0));
    QVERIFY(!QFile::exists(options.workingDirectory));
  }
  void intactWorkDirectoryCannotAuthorizeReplacedHigherAncestor() {
    Fixture fixture; QVERIFY(fixture.ready);
    const auto ancestor = fixture.root.path()+QStringLiteral("/ancestor");
    QVERIFY(QDir().mkdir(ancestor));
    QVERIFY(QFile::setPermissions(ancestor,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
    auto options = fixture.options(); options.workingDirectory = ancestor+QStringLiteral("/run");
    OwnedTorChild child; QSignalSpy ready(&child,&OwnedTorChild::ready);
    QVERIFY(child.start(options)); QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,5000);
    const auto old = ancestor+QStringLiteral(".kept");
    QVERIFY(QDir().rename(ancestor,old)); QVERIFY(QDir().mkdir(ancestor));
    QVERIFY(QFile::setPermissions(ancestor,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
    QVERIFY(QDir().rename(old+QStringLiteral("/run"),options.workingDirectory));
    QVERIFY(!child.sourceCurrent());
    child.stop(); QTRY_VERIFY_WITH_TIMEOUT(child.joined(),5000);
    QCOMPARE(child.state(),OwnedTorChild::State::Stopped);
    QCOMPARE(read(options.workingDirectory+QStringLiteral("/data/control_auth_cookie")).size(),32);
  }
  void signalCancellationAndImmediateDeletionCannotLaunchAfterCallback() {
    Fixture fixture; QVERIFY(fixture.ready);
    OwnedTorChild child;
    connect(&child,&OwnedTorChild::stateChanged,&child,[&](OwnedTorChild::State state){ if (state == OwnedTorChild::State::Starting) { child.stop(); } });
    QVERIFY(child.start(fixture.options())); QVERIFY(child.joined()); QCOMPARE(child.state(),OwnedTorChild::State::Stopped);
    QCOMPARE(child.processId(),qint64(0)); QVERIFY(!QFile::exists(fixture.options().workingDirectory+QStringLiteral("/child-audit.json")));
    auto* deleted = new OwnedTorChild; QPointer<OwnedTorChild> guard(deleted);
    connect(deleted,&OwnedTorChild::stateChanged,deleted,[deleted](OwnedTorChild::State state){ if (state == OwnedTorChild::State::Starting) { delete deleted; } });
    QVERIFY(deleted->start(fixture.options(QStringLiteral("deleted")))); QVERIFY(!guard);
    QVERIFY(!QFile::exists(fixture.options(QStringLiteral("deleted")).workingDirectory+QStringLiteral("/child-audit.json")));
  }
  void deletionAtAuthenticationReapsTheAlreadyRunningChild() {
    Fixture fixture; QVERIFY(fixture.ready);
    auto* child = new OwnedTorChild; QPointer<OwnedTorChild> guard(child); qint64 pid = 0;
    connect(child,&OwnedTorChild::stateChanged,child,[&](OwnedTorChild::State state){
      if (state == OwnedTorChild::State::Authenticating) { pid = child->processId(); delete child; }
    });
    QVERIFY(child->start(fixture.options()));
    QTRY_VERIFY_WITH_TIMEOUT(!guard,5000); QVERIFY(reaped(pid));
  }
  void failedExecIsJoinedAndCanRetryWithFreshAuthority() {
    Fixture fixture; QVERIFY(fixture.ready);
    const auto invalid = fixture.root.path()+QStringLiteral("/invalid-executable");
    QVERIFY(write(invalid,"#!/no-such-owned-tor-interpreter\n",true));
    auto options = fixture.options(QStringLiteral("failed")); options.executable = invalid;
    OwnedTorChild child; QSignalSpy failed(&child,&OwnedTorChild::failed), ready(&child,&OwnedTorChild::ready);
    QVERIFY(child.start(options)); QTRY_COMPARE_WITH_TIMEOUT(failed.count(),1,5000);
    QVERIFY(child.joined()); QCOMPARE(child.state(),OwnedTorChild::State::Failed);
    QVERIFY(child.start(fixture.options(QStringLiteral("retried"))));
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,5000); QVERIFY(child.sourceCurrent());
    child.stop(); QTRY_VERIFY_WITH_TIMEOUT(child.joined(),5000);
  }
  void destructionJoinsOnlyTheActuallyOwnedReadyChild() {
    Fixture fixture; QVERIFY(fixture.ready);
    qint64 pid = 0;
    {
      auto child = std::make_unique<OwnedTorChild>(); QSignalSpy ready(child.get(),&OwnedTorChild::ready);
      QVERIFY(child->start(fixture.options())); QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,5000);
      pid = child->processId();
    }
    QVERIFY(reaped(pid));
  }
};
// Explicit separate mode: default generated-peer suite has no real-Tor row and
// cannot silently grant it pass credit. Root supplies a pinned private launcher
// and its complete extracted runtime receipt; this never searches PATH/daemons.
class RealOfflineTorTests final : public QObject {
  Q_OBJECT
private slots:
  void extractedTorAuthenticatesOfflineThenStopsAndCancelsAnotherActualChild() {
    const auto executable = qEnvironmentVariable("BIGLYBTC_OWNED_TOR_EXECUTABLE");
    const auto receiptPath = qEnvironmentVariable("BIGLYBTC_OWNED_TOR_RUNTIME_ARTIFACT");
    const auto receiptHash = qgetenv("BIGLYBTC_OWNED_TOR_RUNTIME_ARTIFACT_SHA256");
    const auto output = qEnvironmentVariable("BIGLYBTC_OWNED_TOR_RESULT");
    if (executable.isEmpty() || receiptPath.isEmpty() || receiptHash.isEmpty() || output.isEmpty()) {
      QSKIP("Real Tor acceptance disabled: explicit private runtime/artifact/output not supplied; no runtime credit");
    }
    QVERIFY(QFileInfo(executable).isAbsolute()); QVERIFY(QFileInfo(receiptPath).isAbsolute());
    QVERIFY(QFileInfo(output).isAbsolute()); QVERIFY(!QFile::exists(output));
    QCOMPARE(fileHash(receiptPath),receiptHash);
    const auto runtime = QJsonDocument::fromJson(read(receiptPath)).object();
    QCOMPARE(runtime["launcher"].toObject()["path"].toString(),executable);
    QCOMPARE(fileHash(executable),runtime["launcher"].toObject()["sha256"].toString().toLatin1());
    const auto originalReference = runtime["tor_artifact"].toObject();
    const auto originalPath = originalReference["path"].toString();
    QCOMPARE(fileHash(originalPath),originalReference["sha256"].toString().toLatin1());
    const auto original = QJsonDocument::fromJson(read(originalPath)).object();
    QCOMPARE(original["package_sha256"].toString(),QStringLiteral("80fa05a025ef6d2fd61e48f85040f025bdd26b3e3539931f97fbb14b2a8abbf3"));
    QCOMPARE(original["executable_sha256"].toString(),QStringLiteral("ee7992cac60c6efd82cb93ba8146f20ceca6e9b506153208cb3b0ab5bda0fbae"));
    QVERIFY(original["system_services_touched"].isBool()); QVERIFY(!original["system_services_touched"].toBool());
    const auto files = runtime["files_sha256"].toObject();
    QVERIFY(files.size() >= 4 && files.size() <= 32);
    QVERIFY(files.contains(executable)); QVERIFY(files.contains(original["executable"].toString()));
    QCOMPARE(files[original["executable"].toString()].toString(),original["executable_sha256"].toString());
    const auto package = QFileInfo(originalPath).absolutePath()+QStringLiteral("/")+original["package"].toString();
    QCOMPARE(fileHash(package),original["package_sha256"].toString().toLatin1());
    for (auto i = files.begin(); i != files.end(); ++i) {
      QVERIFY(QFileInfo(i.key()).isAbsolute()); QVERIFY(QFileInfo(i.key()).size() <= 8*1024*1024);
      QCOMPARE(fileHash(i.key()),i.value().toString().toLatin1());
    }
    QTemporaryDir generated; QVERIFY(generated.isValid());
    OwnedTorChild child; QSignalSpy ready(&child,&OwnedTorChild::ready), failed(&child,&OwnedTorChild::failed);
    OwnedTorChildOptions options{executable,generated.path()+QStringLiteral("/actual-tor"),10000,1000};
    QVERIFY(child.start(options));
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,12000);
    QCOMPARE(failed.count(),0); QCOMPARE(child.state(),OwnedTorChild::State::Ready);
    QVERIFY(child.sourceCurrent()); QVERIFY(!child.joined());
    const auto pid = child.processId(); const auto port = child.controlPort();
    QVERIFY(pid > 0); QVERIFY(port > 0);
    QVERIFY(child.rawVersionInfo().startsWith("version=0.4.9.11"));
    const auto version = child.rawVersionInfo();
    const auto config = read(options.workingDirectory+QStringLiteral("/torrc"));
    for (const auto& line : {QByteArray("DisableNetwork 1\n"),QByteArray("SocksPort 0\n"),
         QByteArray("RunAsDaemon 0\n"),QByteArray("ControlPort 127.0.0.1:auto\n")}) { QVERIFY(config.contains(line)); }
    const auto portText = read(options.workingDirectory+QStringLiteral("/control.port"));
    QCOMPARE(portText.trimmed(),QByteArray("PORT=127.0.0.1:")+QByteArray::number(port));
    const auto cookiePath = options.workingDirectory+QStringLiteral("/data/control_auth_cookie");
    struct stat cookie{}; QVERIFY(!::lstat(QFile::encodeName(cookiePath).constData(),&cookie));
    QVERIFY(S_ISREG(cookie.st_mode)); QCOMPARE(cookie.st_size,off_t(32));
    QCOMPARE(cookie.st_uid,::geteuid()); QCOMPARE(cookie.st_nlink,nlink_t(1)); QVERIFY(!(cookie.st_mode & 0077));
    const auto cookieHash = fileHash(cookiePath);
    child.stop(); QTRY_VERIFY_WITH_TIMEOUT(child.joined(),5000);
    QCOMPARE(child.state(),OwnedTorChild::State::Stopped); QCOMPARE(failed.count(),0); QVERIFY(reaped(pid));

    OwnedTorChild cancelling; QSignalSpy cancelReady(&cancelling,&OwnedTorChild::ready), cancelFailed(&cancelling,&OwnedTorChild::failed);
    qint64 cancelledPid = 0;
    connect(&cancelling,&OwnedTorChild::stateChanged,&cancelling,[&](OwnedTorChild::State state){
      if (state == OwnedTorChild::State::Authenticating) { cancelledPid = cancelling.processId(); cancelling.stop(); }
    });
    options.workingDirectory = generated.path()+QStringLiteral("/cancelled-tor");
    QVERIFY(cancelling.start(options)); QTRY_VERIFY_WITH_TIMEOUT(cancelling.joined(),5000);
    QCOMPARE(cancelling.state(),OwnedTorChild::State::Stopped); QVERIFY(cancelledPid > 0); QVERIFY(reaped(cancelledPid));
    QCOMPARE(cancelReady.count(),0); QCOMPARE(cancelFailed.count(),0);
    for (auto i = files.begin(); i != files.end(); ++i) { QCOMPARE(fileHash(i.key()),i.value().toString().toLatin1()); }
    const QJsonObject observations{{"schema",1},{"status","PASS_REAL_EXTRACTED_OFFLINE_TOR"},
      {"runtime_artifact_sha256",QString::fromLatin1(receiptHash)},{"executable",executable},
      {"child_pid",pid},{"control_port",port},{"raw_version_info",QString::fromLatin1(version)},
      {"cookie_bytes",32},{"cookie_sha256",QString::fromLatin1(cookieHash)},
      {"cookie_device",QString::number(cookie.st_dev)},{"cookie_inode",QString::number(cookie.st_ino)},
      {"joined",true},{"cancelled_child_pid",cancelledPid},{"cancelled_before_authentication",true},
      {"original_profile_used",false},{"system_installed",false},
      {"network_scope","Fixed DisableNetwork1/SocksPort0; only observed control traffic is numeric loopback; no packet-wide absence measurement by this Qt assertion"}};
    QVERIFY(write(output,QJsonDocument(observations).toJson(QJsonDocument::Indented)));
  }
};

int main(int argc, char** argv) {
  QCoreApplication application(argc,argv);
  auto arguments = application.arguments();
  if (arguments.removeOne(QStringLiteral("--real-offline-tor"))) {
    RealOfflineTorTests test;
    return QTest::qExec(&test,arguments);
  }
  OwnedTorChildTests test;
  return QTest::qExec(&test,arguments);
}
#include "owned_child_tests.moc"
