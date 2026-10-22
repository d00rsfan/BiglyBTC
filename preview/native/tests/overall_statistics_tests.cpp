#include "biglybtc/overall_statistics.hpp"
#include "biglybtc/overall_document.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace biglybtc::core;
std::size_t assertions = 0;
void require(bool value, std::string_view context) {
  ++assertions;
  if (!value) { throw std::runtime_error(std::string(context)); }
}
constexpr std::size_t index(SavedStatistic key) { return static_cast<std::size_t>(key); }
std::string bytes(std::string_view value) { return std::to_string(value.size()) + ":" + std::string(value); }
std::string dict(const std::vector<std::pair<std::string, std::string>>& fields) {
  std::string value = "d";
  for (const auto& [key, encoded] : fields) { value += bytes(key) + encoded; }
  return value + "e";
}
std::string raw(const Document& document, const Node& node) {
  return document.originalBytes().substr(node.begin, node.end - node.begin);
}
std::vector<std::string> keys(const Node& dictionary) {
  std::vector<std::string> result;
  for (std::size_t i = 0; i < dictionary.children.size(); i += 2) { result.emplace_back(dictionary.children[i].bytes); }
  return result;
}
void initialSnapshotAndBaselines() {
  OverallStatistics value({}, {11, 13, 17, 19}, 1000000, 1000000);
  const auto initial = value.observe();
  require(initial.snapshot[6] == 19 && initial.snapshot[9] == 11, "initial snapshot contains current peer sample");
  auto result = value.update({11, 13, 17, 19}, {}, 1000000, false);
  require(result && !result->save, "zero-time first accepted sample has no forced save");
  require(value.observe().downloaded == 24 && value.observe().uploaded == 36, "initial peer sample did not seed zero delta baselines");
  auto again = value.update({11, 13, 17, 19}, {}, 1000000, true);
  require(again && again->save && value.observe().downloaded == 24, "equal repeated sample does not add bytes again");
}
void partialAndClockOrdering() {
  OverallStatistics value({}, {}, 0, 0);
  const auto initial = value.observe().snapshot;
  auto gap = value.update({9, 1, 5, 2}, {}, 101000, true);
  require(gap && gap->disposition == OverallUpdateDisposition::ClockGap && !gap->save, "force does not bypass clock gap");
  require(value.observe().downloaded == 10 && value.observe().uptime == 0 && value.observe().snapshot == initial,
      "gap changes byte baselines but not uptime or snapshot");
  auto accepted = value.update({10, 1, 5, 3}, {}, 102000, true);
  require(accepted && accepted->save && value.observe().downloaded == 11 && value.observe().uptime == 1,
      "next sample uses byte/time baselines already advanced by gap");
  const auto before = value.observe();
  auto backwards = value.update({100, 100, 100, 100}, {}, 101000, true);
  require(backwards && backwards->disposition == OverallUpdateDisposition::ClockBackwards && value.observe() == before,
      "backward clock returns before counters or snapshot");
  auto next = value.update({11, 1, 5, 3}, {}, 102000, true);
  require(next && next->save && value.observe().uptime == 2 && value.observe().downloaded == 12,
      "backward return advances only private clock baseline");
}
void transportFailureAndInvalidInput() {
  OverallStatistics value({}, {}, 0, 0);
  std::array<std::optional<OverallTransportCounters>, 2> cells{OverallTransportCounters{2, 3}, std::nullopt};
  auto failed = value.update({4, 5, 6, 7}, {OverallDhtResolution::Ready, cells}, 1000, true);
  require(!failed && failed.error() == OverallError::TransportUnavailable, "transport failure surfaced");
  require(value.observe().downloaded == 9 && value.observe().uploaded == 13 && value.observe().uptime == 0,
      "transport failure retains original partial peer changes only");
  const auto before = value.observe();
  auto invalid = value.update({100, 100, 100, 100}, {OverallDhtResolution::Ready, {}}, 1000, true);
  require(!invalid && invalid.error() == OverallError::InvalidInput && value.observe() == before,
      "missing cached transport cells rejected before mutation");
  cells[1] = OverallTransportCounters{11, 17};
  auto recovered = value.update({4, 5, 6, 7}, {OverallDhtResolution::Empty, cells}, 1000, true);
  require(recovered && recovered->save && (*recovered->save)[index(SavedStatistic::DhtUploaded)] == 13
      && (*recovered->save)[index(SavedStatistic::DhtDownloaded)] == 20 && value.observe().downloaded == 9,
      "cached transports survive provider mode change; peer delta not applied twice");
  const auto final = value.observe();
  std::array<std::optional<OverallTransportCounters>, 17> excessive{};
  auto tooMany = value.update({}, {OverallDhtResolution::Ready, excessive}, 2000, false);
  require(!tooMany && value.observe() == final, "transport bound validation is mutation-free");
  auto badEnum = value.update({}, {static_cast<OverallDhtResolution>(99), cells}, 2000, false);
  require(!badEnum && value.observe() == final, "invalid resolution validation is mutation-free");
}
void markGetterAndOverflow() {
  OverallValues loaded{};
  loaded[index(SavedStatistic::Downloaded)] = std::numeric_limits<std::int64_t>::max();
  loaded[index(SavedStatistic::Uploaded)] = 12;
  loaded[index(SavedStatistic::Uptime)] = 3;
  loaded[index(SavedStatistic::MarkDownloaded)] = -1;
  OverallStatistics value(loaded, {}, std::numeric_limits<std::int64_t>::max(), 0);
  const auto since = value.querySince(OverallSince::Downloaded);
  require(since && since->value == std::numeric_limits<std::int64_t>::min(), "since subtraction wraps Java long");
  const auto observed = value.observe();
  require(observed.averageUpload == 4, "lifetime average uses uptime quotient");
  value.setMark(-9);
  require(value.observe().markTime == -9 && value.querySince(OverallSince::Uploaded)->value == 0, "mark copies totals without sampling");
  value.clearMark();
  require(value.observe().markTime == 0 && value.querySince(OverallSince::Uploaded)->value == 12, "clear mark zeros only mark fields");
  require(value.sessionUptime(std::numeric_limits<std::int64_t>::min()) == 0, "session elapsed wraps before truncation");
  const auto before = value.observe();
  require(!value.querySince(static_cast<OverallSince>(99)) && value.observe() == before, "invalid getter has no effect");
}
void codecFieldsAndShapes() {
  auto missing = readOverallDocument("de");
  require(missing && !missing->hasAll && missing->values == OverallValues{}, "missing all gives Java zero provenance");
  const auto input = dict({{"all", dict({{"downloaded", "i-9223372036854775808e"},
      {"uploaded", "i9223372036854775807e"}, {"uptime", bytes("17")}, {"mark_time", "le"}})}});
  auto fields = readOverallDocument(input);
  require(fields && fields->values[0] == std::numeric_limits<std::int64_t>::min()
      && fields->values[1] == std::numeric_limits<std::int64_t>::max(), "codec retains signed bounds");
  require(fields->presence[0] == OverallFieldPresence::Integer && fields->presence[2] == OverallFieldPresence::WrongType
      && fields->presence[3] == OverallFieldPresence::WrongType && fields->presence[4] == OverallFieldPresence::Missing
      && fields->values[2] == 0, "codec keeps malformed versus missing provenance");
  for (const auto* invalid : {"le", "deX", "d3:alli0ee", "d3:allde3:alldee"}) {
    require(!readOverallDocument(invalid), "strict structure and duplicate rejection");
  }
  auto wrongAll = readOverallDocument("d3:alli0ee");
  require(!wrongAll && wrongAll.error() == OverallDocumentError::InvalidAll, "wrong all shape distinguished");
}
void codecOpaqueOwnershipAndOrder() {
  const auto opaque = dict({{"blob", bytes(std::string("\0\xff", 2))}, {"list", "li-2e0:e"}});
  const std::string inputBytes = dict({{"tail", bytes(std::string("\xff\0x", 3))}, {"all", dict({{"plugin", opaque},
      {"uploaded", bytes("wrong")}, {"downloaded", "i7e"}})}, {"end", "i-3e"}});
  std::string input = inputBytes;
  auto original = readOverallDocument(input);
  require(original.has_value(), "owned opaque baseline parses");
  OverallValues replacement{};
  replacement[0] = -11; replacement[1] = 21;
  auto prepared = prepareOverallDocument(input, replacement);
  require(prepared && prepared->values == replacement, "complete13 replacement projection");
  const auto& before = *original->source;
  const auto& after = *prepared->source;
  require(keys(before.root()) == keys(after.root()), "root order retained");
  for (const auto* key : {"tail", "end"}) { require(raw(before, *before.find(key)) == raw(after, *after.find(key)), "root unknown bytes retained"); }
  require(raw(before, *before.find("all")->find("plugin")) == raw(after, *after.find("all")->find("plugin")), "all unknown subtree bytes retained");
  const auto order = keys(*after.find("all"));
  require(order[0] == "plugin" && order[1] == "uploaded" && order[2] == "downloaded" && order[3] == "uptime", "old key order then missing canonical append");
  const auto saved = after.originalBytes();
  input.assign(input.size(), 'x');
  require(before.find("all")->find("downloaded")->integer == 7 && after.originalBytes() == saved,
      "both immutable source owners outlive caller bytes and each other");
  auto repeated = prepareOverallDocument(saved, replacement);
  require(repeated && repeated->source->originalBytes() == saved, "preparing same absolutes is byte-idempotent");
}
void codecLimitsAndAtomicFailure() {
  auto input = dict({{"all", dict({{"downloaded", "i1e"}})}});
  Limits lower;
  lower.inputBytes = input.size();
  require(readOverallDocument(input, lower).has_value(), "exact input cap admitted");
  --lower.inputBytes;
  require(!readOverallDocument(input, lower), "lower input cap enforced");
  lower = {}; lower.stringBytes = 9;
  require(!readOverallDocument(input, lower), "lower key string cap enforced");
  lower = {}; lower.nodes = 4;
  require(!readOverallDocument(input, lower), "lower node cap enforced");
  lower = {}; lower.depth = 1;
  require(!readOverallDocument(input, lower), "lower depth cap enforced");
  lower = {}; lower.inputBytes = input.size();
  const auto original = input;
  require(!prepareOverallDocument(input, {}, lower) && input == original, "expanded save result cap fails without touching input");
  for (int field = 0; field < 4; ++field) {
    Limits raised;
    if (field == 0) { ++raised.inputBytes; }
    if (field == 1) { ++raised.stringBytes; }
    if (field == 2) { ++raised.nodes; }
    if (field == 3) { ++raised.depth; }
    auto failed = readOverallDocument("de", raised);
    require(!failed && failed.error() == OverallDocumentError::Limit, "raised ordinary limit rejected");
  }
  std::string huge(Limits{}.inputBytes + 1, 'x');
  require(!readOverallDocument(huge), "ordinary document byte maximum enforced before copy");
}
#include "fixtures/overall-java/replay.inc"
} // namespace

int main() {
  try {
    initialSnapshotAndBaselines(); partialAndClockOrdering(); transportFailureAndInvalidInput();
    markGetterAndOverflow(); codecFieldsAndShapes(); codecOpaqueOwnershipAndOrder(); codecLimitsAndAtomicFailure();
    javaLiteralCases();
    std::cout << "PASS overall-statistics: 7 native policy families; original Java reducer scenarios95 observations164 captured-save-maps91; assertions=" << assertions << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL overall-statistics: " << error.what() << '\n';
    return 1;
  }
}
