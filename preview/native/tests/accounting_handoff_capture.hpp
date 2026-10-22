#pragma once

#include "biglybtc/overall_statistics.hpp"
#include "biglybtc/saved_history.hpp"
#include "native_session_presentation.hpp"

#include <QCryptographicHash>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QWidget>

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

namespace biglybtc::test {
// Test-only, opt-in capture of physical native output. No serialization of the
// statistics document or reconstruction of minute CSV is used for its copies.
inline void captureAccountingHandoff(bool multifile, const QString& profile,
    const QByteArray& initial, const core::OverallValues& nativeValues,
    const std::array<QDate, 2>& dates, const ui::NativeSessionPresentation& final,
    bool joined, bool storageReleased, QWidget& window) {
  const auto requested = qEnvironmentVariable("BIGLYBTC_ACCOUNTING_HANDOFF_DIRECTORY");
  if (requested.isEmpty()) { return; } // Default tests perform no extra IO.
  const auto fail = [] { throw std::runtime_error("Unsafe or incomplete optional accounting handoff capture"); };
  const QFileInfo output(requested), input(profile);
  struct stat directory{};
  if (!output.isAbsolute() || output.isSymLink() || !output.isDir()
      || output.canonicalFilePath() != requested
      || ::lstat(requested.toLocal8Bit().constData(), &directory) || directory.st_uid != ::getuid()
      || (directory.st_mode & 0022) || input.isSymLink() || input.canonicalFilePath() != profile
      || !profile.startsWith(QStringLiteral("/tmp/biglybtc-engine-statistics-"))
      || !joined || !storageReleased || !final || final->state != ui::NativeSessionState::Closed || !final->sample) { fail(); }
  const auto name = multifile ? QStringLiteral("multifile") : QStringLiteral("single");
  QDir root(requested);
  if (!root.mkdir(name)) { fail(); } // Fresh case only, never replace a capture.
  const QDir target(root.filePath(name));
  QJsonArray files;
  const auto hash = [](const QByteArray& bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
  };
  const auto store = [&](const QString& relative, const QByteArray& bytes) {
    const auto path = target.filePath(relative);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) { fail(); }
    QFile outputFile(path);
    if (!outputFile.open(QIODevice::WriteOnly | QIODevice::NewOnly) || outputFile.write(bytes) != bytes.size()
        || !outputFile.flush() || ::fsync(outputFile.handle())) { fail(); }
    outputFile.close(); if (outputFile.error() != QFileDevice::NoError) { fail(); }
    files.append(QJsonObject{{QStringLiteral("path"), relative}, {QStringLiteral("bytes"), double(bytes.size())},
        {QStringLiteral("sha256"), hash(bytes)}});
  };
  const auto copyActual = [&](const QString& relative) {
    const auto path = profile + QLatin1Char('/') + relative;
    const QFileInfo leaf(path);
    if (leaf.isSymLink() || leaf.canonicalFilePath() != path) { fail(); }
    const int descriptor = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (descriptor < 0) { fail(); }
    struct Close { int fd; ~Close() { if (fd >= 0) { (void)::close(fd); } } } close{descriptor};
    struct stat before{}, after{}, named{};
    if (::fstat(descriptor, &before) || !S_ISREG(before.st_mode) || before.st_uid != ::getuid()
        || before.st_nlink != 1 || before.st_size < 0 || before.st_size > 4 * 1024 * 1024) { fail(); }
    QByteArray bytes;
    std::array<char, 8192> buffer{};
    for (;;) {
      const auto count = ::read(descriptor, buffer.data(), buffer.size());
      if (count < 0 && errno == EINTR) { continue; }
      if (count < 0) { fail(); } if (!count) { break; }
      bytes.append(buffer.data(), static_cast<qsizetype>(count));
      if (bytes.size() > 4 * 1024 * 1024) { fail(); }
    }
    if (::fstat(descriptor, &after) || ::lstat(path.toLocal8Bit().constData(), &named)
        || before.st_dev != after.st_dev || before.st_ino != after.st_ino || before.st_size != after.st_size
        || before.st_mtim.tv_sec != after.st_mtim.tv_sec || before.st_mtim.tv_nsec != after.st_mtim.tv_nsec
        || before.st_ctim.tv_sec != after.st_ctim.tv_sec || before.st_ctim.tv_nsec != after.st_ctim.tv_nsec
        || before.st_mode != after.st_mode || before.st_nlink != after.st_nlink
        || before.st_uid != after.st_uid || named.st_dev != after.st_dev || named.st_ino != after.st_ino
        || !S_ISREG(named.st_mode) || bytes.size() != after.st_size) { fail(); }
    store(relative, bytes);
    return bytes;
  };
  const auto actual = copyActual(QStringLiteral("BiglyBT.statistics"));
  // Native expectations come from the actual successful test's readback, never
  // from an independently reimplemented accounting formula.
  auto parsed = core::readSavedStatistics(std::string_view(actual.constData(), static_cast<std::size_t>(actual.size())));
  if (!parsed || !parsed->hasAll) { fail(); }
  QByteArray expected("# kind\tkey/date/hour\tactual native values; known/observations are not Java equality\n");
  for (std::size_t i = 0; i < nativeValues.size(); ++i) {
    if (parsed->values[i].value != nativeValues[i]) { fail(); }
    expected += "overall\t" + QByteArray(core::SavedStatisticKeys[i].data(), core::SavedStatisticKeys[i].size())
        + '\t' + QByteArray::number(static_cast<qlonglong>(nativeValues[i])) + '\n';
  }
  store(QStringLiteral("initial.BiglyBT.statistics"), initial);
  std::size_t days = 0;
  for (std::size_t index = 0; index < dates.size(); ++index) {
    if (index && dates[index] == dates[0]) { continue; }
    const auto date = dates[index];
    const auto relative = QStringLiteral("stats/") + date.toString(QStringLiteral("yyyy/MM/dd")) + QStringLiteral(".dat");
    if (!QFileInfo::exists(profile + QLatin1Char('/') + relative)) { continue; }
    const auto bytes = copyActual(relative);
    auto day = core::readSavedHistoryDay(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())),
        {date.year(), static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day())});
    if (!day) { fail(); } ++days;
    for (int hour = -1; hour < 24; ++hour) {
      const auto& value = hour < 0 ? day->totals : day->hours[static_cast<std::size_t>(hour)];
      expected += "minute\t" + date.toString(Qt::ISODate).toLatin1() + '\t' + QByteArray::number(hour)
          + '\t' + (value ? "1" : "0");
      for (const auto scalar : value.value_or(core::HistoryCounters{})) {
        expected += '\t' + QByteArray::number(static_cast<qlonglong>(scalar));
      }
      expected += '\n';
    }
  }
  if (!days) { fail(); }
  store(QStringLiteral("native-expected.tsv"), expected);
  if (!window.isVisible() || window.isMinimized()) { fail(); }
  if (!window.grab().save(target.filePath(QStringLiteral("current.png")))) { fail(); }
  QFile png(target.filePath(QStringLiteral("current.png")));
  if (!png.open(QIODevice::ReadOnly)) { fail(); }
  const auto pngBytes = png.readAll();
  files.append(QJsonObject{{QStringLiteral("path"), QStringLiteral("current.png")},
      {QStringLiteral("bytes"), double(pngBytes.size())}, {QStringLiteral("sha256"), hash(pngBytes)}});
  QJsonObject receipt{{QStringLiteral("schema"), 1}, {QStringLiteral("case"), name},
      {QStringLiteral("status"), QStringLiteral("ACTUAL_NATIVE_JOINED_CLOSE_CAPTURE")},
      {QStringLiteral("session_joined"), joined}, {QStringLiteral("storage_released"), storageReleased},
      {QStringLiteral("session"), QString::number(final->session)}, {QStringLiteral("sequence"), QString::number(final->sequence)},
      {QStringLiteral("day_files"), double(days)}, {QStringLiteral("files"), files},
      {QStringLiteral("scope"), QStringLiteral("Actual owned-profile bytes and native readback, not Java execution")}};
  store(QStringLiteral("capture.json"), QJsonDocument(receipt).toJson(QJsonDocument::Indented));
}
} // namespace biglybtc::test
