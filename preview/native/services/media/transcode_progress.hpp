// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QString>
#include <QStringView>

namespace biglybtc::media {

enum class TranscodeOutputMode {Classic,V2};
enum class TranscodeLine {Progress,Diagnostic,Malformed};
enum class TranscodeEnd {
  Complete,Cancelled,ProcessFailed,PartialInput,MissingProgress,MissingTrailer,MalformedProgress
};

struct TranscodeProgressValue {
  int frame=0;
  int framesPerSecond=0;
};

class TranscodeProgress final {
public:
  explicit TranscodeProgress(TranscodeOutputMode mode) : mode_(mode) {}
  TranscodeLine consume(QStringView line);
  TranscodeProgressValue current() const noexcept { return current_; }
  bool trailerSeen() const noexcept { return trailerSeen_; }
  QStringView diagnostics() const noexcept { return diagnostics_; }
  TranscodeEnd finish(int exitCode,bool cancelled) const noexcept;

private:
  TranscodeOutputMode mode_;
  TranscodeProgressValue current_;
  QString diagnostics_;
  bool progressSeen_=false;
  bool trailerSeen_=false;
  bool malformed_=false;
};

} // namespace biglybtc::media
