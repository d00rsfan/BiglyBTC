// SPDX-License-Identifier: GPL-2.0-or-later
#include "plugin_release_bundle.hpp"
#include "plugin_activation_store.hpp"
#include "plugin_release_trial.hpp"
#include "plugin_release_slot.hpp"
#include "plugin_release_stage.hpp"
#include "biglybtc/plugin_host.hpp"
#include "plugin_release_test_bundle.hpp"

#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <archive.h>
#include <archive_entry.h>
#include <array>
#include <cerrno>
#include <csignal>
#include <iostream>
#include <string_view>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>

using namespace biglybtc::update;
using namespace biglybtc::update::test;

namespace {
enum class ActivationCut { None, BeforeReceiptSync, BeforeRename, AfterRename };
ActivationCut activationCut = ActivationCut::None;
std::stop_source* cancelPreparedReceipt = nullptr;

void stopAtCut(std::string_view marker) {
  if (::write(STDOUT_FILENO, marker.data(), marker.size()) != static_cast<ssize_t>(marker.size()) ||
      std::raise(SIGSTOP) != 0) {
    ::_exit(2);
  }
}
} // namespace

extern "C" int __real_fsync(int);
extern "C" int __wrap_fsync(int descriptor) {
  if (activationCut == ActivationCut::BeforeReceiptSync || cancelPreparedReceipt) {
    constexpr std::string_view activationPrefix = "{\"active\":";
    std::array<char, activationPrefix.size()> prefix{};
    struct stat file{};
    if (::fstat(descriptor, &file) == 0 && S_ISREG(file.st_mode) && file.st_nlink == 0 &&
        ::pread(descriptor, prefix.data(), prefix.size(), 0) == static_cast<ssize_t>(prefix.size()) &&
        std::string_view(prefix.data(), prefix.size()) == activationPrefix) {
      if (cancelPreparedReceipt) { cancelPreparedReceipt->request_stop(); }
      else { stopAtCut("before anonymous receipt fsync\n"); }
    }
  }
  return __real_fsync(descriptor);
}

extern "C" int __real_renameat2(int, const char*, int, const char*, unsigned int);
extern "C" int __wrap_renameat2(int fromDirectory, const char* from, int toDirectory,
    const char* to, unsigned int flags) {
  const bool switchPointer = activationCut != ActivationCut::None && fromDirectory == toDirectory &&
      std::string_view(from).starts_with(".activate-") && std::string_view(to) == "installed.json";
  if (switchPointer && activationCut == ActivationCut::BeforeRename) {
    stopAtCut("before pointer rename\n");
  }
  const auto result = __real_renameat2(fromDirectory, from, toDirectory, to, flags);
  if (result == 0 && switchPointer && activationCut == ActivationCut::AfterRename) {
    stopAtCut("after pointer rename\n");
  }
  return result;
}

namespace {
void writeFixture(const QString& path, QByteArrayView bytes) {
  QFile file(path);
  require(file.open(QIODevice::WriteOnly) && file.write(bytes.data(), bytes.size()) == bytes.size(),
      "Write the generated slot fixture");
}

QByteArray readFixture(const QString& path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly), "Read the generated slot fixture");
  return file.readAll();
}

