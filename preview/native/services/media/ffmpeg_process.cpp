// SPDX-License-Identifier: GPL-2.0-or-later
#include "ffmpeg_process.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QThread>
#include <algorithm>
#include <array>
#include <limits>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace biglybtc::media {
namespace {
constexpr qint64 maxChunk=64*1024;
constexpr qint64 maxQueued=128*1024;
constexpr qsizetype maxPending=4096;
constexpr qsizetype maxDiagnostic=48*1024;
constexpr auto stopGrace=std::chrono::milliseconds(650);

std::shared_ptr<QFile> openOutput(const QString& path, std::optional<qint64> joinedSize) {
  if (joinedSize && *joinedSize <= 0) { return {}; }
  auto result = std::make_shared<QFile>();
  const auto encoded = QFile::encodeName(path);
  const auto fd = ::open(encoded.constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
  if (fd < 0) { return {}; }
  struct stat info{};
  if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != ::geteuid() || info.st_nlink != 1 ||
      info.st_size <= 0 || (joinedSize && (info.st_size != *joinedSize || ::fchmod(fd, 0400) != 0)) ||
      !result->open(fd, QIODevice::ReadOnly | QIODevice::Unbuffered, QFileDevice::AutoCloseHandle)) {
    ::close(fd);
    return {};
  }
  return result;
}

bool sameOutput(const QFile& growing, const QFile& joined) {
  struct stat before{}, after{};
  return ::fstat(growing.handle(), &before) == 0 && ::fstat(joined.handle(), &after) == 0 &&
      before.st_dev == after.st_dev && before.st_ino == after.st_ino;
}

QString containerName(FfmpegContainer value) {
  switch(value){
  case FfmpegContainer::Matroska:return QStringLiteral("matroska");
  case FfmpegContainer::Mp4:return QStringLiteral("mp4");
  case FfmpegContainer::MpegTs:return QStringLiteral("mpegts");
  }
  return {};
}

bool validGeometry(const TranscodeVideoGeometry& video) {
  constexpr int limit = 4096;
  if (video.width <= 0 || video.height <= 0 || video.width > limit || video.height > limit ||
      video.width % 2 || video.height % 2 || video.padX < 0 || video.padY < 0 ||
      video.padX > limit || video.padY > limit || video.padX % 2 || video.padY % 2 ||
      video.width + 2 * video.padX > limit || video.height + 2 * video.padY > limit) {
    return false;
  }
  return !video.display || (video.display->numerator > 0 && video.display->denominator > 0 &&
      video.display->numerator <= 32768 && video.display->denominator <= 32768);
}

bool validVideoBounds(const FfmpegVideoBounds& bounds) {
  return bounds.width >= 8 && bounds.width <= 4096 && bounds.width % 2 == 0 &&
      bounds.height >= 8 && bounds.height <= 4096 && bounds.height % 2 == 0 &&
      bounds.pixelMod >= 2 && bounds.pixelMod <= 32 && bounds.pixelMod % 2 == 0;
}

constexpr std::array aacSampleRates{7350, 8000, 11025, 12000, 16000, 22050, 24000,
    32000, 44100, 48000, 64000, 88200, 96000};

bool validAudioBounds(const FfmpegAudioBounds& bounds) {
  return bounds.sampleRate >= aacSampleRates.front() && bounds.sampleRate <= aacSampleRates.back() &&
      (bounds.channels == 1 || bounds.channels == 2);
}

QString audioWithinBounds(const FfmpegAudioBounds& bounds) {
  QStringList rates;
  for (const auto rate : aacSampleRates) {
    if (rate <= bounds.sampleRate) { rates.push_back(QString::number(rate)); }
  }
  return QStringLiteral("aformat=sample_rates=%1:channel_layouts=%2").arg(
      rates.join(u'|'), bounds.channels == 2 ? QStringLiteral("stereo|mono") : QStringLiteral("mono"));
}

QString scaleWithinBounds(const FfmpegVideoBounds& bounds) {
  // Apply the original max-width pass, then the max-height pass if needed.
  // The filter reads the coded source dimensions from the verified input.
  const auto round = [&](const QString& extent) {
    return QStringLiteral("%1*trunc((trunc(%2)+%3)/%1)")
        .arg(bounds.pixelMod).arg(extent).arg(bounds.pixelMod / 2);
  };
  const auto wide = round(QStringLiteral("%1*(ih/iw)").arg(bounds.width));
  const auto tall = round(QStringLiteral("%1*(iw/ih)").arg(bounds.height));
  const auto width = QStringLiteral("if(gt(iw,%1),%1,if(gt(ih,%2),min(%1,%3),iw))")
      .arg(bounds.width).arg(bounds.height).arg(tall);
  const auto height = QStringLiteral("if(gt(iw,%1),min(%2,%3),min(%2,ih))")
      .arg(bounds.width).arg(bounds.height).arg(wide);
  return QStringLiteral("scale=w='if(gt(%1,0),%1,NAN)':h='if(gt(%2,0),%2,NAN)'").arg(width, height);
}

bool stageProgram(const FfmpegProgram& program, const QString& destination) {
  constexpr qint64 maxExecutable = 64 * 1024 * 1024;
  const QFileInfo info(program.executable);
  if(!info.isAbsolute() || !info.isFile() || !info.isExecutable() || info.isSymLink() ||
      program.sha256.size() != 32 || info.size() <= 0 || info.size() > maxExecutable){
    return false;
  }
  QFile source(program.executable);
  QFile target(destination);
  if(!source.open(QIODevice::ReadOnly) ||
      !target.open(QIODevice::WriteOnly | QIODevice::NewOnly, QFileDevice::WriteOwner | QFileDevice::ReadOwner)){
    return false;
  }
  // Verify the bytes copied into the private job directory, then execute
  // that same copy. A later replacement of the source pathname is irrelevant.
  QCryptographicHash hash(QCryptographicHash::Sha256);
  qint64 copied = 0;
  while(!source.atEnd()){
    const QByteArray chunk = source.read(64 * 1024);
    if(chunk.isEmpty() || chunk.size() > maxExecutable - copied || target.write(chunk) != chunk.size()){
      return false;
    }
    hash.addData(chunk);
    copied += chunk.size();
  }
  return copied > 0 && hash.result() == program.sha256 && target.flush() &&
      target.setPermissions(QFileDevice::ReadOwner | QFileDevice::ExeOwner);
}
}

