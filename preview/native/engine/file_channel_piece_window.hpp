// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace biglybtc::core {

struct FileChannelPieceWindow {
  std::uint32_t first=0;
  std::uint32_t count=0;
  std::int32_t delay=0;
  std::uint32_t spacing=0;
};

inline std::optional<FileChannelPieceWindow> fileChannelPieceWindow(
    std::uint64_t fileOffset,std::uint64_t readOffset,std::uint32_t pieceBytes,
    std::uint32_t pieceCount,std::uint64_t bytesPerSecond,std::uint32_t bufferMilliseconds=60000,
    std::uint32_t minimumPieces=5,std::int32_t delayMilliseconds=0) noexcept {
  if(!pieceBytes||!minimumPieces||readOffset>std::numeric_limits<std::uint64_t>::max()-fileOffset){
    return {};
  }
  const auto first=(fileOffset+readOffset)/pieceBytes;
  if(first>=pieceCount){return FileChannelPieceWindow{pieceCount,0,delayMilliseconds,0};}
  if(!bufferMilliseconds){bufferMilliseconds=60000;}
  const auto tail=bytesPerSecond%1000*bufferMilliseconds/1000;
  const auto whole=bytesPerSecond/1000;
  if(whole>(std::numeric_limits<std::uint64_t>::max()-tail)/bufferMilliseconds){return {};}
  const auto bytes=whole*bufferMilliseconds+tail;
  const auto timedPieces=std::max<std::uint64_t>(1,bytes/pieceBytes);
  // The original computes spacing before adjusting to the minimum.
  const auto spacing=std::uint32_t(bufferMilliseconds/timedPieces);
  const auto count=std::min<std::uint64_t>(pieceCount-first,
      std::max<std::uint64_t>(minimumPieces,timedPieces));
  const auto milliseconds=std::uint64_t(count-1)*spacing;
  if(milliseconds>std::uint64_t(std::int64_t(std::numeric_limits<std::int32_t>::max())-delayMilliseconds)){
    return {};
  }
  return FileChannelPieceWindow{std::uint32_t(first),std::uint32_t(count),delayMilliseconds,spacing};
}

} // namespace biglybtc::core
