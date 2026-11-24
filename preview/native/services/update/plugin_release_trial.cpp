// SPDX-License-Identifier: GPL-2.0-or-later
#include "plugin_release_trial.hpp"
#include "plugin_release_slot_internal.hpp"

#include "biglybtc/plugin.h"
#include "file_io.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <algorithm>
#include <fcntl.h>

namespace biglybtc::update {
namespace {
constexpr qint64 maximumProgramBytes = 16 * 1024 * 1024;

std::string_view bytes(const QByteArray& data) {
  return {data.constData(), static_cast<std::size_t>(data.size())};
}
} // namespace

std::expected<void, PluginTrialError> trialPluginRelease(const VerifiedPluginBundle& bundle,
    const PluginTrialProgram& program, std::uint64_t capabilities, std::chrono::milliseconds deadline,
    std::stop_token stop) {
  if (stop.stop_requested()) { return std::unexpected(PluginTrialError::Cancelled); }
  if (bundle.release().expiresAt() <= QDateTime::currentSecsSinceEpoch()) {
    return std::unexpected(PluginTrialError::Input);
  }
  return detail::trialPluginModule(bundle, program, capabilities, deadline, stop);
}

std::expected<void, PluginTrialError> detail::trialPluginModule(const VerifiedPluginBundle& bundle,
    const PluginTrialProgram& program, std::uint64_t capabilities, std::chrono::milliseconds deadline,
    std::stop_token stop) {
  using Error = PluginTrialError;
  if (stop.stop_requested()) { return std::unexpected(Error::Cancelled); }
  const auto& release = bundle.release();
  if (release.target().hostAbi != BT_PLUGIN_ABI_V1 ||
      deadline.count() <= 0 || deadline > std::chrono::seconds(30) || program.sha256.size() != 32) {
    return std::unexpected(Error::Input);
  }
  QFileInfo executable(program.executable);
  QFile source(program.executable);
  if (!executable.isAbsolute() || !executable.isFile() || executable.isSymLink() || !executable.isExecutable() ||
      !source.open(QIODevice::ReadOnly) || source.size() <= 0 || source.size() > maximumProgramBytes) {
    return std::unexpected(Error::ProgramIdentity);
  }
  const auto image = source.read(maximumProgramBytes + 1);
  if (!source.atEnd() || image.size() != source.size() ||
      QCryptographicHash::hash(image, QCryptographicHash::Sha256) != program.sha256) {
    return std::unexpected(Error::ProgramIdentity);
  }

  QTemporaryDir work;
  if (!work.isValid()) { return std::unexpected(Error::Storage); }
  const auto path = QFile::encodeName(work.path());
  const storage::detail::Descriptor root(::open(path.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  if (root.get() < 0 || !storage::detail::writeExclusiveOwned(root.get(), "trial", bytes(image), 0500) ||
      !storage::detail::writeExclusiveOwned(root.get(), "plugin.so", bytes(bundle.module()), 0400)) {
    return std::unexpected(Error::Storage);
  }
  const auto digest = QCryptographicHash::hash(bundle.module(), QCryptographicHash::Sha256).toHex();
  QProcess process;
  process.setWorkingDirectory(work.path());
  QProcessEnvironment environment;
  environment.insert(QStringLiteral("HOME"), work.path());
  environment.insert(QStringLiteral("LANG"), QStringLiteral("C.UTF-8"));
  process.setProcessEnvironment(environment);
  process.setStandardOutputFile(QProcess::nullDevice());
  process.setStandardErrorFile(QProcess::nullDevice());
  process.setProgram(work.filePath(QStringLiteral("trial")));
  process.setArguments({QString::fromLatin1(digest), release.target().runtimeId, release.asset().version,
      QString::number(capabilities)});
  if (stop.stop_requested()) { return std::unexpected(Error::Cancelled); }
  QDeadlineTimer window(deadline.count());
  const auto waitTime = [&] {
    const auto remaining = window.remainingTime();
    return static_cast<int>(stop.stop_possible() ? std::min<qint64>(25, remaining) : remaining);
  };
  const auto abort = [&](Error reason) {
    if (process.state() != QProcess::NotRunning) {
      process.kill();
      process.waitForFinished(1000);
    }
    return std::unexpected(reason);
  };
  process.start();
  while (process.state() == QProcess::Starting && !stop.stop_requested() && !window.hasExpired()) {
    if (process.waitForStarted(waitTime()) || process.error() != QProcess::Timedout) { break; }
  }
  if (stop.stop_requested()) { return abort(Error::Cancelled); }
  if (process.state() == QProcess::Starting || process.error() == QProcess::FailedToStart) {
    return abort(window.hasExpired() ? Error::Deadline : Error::Start);
  }
  while (process.state() == QProcess::Running && !stop.stop_requested() && !window.hasExpired()) {
    if (process.waitForFinished(waitTime()) || process.error() != QProcess::Timedout) { break; }
  }
  if (stop.stop_requested()) { return abort(Error::Cancelled); }
  if (process.state() != QProcess::NotRunning) {
    return abort(window.hasExpired() ? Error::Deadline : Error::Rejected);
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    return std::unexpected(Error::Rejected);
  }
  return {};
}

} // namespace biglybtc::update