FfmpegProcess::FfmpegProcess(QObject* parent):QObject(parent),process_(this),deadline_(this),forceStop_(this) {
  deadline_.setSingleShot(true);
  forceStop_.setSingleShot(true);
  connect(&deadline_,&QTimer::timeout,this,[this]{stop(FfmpegJobState::TimedOut);});
  connect(&forceStop_,&QTimer::timeout,this,[this]{
    if(process_.state()!=QProcess::NotRunning){process_.kill();}
  });
  connect(&process_,&QProcess::started,this,[this]{
    if(stopCause_){process_.terminate();return;}
    state_=FfmpegJobState::Running;
  });
  connect(&process_, &QProcess::bytesWritten, this, [this](qint64 count){ writtenBytes_ += count; });
  connect(&process_,&QProcess::readyReadStandardOutput,this,[this]{readProgress();});
  connect(&process_,&QProcess::readyReadStandardError,this,[this]{readDiagnostic();});
  connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
    if(error==QProcess::FailedToStart){failToStart();}
    else if(error==QProcess::ReadError||error==QProcess::WriteError){stop(FfmpegJobState::Failed);}
  });
  connect(&process_,&QProcess::finished,this,[this](int code,QProcess::ExitStatus exit){finish(code,exit);});
}
FfmpegProcess::~FfmpegProcess() {
  process_.disconnect(this);
  if(process_.state()!=QProcess::NotRunning){
    process_.kill();
    process_.waitForFinished(3000);
  }
}

