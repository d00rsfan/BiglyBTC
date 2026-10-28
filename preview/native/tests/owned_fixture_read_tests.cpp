#include "owned_engine_fixture.hpp"
#include <QtTest>
#include <cerrno>

namespace {
QString readFailure(const QString& path) {
  try { static_cast<void>(biglybtc::test::read(path)); }
  catch (const std::runtime_error& error) { return QString::fromUtf8(error.what()); }
  return {};
}
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
  void linuxDirectoryOpenThenReadFailureIsNotSuccessfulEmptyData() {
    QTemporaryDir area; QVERIFY(area.isValid());
    const auto path = area.filePath(QStringLiteral("owned-directory"));
    QVERIFY(QDir().mkpath(path));
    { QFile probe(path); QVERIFY(probe.open(QIODevice::ReadOnly)); }
    const auto message = readFailure(path); QVERIFY2(!message.isEmpty(), "Directory read must throw");
    QVERIFY2(message.contains(path), qPrintable(message));
    QVERIFY2(message.contains(QStringLiteral("stage=readAll")), qPrintable(message));
    QVERIFY2(message.contains(QStringLiteral("qfileError=%1").arg(static_cast<int>(QFileDevice::ReadError))), qPrintable(message));
    QVERIFY2(message.contains(QStringLiteral("errno=%1").arg(EISDIR)), qPrintable(message));
  }
};
QTEST_GUILESS_MAIN(OwnedFixtureReadTests)
#include "owned_fixture_read_tests.moc"