void slotCases(const VerifiedPluginBundle& bundle, const QByteArray& package, const PluginTrialProgram& program) {
  using biglybtc::storage::ErrorCode;
  using namespace std::chrono_literals;
  QTemporaryDir area;
  const auto config = area.filePath(QStringLiteral("biglybt.config"));
  const auto original = area.filePath(QStringLiteral("plugins/LocationProvider"));
  const auto native = original + QStringLiteral("/biglybtc");
  const auto pending = native + QStringLiteral("/pending/linux-amd64/1/0.1.0/release.asset");
  const auto version = native + QStringLiteral("/linux-amd64/1/0.1.0");
  const auto descriptor = original + QStringLiteral("/plugin.properties");
  const auto jar = original + QStringLiteral("/azlocprov_0.6.9.jar");
  const auto pointer = native + QStringLiteral("/installed.json");
  require(area.isValid() && QDir().mkpath(original), "Prepare the generated original location plugin");
  writeFixture(config, "de");
  writeFixture(descriptor, "plugin.id=azlocprov\r\nplugin.version=0.6.9\n");
  writeFixture(jar, "untouched synthetic original JAR");
  const auto lease = biglybtc::storage::ProfileLease::acquire(area.path().toStdString());
  require(bool(lease), "Hold the generated original profile lease");
  const auto publish = [&](std::uint64_t capabilities = BT_CAP_LOCATION) {
    return publishPluginReleaseSlot(*lease, "LocationProvider", bundle.release(), program, capabilities, 4s);
  };
  const auto unprepared = publish();
  require(!unprepared && unprepared.error().code == ErrorCode::Missing && !QFileInfo::exists(native),
      "A version requires an already staged package");
  QBuffer source;
  source.setData(package);
  require(source.open(QIODevice::ReadOnly) && stagePluginRelease(*lease, "LocationProvider", bundle.release(), source),
      "Stage the signed bundle using the actual pending profile sink");
  const auto badTrial = publish(BT_CAP_SCHEDULER);
  require(!badTrial && badTrial.error().code == ErrorCode::Unsupported && !QFileInfo::exists(version),
      "A rejected module trial never creates an installable version");

  const auto published = publish();
  require(published && *published == PluginSlotPublication::Published, "Publish a complete native version after the isolated trial");
  const auto module = version + QStringLiteral("/plugin.so");
  const auto manifest = version + QStringLiteral("/manifest.json");
  require(readFixture(module) == bundle.module() && readFixture(manifest) == bundle.manifest() &&
      !QFileInfo::exists(pointer) && readFixture(pending) == package, "Persist the exact immutable files beside the original JAR");
  struct stat before{}, after{};
  require(::stat(module.toUtf8().constData(), &before) == 0 && (before.st_mode & 0777) == 0400, "Capture the immutable module identity");
  const auto retry = publish();
  require(retry && *retry == PluginSlotPublication::AlreadyPresent && ::stat(module.toUtf8().constData(), &after) == 0 &&
      before.st_ino == after.st_ino, "Retry retains the exact prior native inode");

  writeFixture(pointer, "previous active version retained");
  require(::chmod(manifest.toUtf8().constData(), 0600) == 0, "Prepare a conflicting generated manifest");
  writeFixture(manifest, "different immutable version receipt");
  require(::chmod(manifest.toUtf8().constData(), 0400) == 0, "Close the generated conflicting file");
  const auto conflict = publish();
  require(!conflict && conflict.error().code == ErrorCode::Conflict &&
      readFixture(manifest) == "different immutable version receipt" && readFixture(module) == bundle.module() &&
      readFixture(pointer) == "previous active version retained" && readFixture(config) == "de" &&
      readFixture(descriptor) == "plugin.id=azlocprov\r\nplugin.version=0.6.9\n" && readFixture(jar) == "untouched synthetic original JAR",
      "Preserve the original plugin and current activation pointer on a same-version conflict");
  std::cout << "PASS isolated trial and immutable version publication beside existing Java artifacts\n";
}

int prepareInterruptedCandidate(const QString& profile, const QString& executable) {
  using namespace std::chrono_literals;
  const QDir area(profile);
  const ReleaseFixture target;
  const auto payload = readFixture(area.filePath(QStringLiteral("trial.catalogue")));
  const auto sig = readFixture(area.filePath(QStringLiteral("trial.signature")));
  const auto key = readFixture(area.filePath(QStringLiteral("trial.public")));
  const auto release = verifyPluginReleaseCatalogue(payload, sig, key, target.target, 12,
      QDateTime::currentSecsSinceEpoch());
  const auto lease = biglybtc::storage::ProfileLease::acquire(profile.toStdString());
  const PluginTrialProgram program{executable, QCryptographicHash::hash(readFixture(executable), QCryptographicHash::Sha256)};
  require(release && lease, "Acquire the generated profile and exact signed interrupted update");
  const auto staged = publishPluginReleaseSlot(*lease, "LocationProvider", *release, program, BT_CAP_LOCATION, 4s);
  require(staged && *staged == PluginSlotPublication::Published, "Durably prepare the slot in the process that will be interrupted");
  std::cout << "prepared\n" << std::flush;
  QFile wait;
  require(wait.open(STDIN_FILENO, QIODevice::ReadOnly), "Keep the installer process at the prepared boundary");
  return wait.read(1).isEmpty() ? 2 : 3;
}

