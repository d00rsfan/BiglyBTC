#include "biglybtc/classic_preferences.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Declared native private-schema tests. Generated memory only; no original Java,
// configuration files, recovery, UI, host, lease or durable-write claim.
namespace {
using namespace biglybtc::core;
using Error = ClassicPreferenceError;
using Pairs = std::vector<std::pair<std::string, std::string>>;
constexpr std::string_view Key = "BiglyBTC.ClassicColumns.v1";
std::size_t cases = 0, assertions = 0;
void check(bool value, const char* message) {
  ++assertions;
  if (!value) { throw std::runtime_error(message); }
}
template<class Function> void run(std::string_view name, Function&& function) {
  try { function(); ++cases; }
  catch (const std::exception& failure) { throw std::runtime_error(std::string(name) + ": " + failure.what()); }
}
std::string b(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
std::string i(std::int64_t value) { return 'i' + std::to_string(value) + 'e'; }
std::string d(const Pairs& pairs) {
  std::string result = "d";
  for (const auto& [key, value] : pairs) { result += b(key) + value; }
  return result + 'e';
}
std::string l(const std::vector<std::string>& values) {
  std::string result = "l";
  for (const auto& value : values) { result += value; }
  return result + 'e';
}
Document doc(std::string raw, Limits limits = {}) {
  auto value = Document::parse(std::move(raw), limits);
  check(value.has_value(), "Generated bencoding did not parse");
  return std::move(*value);
}
ClassicPreferences preferences() {
  ClassicPreferences result;
  for (std::size_t n = 0; n < 13; ++n) {
    result.incomplete.columns[n] = {n % 2 == 0, n ? std::optional<int>{100 + static_cast<int>(n)} : std::nullopt};
    result.complete.columns[n] = {n % 2 != 0, n ? std::optional<int>{300 + static_cast<int>(n)} : std::nullopt};
    result.incomplete.order[n] = ClassicPreferenceIds[12 - n];
    result.complete.order[n] = ClassicPreferenceIds[(n + 4) % 13];
  }
  result.incomplete.sortId = "saved-selected-done"; result.incomplete.ascending = false;
  result.complete.sortId = "state"; result.complete.ascending = true;
  return result;
}
// Independent fixture renderer: field lists are deliberately not writer order.
Pairs columnFields(std::size_t index) {
  Pairs fields{{"visible", i(index % 2 == 0 ? 1 : 0)}};
  if (index != 0) { fields.emplace_back("width", i(160 + static_cast<std::int64_t>(index))); }
  return fields;
}
Pairs paneFields() {
  Pairs columns;
  std::vector<std::string> order;
  for (std::size_t n = 0; n < 13; ++n) {
    columns.emplace_back(ClassicPreferenceIds[n], d(columnFields(n)));
    order.push_back(b(ClassicPreferenceIds[n]));
  }
  return {{"sort", d({{"ascending", i(1)}, {"id", b("name")}})},
          {"order", l(order)}, {"columns", d(columns)}};
}
void replace(Pairs& fields, std::string_view key, std::string value) {
  for (auto& [name, old] : fields) { if (name == key) { old = std::move(value); return; } }
  throw std::runtime_error("Bad generated mutation key");
}
std::string privateValue(std::string incomplete = d(paneFields()), std::string complete = d(paneFields())) {
  return d({{"complete", std::move(complete)}, {"version", i(1)}, {"incomplete", std::move(incomplete)}});
}
std::string root(std::string value) { return d({{std::string(Key), std::move(value)}}); }
void invalid(std::string raw, Error expected, Limits limits = {}) {
  auto input = doc(std::move(raw), limits);
  const auto before = input.serialize();
  const auto read = readClassicPreferences(input);
  check(!read && read.error() == expected, "Read accepted or misclassified invalid schema");
  const auto write = writeClassicPreferences(input, preferences());
  check(!write && write.error() == expected, "Write overwrote or misclassified invalid namespace");
  check(input.serialize() == before, "Rejected write changed input");
}
void invalidPane(Pairs fields, Error expected = Error::Invalid) { invalid(root(privateValue(d(fields))), expected); }
std::string nested(std::size_t levels) { return std::string(levels, 'l') + i(0) + std::string(levels, 'e'); }
std::string exactBytes(std::size_t size) {
  std::size_t payload = size;
  for (int n = 0; n < 8; ++n) {
    const auto overhead = d({{"opaque", b(std::string(payload, 'x'))}}).size() - payload;
    payload = size - overhead;
  }
  auto result = d({{"opaque", b(std::string(payload, 'x'))}});
  check(result.size() == size, "Fixture byte-bound arithmetic failed");
  return result;
}
std::vector<std::pair<std::string, std::string>> opaque(const Document& input) {
  std::vector<std::pair<std::string, std::string>> result;
  const auto& node = input.root();
  for (std::size_t n = 0; n < node.children.size(); n += 2) {
    if (node.children[n].bytes == Key) { continue; }
    const auto raw = [&](const Node& value) { return input.originalBytes().substr(value.begin, value.end - value.begin); };
    result.emplace_back(raw(node.children[n]), raw(node.children[n + 1]));
  }
  return result;
}
} // namespace

int main() {
  try {
    run("fixed13IDcatalog", [] {
      const std::array<std::string_view, 13> expected{"name", "state", "size", "progress", "download-rate", "upload-rate",
          "received", "sent", "hash", "saved-queue-position", "saved-selected-size", "saved-selected-remaining", "saved-selected-done"};
      check(ClassicPreferenceIds == expected, "Private stable-ID catalog changed");
    });
    run("missingIsNotDefaults", [] {
      const auto input = doc("de"); const auto value = readClassicPreferences(input);
      check(value && !*value, "Missing namespace fabricated preferences");
    });
    run("bothPanesAll13RoundtripOwned", [] {
      auto input = doc("de"); auto prefs = preferences(); const auto expected = prefs;
      check(validateClassicPreferences(prefs).has_value(), "Valid snapshot rejected");
      check(writeClassicPreferences(input, prefs).has_value(), "Valid write failed");
      prefs.incomplete.order[0] = "mutated"; prefs.complete.sortId.clear();
      auto output = doc(input.serialize());
      auto read = readClassicPreferences(output);
      check(read && read->has_value() && **read == expected, "Roundtrip aliases caller or loses fields");
      output = doc("de"); check(**read == expected, "Read result borrowed document bytes");
    });
    run("allHiddenAndWidthEndpoints", [] {
      auto prefs = preferences();
      for (auto* pane : {&prefs.incomplete, &prefs.complete}) {
        for (std::size_t n = 0; n < 13; ++n) { pane->columns[n].visible = false; if (n) { pane->columns[n].width = n % 2 ? 16 : 8192; } }
      }
      auto input = doc("de"); check(writeClassicPreferences(input, prefs).has_value(), "All hidden/endpoints rejected");
      auto output = doc(input.serialize()); const auto read = readClassicPreferences(output);
      check(read && read->has_value() && **read == prefs, "All-hidden snapshot changed");
    });
    run("repeatWriteNeedsPristineExceptIdenticalNoop", [] {
      auto input = doc("de"); const auto prefs = preferences();
      check(writeClassicPreferences(input, prefs).has_value(), "Write failed");
      check(!readClassicPreferences(input) && readClassicPreferences(input).error() == Error::EditedDocument, "Edited output read as pristine");
      const auto raw = input.serialize(); const auto again = writeClassicPreferences(input, prefs);
      check(!again && again.error() == Error::EditedDocument && input.serialize() == raw, "Repeated edit accepted");
      auto fresh = doc(raw); check(writeClassicPreferences(fresh, prefs).has_value(), "Identical replacement failed");
      check(fresh.serialize() == raw, "Identical replacement changed raw bytes");
    });
    run("strictDTOValidationAndRollback", [] {
      for (int mode = 0; mode < 7; ++mode) {
        auto prefs = preferences();
        if (mode == 0) { prefs.incomplete.columns[0].width = 100; }
        if (mode == 1) { prefs.complete.columns[1].width.reset(); }
        if (mode == 2) { prefs.incomplete.columns[12].width = 15; }
        if (mode == 3) { prefs.complete.columns[2].width = 8193; }
        if (mode == 4) { prefs.incomplete.order[0] = prefs.incomplete.order[1]; }
        if (mode == 5) { prefs.complete.order[0] = "foreign"; }
        if (mode == 6) { prefs.incomplete.sortId = std::string("name\0", 5); }
        const auto validation = validateClassicPreferences(prefs);
        check(!validation && validation.error() == Error::Invalid, "Bad DTO accepted");
        auto input = doc(d({{"original", l({i(7), b("untouched")})}})); const auto before = input.serialize();
        const auto write = writeClassicPreferences(input, prefs);
        check(!write && write.error() == Error::Invalid && input.serialize() == before, "Bad DTO changed input");
      }
    });
    run("unknownNamespaceAlwaysBlocksEvenWithV1", [] {
      for (const auto other : {"BiglyBTC.ClassicColumns", "BiglyBTC.ClassicColumns.v2", "BiglyBTC.ClassicColumns.future"}) {
        invalid(d({{std::string(Key), privateValue()}, {other, i(1)}}), Error::Unsupported);
        invalid(d({{other, i(1)}, {std::string(Key), privateValue()}}), Error::Unsupported);
      }
    });
    run("unrelatedSimilarRootNameIsOpaque", [] {
      auto input = doc(d({{"BiglyBTC.ClassicColumn", b("foreign")}, {"other.BiglyBTC.ClassicColumns", i(3)}}));
      const auto before = opaque(input); check(writeClassicPreferences(input, preferences()).has_value(), "Opaque root names blocked save");
      check(opaque(doc(input.serialize())) == before, "Unrelated root names changed");
    });
    run("wrongRootAndPrivateType", [] { invalid("le", Error::Invalid); invalid(root(i(1)), Error::Invalid); });
    run("privateVersionAndRequiredKeys", [] {
      invalid(root(d({{"version", i(2)}, {"incomplete", d(paneFields())}, {"complete", d(paneFields())}})), Error::Unsupported);
      invalid(root(d({{"version", b("1")}, {"incomplete", d(paneFields())}, {"complete", d(paneFields())}})), Error::Invalid);
      invalid(root(d({{"incomplete", d(paneFields())}, {"complete", d(paneFields())}})), Error::Invalid);
      invalid(root(d({{"version", i(1)}, {"complete", d(paneFields())}})), Error::Invalid);
      invalid(root(d({{"version", i(1)}, {"incomplete", d(paneFields())}})), Error::Invalid);
    });
    run("unknownPrivateFieldsAtEveryDictionary", [] {
      invalid(root(d({{"version", i(1)}, {"incomplete", d(paneFields())}, {"complete", d(paneFields())}, {"future", "de"}})), Error::Unsupported);
      auto fields = paneFields(); fields.emplace_back("future", i(1)); invalidPane(fields, Error::Unsupported);
      fields = paneFields(); Pairs columns;
      for (std::size_t n = 0; n < 13; ++n) { auto c = columnFields(n); if (n == 2) { c.emplace_back("future", i(1)); } columns.emplace_back(ClassicPreferenceIds[n], d(c)); }
      replace(fields, "columns", d(columns)); invalidPane(fields, Error::Unsupported);
      fields = paneFields(); replace(fields, "sort", d({{"id", b("name")}, {"ascending", i(1)}, {"future", i(1)}})); invalidPane(fields, Error::Unsupported);
    });
    run("missingOrWrongPaneFields", [] {
      for (const auto key : {"columns", "order", "sort"}) {
        auto fields = paneFields(); fields.erase(std::remove_if(fields.begin(), fields.end(), [&](const auto& p) { return p.first == key; }), fields.end()); invalidPane(fields);
        fields = paneFields(); replace(fields, key, i(1)); invalidPane(fields);
      }
    });
    run("columnSetMustBeExactAndTyped", [] {
      for (int mode = 0; mode < 4; ++mode) {
        Pairs columns;
        for (std::size_t n = 0; n < 13; ++n) { if (mode == 0 && n == 12) { continue; } columns.emplace_back(ClassicPreferenceIds[n], mode == 1 && n == 1 ? i(1) : d(columnFields(n))); }
        if (mode == 2) { columns.emplace_back("foreign", d({{"visible", i(1)}, {"width", i(100)}})); }
        if (mode == 3) { columns[2].first = "Size"; }
        auto fields = paneFields(); replace(fields, "columns", d(columns)); invalidPane(fields, mode >= 2 ? Error::Unsupported : Error::Invalid);
      }
    });
    run("strictBooleanAndWidthIntegers", [] {
      for (const auto& bad : {i(-1), i(2), b("1"), std::string{"le"}}) {
        Pairs columns;
        for (std::size_t n = 0; n < 13; ++n) { auto c = columnFields(n); if (n == 1) { replace(c, "visible", bad); } columns.emplace_back(ClassicPreferenceIds[n], d(c)); }
        auto fields = paneFields(); replace(fields, "columns", d(columns)); invalidPane(fields);
      }
      for (const auto& bad : {i(15), i(8193), i(std::numeric_limits<std::int64_t>::max()), i(std::numeric_limits<std::int64_t>::min()), b("100")}) {
        Pairs columns;
        for (std::size_t n = 0; n < 13; ++n) { auto c = columnFields(n); if (n == 1) { replace(c, "width", bad); } columns.emplace_back(ClassicPreferenceIds[n], d(c)); }
        auto fields = paneFields(); replace(fields, "columns", d(columns)); invalidPane(fields);
      }
    });
    run("nameWidthIsUnsupportedAndOtherWidthRequired", [] {
      for (const bool name : {false, true}) {
        Pairs columns;
        for (std::size_t n = 0; n < 13; ++n) { auto c = columnFields(n); if (name && n == 0) { c.emplace_back("width", i(100)); } if (!name && n == 1) { c.pop_back(); } columns.emplace_back(ClassicPreferenceIds[n], d(c)); }
        auto fields = paneFields(); replace(fields, "columns", d(columns)); invalidPane(fields, name ? Error::Unsupported : Error::Invalid);
      }
    });
    run("orderIs13UniqueKnownByteIds", [] {
      for (int mode = 0; mode < 5; ++mode) {
        std::vector<std::string> values; for (const auto key : ClassicPreferenceIds) { values.push_back(b(key)); }
        if (mode == 0) { values.pop_back(); } if (mode == 1) { values.push_back(b("name")); }
        if (mode == 2) { values[0] = values[1]; } if (mode == 3) { values[0] = b("foreign"); } if (mode == 4) { values[0] = i(0); }
        auto fields = paneFields(); replace(fields, "order", l(values)); invalidPane(fields);
      }
    });
    run("sortRequiresKnownIdAndLiteralBoolean", [] {
      for (const auto& bad : {d({{"id", b("foreign")}, {"ascending", i(1)}}), d({{"id", i(1)}, {"ascending", i(1)}}),
          d({{"id", b("name")}, {"ascending", i(2)}}), d({{"id", b("name")}, {"ascending", b("1")}}), d({{"id", b("name")}}), d({{"ascending", i(1)}})}) {
        auto fields = paneFields(); replace(fields, "sort", bad); invalidPane(fields);
      }
    });
    run("duplicateDictionaryKeysRemainParserRejection", [] { check(!Document::parse("d1:ai0e1:ai1ee"), "Duplicate dictionary admitted"); });
    run("privateByteCapExactAndExcess", [] {
      for (const std::size_t target : {std::size_t{32768}, std::size_t{32769}}) {
        std::size_t size = target - 20;
        for (int n = 0; n < 5; ++n) { size = target - (d({{"future", b(std::string(size, 'x'))}}).size() - size); }
        const auto value = d({{"future", b(std::string(size, 'x'))}}); check(value.size() == target, "Private byte fixture mis-sized");
        invalid(root(value), target == 32768 ? Error::Unsupported : Error::Limit);
      }
    });
    run("privateNodeAndDepthCaps", [] {
      invalid(root(d({{"future", l(std::vector<std::string>(1021, i(0)))}})), Error::Unsupported);
      invalid(root(d({{"future", l(std::vector<std::string>(1022, i(0)))}})), Error::Limit);
      invalid(root(d({{"future", nested(7)}})), Error::Unsupported);
      invalid(root(d({{"future", nested(8)}})), Error::Limit);
    });
    run("wholeBytesCapAndWriteOverflow", [] {
      auto input = doc(exactBytes(4 * 1024 * 1024)); const auto before = input.serialize();
      check(readClassicPreferences(input).has_value(), "Exact whole byte cap rejected");
      const auto write = writeClassicPreferences(input, preferences());
      check(!write && write.error() == Error::Limit && input.serialize() == before, "Whole output byte overflow changed input");
      Limits high; ++high.inputBytes; invalid(exactBytes(high.inputBytes), Error::Limit, high);
    });
    run("wholeNodeCapAndWriteOverflow", [] {
      auto input = doc(d({{"opaque", l(std::vector<std::string>(99997, i(0)))}})); const auto before = input.serialize();
      check(readClassicPreferences(input).has_value(), "Exact whole node cap rejected");
      const auto write = writeClassicPreferences(input, preferences());
      check(!write && write.error() == Error::Limit && input.serialize() == before, "Whole output node overflow changed input");
      Limits high; ++high.nodes; invalid(d({{"opaque", l(std::vector<std::string>(99998, i(0)))}}), Error::Limit, high);
    });
    run("wholeDepthCap", [] {
      auto input = doc(d({{"opaque", nested(63)}})); check(readClassicPreferences(input).has_value(), "Exact whole depth rejected");
      Limits high; ++high.depth; invalid(d({{"opaque", nested(64)}}), Error::Limit, high);
    });
    run("inheritedLowerEditQuotasRollback", [] {
      for (int mode = 0; mode < 4; ++mode) {
        Limits limits; if (mode == 0) { limits.inputBytes = 2; } if (mode == 1) { limits.nodes = 1; }
        if (mode == 2) { limits.depth = 0; } if (mode == 3) { limits.stringBytes = 3; }
        auto input = doc("de", limits); const auto write = writeClassicPreferences(input, preferences());
        check(!write && write.error() == Error::Limit && input.serialize() == "de", "Lower inherited edit quota bypassed");
      }
    });
    run("editedDocumentRejected", [] {
      auto input = doc(d({{"original", i(1)}})); check(input.setInteger("original", 2), "Fixture edit failed");
      const auto before = input.serialize(); const auto read = readClassicPreferences(input); const auto write = writeClassicPreferences(input, preferences());
      check(!read && read.error() == Error::EditedDocument && !write && write.error() == Error::EditedDocument, "Edited document accepted");
      check(input.serialize() == before, "Edited input changed on failure");
    });
    run("hostileViewsAndShapeNeverDereferenced", [] {
      for (int mode = 0; mode < 8; ++mode) {
        auto input = doc(d({{"original", b("abc")}})); auto& rootNode = const_cast<Node&>(input.root());
        auto& value = rootNode.children[1];
        if (mode == 0) { value.bytes = std::string_view(reinterpret_cast<const char*>(std::uintptr_t{1}), 3); }
        if (mode == 1) { value.bytes = "abc"; }
        if (mode == 2) { ++value.begin; } if (mode == 3) { --value.end; }
        if (mode == 4) { value.integer = 9; } if (mode == 5) { rootNode.children.pop_back(); }
        if (mode == 6) { rootNode.children[0].type = Node::Type::Integer; }
        if (mode == 7) { value.bytes = std::string_view(reinterpret_cast<const char*>(std::uintptr_t{1}), 4 * 1024 * 1024 + 1); }
        const auto expected = mode == 7 ? Error::Limit : Error::EditedDocument;
        const auto read = readClassicPreferences(input); const auto write = writeClassicPreferences(input, preferences());
        check(!read && read.error() == expected && !write && write.error() == expected, "Hostile public tree accepted");
      }
    });
    run("originalSubtreesAndRootOrderByteExact", [] {
      const std::string binary("\0\xff\r\n", 4);
      const Pairs original{{"z-last", b(binary)}, {"Table.MyTorrents", d({{"Column.plugin", l({i(-2), b(binary), "de"})}, {"SortColumns", l({b("plugin"), b("name")})}})},
          {"legacy", l({"de", b(binary), i(-999)})}, {std::string("key\0", 4), d({{"z", i(3)}, {"a", b(binary)}})}};
      for (const bool replaceExisting : {false, true}) {
        auto fields = original; if (replaceExisting) { fields.insert(fields.begin() + 1, {std::string(Key), privateValue()}); }
        auto input = doc(d(fields)); const auto before = opaque(input); const auto originalRaw = input.originalBytes();
        std::size_t begin = originalRaw.size() - 1, end = begin;
        if (const auto* value = input.find(Key)) { begin = value->begin; end = value->end; }
        check(writeClassicPreferences(input, preferences()).has_value(), "Opaque preserving write failed");
        auto output = doc(input.serialize()); check(opaque(output) == before, "Original opaque bytes/order changed");
        const auto* changed = output.find(Key); check(changed != nullptr, "Private value absent");
        const auto encoded = output.originalBytes().substr(changed->begin, changed->end - changed->begin);
        const auto replacement = (replaceExisting ? std::string{} : b(Key)) + encoded;
        check(output.originalBytes() == originalRaw.substr(0, begin) + replacement + originalRaw.substr(end), "Write changed bytes outside private value insertion/replacement");
      }
    });
    std::cout << "PASS " << cases << " classic preferences cases; " << assertions << " assertions\n";
    return 0;
  } catch (const std::exception& failure) { std::cerr << failure.what() << '\n'; return 1; }
}
