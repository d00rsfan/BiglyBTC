#include "biglybtc/file_policy.hpp"

#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <string_view>

namespace {
using namespace biglybtc::compat;
std::size_t assertions = 0;
void check(bool condition, const char* message) {
  ++assertions;
  if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
std::vector<std::string_view> split(std::string_view text, char delimiter) {
  std::vector<std::string_view> result;
  for (;;) {
    const auto at = text.find(delimiter);
    result.push_back(text.substr(0, at));
    if (at == text.npos) { return result; }
    text.remove_prefix(at + 1);
  }
}
std::int64_t integer(std::string_view text) {
  std::int64_t result = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
  check(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size(), "Invalid generated integer");
  return result;
}
using Saved = std::vector<std::optional<std::int64_t>>;
OriginalFilePriorityLoad plan(std::size_t files, const Saved& saved) {
  const auto result = planOriginalFilePriorityLoad(files, std::span<const std::optional<std::int64_t>>(saved));
  check(result.has_value(), "Valid priority-load fixture rejected");
  return *result;
}
void units() {
  auto absent = planOriginalFilePriorityLoad(3, std::nullopt);
  check(absent && !absent->replacement && !absent->clearCachedPriorities, "Absent source must keep existing values and source cache");
  auto empty = plan(3, {});
  check(!empty.replacement && empty.clearCachedPriorities, "Empty list keeps constructor defaults and clears cached priorities");
  auto normal = plan(3, {-1, 4294967295LL, -4294967297LL});
  check(!normal.replacement && normal.clearCachedPriorities, "Wide Long values narrowing to -1 must keep constructor defaults");
  auto holes = plan(3, {std::nullopt, -1});
  check(!holes.replacement && holes.clearCachedPriorities, "Caught holes do not turn all-normal into a replacement");
  auto replace = plan(3, {1, std::nullopt});
  check(replace.replacement && *replace.replacement == std::vector<FilePolicy>{{false, 1}, {false, 0}, {false, 0}}
      && !replace.clearCachedPriorities, "Any nonnormal item replaces every file, including normal defaults at holes");
  auto narrow = plan(4, {4294967296LL, std::numeric_limits<std::int32_t>::min(), -4, std::numeric_limits<std::int64_t>::max()});
  check(narrow.replacement && *narrow.replacement == std::vector<FilePolicy>{{true, 0}, {false, -2147483647}, {false, -3}, {false, 0}}, "Signed restore values and disabled files differ");
  auto extra = plan(1, {-1, 0});
  check(!extra.replacement && extra.clearCachedPriorities, "Original ignores values beyond the current file count");
  auto zero = plan(0, {0});
  check(!zero.replacement && zero.clearCachedPriorities, "Zero-file present list follows original all-normal cleanup");
  check(!planOriginalFilePriorityLoad(100001, std::nullopt), "File bound must be checked before allocation");
  check(!planOriginalFilePriorityLoad(0, std::nullopt, 100001), "Unsupported maximum must refuse");
}
} // namespace

int main(int argc, char** argv) {
  units();
  if (argc == 1) { std::cout << "PASS_FILE_PRIORITY_LOAD_UNITS " << assertions << '\n'; return 0; }
  check(argc == 3, "Expected cases and actual Original callback output");
  std::ifstream cases(argv[1]), original(argv[2]);
  check(cases.good() && original.good(), "Reference files could not be read");
  std::size_t count = 0;
  for (std::string input, expected; std::getline(cases, input);) {
    check(input.size() < 8192 && bool(std::getline(original, expected)), "Bounded reference rows must match");
    const auto fields = split(input, '\t');
    check(fields.size() == 3, "Input protocol fields");
    const auto fileCount = integer(fields[1]);
    check(fileCount >= 0 && fileCount <= 32, "Bounded reference file count");
    Saved saved;
    std::optional<std::span<const std::optional<std::int64_t>>> source;
    if (fields[2] != "absent") {
      if (fields[2] != "empty") {
        for (const auto value : split(fields[2], ',')) {
          saved.push_back(value == "null" || value == "bad" ? std::nullopt : std::optional{integer(value)});
        }
      }
      source.emplace(saved);
    }
    auto result = planOriginalFilePriorityLoad(static_cast<std::size_t>(fileCount), source);
    check(result.has_value(), "Generated load plan must succeed");
    std::string actual(fields[0]);
    actual += result->replacement ? "\t1" : "\t0";
    actual += result->clearCachedPriorities ? "\t1\t" : "\t0\t";
    if (result->replacement) {
      for (const auto& file : *result->replacement) {
        if (&file != &result->replacement->front()) { actual += ','; }
        actual += file.skipped ? "1:" : "0:";
        actual += std::to_string(file.priority);
      }
    } else { actual += '-'; }
    check(actual == expected, "Native load/cleanup plan differs from actual Original call");
    ++count;
  }
  std::string extra;
  check(!std::getline(original, extra) && count > 0, "Reference output has no unmatched rows");
  std::cout << "PASS_NATIVE_FILE_PRIORITY_LOAD " << count << " rows; " << assertions << " assertions\n";
}
