#include "owned_engine_fixture.hpp"
#include <QtTest>
#include <atomic>
#include <cerrno>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
std::atomic<dev_t> errorDevice{};
std::atomic<ino_t> errorInode{};
std::atomic<bool> readErrorArmed{false};
std::atomic<bool> readErrorInjected{false};

class OneShotReadError final {
public:
  explicit OneShotReadError(const struct stat& target) {
    errorDevice.store(target.st_dev, std::memory_order_relaxed);
    errorInode.store(target.st_ino, std::memory_order_relaxed);
    readErrorInjected.store(false, std::memory_order_relaxed);
    readErrorArmed.store(true, std::memory_order_release);
  }
  ~OneShotReadError() { readErrorArmed.store(false, std::memory_order_release); }
  OneShotReadError(const OneShotReadError&) = delete;
  OneShotReadError& operator=(const OneShotReadError&) = delete;
};

QString readFailure(const QString& path) {
  try { static_cast<void>(biglybtc::test::read(path)); }
  catch (const std::runtime_error& error) { return QString::fromUtf8(error.what()); }
  return {};
}
}

// Linked only into this Linux test executable. Match the descriptor's actual
// inode, fail one nonempty read, and pass every other call directly to Linux.
extern "C" __attribute__((visibility("default"))) ssize_t read(int fd, void* data, size_t size) {
  if (size != 0 && readErrorArmed.load(std::memory_order_acquire)) {
    struct stat actual{};
    if (::fstat(fd, &actual) == 0
        && actual.st_dev == errorDevice.load(std::memory_order_relaxed)
        && actual.st_ino == errorInode.load(std::memory_order_relaxed)
        && readErrorArmed.exchange(false, std::memory_order_acq_rel)) {
      readErrorInjected.store(true, std::memory_order_release);
      errno = EIO;
      return -1;
    }
  }
  return static_cast<ssize_t>(::syscall(SYS_read, fd, data, size));
}

class OwnedFixtureReadTests final : public QObject {
  Q_OBJECT
private slots:
  void missingFileReportsOwnedPathAndOpenFailure() {
    QTemporaryDir area; QVERIFY(area.isValid());
    const auto path = area.filePath(QStringLiteral("missing.bin"));
    const auto message = readFailure(path); QVERIFY2(!message.isEmpty(), "Missing file read must throw");
    QVERIFY2(message.contains(path), qPrintable(message));
    QVERIFY2(message.contains(QStringLiteral("stage=open")), qPrintable(message));
    QVERIFY2(message.contains(QStringLiteral("qfileError=%1").arg(static_cast<int>(QFileDevice::OpenError))), qPrintable(message));
    QVERIFY2(message.contains(QStringLiteral("errno=%1").arg(ENOENT)), qPrintable(message));
    QVERIFY2(message.contains(QStringLiteral("errorString=\"")), qPrintable(message));
  }
  void genuineEmptyFileIsSuccessful() {
    QTemporaryDir area; QVERIFY(area.isValid());
    const auto path = area.filePath(QStringLiteral("empty.bin"));
    { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly)); }
    QCOMPARE(biglybtc::test::read(path), QByteArray{});
  }
  void linuxDirectoryReportsActualOpenRejection() {
    QTemporaryDir area; QVERIFY(area.isValid());
    const auto path = area.filePath(QStringLiteral("owned-directory"));
    QVERIFY(QDir().mkpath(path));
    { QFile probe(path); QVERIFY(!probe.open(QIODevice::ReadOnly)); }
    const auto message = readFailure(path); QVERIFY2(!message.isEmpty(), "Directory read must throw");
    QVERIFY2(message.contains(path), qPrintable(message));
    QVERIFY2(message.contains(QStringLiteral("stage=open")), qPrintable(message));
    QVERIFY2(message.contains(QStringLiteral("qfileError=%1").arg(static_cast<int>(QFileDevice::OpenError))), qPrintable(message));
    QVERIFY2(message.contains(QStringLiteral("errorString=\"")), qPrintable(message));
  }
  void actualReadAllErrorReportsOwnedPathAndImmediateErrno() {
    QTemporaryDir area; QVERIFY(area.isValid());
    const auto path = area.filePath(QStringLiteral("read-error.bin"));
    biglybtc::test::write(path, QByteArrayLiteral("generated nonempty regular file"));
    struct stat target{};
    const auto native = QFile::encodeName(path);
    QCOMPARE(::stat(native.constData(), &target), 0);
    QVERIFY(S_ISREG(target.st_mode));
    QString message;
    bool injected = false;
    {
      OneShotReadError failure(target);
      message = readFailure(path);
      injected = readErrorInjected.load(std::memory_order_acquire);
    }
    QVERIFY2(injected, "The inode-scoped read interposer must actually inject EIO");
    QVERIFY2(!message.isEmpty(), "A real QFile readAll error must throw");
    QVERIFY2(message.contains(path), qPrintable(message));
    QVERIFY2(message.contains(QStringLiteral("stage=readAll")), qPrintable(message));
    QVERIFY2(message.contains(QStringLiteral("qfileError=%1").arg(static_cast<int>(QFileDevice::ReadError))), qPrintable(message));
    QVERIFY2(message.contains(QStringLiteral("errno=%1").arg(EIO)), qPrintable(message));
    QVERIFY2(message.contains(QStringLiteral("errorString=\"")), qPrintable(message));
  }
};
QTEST_GUILESS_MAIN(OwnedFixtureReadTests)
#include "owned_fixture_read_tests.moc"
