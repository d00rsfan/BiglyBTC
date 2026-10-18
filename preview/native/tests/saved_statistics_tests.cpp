#include "biglybtc/saved_statistics.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

// Generated bencode and source-reading-derived facts only. No original Java,
// Core/manager startup, profile IO, live totals or runtime equality claim.
namespace {
using namespace biglybtc::core;
using I = std::int64_t;
using O = std::optional<I>;
std::size_t assertions = 0;
std::size_t cases = 0;
void check(bool value, const char* message) {
  ++assertions;
  if (!value) { throw std::runtime_error(message); }
}
template<class Function>
void run(std::string_view name, Function&& function) {
  try { function(); ++cases; }
  catch (const std::exception& failure) {
    throw std::runtime_error(std::string(name) + ": " + failure.what());
  }
}
std::string bytes(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
std::string integer(I value) { return 'i' + std::to_string(value) + 'e'; }
std::string map(std::initializer_list<std::pair<std::string_view, std::string>> fields) {
  std::string result = "d";
  for (const auto& [key, value] : fields) { result += bytes(key) + value; }
  return result + 'e';
}
SavedStatistics read(std::string_view input) {
  auto result = readSavedStatistics(input);
  check(result.has_value(), "Generated saved statistics must project");
  check(result->source != nullptr, "Every success must retain its owned source");
  check(result->source->originalBytes() == input && result->source->serialize() == input,
      "Source encoding/order/unknown bytes must remain exact");
  return std::move(*result);
}
void error(std::string_view input, SavedStatisticsError expected) {
  const auto result = readSavedStatistics(input);
  check(!result && result.error() == expected, "Typed error mismatch");
}
void absent(const SavedStatistics& value) {
  for (const auto& field : value.values) {
    check(!field.value && !field.invalidType, "Absent field must not become zero or invalid");
  }
}
std::string listWithIntegers(std::size_t count) {
  std::string result = "l";
  result.reserve(count * 3 + 2);
  for (std::size_t i = 0; i < count; ++i) { result += "i0e"; }
  return result + 'e';
}
std::string documentOfSize(std::size_t wanted) {
  for (std::size_t overhead = 1; overhead < 64; ++overhead) {
    const auto payload = wanted - overhead;
    auto encoded = map({{"opaque", bytes(std::string(payload, 'x'))}});
    if (encoded.size() == wanted) { return encoded; }
  }
  throw std::runtime_error("Cannot construct requested boundary document");
}
void since(const O& total, const O& baseline, const O& expected, bool clamped = false) {
  const auto value = deriveSavedSinceMark(total, baseline);
  check(value.has_value() == expected.has_value(), "Since-mark presence mismatch");
  if (expected) {
    check(value->value == *expected && value->clamped == clamped, "Since-mark value/clamp mismatch");
  }
}
} // namespace

int main() {
  try {
    run("empty-root-does-not-invent-all-or-zero", [] {
      const auto value = read("de");
      check(!value.hasAll, "Absent all differs from an empty dictionary");
      absent(value);
    });
    run("empty-all-is-present-with-thirteen-unknowns", [] {
      const auto value = read("d3:alldee");
      check(value.hasAll, "Empty all is present");
      absent(value);
    });
    run("all-thirteen-original-key-enum-mappings", [] {
      const std::array<std::string_view, 13> originalKeys{
          "downloaded", "uploaded", "uptime", "mark_time", "mark_downloaded", "mark_uploaded",
          "mark_uptime", "dht_down", "dht_up", "p_uploaded", "d_uploaded", "p_downloaded", "d_downloaded"};
      const std::array<SavedStatistic, 13> fields{
          SavedStatistic::Downloaded, SavedStatistic::Uploaded, SavedStatistic::Uptime, SavedStatistic::MarkTime,
          SavedStatistic::MarkDownloaded, SavedStatistic::MarkUploaded, SavedStatistic::MarkUptime,
          SavedStatistic::DhtDownloaded, SavedStatistic::DhtUploaded, SavedStatistic::ProtocolUploaded,
          SavedStatistic::DataUploaded, SavedStatistic::ProtocolDownloaded, SavedStatistic::DataDownloaded};
      check(SavedStatisticKeys == originalKeys, "Public source key order mismatch");
      std::string all = "d";
      for (std::size_t i = 0; i < originalKeys.size(); ++i) {
        all += bytes(originalKeys[i]) + integer(static_cast<I>(101 + i * 17));
      }
      const auto value = read(map({{"all", all + 'e'}}));
      check(value.hasAll, "Complete all is present");
      for (std::size_t i = 0; i < fields.size(); ++i) {
        check(value.get(fields[i]).value == static_cast<I>(101 + i * 17)
            && !value.get(fields[i]).invalidType, "Distinct original fields must not be swapped or summed");
      }
    });
    run("scalar-signed64-bounds-and-zero-are-raw-facts", [] {
      const auto value = read(map({{"all", map({{"downloaded", integer(std::numeric_limits<I>::min())},
          {"uploaded", integer(std::numeric_limits<I>::max())}, {"uptime", integer(0)},
          {"mark_time", integer(-1)}, {"dht_down", integer(-9)}})}}));
      check(value.get(SavedStatistic::Downloaded).value == std::numeric_limits<I>::min(), "Signed minimum retained");
      check(value.get(SavedStatistic::Uploaded).value == std::numeric_limits<I>::max(), "Signed maximum retained");
      check(value.get(SavedStatistic::Uptime).value == 0, "Stored zero is known");
      check(value.get(SavedStatistic::MarkTime).value == -1 && value.get(SavedStatistic::DhtDownloaded).value == -9,
          "Negative timestamps/counters are retained rather than sanitized");
      check(!value.get(SavedStatistic::MarkUploaded).value, "Other missing fields remain unknown");
    });
    run("bytes-list-dictionary-types-flag-only-the-affected-field", [] {
      const auto value = read(map({{"all", map({{"downloaded", bytes("12")}, {"uploaded", "le"},
          {"uptime", "de"}, {"mark_time", integer(1234)}})}}));
      for (const auto field : {SavedStatistic::Downloaded, SavedStatistic::Uploaded, SavedStatistic::Uptime}) {
        check(!value.get(field).value && value.get(field).invalidType, "Wrong scalar type is explicit, never default zero");
      }
      check(value.get(SavedStatistic::MarkTime).value == 1234 && !value.get(SavedStatistic::MarkTime).invalidType,
          "A malformed neighbor must not poison a valid scalar");
      check(!value.get(SavedStatistic::DataUploaded).value && !value.get(SavedStatistic::DataUploaded).invalidType,
          "Missing scalar differs from wrong type");
    });
    run("every-known-field-validates-its-own-type", [] {
      for (std::size_t i = 0; i < SavedStatisticKeys.size(); ++i) {
        const auto value = read(map({{"all", map({{SavedStatisticKeys[i], bytes("")}})}}));
        for (std::size_t j = 0; j < value.values.size(); ++j) {
          check(!value.values[j].value && value.values[j].invalidType == (i == j), "Independent per-field type provenance");
        }
      }
    });
    run("root-and-all-opaque-bytes-order-and-nesting-survive", [] {
      const auto opaque = std::string("\0\xff\r\n", 4);
      const auto input = map({{"z-plugin", map({{"raw", bytes(opaque)}})},
          {"all", map({{"unknown", 'l' + bytes(opaque) + "i-7ee"}, {"downloaded", integer(17)}})},
          {"a-tail", bytes("tail")}});
      const auto value = read(input);
      check(value.get(SavedStatistic::Downloaded).value == 17, "Known field alongside opaque subtrees");
      check(value.source->root().children.front().bytes == "z-plugin", "Unsorted root order retained");
      check(value.source->find("z-plugin")->find("raw")->bytes == opaque, "Binary unknown content retained");
      const auto* list = value.source->find("all")->find("unknown");
      check(list->type == Node::Type::List && list->children[0].bytes == opaque
          && list->children[1].integer == -7, "Nested unknown values retained without interpretation");
    });
    run("source-ownership-survives-input-overwrite-copy-move-and-release", [] {
      std::string input = map({{"all", map({{"downloaded", integer(89)}})}, {"opaque", bytes("owned")}});
      const auto expected = input;
      auto first = read(input);
      auto copied = first;
      auto moved = std::move(first);
      input.assign(input.size(), 'x');
      input.clear();
      check(copied.source == moved.source, "Copies share immutable owned source without duplicate tree");
      moved.source.reset();
      check(copied.source->originalBytes() == expected && copied.source->find("opaque")->bytes == "owned",
          "Retained immutable source cannot borrow caller input or moved-away owner");
      check(copied.get(SavedStatistic::Downloaded).value == 89, "Primitive facts outlive caller bytes");
    });
    run("independent-source-parses-do-not-alias-or-mutate", [] {
      const auto first = read("d3:alld10:downloadedi5eee");
      const auto second = read("d3:alld10:downloadedi8eee");
      check(first.source != second.source, "Independent reads retain independent source ownership");
      check(first.get(SavedStatistic::Downloaded).value == 5
          && second.get(SavedStatistic::Downloaded).value == 8, "Later parse must not alter prior facts");
    });
    run("non-dictionary-roots-fail", [] {
      for (const auto input : {"i1e", "0:", "le"}) { error(input, SavedStatisticsError::InvalidDocument); }
    });
    run("wrong-all-types-fail-with-distinct-error", [] {
      for (const auto input : {"d3:alli0ee", "d3:all0:e", "d3:alllee"}) {
        error(input, SavedStatisticsError::InvalidAll);
      }
    });
    run("syntax-trailing-and-out-of-int64-are-invalid-document", [] {
      for (const auto input : {"", "d", "deX", "d3:alli9223372036854775808ee", "d3:alli-9223372036854775809ee",
          "d3:alld10:downloadedi01eee", "d3:alld10:downloadedi-0eee", "d3:alld1:xe"}) {
        error(input, SavedStatisticsError::InvalidDocument);
      }
    });
    run("duplicate-all-and-field-keys-are-parser-rejections", [] {
      error("d3:allde3:alldee", SavedStatisticsError::InvalidDocument);
      error("d3:alld10:downloadedi1e10:downloadedi2eee", SavedStatisticsError::InvalidDocument);
    });
    run("whole-input-exact-limit-retains-opaque-payload", [] {
      const auto input = documentOfSize(Limits{}.inputBytes);
      const auto value = read(input);
      check(!value.hasAll && value.source->originalBytes().size() == Limits{}.inputBytes, "Inclusive whole-document cap");
      absent(value);
    });
    run("whole-input-over-limit-rejected-before-copy-or-parse", [] {
      const std::string input(Limits{}.inputBytes + 1, 'x');
      error(input, SavedStatisticsError::Limit);
    });
    run("declared-string-over-limit-is-limit-not-truncated-format", [] {
      error("d1:x4194305:e", SavedStatisticsError::Limit);
    });
    run("exact-node-cap-includes-root-dictionary-key-and-list", [] {
      const auto value = read(map({{"x", listWithIntegers(Limits{}.nodes - 3)}}));
      check(value.source->find("x")->children.size() == Limits{}.nodes - 3, "Inclusive node cap");
    });
    run("node-over-limit-fails-even-in-unknown-subtree", [] {
      error(map({{"x", listWithIntegers(Limits{}.nodes - 2)}}), SavedStatisticsError::Limit);
    });
    run("exact-depth-cap-is-root-zero-and-unknowns-are-bounded", [] {
      const auto lists = Limits{}.depth - 1;
      const auto value = read(map({{"x", std::string(lists, 'l') + "i0e" + std::string(lists, 'e')}}));
      check(!value.hasAll, "Depth-bounded opaque document remains valid");
    });
    run("depth-over-limit-fails-before-unbounded-recursion", [] {
      const auto lists = Limits{}.depth;
      error(map({{"x", std::string(lists, 'l') + "i0e" + std::string(lists, 'e')}}), SavedStatisticsError::Limit);
    });
    run("since-mark-requires-both-known-nonnegative-operands", [] {
      since({}, {}, {}); since({}, 0, {}); since(0, {}, {});
      since(-1, 0, {}); since(0, -1, {});
      since(std::numeric_limits<I>::min(), 0, {});
      since(std::numeric_limits<I>::max(), std::numeric_limits<I>::min(), {});
    });
    run("since-mark-subtraction-and-zero-boundaries", [] {
      since(0, 0, 0); since(19, 0, 19); since(19, 7, 12); since(19, 19, 0);
      since(std::numeric_limits<I>::max(), 0, std::numeric_limits<I>::max());
      since(std::numeric_limits<I>::max(), std::numeric_limits<I>::max() - 1, 1);
      since(std::numeric_limits<I>::max(), std::numeric_limits<I>::max(), 0);
    });
    run("since-mark-upper-baseline-clamps-without-overflow", [] {
      since(0, 1, 0, true); since(19, 20, 0, true);
      since(0, std::numeric_limits<I>::max(), 0, true);
      since(std::numeric_limits<I>::max() - 1, std::numeric_limits<I>::max(), 0, true);
    });
    run("mark-time-zero-does-not-disable-since-mark-and-source-stays-raw", [] {
      const auto input = map({{"all", map({{"downloaded", integer(12)}, {"mark_downloaded", integer(5)},
          {"uploaded", integer(8)}, {"mark_uploaded", integer(10)}, {"mark_time", integer(0)}})}});
      const auto value = read(input);
      since(value.get(SavedStatistic::Downloaded).value, value.get(SavedStatistic::MarkDownloaded).value, 7);
      since(value.get(SavedStatistic::Uploaded).value, value.get(SavedStatistic::MarkUploaded).value, 0, true);
      check(value.get(SavedStatistic::MarkUploaded).value == 10 && value.get(SavedStatistic::MarkTime).value == 0,
          "Derivation must not normalize raw baseline or mark timestamp");
      check(value.source->serialize() == input, "Since-mark calculation cannot write the source");
    });
    run("malformed-or-negative-metric-does-not-poison-independent-since-mark", [] {
      const auto value = read(map({{"all", map({{"downloaded", bytes("bad")}, {"mark_downloaded", integer(0)},
          {"uploaded", integer(50)}, {"mark_uploaded", integer(7)}, {"uptime", integer(-1)},
          {"mark_uptime", integer(0)}})}}));
      since(value.get(SavedStatistic::Downloaded).value, value.get(SavedStatistic::MarkDownloaded).value, {});
      since(value.get(SavedStatistic::Uploaded).value, value.get(SavedStatistic::MarkUploaded).value, 43);
      since(value.get(SavedStatistic::Uptime).value, value.get(SavedStatistic::MarkUptime).value, {});
    });
    std::cout << "PASS " << cases << " saved-statistics generated cases; " << assertions
        << " assertions; no Java/runtime equality credit\n";
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << "FAIL saved-statistics: " << failure.what() << '\n';
    return 1;
  }
}
