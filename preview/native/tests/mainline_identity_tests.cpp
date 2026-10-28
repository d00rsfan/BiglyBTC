#include "mainline_identity.hpp"
#include "plugin_settings.hpp"

#include <QtTest>

#include <array>
#include <cerrno>
#include <optional>
#include <stdexcept>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
using namespace biglybtc;
using Source = storage::OriginalMainlineIdentity;
using State = storage::OriginalMainlineIdentityState;
const QByteArray Root = "00112233445566778899aabbccddeeff01234567";
void require(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }
void write(const QString& path, const QByteArray& bytes) {
  require(QDir().mkpath(QFileInfo(path).absolutePath()), "Cannot create owned identity parent");
  QFile file(path); require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Cannot write owned identity");
  require(file.write(bytes) == bytes.size() && file.flush(), "Cannot publish owned identity fixture");
}
QByteArray read(const QString& path) {
  QFile file(path); require(file.open(QIODevice::ReadOnly), "Cannot read owned identity fixture");
  auto bytes = file.readAll(); require(file.error() == QFileDevice::NoError, "Cannot read all owned identity bytes");
  return bytes;
}
struct Snapshot { struct stat stat{}; QByteArray bytes; };
Snapshot snapshot(const QString& path) {
  Snapshot result; const auto name = QFile::encodeName(path);
  require(::lstat(name.constData(), &result.stat) == 0, "Cannot stat owned identity");
  if (S_ISREG(result.stat.st_mode)) { result.bytes = read(path); }
  return result;
}
bool same(const Snapshot& a, const Snapshot& b) {
  const auto& x = a.stat; const auto& y = b.stat;
  return x.st_dev == y.st_dev && x.st_ino == y.st_ino && x.st_mode == y.st_mode
      && x.st_uid == y.st_uid && x.st_gid == y.st_gid && x.st_nlink == y.st_nlink && x.st_size == y.st_size
      && x.st_mtim.tv_sec == y.st_mtim.tv_sec && x.st_mtim.tv_nsec == y.st_mtim.tv_nsec
      && x.st_ctim.tv_sec == y.st_ctim.tv_sec && x.st_ctim.tv_nsec == y.st_ctim.tv_nsec && a.bytes == b.bytes;
}
struct Profile {
  QTemporaryDir root{QStringLiteral("/tmp/biglybtc-original-mainline-identity-XXXXXX")};
  QString path;
  std::optional<storage::ProfileLease> lease;
  Profile() {
    require(root.isValid(), "Cannot create owned identity root"); path = root.filePath("profile");
    require(QDir().mkpath(path), "Cannot create owned identity profile");
    auto held = storage::ProfileLease::acquire(path.toStdString()); require(held.has_value(), "Cannot lease owned identity profile");
    lease.emplace(std::move(*held));
  }
  QString leaf(const QString& directory = "mldht") const { return path + "/plugins/" + directory + "/baseID.config"; }
};
qsizetype descriptors() { return QDir("/proc/self/fd").entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size(); }
} // namespace

