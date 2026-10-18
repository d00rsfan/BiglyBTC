#include "biglybtc/saved_history.hpp"

#include <bitset>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace biglybtc::core;
using I = std::int64_t;
constexpr HistoryDate Date{1970, 1, 2};
std::size_t cases = 0, assertions = 0, javaScalars = 0;
void check(bool value, std::string_view message) {
  ++assertions; if (!value) { throw std::runtime_error(std::string(message)); }
}
template<class F> void run(std::string_view name, F body) {
  try { body(); ++cases; } catch (const std::exception& e) { throw std::runtime_error(std::string(name) + ": " + e.what()); }
}
std::string row(HistoryCounters values = {1, 2, 3, 4, 5, 6}) {
  std::string s;
  for (const auto value : values) { if (!s.empty()) { s += ','; } s += std::to_string(value); }
  return s;
}
std::string start(I minute = 1440, HistoryCounters values = {}) { return "s,1," + std::to_string(minute) + ',' + row(values) + '\n'; }
std::string finish(I minute = 1440, HistoryCounters values = {1, 2, 3, 4, 5, 6}) {
  return "e,1," + std::to_string(minute) + ',' + row(values) + '\n';
}
SavedHistoryDay read(std::string_view text, HistoryDate date = Date) {
  auto result = readSavedHistoryDay(text, date); check(result.has_value(), "expected valid native projection");
  check(result->date == date, "requested date retained"); return *result;
}
void error(std::string_view text, SavedHistoryError expected, HistoryDate date = Date) {
  const auto result = readSavedHistoryDay(text, date); check(!result && result.error() == expected, "wrong fixed error");
}
void unknown(const SavedHistoryDay& day) {
  check(!day.totals && day.observations == 0, "absence must not invent totals or observations");
  for (const auto& hour : day.hours) { check(!hour, "unobserved hour must be absent"); }
}
std::string fileBytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary); check(bool(input), "cannot open explicit generated fixture");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
I number(std::string_view value) {
  I result = 0; const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
  check(parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size(), "invalid frozen expectation integer"); return result;
}
void javaFixtures(const std::filesystem::path& directory) {
  std::ifstream input(directory / "expected.tsv"); check(bool(input), "expected.tsv required; no skipped oracle comparison");
  std::map<std::string, std::bitset<25>> seen;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line.front() == '#') { continue; }
    std::vector<std::string_view> fields;
    std::string_view rest(line);
    for (;;) { const auto tab = rest.find('\t'); fields.push_back(rest.substr(0, tab));
      if (tab == std::string_view::npos) { break; } rest.remove_prefix(tab + 1); }
    check(fields.size() == 11, "oracle TSV must have eleven fields");
    const std::string name(fields[0]);
    check(name.ends_with(".dat") && name.find('/') == std::string::npos && name.find('\\') == std::string::npos
        && name.find("..") == std::string::npos, "oracle basename must remain a fixed fixture leaf");
    const I year = number(fields[1]), month = number(fields[2]), day = number(fields[3]), hour = number(fields[4]);
    check(year >= 1970 && year <= 9999 && month >= 1 && month <= 12 && day >= 1 && day <= 31
        && hour >= -1 && hour <= 23, "oracle date/hour shape invalid");
    const auto slot = static_cast<std::size_t>(hour + 1);
    check(!seen[name].test(slot), "duplicate fixture hour in oracle TSV"); seen[name].set(slot);
    const auto snapshot = read(fileBytes(directory / name), {static_cast<int>(year), static_cast<unsigned>(month), static_cast<unsigned>(day)});
    const auto actual = hour == -1 ? snapshot.totals : snapshot.hours[static_cast<std::size_t>(hour)];
    for (std::size_t i = 0; i < 6; ++i) {
      // Java period API returns zero for a period with no observations. Native
      // absence remains absent; this scalar mapping does not invent known data.
      check((actual ? (*actual)[i] : 0) == number(fields[i + 5]), "original Java period scalar mismatch"); ++javaScalars;
    }
  }
  check(input.eof() && !seen.empty(), "oracle input incomplete/empty");
  for (const auto& [name, hours] : seen) { (void)name; check(hours.all(), "fixture must cover day plus all24 UTC hours"); }
}
} // namespace

