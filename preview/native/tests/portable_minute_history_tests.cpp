// SPDX-License-Identifier: GPL-2.0-or-later
#include "file_operation_io.hpp"
#include "history_month_view.hpp"
#include "history_range_view.hpp"
#include "history_view.hpp"
#include "history_week_view.hpp"
#include "minute_history_writer.hpp"
#include "../platform/utf8_path.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
using namespace biglybtc;
namespace fs = std::filesystem;
namespace s = storage;

constexpr std::int64_t Jan1 = 1704067200000LL;
constexpr core::HistoryDate Date{2024, 1, 1};
constexpr core::MinuteHistoryIdentity Identity{1, 2, 3};
constexpr core::HistoryCounters Totals{3, 6, 9, 12, 15, 18};

void check(bool passed, const char* message) {
  if (!passed) { throw std::runtime_error(message); }
}

template<class T> T take(std::expected<T, s::Error> result) {
  if (!result) { throw std::runtime_error(result.error().message); }
  return std::move(*result);
}

void take(std::expected<void, s::Error> result) {
  if (!result) { throw std::runtime_error(result.error().message); }
}

struct Fixture {
  fs::path profile;
  std::optional<s::ProfileLease> lease;
  Fixture() {
    const auto temporary = fs::canonical(fs::temp_directory_path()).make_preferred();
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    unsigned attempt = 0;
    do {
      profile = temporary / platform::pathFromUtf8(
          "BiglyBTC история " + std::to_string(tick) + '-' + std::to_string(++attempt));
    } while (!fs::create_directory(profile));
    lease.emplace(take(s::ProfileLease::acquire(profile)));
  }
  ~Fixture() {
    lease.reset();
    std::error_code ignored;
    fs::remove_all(profile, ignored);
  }
};

std::string readBytes(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  check(bool(input), "Could not read generated history");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

core::MinuteHistoryRequest request(core::MinuteHistoryEvent event, unsigned minutes, core::HistoryCounters raw) {
  return {event, Identity, Jan1 + minutes * 60000,
      {std::array{raw[0], raw[1], raw[2], raw[3]}, std::array{raw[4], raw[5]}},
      event == core::MinuteHistoryEvent::Start
          ? std::optional{core::HistoryCounters{100, 200, 300, 400, 500, 600}} : std::nullopt};
}

void appendAndReadOriginalHistory() {
  Fixture fixture;
  auto& lease = *fixture.lease;
  core::MinuteHistoryRecorder recorder;
  take(s::writeMinuteHistory(lease, recorder,
      request(core::MinuteHistoryEvent::Start, 0, {10, 20, 30, 40, 50, 60})));
  const auto file = fixture.profile / "stats" / "2024" / "01" / "01.dat";
  const auto monthDirectory = s::fileop::directory(file.parent_path());
  auto witness = take(s::detail::openStateWitness(monthDirectory.get(), file.filename()));
  const auto initial = take(s::detail::stateStamp(witness.get()));
  check(s::fileop::privateEntry(witness.get()), "New history file lacks private access");
  witness = s::detail::StateDescriptor{};

  take(s::writeMinuteHistory(lease, recorder,
      request(core::MinuteHistoryEvent::Tick, 1, {11, 22, 33, 44, 55, 66})));
  take(s::writeMinuteHistory(lease, recorder,
      request(core::MinuteHistoryEvent::End, 2, {13, 26, 39, 52, 65, 78})));
  constexpr std::string_view Data =
      "s,1,28401120,100,200,300,400,500,600\n"
      "1,2,3,4,5,6\n"
      "e,1,28401122,2,4,6,8,10,12\n";
  const auto stamp = take(s::detail::stateStampAt(monthDirectory.get(), file.filename()));
  check(stamp && s::detail::sameStateIdentity(initial, *stamp) && readBytes(file) == Data,
      "History append changed the original inode or bytes");

  const auto day = take(s::readSavedHistoryDayView(lease, Date));
  check(day.snapshot && day.snapshot->totals == Totals, "Day history did not read the appended records");
  const auto week = take(s::readSavedHistoryWeekView(lease, Date));
  const auto month = take(s::readSavedHistoryMonthView(lease, Date));
  check(week.days.back().snapshot && week.days.back().snapshot->totals == Totals
      && month.days.back().snapshot && month.days.back().snapshot->totals == Totals,
      "Week or month history did not include the appended day");
  std::ofstream(file.parent_path() / platform::pathFromUtf8("заметка")).put('x');
  const auto all = take(s::readSavedHistoryRangeView(lease, {core::HistoryPeriod::All, 0, Jan1 + 120000}));
  check(all.presentFiles == 1 && all.totals == Totals, "History inventory did not find the appended day");

  const auto alias = fixture.profile / "history-file-link";
  fs::create_hard_link(file, alias);
  core::MinuteHistoryRecorder another;
  const auto held = s::writeMinuteHistory(lease, another,
      request(core::MinuteHistoryEvent::Start, 3, {13, 26, 39, 52, 65, 78}));
  check(!held && held.error().code == s::ErrorCode::UnsafePath && readBytes(file) == Data,
      "An append modified a shared history file");
}

void cancellationAvoidsCreatingFiles() {
  Fixture fixture;
  std::stop_source stop;
  stop.request_stop();
  core::MinuteHistoryRecorder recorder;
  const auto wrote = s::writeMinuteHistory(*fixture.lease, recorder,
      request(core::MinuteHistoryEvent::Start, 0, {10, 20, 30, 40, 50, 60}), stop.get_token());
  check(!wrote && wrote.error().code == s::ErrorCode::Cancelled && !fs::exists(fixture.profile / "stats"),
      "Cancellation created history files");
}
}

int main() {
  try {
    appendAndReadOriginalHistory();
    cancellationAvoidsCreatingFiles();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
