// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "ffmpeg_progress.hpp"
#include "transcode_profile.hpp"
#include <QByteArray>
#include <QFile>
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <chrono>
#include <expected>
#include <memory>

namespace biglybtc::media {

enum class FfmpegContainer {Matroska,Mp4,MpegTs};
enum class FfmpegJobState {Idle,Starting,Running,Stopping,Produced,Cancelled,TimedOut,Failed};

struct FfmpegProgram {
  QString executable;
  QByteArray sha256;
};

struct FfmpegVideoBounds {
  int width;
  int height;
  int pixelMod;
};

struct FfmpegAudioBounds {
  int sampleRate;
  int channels;
};

struct FfmpegJob {
  FfmpegProgram program;
  FfmpegContainer input=FfmpegContainer::Matroska;
  FfmpegContainer output=FfmpegContainer::Matroska;
  std::optional<TranscodeVideoGeometry> geometry;
  std::optional<FfmpegVideoBounds> videoBounds;
  std::chrono::milliseconds deadline{300000};
  bool fragmentMp4 = false;
  std::optional<FfmpegAudioBounds> audioBounds{};
  bool requireAudio = false;
};

// One worker-confined, typed transcode. The input caller supplies owned bytes.
class FfmpegProcess final:public QObject {
public:
  explicit FfmpegProcess(QObject* parent=nullptr);
  ~FfmpegProcess() override;
  std::expected<void,QString> start(const FfmpegJob&);
  qint64 push(QByteArrayView bytes);
  bool finishInput();
  bool cancel();
  FfmpegJobState state()const noexcept{return state_;}
  qint64 childPid()const noexcept{return process_.processId();}
  const std::optional<FfmpegProgressSample>& progress()const noexcept{return progress_.latest();}
  QString resultFile()const;
  std::shared_ptr<QFile> resultHandle() const;
  // Worker-only temporary output. Recheck the process state before each read.
  std::shared_ptr<QFile> progressiveHandle();
  QString diagnostics() const;

private:
  void readProgress();
  void readDiagnostic();
  void finish(int code,QProcess::ExitStatus);
  void stop(FfmpegJobState);
  void failToStart();
  std::unique_ptr<QTemporaryDir> work_;
  QProcess process_;
  QTimer deadline_,forceStop_;
  FfmpegProgress progress_;
  FfmpegJobState state_=FfmpegJobState::Idle;
  std::optional<FfmpegJobState> stopCause_;
  QByteArray pending_,diagnostics_;
  QString output_;
  std::shared_ptr<QFile> ownedOutput_, growingOutput_;
  bool fragmentMp4_ = false;
  qint64 submittedBytes_=0,writtenBytes_=0;
  bool inputClosed_=false;
};

} // namespace biglybtc::media
