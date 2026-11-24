// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QByteArrayView>
#include <QString>
#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

namespace biglybtc::media {

enum class ProfileError {
  Bounds,
  InvalidProperties,
  MissingName
};

struct AspectRatio {
  int numerator=0;
  int denominator=0;
};

struct TranscodeProfile {
  QString name;
  std::optional<QString> description;
  std::optional<QString> iconUrl;
  std::optional<QString> extension;
  std::optional<QString> container;
  std::optional<QString> videoTarget;
  std::optional<QString> videoOptions;
  std::optional<QString> audioOptions;
  QString device;
  int iconIndex=0;
  int videoWidth=0,videoHeight=0,maxWidth=0,maxHeight=0;
  int pixelMod=8,paddingMod=1;
  int overallBitrateHint=0;
  int audioMinBitrate=0,audioMaxBitrate=0;
  int audioMaxSampleRate=0,audioMaxChannels=0;
  std::int64_t videoTargetBitrate=0,audioBitrate=0,maxSize=0;
  double maxFrameRate=0;
  bool autoThreads=false,autoPadding=false,limitVideoBitrate=false,disableFrameRate=false;
  bool streamable=false,v2Supported=false;
  std::vector<AspectRatio> aspectRatios;
  std::optional<std::vector<int>> audioAllowedChannels,audioAllowedSampleRates,audioAllowedBitrates;
  std::vector<QString> rawOptions;

  std::int64_t estimatedSize(int durationSeconds) const noexcept;
};

struct TranscodeVideoGeometry {
  int width;
  int height;
  int padX;
  int padY;
  std::optional<AspectRatio> display;
};

std::optional<TranscodeVideoGeometry> transcodeVideoGeometry(const TranscodeProfile&, int sourceWidth, int sourceHeight);

// Interpret a saved Java Properties profile. No process or device access occurs.
std::expected<TranscodeProfile,ProfileError> readTranscodeProfile(QByteArrayView bytes,bool v2Enabled);

} // namespace biglybtc::media