class MainlineIdentityTests final : public QObject {
  Q_OBJECT
private slots:
  void absentSourceNeverCreatesOrFallsBackToInventedBackup() {
    Profile p; auto missing = Source::load(*p.lease, "mldht"); QVERIFY(missing);
    QCOMPARE(missing->snapshot().state, State::Missing); QVERIFY(!missing->snapshot().root);
    QVERIFY(missing->snapshot().originalBytes.empty()); QVERIFY(missing->validate(*p.lease));
    QVERIFY(!QFileInfo::exists(p.path + "/plugins")); QVERIFY(!missing->select({}));
    QVERIFY(QDir().mkpath(p.path + "/plugins")); auto missingDirectory = Source::load(*p.lease, "mldht"); QVERIFY(missingDirectory);
    QCOMPARE(missingDirectory->snapshot().state, State::Missing); QVERIFY(missingDirectory->validate(*p.lease));
    write(p.leaf() + ".bak", Root + '\n'); write(p.leaf() + ".saving", Root + '\n');
    QVERIFY(!missingDirectory->validate(*p.lease));
    auto noBackup = Source::load(*p.lease, "mldht"); QVERIFY(noBackup);
    QCOMPARE(noBackup->snapshot().state, State::Missing); QVERIFY(noBackup->validate(*p.lease));
    QVERIFY(!QFileInfo::exists(p.leaf()));
    QVERIFY(!missing->validate(*p.lease)); // Captured absence is not refreshed into authority.
    write(p.leaf(), Root + '\n'); QVERIFY(!noBackup->validate(*p.lease));
  }
  void importedBytesAndQualifiedFirstFreeSlotsStaySeparateFromWireAuthority() {
    Profile p; const auto raw = QByteArray("# original retained comment\r\n \t") + Root + "\t \r\n# tail\n";
    write(p.leaf(), raw); const auto before = snapshot(p.leaf());
    auto loaded = Source::load(*p.lease, "mldht"); QVERIFY2(loaded, loaded ? "" : loaded.error().message.c_str());
    QCOMPARE(loaded->snapshot().state, State::Imported); QVERIFY(loaded->snapshot().root);
    QCOMPARE(loaded->snapshot().originalBytes, raw.toStdString());
    auto first = loaded->select({}); QVERIFY(first); QCOMPARE(first->index, std::uint32_t{0});
    QVERIFY(first->key == *loaded->snapshot().root);
    std::vector<storage::MainlineIdentityKey> active{first->key}; auto second = loaded->select(active); QVERIFY(second);
    QCOMPARE(second->index, std::uint32_t{1}); auto expected = first->key; expected[0] ^= 0x80; QVERIFY(second->key == expected);
    active.push_back(second->key); active.erase(active.begin()); auto reused = loaded->select(active); QVERIFY(reused);
    QCOMPARE(reused->index, std::uint32_t{0}); QVERIFY(reused->key == first->key);
    active.assign(4097, first->key); auto bounded = loaded->select(active); QVERIFY(!bounded);
    QCOMPARE(bounded.error().code, storage::ErrorCode::Unsupported);
    QVERIFY(loaded->validate(*p.lease)); QVERIFY(same(before, snapshot(p.leaf())));
    // Unrelated plugin/profile activity changes parent timestamps, not identity.
    write(p.path + "/plugins/another/value", "unrelated"); write(p.path + "/separate.config", "de");
    QVERIFY(loaded->validate(*p.lease)); QVERIFY(same(before, snapshot(p.leaf())));
  }
  void originalDirectoryAndRestorePreferenceComeFromQualifiedSettings() {
    Profile p;
    const QByteArray key("Plugin.external-id.alwaysRestoreID");
    const auto prefix = QByteArray("d") + QByteArray::number(key.size()) + ':' + key;
    const auto configured = core::Document::parse((prefix + "i1ee").toStdString()); QVERIFY(configured);
    auto settings = storage::readOriginalMldhtSettings(*configured, "external-id"); QVERIFY(settings); QVERIFY(settings->alwaysRestoreId);
    write(p.leaf("external-id"), Root + '\n'); write(p.leaf(), "not-an-id\n");
    auto loaded = Source::load(*p.lease, settings->binding.directoryName, settings->alwaysRestoreId); QVERIFY(loaded);
    QCOMPARE(loaded->snapshot().pluginDirectory, std::string("external-id")); QCOMPARE(loaded->snapshot().state, State::Imported);
    QVERIFY(loaded->validate(*p.lease));
    const auto disabledConfig = core::Document::parse((prefix + "i0ee").toStdString()); QVERIFY(disabledConfig);
    auto disabledSettings = storage::readOriginalMldhtSettings(*disabledConfig, "external-id"); QVERIFY(disabledSettings); QVERIFY(!disabledSettings->alwaysRestoreId);
    // A would-be unsafe original tree cannot be opened in disabled mode.
    Profile disabled; const auto target = QFile::encodeName(p.path + "/plugins"); const auto link = QFile::encodeName(disabled.path + "/plugins");
    QVERIFY(::symlink(target.constData(), link.constData()) == 0);
    auto skipped = Source::load(*disabled.lease, disabledSettings->binding.directoryName, disabledSettings->alwaysRestoreId); QVERIFY(skipped);
    QCOMPARE(skipped->snapshot().state, State::Disabled); QVERIFY(!skipped->snapshot().root); QVERIFY(skipped->validate(*disabled.lease));
    QVERIFY(skipped->snapshot().originalBytes.empty()); QVERIFY(!skipped->select({}));
    auto unsafeWhenEnabled = Source::load(*disabled.lease, "external-id"); QVERIFY(!unsafeWhenEnabled);
    QCOMPARE(unsafeWhenEnabled.error().code, storage::ErrorCode::UnsafePath);
    auto defaults = core::Document::parse("de"); QVERIFY(defaults); auto originalDefaults = storage::readOriginalMldhtSettings(*defaults, "mldht");
    QVERIFY(originalDefaults && originalDefaults->alwaysRestoreId);
  }
  void originalZeroRootRemainsAParsedSourceNotAnLtWireGuarantee() {
    Profile p; write(p.leaf(), QByteArray(40, '0') + '\n');
    auto loaded = Source::load(*p.lease, "mldht"); QVERIFY(loaded); QVERIFY(loaded->snapshot().root);
    auto slot = loaded->select({}); QVERIFY(slot); QCOMPARE(slot->index, std::uint32_t{0});
    QVERIFY(slot->key == storage::MainlineIdentityKey{}); QVERIFY(loaded->validate(*p.lease));
  }
  void malformedAndUnsafeSourcesAreRefusedWithoutOriginalFallbackWrite_data() {
    QTest::addColumn<QByteArray>("source");
    QTest::newRow("empty") << QByteArray{};
    QTest::newRow("missing-key") << QByteArray("comments only\n");
    QTest::newRow("early-overlong-candidate") << (Root + "0\n" + Root + '\n');
    QTest::newRow("invalid-utf8-after-key") << (Root + '\n' + QByteArray::fromHex("ff"));
  }
  void malformedAndUnsafeSourcesAreRefusedWithoutOriginalFallbackWrite() {
    QFETCH(QByteArray, source); Profile p; write(p.leaf(), source); const auto before = snapshot(p.leaf());
    auto loaded = Source::load(*p.lease, "mldht"); QVERIFY(!loaded); QCOMPARE(loaded.error().code, storage::ErrorCode::InvalidFormat);
    QVERIFY(same(before, snapshot(p.leaf()))); QVERIFY(!QFileInfo::exists(p.leaf() + ".bak"));
    QVERIFY(!QFileInfo::exists(p.leaf() + ".tmp")); QVERIFY(!QFileInfo::exists(p.leaf() + ".saving"));
    QVERIFY(!Source::load(*p.lease, "../mldht")); QVERIFY(!Source::load(*p.lease, ""));
  }
  void unsafeTypesAndBoundsCannotAcquireImportedAuthority() {
    Profile p; write(p.leaf(), Root + '\n'); const auto alias = QFile::encodeName(p.leaf() + ".alias"), leaf = QFile::encodeName(p.leaf());
    QVERIFY(::link(leaf.constData(), alias.constData()) == 0); auto shared = Source::load(*p.lease, "mldht");
    QVERIFY(!shared); QCOMPARE(shared.error().code, storage::ErrorCode::UnsafePath);
    QVERIFY(QFile::remove(p.leaf() + ".alias"));
    QVERIFY(QFile::rename(p.leaf(), p.leaf() + ".original")); const auto target = QFile::encodeName(p.leaf() + ".original");
    QVERIFY(::symlink(target.constData(), leaf.constData()) == 0); auto linked = Source::load(*p.lease, "mldht");
    QVERIFY(!linked); QCOMPARE(linked.error().code, storage::ErrorCode::UnsafePath); QVERIFY(QFile::remove(p.leaf()));
    QVERIFY(QDir().mkpath(p.leaf())); auto directory = Source::load(*p.lease, "mldht"); QVERIFY(!directory);
    QCOMPARE(directory.error().code, storage::ErrorCode::UnsafePath); QVERIFY(QDir().rmdir(p.leaf()));
    write(p.leaf(), QByteArray(static_cast<qsizetype>(Source::MaximumBytes + 1), 'x')); const auto before = snapshot(p.leaf());
    auto huge = Source::load(*p.lease, "mldht"); QVERIFY(!huge); QCOMPARE(huge.error().code, storage::ErrorCode::Unsupported);
    QVERIFY(same(before, snapshot(p.leaf())));
  }
  void fileAndDirectoryReplacementAndForeignLeaseNeverRefreshTheSnapshot_data() {
    QTest::addColumn<int>("kind");
    QTest::newRow("replace-file") << 0; QTest::newRow("rewrite-same-content") << 1;
    QTest::newRow("replace-plugin-directory") << 2; QTest::newRow("replace-plugins-directory") << 3;
  }
  void fileAndDirectoryReplacementAndForeignLeaseNeverRefreshTheSnapshot() {
    QFETCH(int, kind); Profile p; write(p.leaf(), Root + '\n'); auto loaded = Source::load(*p.lease, "mldht"); QVERIFY(loaded);
    Profile other; QVERIFY(!loaded->validate(*other.lease)); QVERIFY(loaded->validate(*p.lease));
    const auto original = loaded->snapshot().originalBytes;
    if (kind == 0) { QVERIFY(QFile::rename(p.leaf(), p.leaf() + ".retained")); write(p.leaf(), Root + '\n'); }
    else if (kind == 1) {
      const auto before = snapshot(p.leaf()); write(p.leaf(), Root + '\n');
      const std::array<timespec, 2> times{{{0, UTIME_OMIT}, {before.stat.st_mtim.tv_sec + 1, before.stat.st_mtim.tv_nsec}}};
      const auto path = QFile::encodeName(p.leaf()); QVERIFY(::utimensat(AT_FDCWD, path.constData(), times.data(), AT_SYMLINK_NOFOLLOW) == 0);
    }
    else if (kind == 2) { QVERIFY(QDir().rename(p.path + "/plugins/mldht", p.path + "/plugins/retained")); write(p.leaf(), Root + '\n'); }
    else { QVERIFY(QDir().rename(p.path + "/plugins", p.path + "/retained")); write(p.leaf(), Root + '\n'); }
    auto stale = loaded->validate(*p.lease); QVERIFY(!stale); QCOMPARE(stale.error().code, storage::ErrorCode::Conflict);
    QCOMPARE(loaded->snapshot().originalBytes, original); // An explicit reload is a different observation.
    auto fresh = Source::load(*p.lease, "mldht"); QVERIFY(fresh); QVERIFY(fresh->validate(*p.lease));
  }
  void boundedDescriptorsRetireWithMoveOnlySnapshots() {
    Profile p; write(p.leaf(), Root + '\n');
    { auto warm = Source::load(*p.lease, "mldht"); QVERIFY(warm); QVERIFY(warm->validate(*p.lease)); }
    const auto before = descriptors();
    for (int i = 0; i < 64; ++i) {
      auto loaded = Source::load(*p.lease, "mldht"); QVERIFY(loaded); QVERIFY(loaded->validate(*p.lease));
      auto moved = std::move(*loaded); QVERIFY(moved.validate(*p.lease)); QVERIFY(!loaded->validate(*p.lease));
      QCOMPARE(descriptors(), before + 3);
    }
    QCOMPARE(descriptors(), before);
  }
};
QTEST_GUILESS_MAIN(MainlineIdentityTests)
#include "mainline_identity_tests.moc"