int interruptPointerSwitch(const QString& profile, const QString& executable, ActivationCut cut) {
  using namespace std::chrono_literals;
  const ReleaseFixture target;
  const auto key = readFixture(QDir(profile).filePath(QStringLiteral("trial.public")));
  const auto lease = biglybtc::storage::ProfileLease::acquire(profile.toStdString());
  require(bool(lease), "Acquire the generated profile in the interrupted activation process");
  const PluginTrialProgram program{executable,
      QCryptographicHash::hash(readFixture(executable), QCryptographicHash::Sha256)};
  activationCut = cut;
  const auto activated = activateLatestStagedPluginRelease(*lease, "LocationProvider", target.target, key,
      program, BT_CAP_LOCATION, 4s);
  if (!activated) { throw std::runtime_error(activated.error().message); }
  throw std::runtime_error("The activation pointer cut was missed");
}

void activationCases(ReleaseFixture& signing, const VerifiedPluginBundle& firstBundle, const QByteArray& firstPackage,
    const PluginTrialProgram& program, const QByteArray& nextModule) {
  using biglybtc::storage::ErrorCode;
  using namespace std::chrono_literals;
  QTemporaryDir area;
  const auto config = area.filePath(QStringLiteral("biglybt.config"));
  const auto original = area.filePath(QStringLiteral("plugins/LocationProvider"));
  const auto native = original + QStringLiteral("/biglybtc");
  const auto pointer = native + QStringLiteral("/installed.json");
  const auto firstFile = native + QStringLiteral("/linux-amd64/1/0.1.0/plugin.so");
  const auto nextFile = native + QStringLiteral("/linux-amd64/1/0.2.0/plugin.so");
  const auto java = original + QStringLiteral("/azlocprov_0.6.9.jar");
  require(area.isValid() && QDir().mkpath(original), "Prepare a generated Java installation for activation");
  writeFixture(config, "de");
  writeFixture(original + QStringLiteral("/plugin.properties"), "plugin.id=azlocprov\nplugin.version=0.6.9\n");
  writeFixture(java, "untouched Java installation");
  auto lease = biglybtc::storage::ProfileLease::acquire(area.path().toStdString());
  require(bool(lease), "Keep the original Java profile excluded during the test");
  const auto stage = [&](const VerifiedPluginRelease& release, const QByteArray& bytes) {
    QBuffer source;
    source.setData(bytes);
    return source.open(QIODevice::ReadOnly) &&
        stagePluginRelease(*lease, "LocationProvider", release, source) == PluginReleaseStage::Published;
  };
  const auto activate = [&](const VerifiedPluginRelease& release, std::uint64_t capabilities = BT_CAP_LOCATION) {
    return activatePluginRelease(*lease, "LocationProvider", release, program, capabilities, 4s);
  };
  const auto rollback = [&](QByteArrayView key, std::uint64_t capabilities = BT_CAP_LOCATION) {
    return rollbackPluginRelease(*lease, "LocationProvider", signing.target, key, program, capabilities, 4s);
  };
  const auto key = signing.signer.publicKey();
  const auto startSelected = [&](QStringView version) {
    const auto selected = openActivatedPluginRelease(*lease, "LocationProvider", signing.target, key);
    require(selected && *selected && (**selected).identity.release.asset().version == version &&
        (**selected).profile->sharesOwnership(*lease), "Open the exact active module with the original profile lock");
    const bt_plugin_options_v1 options{sizeof(bt_plugin_options_v1), BT_PLUGIN_ABI_V1,
        (**selected).originalDirectory.get(), 0, "0.6.9", nullptr, nullptr};
    const auto started = biglybtc::plugins::Module::loadDescriptor((**selected).descriptor.get(), "azlocprov",
        BT_CAP_LOCATION, options, version.toString().toStdString());
    require(bool(started), "The normal plugin host starts and joins the selected authenticated descriptor");
  };
  const auto absent = openActivatedPluginRelease(*lease, "LocationProvider", signing.target, key);
  require(absent && !*absent, "A Java plugin with no activation marker supplies no native startup artifact");
  const auto resume = [&] {
    return activateLatestStagedPluginRelease(*lease, "LocationProvider", signing.target, key,
        program, BT_CAP_LOCATION, 4s);
  };
  const auto noPending = resume();
  require(noPending && !*noPending, "A Java installation without a staged native artifact needs no activation");
  const auto firstRelease = firstBundle.release();
  require(stage(firstRelease, firstPackage), "Stage the first actual signed module");
  const auto interruptedPublish = original + QStringLiteral("/biglybtc/pending/linux-amd64/1/9.9.9");
  require(QDir().mkpath(interruptedPublish) && ::chmod(interruptedPublish.toUtf8().constData(), 0700) == 0,
      "Reproduce an owned incomplete pending directory");
  writeFixture(interruptedPublish + QStringLiteral("/release.asset"), firstPackage);
  require(QFile::setPermissions(interruptedPublish + QStringLiteral("/release.asset"), QFileDevice::ReadOwner),
      "Reproduce a read-only asset left before its proof was published");
  const auto first = resume();
  if (!first) { throw std::runtime_error(first.error().message); }
  require(*first && (**first).highestSequence == 12 &&
      (**first).active->release.asset().version == u"0.1.0" && !(**first).previous &&
      readFixture(firstFile) == firstBundle.module(), "Activate the first verified module");
  startSelected(u"0.1.0");
  const auto initialPointer = readFixture(pointer);
  struct stat initial{}, current{};
  require(::stat(pointer.toUtf8().constData(), &initial) == 0 && (initial.st_mode & 0777) == 0400,
      "Capture the installed activation pointer identity");
  const auto noPrevious = rollback(key);
  require(!noPrevious && noPrevious.error().code == ErrorCode::Missing && readFixture(pointer) == initialPointer,
      "Refuse rollback until a previous native version exists");

  signing.version = QStringLiteral("0.2.0");
  const auto secondPackage = archiveFixture({{"manifest.json", signing.manifest(nextModule)}, {"plugin.so", nextModule}});
  const auto secondRelease = signing.select(secondPackage, 15);
  require(stage(secondRelease, secondPackage), "Stage the second signed version");
  writeFixture(area.filePath(QStringLiteral("trial.catalogue")), secondRelease.catalogue());
  writeFixture(area.filePath(QStringLiteral("trial.signature")), secondRelease.signature());
  writeFixture(area.filePath(QStringLiteral("trial.public")), key);
  const auto terminateAt = [&](const QString& command, QByteArrayView marker) {
    lease = std::unexpected(biglybtc::storage::Error{ErrorCode::Missing, "Parent released the temporary lease"});
    QProcess interrupted;
    interrupted.start(QCoreApplication::applicationFilePath(), {command, area.path(), program.executable});
    require(interrupted.waitForStarted(2000) && interrupted.waitForReadyRead(5000) &&
        interrupted.readAllStandardOutput() == marker, "Reach the requested cut inside a separate installer process");
    interrupted.kill();
    require(interrupted.waitForFinished(2000) && interrupted.exitStatus() == QProcess::CrashExit,
        "Force the pending installer to stop and join it");
    lease = biglybtc::storage::ProfileLease::acquire(area.path().toStdString());
    require(bool(lease), "Reopen the profile when the installer holder exits");
  };
  terminateAt(QStringLiteral("--prepare-candidate"), "prepared\n");
  require(readFixture(pointer) == initialPointer && readFixture(nextFile) == nextModule,
      "Reopen the shared profile with the previous pointer and complete prepared version after process death");
  startSelected(u"0.1.0");
  const auto rejected = activate(secondRelease, BT_CAP_SCHEDULER);
  require(!rejected && rejected.error().code == ErrorCode::Unsupported && readFixture(pointer) == initialPointer,
      "A failed activation trial preserves the existing pointer");
  signing.version = QStringLiteral("0.3.0");
  const auto duplicateSequence = signing.select(secondPackage, 15);
  require(stage(duplicateSequence, secondPackage), "Stage a generated distinct signed version at the same sequence");
  const auto duplicate = resume();
  require(!duplicate && duplicate.error().code == ErrorCode::Conflict && readFixture(pointer) == initialPointer,
      "Refuse ambiguous newest pending proofs before activation");
  const auto duplicateProof = original + QStringLiteral("/biglybtc/pending/linux-amd64/1/0.3.0/proof-15.signed");
  require(QFile::remove(duplicateProof), "Remove only the generated ambiguous proof fixture");
  const auto currentProof = original + QStringLiteral("/biglybtc/pending/linux-amd64/1/0.2.0/proof-15.signed");
  const auto originalProof = readFixture(currentProof);
  auto changedProof = originalProof;
  changedProof[changedProof.size() - 1] ^= 1;
  require(::chmod(currentProof.toUtf8().constData(), 0600) == 0, "Allow a generated staged-proof mutation");
  writeFixture(currentProof, changedProof);
  require(::chmod(currentProof.toUtf8().constData(), 0400) == 0, "Retain the expected immutable mode for the changed fixture");
  const auto tampered = resume();
  require(!tampered && tampered.error().code == ErrorCode::Conflict && readFixture(pointer) == initialPointer,
      "Refuse a tampered newest proof without changing the installed version");
  require(::chmod(currentProof.toUtf8().constData(), 0600) == 0, "Allow restoring only the generated staged proof");
  writeFixture(currentProof, originalProof);
  require(::chmod(currentProof.toUtf8().constData(), 0400) == 0, "Restore the original signed proof mode");
  std::stop_source beforePublication;
  cancelPreparedReceipt = &beforePublication;
  const auto cancelled = activatePluginRelease(*lease, "LocationProvider", secondRelease, program, BT_CAP_LOCATION,
      4s, beforePublication.get_token());
  cancelPreparedReceipt = nullptr;
  require(!cancelled && cancelled.error().code == ErrorCode::Cancelled && beforePublication.stop_requested() &&
      readFixture(pointer) == initialPointer &&
      QDir(native).entryList({QStringLiteral(".activate-*")}, QDir::Files | QDir::Hidden).isEmpty(),
      "Cancel a candidate after writing its unnamed receipt and keep the previous pointer");
  startSelected(u"0.1.0");
  std::cout << "PASS cancellation before publishing the activation receipt keeps the installed module\n";
  terminateAt(QStringLiteral("--before-pointer-fsync"), "before anonymous receipt fsync\n");
  require(readFixture(pointer) == initialPointer &&
      QDir(native).entryList({QStringLiteral(".activate-*")}, QDir::Files | QDir::Hidden).isEmpty(),
      "Process death before receipt sync leaves the prior pointer and no named draft");
  startSelected(u"0.1.0");
  std::cout << "PASS unpublished activation receipt is removed on process death before sync\n";
  terminateAt(QStringLiteral("--before-pointer-rename"), "before pointer rename\n");
  require(readFixture(pointer) == initialPointer && readFixture(nextFile) == nextModule,
      "Keep the previous activation and complete new slot after death before rename");
  startSelected(u"0.1.0");
  const auto preparedPointers = QDir(native).entryList({QStringLiteral(".activate-*")}, QDir::Files | QDir::Hidden);
  require(preparedPointers.size() == 1, "Find the exact generated staging file before the pointer switch");
  const auto receipt = readPluginActivationState(readFixture(QDir(native).filePath(preparedPointers.front())),
      key, signing.target, QDateTime::currentSecsSinceEpoch());
  require(receipt && receipt->highestSequence == 15 && receipt->active && receipt->previous &&
      receipt->active->release.asset().version == u"0.2.0" && receipt->previous->release.asset().version == u"0.1.0",
      "Authenticate the fully written pointer that has never been published");

  const auto unrelatedPath = QDir(native).filePath(QStringLiteral(".activate-00000000-0000-4000-8000-000000000001"));
  writeFixture(unrelatedPath, initialPointer);
  require(::chmod(unrelatedPath.toUtf8().constData(), 0400) == 0, "Make a generated unrelated signed receipt immutable");
  const auto symbolic = QDir(native).filePath(QStringLiteral(".activate-00000000-0000-4000-8000-000000000002"));
  const auto preparedPath = QDir(native).filePath(preparedPointers.front());
  require(::symlink(preparedPath.toUtf8().constData(), symbolic.toUtf8().constData()) == 0,
      "Add a generated symbolic lookalike for the abandoned receipt");
  terminateAt(QStringLiteral("--after-pointer-rename"), "after pointer rename\n");
  require(!QFileInfo::exists(preparedPath) && readFixture(unrelatedPath) == initialPointer &&
      QFileInfo(symbolic).isSymLink(),
      "Retry reclaims the same interrupted receipt and leaves unrelated and symbolic entries alone");
  std::cout << "PASS activation retry reclaims only the matching abandoned receipt\n";
  const auto second = readPluginInstallation(*lease, "LocationProvider", signing.target, key);
  require(second && second->highestSequence == 15 && second->active && second->previous &&
      second->active->release.asset().version == u"0.2.0" && second->previous->release.asset().version == u"0.1.0" &&
      ::stat(pointer.toUtf8().constData(), &current) == 0 && current.st_ino != initial.st_ino,
      "Atomically select the prepared version and retain the previous native artifact");
  startSelected(u"0.2.0");
  std::cout << "PASS process death before and after atomic activation pointer rename\n";
  const auto replayedStage = activateStagedPluginRelease(*lease, "LocationProvider", signing.target, u"0.1.0", 12,
      key, program, BT_CAP_LOCATION, 4s);
  require(!replayedStage, "Offline activation preserves the highest previously accepted sequence");
  const auto noNewCandidate = resume();
  require(noNewCandidate && !*noNewCandidate, "Previously accepted sequences and asset-only slots do not trigger another activation");
  std::cout << "PASS discover newest authenticated staged release after restart without external version or sequence\n";
  std::cout << "PASS activate actual staged native versions and resume after installer interruption using durable proof\n";
  const auto nextPointer = readFixture(pointer);
  const SigningFixture unrelated;
  const auto wrongKey = rollback(unrelated.publicKey());
  const auto failed = rollback(key, BT_CAP_SCHEDULER);
  require(!wrongKey && !failed && readFixture(pointer) == nextPointer, "Preserve the current pointer after rollback refusal");
  const auto reverted = rollback(key);
  require(reverted && reverted->highestSequence == 15 && reverted->active->release.asset().version == u"0.1.0" &&
      reverted->previous->release.asset().version == u"0.2.0", "Rollback uses the existing and rechecked native version");
  startSelected(u"0.1.0");
  const auto rollbackPointer = readFixture(pointer);
  const auto replay = activate(secondRelease);
  require(!replay && replay.error().code == ErrorCode::Conflict && readFixture(pointer) == rollbackPointer,
      "A previously accepted catalogue cannot replay after rollback");
  require(::chmod(nextFile.toUtf8().constData(), 0600) == 0, "Prepare tampered inactive module");
  writeFixture(nextFile, "tampered test module");
  require(::chmod(nextFile.toUtf8().constData(), 0400) == 0, "Close the changed fixture module");
  const auto unusable = rollback(key);
  require(!unusable && unusable.error().code == ErrorCode::Conflict && readFixture(pointer) == rollbackPointer &&
      readFixture(firstFile) == firstBundle.module() && readFixture(java) == "untouched Java installation" && readFixture(config) == "de",
      "A changed previous native module cannot replace the active version or alter Java artifacts");
  startSelected(u"0.1.0");
  std::cout << "PASS interrupted version preparation, atomic activation, joined rollback and signed startup\n";
}
} // namespace

