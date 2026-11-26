// SPDX-License-Identifier: GPL-2.0-or-later
#include "biglybtc/auto_speed_v1.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace biglybtc::core;
constexpr int KiB = 1024;

void check(bool condition, const char* message) {
  if (!condition) { throw std::runtime_error(message); }
}

AutoSpeedV1Advice advice(AutoSpeedV1& speed, int ping, std::int64_t limit) {
  const auto result = speed.adjust(std::array<std::int32_t, 1>{ping}, limit);
  check(result.has_value(), "valid probe did not yield advice");
  return *result;
}
}

int main() {
  try {
    AutoSpeedV1 demand;
    check(!demand.adjust(std::array<std::int32_t, 1>{30}, 100 * KiB), "advice before first rate sample");
    demand.observeRates(30 * KiB, 0);
    check(!demand.adjust(std::array<std::int32_t, 3>{-1, -1, -1}, 100 * KiB), "failed pings produced advice");
    check(advice(demand, 30, 100 * KiB).upload == 100 * KiB, "idle upload lifted a cap");
    demand.observeRates(99 * KiB, 0);
    const auto biased = demand.adjust(std::array<std::int32_t, 3>{50, 80, 900}, 100 * KiB);
    check(biased && biased->upload == 101 * KiB, "outlier biased a healthy batch");

    AutoSpeedV1Settings settings;
    settings.downloadRatio = 2.5f;
    AutoSpeedV1 choke(settings);
    choke.observeRates(2 * KiB, 97 * KiB);
    const auto reduced = advice(choke, 850, 100 * KiB);
    check(reduced == AutoSpeedV1Advice{98 * KiB, 245 * KiB}, "protocol floor or download ratio changed");

    AutoSpeedV1 calibration;
    for (int tick = 0; tick < 61; ++tick) { calibration.observeRates(110 * KiB, 0); }
    check(advice(calibration, 180, 100 * KiB).upload == 4 * KiB, "start-up calibration did not lower upload");
    check(advice(calibration, 180, 4 * KiB).upload == 4 * KiB, "calibration did not keep its cap");
    for (int tick = 0; tick < 61; ++tick) { calibration.observeRates(110 * KiB, 0); }
    check(advice(calibration, 180, 4 * KiB).upload == 101 * KiB, "calibration timeout did not restore and resume");

    AutoSpeedV1 replacements;
    replacements.observeRates(0, 0);
    for (int sample = 0; sample < 5; ++sample) { check(advice(replacements, 40, 100 * KiB).upload == 100 * KiB, "idle calibration lifted upload"); }
    replacements.sourceReplaced();
    replacements.sourceReplaced();
    check(advice(replacements, 40, 100 * KiB).upload == 4 * KiB, "replacement contacts did not recalibrate");

    AutoSpeedV1 unlimited;
    unlimited.observeRates(10 * KiB, 0);
    check(advice(unlimited, 30, std::numeric_limits<std::int64_t>::max()).upload == KiB,
        "unlimited source limit wrapped incorrectly");
    std::cout << "Classic Auto Speed cases passed\n";
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
}
