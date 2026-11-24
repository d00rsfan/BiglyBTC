// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QStringView>
#include <cstdint>
#include <optional>

namespace biglybtc::media {

struct FfmpegProgressSample {
  std::optional<std::int64_t> frames,duplicateFrames,droppedFrames;
  std::optional<std::int64_t> elapsedMicroseconds,outputBytes;
  std::optional<double> framesPerSecond;
};

enum class FfmpegProgressLine {Ignored,Sample,End,Malformed};

class FfmpegProgress final {
public:
  FfmpegProgressLine consume(QStringView line);
  const std::optional<FfmpegProgressSample>& latest() const noexcept { return latest_; }
  bool complete(int exitCode,bool cancelled) const noexcept;

private:
  FfmpegProgressSample pending_;
  std::optional<FfmpegProgressSample> latest_;
  std::uint32_t fields_=0;
  bool ended_=false;
  bool failed_=false;
};

} // namespace biglybtc::media