int main(int argc, char** argv) try {
  QCoreApplication application(argc, argv);
  if (argc == 4) {
    const auto command = QByteArrayView(argv[1]);
    const auto profile = QString::fromLocal8Bit(argv[2]);
    const auto executable = QString::fromLocal8Bit(argv[3]);
    if (command == "--prepare-candidate") { return prepareInterruptedCandidate(profile, executable); }
    if (command == "--before-pointer-fsync") {
      return interruptPointerSwitch(profile, executable, ActivationCut::BeforeReceiptSync);
    }
    if (command == "--before-pointer-rename") {
      return interruptPointerSwitch(profile, executable, ActivationCut::BeforeRename);
    }
    if (command == "--after-pointer-rename") {
      return interruptPointerSwitch(profile, executable, ActivationCut::AfterRename);
    }
  }
  require(argc >= 2 && argc <= 5 && (argc != 4 || QByteArrayView(argv[3]) == "--slot") &&
      (argc != 5 || QByteArrayView(argv[3]) == "--activation"),
      "Pass the owned native module and optional trial build");
  QFile fixture(QString::fromLocal8Bit(argv[1]));
  require(fixture.open(QIODevice::ReadOnly), "Read the generated SDK module");
  const auto module = fixture.readAll();
  require(module.size() > 64 && module.size() < 1024 * 1024, "Read a bounded ELF fixture");
  ReleaseFixture fixtureRelease;
  const auto manifest = fixtureRelease.manifest(module);
  const auto intact = archiveFixture({{"manifest.json", manifest}, {"plugin.so", module}});
  const auto selected = fixtureRelease.select(intact);
  const auto check = [](const QByteArray& bytes, const VerifiedPluginRelease& release) {
    QBuffer source;
    source.setData(bytes);
    require(source.open(QIODevice::ReadOnly), "Open signed package input");
    return readPluginReleaseBundle(source, release);
  };

  const auto bundle = check(intact, selected);
  require(bundle && bundle->module() == module && bundle->manifest() == manifest && bundle->release().asset().sha256 == selected.asset().sha256, "Return only the verified native archive bytes");
  auto damaged = intact;
  damaged[damaged.size() / 2] ^= 1;
  require(check(damaged, selected) == std::unexpected(PluginBundleError::AssetIdentity), "Reject changed compressed bytes before parsing");

  const auto expect = [&](const QByteArray& bytes, PluginBundleError error) {
    const auto result = check(bytes, fixtureRelease.select(bytes));
    require(!result && result.error() == error, "Reject a structurally invalid signed package");
  };
  expect(archiveFixture({{"plugin.so", module}, {"manifest.json", manifest}}, false), PluginBundleError::Format);
  expect(archiveFixture({{"plugin.so", module}}), PluginBundleError::Format);
  expect(archiveFixture({{"manifest.json", manifest}, {"../plugin.so", module}}), PluginBundleError::UnexpectedEntry);
  expect(archiveFixture({{"manifest.json", manifest}, {"plugin.so", {}, "original.jar"}}), PluginBundleError::UnexpectedEntry);
  expect(archiveFixture({{"manifest.json", manifest}, {"plugin.so", module}, {"plugin.so", module}}), PluginBundleError::UnexpectedEntry);
  expect(archiveFixture({{"manifest.json", manifest}, {"plugin.so", module}, {"java.jar", "opaque fixture"}}),
      PluginBundleError::UnexpectedEntry);
  expect(archiveFixture({{"manifest.json", fixtureRelease.manifest(module, QStringLiteral("xmwebui"))}, {"plugin.so", module}}),
      PluginBundleError::ModuleIdentity);
  auto wrongPlatform = module;
  wrongPlatform[18] = 3; // The fixture has an ELF64 header. Change its declared machine.
  expect(archiveFixture({{"manifest.json", fixtureRelease.manifest(wrongPlatform)}, {"plugin.so", wrongPlatform}}),
      PluginBundleError::ModuleIdentity);
  auto wrongDigest = module;
  wrongDigest[100] ^= 1;
  expect(archiveFixture({{"manifest.json", manifest}, {"plugin.so", wrongDigest}}), PluginBundleError::ModuleIdentity);
  if (argc >= 3) {
    using namespace std::chrono_literals;
    const auto executable = QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath();
    QFile helper(executable);
    require(helper.open(QIODevice::ReadOnly), "Read the actual built-in trial executable");
    const PluginTrialProgram program{executable, QCryptographicHash::hash(helper.readAll(), QCryptographicHash::Sha256)};
    if (argc == 5) {
      const auto nextModule = readFixture(QString::fromLocal8Bit(argv[4]));
      activationCases(fixtureRelease, *bundle, intact, program, nextModule);
      return 0;
    }
    if (argc == 4) {
      slotCases(*bundle, intact, program);
      return 0;
    }
    require(bool(trialPluginRelease(*bundle, program, BT_CAP_LOCATION, 4s)),
        "Trial loading and shutdown qualify the signed native module");
    require(trialPluginRelease(*bundle, program, BT_CAP_SCHEDULER, 4s) == std::unexpected(PluginTrialError::Rejected),
        "Reject module capabilities outside the exact trial request");
    auto mismatchedExecutable = program;
    mismatchedExecutable.sha256[0] ^= 1;
    require(trialPluginRelease(*bundle, mismatchedExecutable, BT_CAP_LOCATION, 4s) ==
        std::unexpected(PluginTrialError::ProgramIdentity), "Refuse a changed probe before executing it");
    ReleaseFixture wrongVersion;
    wrongVersion.version = QStringLiteral("0.2.0");
    const auto foreign = archiveFixture({{"plugin.so", module}, {"manifest.json", wrongVersion.manifest(module)}});
    const auto versioned = check(foreign, wrongVersion.select(foreign));
    require(versioned && trialPluginRelease(*versioned, program, BT_CAP_LOCATION, 4s) ==
        std::unexpected(PluginTrialError::Rejected), "Refuse a mismatched native ABI descriptor version");

    QTemporaryDir area;
    const QByteArray sleeper("#!/usr/bin/sh\nexec /usr/bin/sleep 2\n");
    const auto testPath = area.filePath(QStringLiteral("long-trial"));
    QFile slow(testPath);
    require(area.isValid() && slow.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
        slow.write(sleeper) == sleeper.size() && slow.setPermissions(QFileDevice::ReadOwner | QFileDevice::ExeOwner),
        "Create a bounded generated timeout child");
    slow.close();
    require(trialPluginRelease(*bundle, {testPath, QCryptographicHash::hash(sleeper, QCryptographicHash::Sha256)},
        BT_CAP_LOCATION, 100ms) == std::unexpected(PluginTrialError::Deadline), "Terminate and join a timed-out trial");
    const auto marker = area.filePath(QStringLiteral("trial.pid"));
    require(!marker.contains(u'\''), "Generated marker path must be safe for a quoted shell argument");
    const auto stoppablePath = area.filePath(QStringLiteral("stoppable-trial"));
    const auto stoppable = "#!/usr/bin/sh\nprintf '%s\\n' \"$$\" > '" + marker.toUtf8() +
        "'\nkill -STOP \"$$\"\n";
    writeFixture(stoppablePath, stoppable);
    require(::chmod(stoppablePath.toUtf8().constData(), 0500) == 0, "Make the generated stoppable trial executable");
    std::stop_source cancellation;
    std::jthread cancel([&] {
      const auto limit = std::chrono::steady_clock::now() + 2s;
      while (!QFileInfo::exists(marker) && std::chrono::steady_clock::now() < limit) {
        std::this_thread::sleep_for(5ms);
      }
      cancellation.request_stop();
    });
    const auto cancelled = trialPluginRelease(*bundle,
        {stoppablePath, QCryptographicHash::hash(stoppable, QCryptographicHash::Sha256)},
        BT_CAP_LOCATION, 4s, cancellation.get_token());
    cancel.join();
    bool validPid = false;
    const auto pid = readFixture(marker).trimmed().toInt(&validPid);
    require(cancelled == std::unexpected(PluginTrialError::Cancelled) && validPid && pid > 0 &&
        ::kill(pid, 0) == -1 && errno == ESRCH, "Cancel and reap the actual stopped trial child");
    std::cout << "PASS cancellation reaps the running native trial before activation\n";
    std::cout << "PASS separately loaded and joined native trial, capabilities, version and deadline\n";
  }
  std::cout << "PASS signed Zstandard plugin archive, exact native module and manifest identity\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
