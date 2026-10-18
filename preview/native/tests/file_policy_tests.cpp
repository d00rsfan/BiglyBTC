#include "biglybtc/file_policy.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
using namespace biglybtc;
void check(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; std::exit(1); } }
std::string bytes(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
core::Document parse(std::string value) {
  auto document = core::Document::parse(std::move(value));
  check(document.has_value(), "Test fixture did not parse");
  return std::move(*document);
}
core::Document group(std::string_view priority, std::string_view ranges) {
  return parse("d17:file_priorities_cd" + bytes(priority) + bytes(ranges) + "ee");
}
}

int main(int argc, char** argv) {
  using namespace biglybtc;
  if (argc == 3) {
    // Differential driver: the argument is an individual entry encoded as hex.
    const std::string hex(argv[1]);
    std::string data;
    auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    check(hex.size() % 2 == 0, "Invalid hex input");
    for (std::size_t i = 0; i < hex.size(); i += 2) {
      const auto high = digit(hex[i]), low = digit(hex[i + 1]);
      check(high >= 0 && low >= 0, "Invalid hex input");
      data.push_back(static_cast<char>(high * 16 + low));
    }
    auto document = parse(std::move(data));
    const auto policies = compat::originalFilePolicy(document.root(), std::stoul(argv[2]));
    check(policies.has_value(), policies ? "" : policies.error().message.c_str());
    for (const auto& value : *policies) { std::cout << value.skipped << ':' << value.priority << '\n'; }
    return 0;
  }
  auto absent = parse("de");
  auto normal = compat::originalFilePolicy(absent.root(), 3);
  check(normal && *normal == std::vector<compat::FilePolicy>(3), "Absent policy must retain original normal defaults");
  auto legacy = parse("d15:file_prioritiesli0ei1ei-1ei-4ei2147483647ei-2147483648ei4294967296eee");
  auto saved = compat::originalFilePolicy(legacy.root(), 7);
  check(saved && *saved == std::vector<compat::FilePolicy>{{true, 0}, {false, 1}, {false, 0}, {false, -3},
      {false, 2147483647}, {false, -2147483647}, {true, 0}}, "Original signed priority/skip/Long truncation semantics differ");
  auto compressed = parse("d17:file_priorities_cd2:-44:0,+12:-13:2-41:01:5e15:file_prioritiesli99eee");
  auto values = compat::originalFilePolicy(compressed.root(), 7);
  check(values && *values == std::vector<compat::FilePolicy>{{false, -3}, {false, -3}, {false, 0},
      {false, 0}, {false, 0}, {true, 0}, {false, 0}}, "Compressed ranges must override legacy and preserve sparse normal defaults");
  for (const auto& item : {std::pair{"1", "0-1000000000"}, {"1", "-1"}, {"1", "2-1"}, {"1", "0,,1"},
      {"1", "0-1-2"}, {"+-1", "0"}, {"9223372036854775808", "0"}, {"1", "2147483648"}}) {
    auto invalid = group(item.first, item.second);
    check(!compat::originalFilePolicy(invalid.root(), 3), "Unsafe original priority range was accepted");
  }
  auto conflict = parse("d17:file_priorities_cd1:11:01:21:0ee");
  check(!compat::originalFilePolicy(conflict.root(), 1), "Conflicting HashMap-dependent overlaps must not activate");
  auto overlap = parse("d17:file_priorities_cd1:11:02:+11:0ee");
  check(compat::originalFilePolicy(overlap.root(), 1).has_value(), "Equivalent priority groups are unambiguous");
  check(!compat::originalFilePolicy(absent.root(), 100001), "File count must be bounded before allocation");
  auto ranks = compat::linearPriorityRanks(std::vector<compat::FilePolicy>{{false, 8}, {false, -3}, {false, 0}, {false, 8}});
  check(ranks && *ranks == std::vector<std::uint8_t>{3, 1, 2, 3}, "Distinct original priority ordering collapsed");
  check(!compat::linearPriorityRanks(std::vector<compat::FilePolicy>{{true, 0}}), "Skipped-file activation must stay explicit");
  std::vector<compat::FilePolicy> many;
  for (int i = 0; i < 8; ++i) { many.push_back({false, i}); }
  check(!compat::linearPriorityRanks(many), "Unrepresentable original priority distinctions must not silently collapse");
  std::cout << "PASS original compressed/legacy priority ranges, Java signed values, bounds and ordered activation\n";
}