std::expected<void,QString> FfmpegProcess::start(const FfmpegJob& job) {
  if(QThread::currentThread()!=thread()||state_!=FfmpegJobState::Idle){
    return std::unexpected(QStringLiteral("Media process owner is unavailable"));
  }
  const auto source=containerName(job.input),target=containerName(job.output);
  if(source.isEmpty()||target.isEmpty()||
      (job.geometry && (job.videoBounds || !validGeometry(*job.geometry))) ||
      (job.videoBounds && !validVideoBounds(*job.videoBounds)) ||
      (job.audioBounds && !validAudioBounds(*job.audioBounds)) ||
      (job.fragmentMp4 && job.output != FfmpegContainer::Mp4) ||
      job.deadline<=std::chrono::milliseconds(0)||job.deadline>std::chrono::hours(1)){
    return std::unexpected(QStringLiteral("Media job or pinned executable is invalid"));
  }
  work_=std::make_unique<QTemporaryDir>(QDir::tempPath()+QStringLiteral("/biglybtc-transcode-XXXXXX"));
  if(!work_->isValid()){
    work_.reset();
    return std::unexpected(QStringLiteral("Cannot create owned media output directory"));
  }
  const QString executable = work_->filePath(QStringLiteral("ffmpeg"));
  if(!stageProgram(job.program, executable)){
    work_.reset();
    return std::unexpected(QStringLiteral("Cannot stage the pinned media executable"));
  }
  output_=work_->filePath(QStringLiteral("output.media"));
  fragmentMp4_ = job.fragmentMp4;
  QStringList arguments{
    QStringLiteral("-nostdin"),QStringLiteral("-hide_banner"),QStringLiteral("-loglevel"),QStringLiteral("error"),
    QStringLiteral("-nostats"),QStringLiteral("-progress"),QStringLiteral("pipe:1"),
    QStringLiteral("-protocol_whitelist"),QStringLiteral("pipe"),QStringLiteral("-f"),source,
  };
  if (fragmentMp4_) {
    arguments << QStringLiteral("-probesize") << QStringLiteral("262144")
        << QStringLiteral("-analyzeduration") << QStringLiteral("1000000");
  }
  arguments.append({
    QStringLiteral("-i"),QStringLiteral("pipe:0"),
    QStringLiteral("-map"),QStringLiteral("0:v:0"),QStringLiteral("-map"),
    job.requireAudio ? QStringLiteral("0:a:0") : QStringLiteral("0:a:0?"),
    QStringLiteral("-c:v"),QStringLiteral("libx264"),QStringLiteral("-preset"),QStringLiteral("ultrafast"),
    QStringLiteral("-threads"),QStringLiteral("1"),QStringLiteral("-pix_fmt"),QStringLiteral("yuv420p"),
    QStringLiteral("-c:a"),QStringLiteral("aac"),
  });
  if (job.geometry) {
    const auto& video = *job.geometry;
    auto filter = QStringLiteral("scale=%1:%2").arg(video.width).arg(video.height);
    if (video.padX || video.padY) {
      filter += QStringLiteral(",pad=%1:%2:%3:%4").arg(video.width + 2 * video.padX).arg(video.height + 2 * video.padY)
          .arg(video.padX).arg(video.padY);
    }
    arguments << QStringLiteral("-vf") << filter;
    if (video.display) {
      arguments << QStringLiteral("-aspect") << QStringLiteral("%1:%2").arg(video.display->numerator).arg(video.display->denominator);
    }
  } else if (job.videoBounds) {
    arguments << QStringLiteral("-vf") << scaleWithinBounds(*job.videoBounds);
  }
  if (job.audioBounds) {
    arguments << QStringLiteral("-af") << audioWithinBounds(*job.audioBounds);
  }
  if (fragmentMp4_) {
    arguments << QStringLiteral("-movflags") << QStringLiteral("+frag_keyframe+empty_moov+default_base_moof")
        << QStringLiteral("-frag_duration") << QStringLiteral("1000000")
        << QStringLiteral("-flush_packets") << QStringLiteral("1");
  }
  arguments<<QStringLiteral("-f")<<target<<QStringLiteral("-n")<<output_;
  QProcessEnvironment environment;
  environment.insert(QStringLiteral("LANG"),QStringLiteral("C.UTF-8"));
  process_.setProcessEnvironment(environment);
  process_.setWorkingDirectory(work_->path());
  process_.setProgram(executable);
  process_.setArguments(arguments);
  state_=FfmpegJobState::Starting;
  deadline_.start(job.deadline);
  process_.start(QIODevice::ReadWrite);
  return {};
}

qint64 FfmpegProcess::push(QByteArrayView bytes) {
  if(QThread::currentThread()!=thread()||state_!=FfmpegJobState::Running||inputClosed_||
      bytes.empty()||bytes.size()>maxChunk||bytes.size()>std::numeric_limits<qint64>::max()-submittedBytes_){return -1;}
  if(bytes.size()>maxQueued-process_.bytesToWrite()){return 0;}
  const auto accepted = process_.write(bytes.data(), bytes.size());
  if(accepted > 0){submittedBytes_ += accepted;}
  return accepted;
}