int main(int argc, char** argv) {
  try {
    run("Gregorian-date-domain", [] {
      for (const auto d : {HistoryDate{1970, 1, 1}, HistoryDate{2000, 2, 29}, HistoryDate{2024, 2, 29}, HistoryDate{9999, 12, 31}}) {
        check(validHistoryDate(d), "valid Gregorian day rejected");
      }
      for (const auto d : {HistoryDate{1969, 12, 31}, HistoryDate{10000, 1, 1}, HistoryDate{1900, 2, 29},
          HistoryDate{2100, 2, 29}, HistoryDate{2024, 2, 30}, HistoryDate{2024, 0, 1}, HistoryDate{2024, 13, 1},
          HistoryDate{2024, 257, 1}, HistoryDate{2024, 1, 0}, HistoryDate{2024, 1, 257}}) {
        check(!validHistoryDate(d), "invalid Gregorian day accepted"); error("", SavedHistoryError::InvalidDate, d);
      }
    });
    run("empty-blank-header-only-remain-unknown", [] {
      for (const auto& text : {std::string{}, std::string("\n\r\n\n"), start(), start(1440, {10, 20, 30, 40, 50, 60})}) { unknown(read(text)); }
    });
    run("six-distinct-fields-baselines-not-counted", [] {
      auto value = read(start(1440, {1000, 2000, 3000, 4000, 5000, 6000}) + row() + '\n');
      check(value.observations == 1 && value.outsideDayRecords == 0, "observation counts differ");
      check(value.totals == HistoryCounters{1, 2, 3, 4, 5, 6} && value.hours[0] == value.totals, "counter order/baseline error");
      check(!value.hours[1], "unobserved adjacent hour fabricated");
    });
    run("known-zero-is-present", [] {
      auto value = read(start() + row({}) + '\n'); check(value.observations == 1, "zero record is an observation");
      check(value.totals == HistoryCounters{} && value.hours[0] == HistoryCounters{}, "known zero lost");
    });
    run("UTC-hour-boundary-and-overlapping-starts-add", [] {
      auto value = read(start(1498) + row() + '\n' + row() + '\n' + start(1499) + row() + '\n');
      check(value.hours[0] == HistoryCounters{1, 2, 3, 4, 5, 6}, "minute59 belongs to hour0");
      check(value.hours[1] == HistoryCounters{2, 4, 6, 8, 10, 12}, "minute60 overlap adds in hour1");
      check(value.totals == HistoryCounters{3, 6, 9, 12, 15, 18} && value.observations == 3, "overlap observations lost");
    });
    run("end-timestamp-ignored-and-anchor-retained", [] {
      auto value = read(start() + finish(99999) + row() + '\n');
      check(value.hours[0] == HistoryCounters{2, 4, 6, 8, 10, 12} && value.observations == 2, "end replaced/cleared reconstructed time");
    });
    run("positive-anchor-gate-no-start-zero-negative", [] {
      unknown(read(row() + '\n' + finish())); unknown(read(start(0) + row() + '\n'));
      unknown(read(start(-1) + row() + '\n'));
      auto value = read(start() + row() + '\n' + start(0) + row() + '\n');
      check(value.observations == 1 && value.outsideDayRecords == 0, "zero start failed to deactivate prior anchor");
    });
    run("outside-day-and-midnight-spill-validated-excluded", [] {
      auto value = read(start(1438) + row() + '\n' + row() + '\n' + start(2878) + row() + '\n' + row() + '\n');
      check(value.observations == 2 && value.outsideDayRecords == 2, "outside-day accounting differs");
      check(value.hours[0] == HistoryCounters{1, 2, 3, 4, 5, 6} && value.hours[23] == value.hours[0], "UTC midnight inclusion wrong");
      error(start(99999) + "bad,2,3,4,5,6\n", SavedHistoryError::InvalidRecord);
      error(start(99999) + "e,2,99999,1,2,3,4,5,6\n", SavedHistoryError::UnsupportedVersion);
    });
    run("signed-adjustments-and-explicit-plus-leading-zeros", [] {
      auto value = read("s,+01,+01440,-1,+2,0,0,0,0\n+1,-2,+0003,-4,+5,-6\n-1,+2,-3,+4,-5,+6\n");
      check(value.totals == HistoryCounters{} && value.observations == 2, "signed adjustments/plus syntax differ");
    });
    run("LF-CRLF-and-complete-EOF", [] {
      auto lf = read(start() + row() + '\n'); auto eof = read(start() + row());
      auto crlf = read("s,1,1440,0,0,0,0,0,0\r\n1,2,3,4,5,6\r\n");
      check(lf.totals == eof.totals && eof.totals == crlf.totals, "line ending changes data");
      error(start() + row() + '\r', SavedHistoryError::InvalidRecord);
    });
    run("owned-results-after-input-destruction", [] {
      std::string input = start() + row(); auto value = read(input); input.assign(1024, 'X');
      auto copy = value; auto moved = std::move(value);
      check(copy.totals == HistoryCounters{1, 2, 3, 4, 5, 6} && moved.hours[0] == copy.totals, "result borrowed input storage");
    });
    run("strict-record-shapes-and-numeric-grammar", [] {
      for (const std::string& text : std::vector<std::string>{"s,1,1440,0,0,0,0,0", "s,1,1440,0,0,0,0,0,0,0", "e,1,1440,0,0,0,0,0", "1,2,3,4,5", "1,2,3,4,5,6,", "1,2,3,4,5,6,7", "x,1,1440,0,0,0,0,0,0", "1,2,3,4,5,", " 1,2,3,4,5,6", "1,2,3,4,5,6 ", "+-1,2,3,4,5,6", "--1,2,3,4,5,6", "0x1,2,3,4,5,6", "+,2,3,4,5,6", " ", std::string("1,2,3,4,5,\0", 12)}) {
        error(text, SavedHistoryError::InvalidRecord);
      }
      for (const auto version : {"0", "2", "-1"}) { error(std::string("s,") + version + ",1440,0,0,0,0,0,0", SavedHistoryError::UnsupportedVersion); }
      error("e,2,0,0,0,0,0,0,0", SavedHistoryError::UnsupportedVersion);
      error("s,x,1440,0,0,0,0,0,0", SavedHistoryError::InvalidRecord);
    });
    run("all-input-validated-no-plausible-partial-result", [] {
      error(start() + row() + '\n' + "1,2,3,4,5,bad\n", SavedHistoryError::InvalidRecord);
      error(row() + '\n' + "e,1,1440,0,0,0,0,0,bad", SavedHistoryError::InvalidRecord);
    });
    run("signed64-values-and-numeric-overflow", [] {
      const auto maximum = std::numeric_limits<I>::max(), minimum = std::numeric_limits<I>::min();
      auto value = read(start() + row({maximum, minimum, 0, -1, 1, 0}));
      check(value.totals == HistoryCounters{maximum, minimum, 0, -1, 1, 0}, "signed64 extremes lost");
      error("9223372036854775808,0,0,0,0,0", SavedHistoryError::Overflow);
      error("-9223372036854775809,0,0,0,0,0", SavedHistoryError::Overflow);
      error("s,9223372036854775808,1,0,0,0,0,0,0", SavedHistoryError::Overflow);
    });
    run("hourly-and-cross-hour-daily-sum-overflow", [] {
      const I maximum = std::numeric_limits<I>::max(), minimum = std::numeric_limits<I>::min();
      error(start() + row({maximum, 0, 0, 0, 0, 0}) + '\n' + row({1, 0, 0, 0, 0, 0}), SavedHistoryError::Overflow);
      error(start() + row({minimum, 0, 0, 0, 0, 0}) + '\n' + row({-1, 0, 0, 0, 0, 0}), SavedHistoryError::Overflow);
      error(start() + row({maximum, 0, 0, 0, 0, 0}) + '\n' + start(1500) + row({1, 0, 0, 0, 0, 0}), SavedHistoryError::Overflow);
      for (std::size_t i = 0; i < 6; ++i) {
        HistoryCounters large{}, one{}; large[i] = maximum; one[i] = 1;
        error(start() + row(large) + '\n' + row(one), SavedHistoryError::Overflow);
      }
    });
    run("checked-source-timestamp-multiplication-and-reconstructed-step", [] {
      const I upper = std::numeric_limits<I>::max() / 60000, lower = std::numeric_limits<I>::min() / 60000;
      unknown(read(start(upper))); unknown(read(start(lower)));
      error(start(upper + 1), SavedHistoryError::Overflow); error(start(lower - 1), SavedHistoryError::Overflow);
      error(start() + finish(upper + 1), SavedHistoryError::Overflow);
      error(start(upper) + row(), SavedHistoryError::Overflow);
      check(read(start() + finish(upper)).observations == 1, "end timestamp improperly replaced clock");
    });
    run("one-MiB-input-boundary", [] {
      unknown(read(std::string(SavedHistoryMaximumBytes, '\n')));
      error(std::string(SavedHistoryMaximumBytes + 1, '\n'), SavedHistoryError::Limit);
    });
    run("line512-boundary-excludes-CRLF", [] {
      const std::string line = std::string(502, '0') + ",0,0,0,0,0";
      check(line.size() == SavedHistoryMaximumLineBytes, "bad line boundary fixture");
      unknown(read(line)); unknown(read(line + "\r\n")); error('0' + line, SavedHistoryError::Limit);
    });
    run("8192-nonblank-record-boundary", [] {
      std::string input;
      for (std::size_t i = 0; i < SavedHistoryMaximumRecords; ++i) { input += "0,0,0,0,0,0\n"; }
      unknown(read(input)); error(input + "0,0,0,0,0,0\n", SavedHistoryError::Limit);
    });
    run("pre-cancellation-even-on-empty-or-invalid-input", [] {
      std::stop_source source; source.request_stop();
      for (const auto text : {std::string_view{}, std::string_view{"bad"}}) {
        auto result = readSavedHistoryDay(text, {0, 0, 0}, source.get_token());
        check(!result && result.error() == SavedHistoryError::Cancelled, "cancel must not publish partial/empty success");
      }
    });
    run("original-Java-valid-fixture-period-scalars", [&] {
      check(argc == 2, "pass the frozen saved-history fixture directory as argv[1]"); javaFixtures(argv[1]);
    });
    std::cout << "PASS " << cases << " saved-history cases; " << assertions << " assertions; "
              << javaScalars << " original-Java valid period scalar comparisons; strict native malformed/overflow policy separate\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << "FAIL saved-history: " << e.what() << '\n'; return 1; }
}
