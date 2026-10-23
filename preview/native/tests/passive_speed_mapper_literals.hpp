#pragma once
#include <array>
#include <cstdint>
// Hand-derived finite traces from pinned cb8 addPing/region/getEstimatedLimit.
// These are source expectations, NOT output from an original-Java execution.
namespace source_literals {
struct Point {std::int64_t up,down,rtt;std::uint16_t x,y,metric;};
inline constexpr std::array<Point,2> Stable{{{25600,51200,10,50,100,0},{25600,51200,10,100,200,0}}};
inline constexpr std::array<Point,4> Choke{{{25600,51200,500,50,100,0},{25600,51200,500,100,200,0},{25600,51200,500,100,200,150},{51200,102400,500,150,300,150}}};
struct Floor {std::int64_t rtt;std::uint16_t metric;float rating;};
inline constexpr std::array<Floor,6> Floors{{{350,0,1},{351,50,1},{424,99,0.02f},{425,100,0},{499,149,-0.98f},{500,150,-1}}};
inline constexpr std::array<std::uint16_t,3> VarianceMetrics{0,7,14};
// Hand-derived trimmed middle eight of 16 choke limits. Not Java outputs.
inline constexpr std::array<std::int64_t,15> BadUpload{16384,16640,16896,17152,17408,17664,17920,18176,18432,18688,18944,19200,19456,19712,19968};
inline constexpr std::array<std::int64_t,15> BadDownload{32768,33280,33792,34304,34816,35328,35840,36352,36864,37376,37888,38400,38912,39424,39936};
inline constexpr std::int64_t ChokeUploadMean=18304,ChokeDownloadMean=36608;
inline constexpr std::int64_t ChokeUploadSquares=2752512,ChokeDownloadSquares=11010048;
inline constexpr std::int64_t ChokeUploadDeviation=586,ChokeDownloadDeviation=1173;
} // namespace source_literals
