// SPDX-License-Identifier: GPL-2.0-or-later
#include "transcode_profile.hpp"

#include <algorithm>

namespace biglybtc::media {
namespace {
constexpr int maxDimension = 32768;
constexpr int maxModifier = 1024;

bool dimensions(int width, int height) {
  return width > 0 && height > 0 && width <= maxDimension && height <= maxDimension;
}

int roundTo(int value, int round) {
  return round * ((value + round / 2) / round);
}

float aspect(const AspectRatio& ratio) {
  return static_cast<float>(ratio.numerator) / static_cast<float>(ratio.denominator);
}

int padding(int difference, int round) {
  const auto half = difference / 2;
  return round > 0 ? half - half % round : half;
}
} // namespace

std::optional<TranscodeVideoGeometry> transcodeVideoGeometry(const TranscodeProfile& profile, int width, int height) {
  if (!dimensions(width, height) || profile.maxWidth < 0 || profile.maxHeight < 0 || profile.videoWidth < 0 ||
      profile.videoHeight < 0 || profile.maxWidth > maxDimension || profile.maxHeight > maxDimension ||
      profile.videoWidth > maxDimension || profile.videoHeight > maxDimension ||
      profile.pixelMod <= 0 || profile.pixelMod > maxModifier || profile.paddingMod < 0 ||
      profile.paddingMod > maxModifier || profile.aspectRatios.size() > 64) {
    return std::nullopt;
  }
  if (profile.maxWidth > 0 && profile.maxHeight > 0) {
    if (width > profile.maxWidth) {
      const auto ratio = static_cast<double>(height) / width;
      width = profile.maxWidth;
      height = std::min(profile.maxHeight, roundTo(static_cast<int>(width * ratio), profile.pixelMod));
    }
    if (height > profile.maxHeight) {
      const auto ratio = static_cast<double>(width) / height;
      height = profile.maxHeight;
      width = std::min(profile.maxWidth, roundTo(static_cast<int>(height * ratio), profile.pixelMod));
    }
  }
  TranscodeVideoGeometry result{width, height, 0, 0, std::nullopt};
  if (profile.videoWidth > 0 && profile.videoHeight > 0) {
    if (!dimensions(width, height)) { return std::nullopt; }
    const auto sourceAspect = static_cast<float>(width) / static_cast<float>(height);
    result.width = profile.videoWidth;
    result.height = profile.videoHeight;
    for (const auto& candidate : profile.aspectRatios) {
      if (!dimensions(candidate.numerator, candidate.denominator)) { return std::nullopt; }
      result.display = candidate;
      if (sourceAspect <= aspect(candidate)) { break; }
    }
    if (result.display && profile.autoPadding) {
      const auto& display = *result.display;
      if (sourceAspect >= aspect(display)) {
        const auto scaled = static_cast<int>(static_cast<float>(result.height * display.numerator) /
            (sourceAspect * static_cast<float>(display.denominator)));
        result.padY = padding(result.height - scaled, profile.paddingMod);
        result.height -= 2 * result.padY;
      } else {
        const auto scaled = static_cast<int>(static_cast<float>(result.width * display.denominator) * sourceAspect /
            static_cast<float>(display.numerator));
        result.padX = padding(result.width - scaled, profile.paddingMod);
        result.width -= 2 * result.padX;
      }
    }
  }
  return dimensions(result.width, result.height) && result.padX >= 0 && result.padY >= 0 ?
      std::optional(result) : std::nullopt;
}

} // namespace biglybtc::media