bool FfmpegProcess::finishInput() {
  if(QThread::currentThread()!=thread()||state_!=FfmpegJobState::Running||inputClosed_){return false;}
  inputClosed_=true;
  process_.closeWriteChannel();
  return true;
}

bool FfmpegProcess::cancel() {
  if(QThread::currentThread()!=thread()||
      (state_!=FfmpegJobState::Starting&&state_!=FfmpegJobState::Running)){return false;}
  stop(FfmpegJobState::Cancelled);
  return true;
}

void FfmpegProcess::stop(FfmpegJobState cause) {
  if(stopCause_||(state_!=FfmpegJobState::Starting&&state_!=FfmpegJobState::Running)){return;}
  stopCause_=cause;
  state_=FfmpegJobState::Stopping;
  deadline_.stop();
  process_.terminate();
  forceStop_.start(stopGrace);
}

void FfmpegProcess::readProgress() {
  const auto bytes=process_.readAllStandardOutput();
  qsizetype first=0;
  while(first<bytes.size()){
    const auto end=bytes.indexOf('\n',first);
    const auto count=(end<0?bytes.size():end)-first;
    if(count>maxPending-pending_.size()){
      pending_.clear();
      stop(FfmpegJobState::Failed);
      return;
    }
    pending_.append(bytes.constData()+first,count);
    if(end<0){return;}
    if(pending_.endsWith('\r')){pending_.chop(1);}
    const auto line=QString::fromLatin1(pending_);
    pending_.clear();
    if(progress_.consume(line)==FfmpegProgressLine::Malformed){
      stop(FfmpegJobState::Failed);
      return;
    }
    first=end+1;
  }
}
void FfmpegProcess::readDiagnostic() {
  const auto raw=process_.readAllStandardError();
  const auto left=maxDiagnostic-diagnostics_.size();
  if(left>0){diagnostics_.append(raw.constData(), std::min(left, raw.size()));}
}
void FfmpegProcess::finish(int code,QProcess::ExitStatus exit) {
  readProgress();
  readDiagnostic();
  deadline_.stop();
  forceStop_.stop();
  const auto& final=progress_.latest();
  if(stopCause_){state_=*stopCause_;}
  else if(exit==QProcess::NormalExit&&inputClosed_&&submittedBytes_>0&&writtenBytes_==submittedBytes_&&
      pending_.isEmpty()&&progress_.complete(code,false)&&final&&final->outputBytes&&
      (ownedOutput_ = openOutput(output_, *final->outputBytes)) &&
      (!growingOutput_ || sameOutput(*growingOutput_, *ownedOutput_))) {
    state_=FfmpegJobState::Produced;
    return;
  }else{
    state_=FfmpegJobState::Failed;
  }
  growingOutput_.reset();
  ownedOutput_.reset();
  work_.reset();
  output_.clear();
}

void FfmpegProcess::failToStart() {
  diagnostics_ = process_.errorString().toUtf8().left(maxDiagnostic);
  deadline_.stop();
  forceStop_.stop();
  state_=stopCause_.value_or(FfmpegJobState::Failed);
  work_.reset();
  output_.clear();
}
QString FfmpegProcess::diagnostics() const {
  return QString::fromUtf8(diagnostics_);
}
QString FfmpegProcess::resultFile()const {
  return QThread::currentThread()==thread()&&state_==FfmpegJobState::Produced?output_:QString{};
}

std::shared_ptr<QFile> FfmpegProcess::resultHandle() const {
  return QThread::currentThread() == thread() && state_ == FfmpegJobState::Produced ? ownedOutput_ : nullptr;
}

std::shared_ptr<QFile> FfmpegProcess::progressiveHandle() {
  if (QThread::currentThread() != thread() || !fragmentMp4_) { return {}; }
  if (state_ == FfmpegJobState::Produced) { return ownedOutput_; }
  if (state_ != FfmpegJobState::Running) { return {}; }
  if (!growingOutput_) { growingOutput_ = openOutput(output_, std::nullopt); }
  return growingOutput_;
}

} // namespace biglybtc::media
