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
} // namespace source_literals
