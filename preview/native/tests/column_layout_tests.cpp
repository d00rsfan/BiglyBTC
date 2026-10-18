#include "biglybtc/column_layout.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Generated in-memory bencoding exercises requested facts only. These tests do
// not initialize Java, apply a layout, resolve registered columns, or read files.
// Authority: P04-10 frozen API 1d4d1c06..., design b556cf73..., plan a1325371... .
namespace {
using namespace biglybtc::core;
using Presence = ColumnLayoutPresence;
using Origin = ColumnLayoutOrigin;
using Sort = ColumnLayoutSortSource;
using Direction = ColumnLayoutDirection;
using Issue = ColumnLayoutIssueCode;
using Field = ColumnLayoutField;
using Table = ColumnLayoutTableId;
using Error = ColumnLayoutError;
using Pairs = std::vector<std::pair<std::string, std::string>>;

std::size_t cases = 0;
std::size_t assertions = 0;
std::string activeCase;

void check(bool condition, std::string_view description) {
  ++assertions;
  if (!condition) { throw std::runtime_error(std::string(description)); }
}

template<typename Function>
void run(std::string name, Function&& function) {
  activeCase = std::move(name);
  ++cases;
  function();
}

std::string b(std::string_view value) {
  return std::to_string(value.size()) + ':' + std::string(value);
}
std::string i(std::int64_t value) { return "i" + std::to_string(value) + 'e'; }
std::string l(const std::vector<std::string>& values) {
  std::string encoded = "l";
  for (const auto& value : values) { encoded += value; }
  return encoded + 'e';
}
std::string d(const Pairs& values) {
  std::string encoded = "d";
  for (const auto& [key, value] : values) { encoded += b(key) + value; }
  return encoded + 'e';
}
std::string tables(std::string incomplete, std::string complete = "de") {
  return d({{"Table.MyTorrents", std::move(incomplete)},
            {"Table.MySeeders", std::move(complete)}});
}
std::vector<std::string> slots() {
  return {i(1), i(7), i(321), i(0), i(1),
          d({{"nameoverride", b(std::string("custom\0name", 11))}}), i(-1),
          l({i(1), i(2), i(3)}), l({i(4), i(5), i(6)}), i(2)};
}
std::string column(std::vector<std::string> values = slots(), std::string id = "name") {
  return d({{"Column." + id, l(values)}});
}
Document parsed(std::string raw, Limits limits = {}) {
  auto value = Document::parse(std::move(raw), limits);
  check(value.has_value(), "Generated fixture must parse using the real Document parser");
  return std::move(*value);
}
ColumnLayouts read(const std::string& raw, ColumnLayoutLimits limits = {}, Limits parserLimits = {}) {
  auto source = parsed(raw, parserLimits);
  auto result = readColumnLayouts(source, limits);
  check(result.has_value(), "Requested-fact projection succeeds");
  check(source.originalBytes() == raw && source.serialize() == raw,
        "Successful projection leaves caller bytes and tree unchanged");
  check(static_cast<bool>(result->source), "Successful snapshot has an owned document");
  check(result->source.get() != &source, "Snapshot does not alias caller Document");
  check(result->source->originalBytes() == raw && result->source->serialize() == raw,
        "Snapshot preserves every raw opaque byte and dictionary order");
  check(result->source->originalBytes().data() != source.originalBytes().data(),
        "Snapshot raw backing storage is independent of caller storage");
  return std::move(*result);
}
void fails(const Document& source, Error error, ColumnLayoutLimits limits = {}) {
  auto result = readColumnLayouts(source, limits);
  check(!result.has_value(), "Invalid or over-budget input has no successful partial snapshot");
  check(result.error() == error, "Failure carries the specified fixed error");
}
void failsRaw(std::string raw, Error error, ColumnLayoutLimits limits = {}, Limits parserLimits = {}) {
  auto source = parsed(std::move(raw), parserLimits);
  fails(source, error, limits);
}
void number(const ColumnLayoutInteger& value, std::int64_t raw, std::int32_t low) {
  check(value.presence == Presence::Present && value.value.has_value(), "Numeric field is present");
  check(value.value->raw == raw && value.value->value == low, "Raw64 and signed-low32 facts remain distinct");
}
template<typename T>
void absent(const ColumnLayoutValue<T>& value, Presence presence) {
  check(value.presence == presence && !value.value, "Unknown field keeps presence without fabricated value");
}
void tableFacts(const SavedTableLayout& value, Presence exact, Origin origin, Presence selected) {
  check(value.exactPresence == exact && value.origin == origin && value.presence == selected,
        "Exact and selected table provenance is explicit");
}
const SavedColumnLayout& onlyColumn(const SavedTableLayout& table) {
  check(table.hasColumnSettings && table.columns.size() == 1, "Exactly one source column is retained");
  return table.columns.front();
}
bool issue(const ColumnLayouts& value, Issue code, Field field, Table table = Table::Incomplete,
           std::size_t sourceIndex = ColumnLayoutNoIndex,
           std::size_t elementIndex = ColumnLayoutNoIndex) {
  return std::any_of(value.issues.begin(), value.issues.end(), [&](const auto& item) {
    return item.code == code && item.field == field && item.table == table
        && item.sourceIndex == sourceIndex && item.elementIndex == elementIndex;
  });
}
std::array<const ColumnLayoutInteger*, 7> numbers(const SavedColumnLayout& value) {
  return {&value.visibility, &value.position, &value.width, &value.autoTooltip,
          &value.sortOrder, &value.alignment, &value.iconEnabled};
}
void text(const ColumnLayoutText& value, std::string_view raw, bool utf8 = true, bool nul = false) {
  check(value.bytes == raw && value.validUtf8 == utf8 && value.embeddedNul == nul,
        "Owned ID bytes and encoding/NUL facts are preserved without normalization");
}
void sortText(const ColumnLayoutSortEntry& entry, std::size_t index, std::string_view raw,
              bool utf8 = true, bool nul = false) {
  check(entry.sourceIndex == index, "Sort entries retain original slot indices");
  check(entry.id.presence == Presence::Present && entry.id.value.has_value(),
        "Every byte-string sort ID stays present, including malformed text");
  text(*entry.id.value, raw, utf8, nul);
}
std::string columns(std::size_t count, bool wrong = false, bool invalidIds = false, bool firstNul = false) {
  Pairs values;
  values.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    std::string id = invalidIds ? std::string(1, '\xff') : "c";
    if (firstNul && index == 0) { id.push_back('\0'); }
    id += std::to_string(index);
    values.emplace_back("Column." + id, wrong ? i(0) : "le");
  }
  return d(values);
}
std::string sortList(std::size_t count) {
  std::vector<std::string> values;
  for (std::size_t index = 0; index < count; ++index) {
    values.push_back(index % 2 == 0 ? b("duplicate") : i(0));
  }
  return l(values);
}
Limits generousParser() {
  Limits limits;
  limits.inputBytes = 8 * 1024 * 1024;
  limits.stringBytes = 8 * 1024 * 1024;
  limits.nodes = 200000;
  limits.depth = 128;
  return limits;
}
std::string exactBytes(std::size_t size) {
  std::size_t payload = size;
  while (payload + std::to_string(payload).size() + 6 != size) {
    payload = size - std::to_string(payload).size() - 6;
  }
  auto raw = d({{"x", b(std::string(payload, 'x'))}});
  check(raw.size() == size, "Fixture has the declared complete encoded byte length");
  return raw;
}
std::string exactNodes(std::size_t count) {
  check(count >= 3, "Node-count fixture includes root, key and list");
  std::string values = "l";
  for (std::size_t index = 0; index < count - 3; ++index) { values += "i0e"; }
  return d({{"opaque", values + 'e'}});
}
std::string exactDepth(std::size_t depth) {
  check(depth >= 1, "Depth fixture has root dictionary at depth zero");
  std::string value = i(0);
  for (std::size_t index = 1; index < depth; ++index) { value = 'l' + value + 'e'; }
  return d({{"opaque", std::move(value)}});
}

void tableCases() {
  run("TABLE/neither-exact-nor-default", [] {
    const auto value = read("de");
    tableFacts(value.incomplete, Presence::Missing, Origin::Absent, Presence::Missing);
    tableFacts(value.complete, Presence::Missing, Origin::Absent, Presence::Missing);
    check(value.issues.empty() && value.incomplete.columns.empty() && value.complete.sort.empty(),
          "Absent tables have no invented columns or diagnostics");
  });
  run("TABLE/both-exact-empty", [] {
    const auto value = read(tables("de"));
    for (const auto* table : {&value.incomplete, &value.complete}) {
      tableFacts(*table, Presence::Present, Origin::ExactTable, Presence::Present);
      check(!table->hasColumnSettings && table->columns.empty(), "Empty map differs from missing table");
    }
    check(value.issues.empty(), "Empty exact tables are valid");
  });
  run("TABLE/both-exact-populated-different", [] {
    auto other = slots(); other[1] = i(-2); other[2] = i(999);
    const auto value = read(tables(column(), column(other, "plugin.other")));
    text(onlyColumn(value.incomplete).id, "name");
    text(onlyColumn(value.complete).id, "plugin.other");
    number(value.incomplete.columns[0].position, 7, 7);
    number(value.complete.columns[0].position, -2, -2);
    number(value.complete.columns[0].width, 999, 999);
    check(value.issues.empty(), "Both pane identities project independently");
  });
  run("TABLE/exact-populated-over-conflicting-default", [] {
    const auto value = read(d({{"TableDefault.MyTorrents", column({}, "default")},
                               {"Table.MyTorrents", column()}}));
    tableFacts(value.incomplete, Presence::Present, Origin::ExactTable, Presence::Present);
    text(onlyColumn(value.incomplete).id, "name");
  });
  run("TABLE/exact-empty-over-populated-default", [] {
    const auto value = read(d({{"Table.MyTorrents", "de"}, {"TableDefault.MyTorrents", column()}}));
    tableFacts(value.incomplete, Presence::Present, Origin::ExactTable, Presence::Present);
    check(!value.incomplete.hasColumnSettings && value.incomplete.columns.empty(), "Empty exact map blocks default");
  });
  run("TABLE/missing-exact-selects-default", [] {
    const auto value = read(d({{"TableDefault.MyTorrents", column({}, "fallback")},
                               {"TableDefault.MySeeders", "de"}}));
    tableFacts(value.incomplete, Presence::Missing, Origin::UserDefault, Presence::Present);
    tableFacts(value.complete, Presence::Missing, Origin::UserDefault, Presence::Present);
    text(onlyColumn(value.incomplete).id, "fallback");
  });
  run("TABLE/wrong-exact-blocks-good-default", [] {
    const auto value = read(d({{"Table.MyTorrents", i(1)}, {"TableDefault.MyTorrents", column()}}));
    tableFacts(value.incomplete, Presence::WrongType, Origin::ExactTable, Presence::WrongType);
    check(!value.incomplete.hasColumnSettings && value.incomplete.columns.empty(), "Wrong exact type never falls back");
    check(issue(value, Issue::Type, Field::Table), "Wrong selected table is diagnosed");
  });
  run("TABLE/missing-exact-wrong-default", [] {
    const auto value = read(d({{"TableDefault.MyTorrents", b("wrong")}}));
    tableFacts(value.incomplete, Presence::Missing, Origin::UserDefault, Presence::WrongType);
    check(issue(value, Issue::Type, Field::Table), "Wrong selected default is diagnosed");
  });
  run("NAMESPACES/scoped-modern-and-unused-defaults-opaque", [] {
    std::vector<std::string> opaqueSort(33, b("opaque"));
    opaqueSort[0] = b(std::string(300, 'x'));
    Pairs opaquePairs{{"SortColumns", l(opaqueSort)},
                      {"Column." + std::string(300, 'y'), "le"}};
    for (std::size_t index = 0; index < 1025; ++index) {
      opaquePairs.emplace_back("Column.unselected-" + std::to_string(index), "le");
    }
    const auto opaque = d(opaquePairs);
    ColumnLayoutLimits limits;
    limits.columnsPerTable = 1; limits.sortIdsPerTable = 1; limits.idBytes = 1;
    const auto value = read(d({{"Table.MyTorrents", "de"},
                               {"TableDefault.MyTorrents", opaque},
                               {"Table.MyTorrents::Tag_7", opaque},
                               {"Table.MySeeders::TagGroup_x", opaque},
                               {"Table.MyTorrents.big", opaque},
                               {"Table.MyTorrents.small", opaque}}), limits);
    check(value.issues.empty() && value.incomplete.columns.empty(), "Unselected namespaces consume no selected quotas");
    tableFacts(value.complete, Presence::Missing, Origin::Absent, Presence::Missing);
  });
}

void slotCases() {
  run("SLOT/complete10-slots-distinct-position-width", [] {
    const auto value = read(tables(d({{"note", b("opaque")}, {"Column.name", l(slots())},
                                      {"gap", i(8)}, {"Column.plugin", "le"}})));
    check(value.incomplete.columns.size() == 2, "Source column order ignores unrelated pairs without renumbering");
    const auto& col = value.incomplete.columns[0];
    check(col.sourceIndex == 1 && value.incomplete.columns[1].sourceIndex == 3, "Column sourceIndex is dictionary pair index");
    check(col.presence == Presence::Present && col.listLength == 10, "Full list shape retained");
    number(col.visibility, 1, 1); number(col.position, 7, 7); number(col.width, 321, 321);
    number(col.autoTooltip, 0, 0); number(col.sortOrder, 1, 1); number(col.alignment, -1, -1);
    number(col.iconEnabled, 2, 2);
    check(col.visible == true && col.automaticTooltip == false && col.iconReferenceEnabled == true,
          "Decoded requested booleans retain known true and known false");
    check(col.direction == Direction::Ascending && col.userData == Presence::Present, "Direction and userData map facts");
    check(col.foreground.presence == Presence::Present && col.background.presence == Presence::Present,
          "Both RGB triplets are present");
    for (std::size_t index = 0; index < 3; ++index) {
      check((*col.foreground.value)[index].raw == static_cast<std::int64_t>(index + 1)
            && (*col.background.value)[index].value == static_cast<std::int32_t>(index + 4), "RGB retains member order");
    }
    check(!col.legacyPositionRequired && !col.legacyWidthRequired && !col.legacyTooltipRequired, "No legacy lookup for numeric slots");
    check(value.issues.empty(), "A complete valid list has no issues");
  });
  const std::array<std::size_t, 7> numericSlots{0, 1, 2, 3, 4, 6, 9};
  for (std::size_t length = 0; length <= 10; ++length) {
    run("SHORT/length-" + std::to_string(length), [length, numericSlots] {
      auto values = slots(); values.resize(length);
      const auto value = read(tables(column(values)));
      const auto& col = onlyColumn(value.incomplete);
      check(col.presence == Presence::Present && col.listLength == length, "Short list remains a present list");
      const auto fields = numbers(col);
      for (std::size_t index = 0; index < fields.size(); ++index) {
        const bool present = numericSlots[index] < length;
        check(fields[index]->presence == (present ? Presence::Present : Presence::Missing), "Only supplied numeric slots are known");
        check(fields[index]->value.has_value() == present, "No missing numeric default fabricated");
      }
      check(col.userData == (length > 5 ? Presence::Present : Presence::Missing), "Missing userData distinct from present map");
      check(col.foreground.presence == (length > 7 ? Presence::Present : Presence::Missing), "Foreground short-list boundary");
      check(col.background.presence == (length > 8 ? Presence::Present : Presence::Missing), "Background short-list boundary");
      check(col.legacyPositionRequired == (length <= 1) && col.legacyWidthRequired == (length <= 2)
            && col.legacyTooltipRequired == (length <= 3), "Legacy-needed flags follow supplied slot boundaries");
      check(value.issues.empty(), "Missing short-list fields do not generate one issue per field");
    });
  }
  const std::array<std::pair<const char*, std::string>, 3> wrongRecords{{
      {"integer-value", i(1)}, {"bytes-value", b("bad")}, {"dictionary-value", "de"}}};
  for (const auto& [name, encoded] : wrongRecords) {
    run(std::string("RECORD/") + name, [&encoded] {
      const auto value = read(tables(d({{"Column.bad", encoded}, {"Column.good", l(slots())}})));
      check(value.incomplete.hasColumnSettings && value.incomplete.columns.size() == 2, "Malformed record and later valid sibling both retained");
      const auto& col = value.incomplete.columns.front();
      check(col.presence == Presence::WrongType && col.listLength == 0, "Non-list record has explicit wrong type");
      for (const auto* field : numbers(col)) { absent(*field, Presence::Missing); }
      check(col.legacyPositionRequired && col.legacyWidthRequired && col.legacyTooltipRequired, "Malformed record requires all three unexecuted legacy lookups");
      check(!col.visible && !col.direction && col.userData == Presence::Missing, "Malformed record has no invented decoded facts");
      check(issue(value, Issue::Type, Field::Column, Table::Incomplete, 0), "Record type issue carries source pair index");
      number(value.incomplete.columns[1].width, 321, 321);
    });
  }
  struct LowCase { const char* name; std::int64_t raw; std::int32_t low; };
  const std::array<LowCase, 9> lows{{
      {"zero", 0, 0}, {"one", 1, 1}, {"minus-one", -1, -1}, {"two", 2, 2},
      {"int32-min-positive64", 2147483648LL, std::numeric_limits<std::int32_t>::min()},
      {"two-to32", 4294967296LL, 0}, {"two-to32-plus1", 4294967297LL, 1},
      {"int64-max", std::numeric_limits<std::int64_t>::max(), -1},
      {"int64-min", std::numeric_limits<std::int64_t>::min(), 0}}};
  for (const auto item : lows) {
    run(std::string("LOW32/") + item.name, [item, numericSlots] {
      auto values = slots();
      for (const auto slot : numericSlots) { values[slot] = i(item.raw); }
      const auto value = read(tables(column(values)));
      const auto& col = onlyColumn(value.incomplete);
      for (const auto* field : numbers(col)) { number(*field, item.raw, item.low); }
      check(col.visible.has_value() && *col.visible == (item.low == 1), "Visibility requires narrowed exactly one");
      check(col.automaticTooltip.has_value() && *col.automaticTooltip == (item.low == 1), "Tooltip requires narrowed exactly one");
      check(col.iconReferenceEnabled.has_value() && *col.iconReferenceEnabled == (item.low != 0), "Icon requires narrowed nonzero");
      if (item.low < 0) { check(!col.direction, "Negative narrowed sort order has no initialized direction claim"); }
      else { check(col.direction == (item.low == 1 ? Direction::Ascending : Direction::Descending), "Nonnegative requested direction follows exact source branch"); }
      check(!col.legacyPositionRequired && !col.legacyWidthRequired && !col.legacyTooltipRequired, "All present numbers avoid legacy lookup even with narrowed extremes");
    });
  }
  const std::array<Field, 7> fieldIds{Field::Visibility, Field::Position, Field::Width,
      Field::AutoTooltip, Field::SortOrder, Field::Alignment, Field::IconEnabled};
  for (std::size_t index = 0; index < numericSlots.size(); ++index) {
    run("WRONG_NUMBER/slot-" + std::to_string(numericSlots[index]), [index, numericSlots, fieldIds] {
      auto values = slots(); values[numericSlots[index]] = b("1");
      const auto value = read(tables(column(values)));
      const auto& col = onlyColumn(value.incomplete);
      const auto fields = numbers(col);
      absent(*fields[index], Presence::WrongType);
      for (std::size_t other = 0; other < fields.size(); ++other) {
        if (other != index) { check(fields[other]->presence == Presence::Present, "Malformed number does not hide valid sibling fields"); }
      }
      check(col.legacyPositionRequired == (index == 1) && col.legacyWidthRequired == (index == 2)
            && col.legacyTooltipRequired == (index == 3), "Only malformed fallback slot requires legacy");
      check(issue(value, Issue::Type, fieldIds[index], Table::Incomplete, 0), "Numeric type issue is field/source indexed");
      if (index == 0) { check(!col.visible, "Wrong visibility is unknown, not false"); }
      if (index == 3) { check(!col.automaticTooltip, "Wrong tooltip is unknown, not false"); }
      if (index == 4) { check(!col.direction, "Wrong sort direction is unknown"); }
      if (index == 6) { check(!col.iconReferenceEnabled, "Wrong icon is unknown, not false"); }
    });
  }
  run("LEGACY_FLAGS/negative-numeric-width-not-legacy", [] {
    auto values = slots(); values[1] = i(-2); values[2] = i(-1); values[3] = i(-1);
    const auto value = read(tables(column(values)));
    const auto& col = onlyColumn(value.incomplete);
    number(col.position, -2, -2); number(col.width, -1, -1);
    check(!col.legacyPositionRequired && !col.legacyWidthRequired && !col.legacyTooltipRequired,
          "Negative stored numbers are not missing fallback requests");
    check(col.automaticTooltip == false, "Numeric negative tooltip is known false");
  });
  run("LEGACY_FLAGS/opaque-global-lookalikes-never-resolved", [] {
    const auto value = read(d({{"Table.MyTorrents", column({})},
                               {"Table.MyTorrents.name.position", i(5)},
                               {"Table.MyTorrents.name.width", i(77)},
                               {"Table.MyTorrents.name.auto_tooltip", i(1)},
                               {"MyTorrents.sortColumn", b("name")}, {"MyTorrents.sortAsc", i(1)}}));
    const auto& col = onlyColumn(value.incomplete);
    absent(col.position, Presence::Missing); absent(col.width, Presence::Missing);
    check(col.legacyPositionRequired && col.legacyWidthRequired && col.legacyTooltipRequired, "Lookalike opaque globals never satisfy missing slots");
    check(value.incomplete.sortSource == Sort::LegacyGlobalRequired && value.incomplete.sort.empty(), "No global key lookup or migration runs");
  });
}

void rgbAndOpaqueCases() {
  run("RGB/valid-three", [] {
    const auto value = read(tables(column()));
    const auto& col = onlyColumn(value.incomplete);
    check(col.foreground.value && col.background.value && value.issues.empty(), "Two well-formed triplets are valid");
    check((*col.foreground.value)[0].raw == 1 && (*col.foreground.value)[2].value == 3
          && (*col.background.value)[0].value == 4 && (*col.background.value)[2].raw == 6, "RGB membership and order remain exact");
  });
  run("RGB/wrong-outer-type", [] {
    auto values = slots(); values[7] = i(7);
    const auto value = read(tables(column(values)));
    const auto& col = onlyColumn(value.incomplete);
    absent(col.foreground, Presence::WrongType);
    check(issue(value, Issue::Type, Field::Foreground, Table::Incomplete, 0), "Non-list RGB diagnosed as type");
    check(col.background.value && col.iconReferenceEnabled == true, "Other fields remain available");
  });
  const std::array<std::pair<const char*, std::size_t>, 3> lengths{{{"zero-items", 0}, {"two-items", 2}, {"four-items", 4}}};
  for (const auto& [name, length] : lengths) {
    run(std::string("RGB/") + name, [length] {
      auto values = slots(); values[7] = l(std::vector<std::string>(length, i(0)));
      const auto value = read(tables(column(values)));
      const auto& col = onlyColumn(value.incomplete);
      absent(col.foreground, Presence::Malformed);
      check(issue(value, Issue::Shape, Field::Foreground, Table::Incomplete, 0), "Wrong triplet length is a shape issue without invented member");
      check(col.background.value && col.width.value, "Malformed RGB does not hide independent requested facts");
    });
  }
  run("RGB/bad-foreground-member-later-bg-icon-valid", [] {
    auto values = slots(); values[7] = l({i(1), b("2"), i(3)});
    const auto value = read(tables(column(values)));
    const auto& col = onlyColumn(value.incomplete);
    absent(col.foreground, Presence::Malformed);
    check(issue(value, Issue::Type, Field::Foreground, Table::Incomplete, 0, 1), "Bad foreground member has actual member index");
    check(col.background.value && col.iconReferenceEnabled == true, "Later valid fields survive bad foreground");
  });
  run("RGB/bad-background-member-earlier-fields-valid", [] {
    auto values = slots(); values[8] = l({b("4"), i(5), b("6")});
    const auto value = read(tables(column(values)));
    const auto& col = onlyColumn(value.incomplete);
    absent(col.background, Presence::Malformed);
    check(issue(value, Issue::Type, Field::Background, Table::Incomplete, 0, 0)
          && issue(value, Issue::Type, Field::Background, Table::Incomplete, 0, 2), "Every malformed RGB member is independently indexed");
    check(col.foreground.value && col.position.value && col.iconReferenceEnabled == true, "Earlier and later fields remain independent");
  });
  run("RGB/raw-outofrange-high32-members", [] {
    auto values = slots(); values[7] = l({i(-1), i(256), i(4294967297LL)});
    const auto value = read(tables(column(values)));
    const auto& rgb = onlyColumn(value.incomplete).foreground;
    check(rgb.presence == Presence::Present && rgb.value.has_value(), "Integer triplet is retained without applied-color validation");
    check((*rgb.value)[0].raw == -1 && (*rgb.value)[0].value == -1
          && (*rgb.value)[1].value == 256 && (*rgb.value)[2].raw == 4294967297LL
          && (*rgb.value)[2].value == 1, "Out-of-range/raw-high32 RGB not clamped");
    check(value.issues.empty(), "Requested RGB facts do not invent SWT color success or range error");
  });
  run("OPAQUE/userdata-map-and-nameoverride", [] {
    const auto raw = tables(column());
    const auto value = read(raw);
    check(onlyColumn(value.incomplete).userData == Presence::Present, "UserData map presence is available");
    text(value.incomplete.columns[0].id, "name");
    check(value.source->originalBytes().find(std::string("custom\0name", 11)) != std::string::npos,
          "Name override remains only raw userdata and never changes ID");
  });
  run("OPAQUE/wrong-userdata-type", [] {
    auto values = slots(); values[5] = l({b("not a map")});
    const auto value = read(tables(column(values)));
    const auto& col = onlyColumn(value.incomplete);
    check(col.userData == Presence::WrongType && col.foreground.value && col.direction == Direction::Ascending,
          "Wrong userdata does not poison unrelated fields");
    check(issue(value, Issue::Type, Field::UserData, Table::Incomplete, 0), "UserData wrong type diagnosed without interpretation");
  });
  run("OPAQUE/unknown-plugin-column-and-extra-list-tail", [] {
    auto values = slots();
    const auto opaque = d({{"future", l({b(std::string("\0\xfftail", 6)), d({{"x", i(-9)}})})}});
    values.push_back(opaque); values.push_back(i(999));
    const auto raw = d({{"plugin.root", opaque}, {"Table.MyTorrents", column(values, "plugin.original-ID")}});
    const auto value = read(raw);
    const auto& col = onlyColumn(value.incomplete);
    text(col.id, "plugin.original-ID");
    check(col.listLength == 12 && col.userData == Presence::Present && value.issues.empty(), "Unknown tails and plugins remain opaque");
    check(value.source->originalBytes() == raw, "Opaque tree bytes retained once in owned document");
  });
}

void idAndOrderCases() {
  struct IdCase { const char* name; std::string bytes; bool utf8; bool nul; };
  const std::array<IdCase, 4> ids{{
      {"empty-suffix", "", true, false}, {"invalid-UTF8", std::string(1, '\xff'), false, false},
      {"embedded-NUL", std::string("a\0b", 3), true, true},
      {"both-NUL-invalid-UTF8", std::string("\0\xff", 2), false, true}}};
  for (const auto& item : ids) {
    run(std::string("IDS/") + item.name, [&item] {
      const auto value = read(tables(d({{"Column." + item.bytes, "le"},
                                        {"SortColumns", l({b(item.bytes)})}})));
      const auto& col = onlyColumn(value.incomplete);
      text(col.id, item.bytes, item.utf8, item.nul);
      check(value.incomplete.sort.size() == 1, "Malformed byte ID is not dropped from sort list");
      sortText(value.incomplete.sort[0], 0, item.bytes, item.utf8, item.nul);
      if (item.bytes.empty()) {
        check(issue(value, Issue::EmptyId, Field::Id, Table::Incomplete, 0)
              && issue(value, Issue::EmptyId, Field::SortId, Table::Incomplete, ColumnLayoutNoIndex, 0), "Empty IDs retained and diagnosed at source locations");
      }
      if (!item.utf8) {
        check(issue(value, Issue::Encoding, Field::Id, Table::Incomplete, 0)
              && issue(value, Issue::Encoding, Field::SortId, Table::Incomplete, ColumnLayoutNoIndex, 0), "Current explicit UTF8 IDs retain encoding issues");
      }
      if (item.nul) {
        check(issue(value, Issue::Nul, Field::Id, Table::Incomplete, 0)
              && issue(value, Issue::Nul, Field::SortId, Table::Incomplete, ColumnLayoutNoIndex, 0), "NUL remains factual, not C-string truncation");
      }
    });
  }
  run("ORDER/duplicate-positions", [] {
    auto values = slots(); values[1] = i(7);
    const auto value = read(tables(d({{"Column.second", l(values)}, {"Column.first", l(values)}})));
    check(value.incomplete.columns.size() == 2, "Duplicate positions keep both source records");
    text(value.incomplete.columns[0].id, "second"); text(value.incomplete.columns[1].id, "first");
    number(value.incomplete.columns[0].position, 7, 7); number(value.incomplete.columns[1].position, 7, 7);
    check(value.issues.empty(), "No registry-integrity position repair or inferred duplicate error");
  });
  run("ORDER/negative-invisible-last-extremes", [] {
    auto hidden = slots(); hidden[0] = i(0); hidden[1] = i(-1);
    auto last = slots(); last[1] = i(-2); last[2] = i(std::numeric_limits<std::int64_t>::max());
    const auto value = read(tables(d({{"Column.hidden", l(hidden)}, {"Column.last", l(last)}})));
    check(value.incomplete.columns.size() == 2 && value.incomplete.columns[0].visible == false, "Invisible record remains present");
    number(value.incomplete.columns[0].position, -1, -1); number(value.incomplete.columns[1].position, -2, -2);
    number(value.incomplete.columns[1].width, std::numeric_limits<std::int64_t>::max(), -1);
  });
  run("ORDER/gapped-order-and-unknown-plugin", [] {
    auto high = slots(); high[1] = i(100);
    auto low = slots(); low[1] = i(2);
    const auto value = read(tables(d({{"Column.plugin.Z", l(high)}, {"note", i(0)}, {"Column.name", l(low)}})));
    text(value.incomplete.columns[0].id, "plugin.Z"); text(value.incomplete.columns[1].id, "name");
    check(value.incomplete.columns[0].sourceIndex == 0 && value.incomplete.columns[1].sourceIndex == 2, "Gapped dictionary positions not compacted");
    number(value.incomplete.columns[0].position, 100, 100); number(value.incomplete.columns[1].position, 2, 2);
  });
}

void sortAndMetadataCases() {
  run("SORT/ordered-multiple", [] {
    const auto value = read(tables(d({{"SortColumns", l({b("plugin.z"), b("name"), b("#")})},
                                      {"SortColumn", b("ignored")}})));
    const auto& table = value.incomplete;
    check(table.sortSource == Sort::SortColumns && table.sortColumnsPresence == Presence::Present
          && table.sortColumnPresence == Presence::Present && table.sort.size() == 3, "Current ordered list takes precedence");
    sortText(table.sort[0], 0, "plugin.z"); sortText(table.sort[1], 1, "name"); sortText(table.sort[2], 2, "#");
    check(value.issues.empty(), "Unknown registered IDs remain requested facts without normalization");
  });
  run("SORT/empty-current-over-legacy", [] {
    for (const auto& legacy : {b(std::string(257, 'x')), i(1)}) {
      const auto value = read(tables(d({{"SortColumns", "le"}, {"SortColumn", legacy}})));
      const auto& table = value.incomplete;
      check(table.sortSource == Sort::SortColumns && table.sort.empty(), "Empty current list suppresses legacy/global and painted fallback");
      check(table.sortColumnPresence == (legacy.front() == 'i' ? Presence::WrongType : Presence::Present), "Unused legacy presence is recorded only");
      check(value.issues.empty(), "Unused legacy contents incur no selected ID quota or type diagnostic");
    }
  });
  run("SORT/current-wrong-members-retained", [] {
    const auto value = read(tables(d({{"SortColumns", l({b("first"), i(7), "le", b("last")})},
                                      {"SortColumn", b("not selected")}})));
    const auto& table = value.incomplete;
    check(table.sortSource == Sort::SortColumns && table.sort.size() == 4, "Malformed current entries never select legacy");
    sortText(table.sort[0], 0, "first"); sortText(table.sort[3], 3, "last");
    absent(table.sort[1].id, Presence::WrongType); absent(table.sort[2].id, Presence::WrongType);
    check(table.sort[1].sourceIndex == 1 && table.sort[2].sourceIndex == 2, "Malformed sort slots retain identity");
    check(issue(value, Issue::Type, Field::SortId, Table::Incomplete, ColumnLayoutNoIndex, 1)
          && issue(value, Issue::Type, Field::SortId, Table::Incomplete, ColumnLayoutNoIndex, 2), "Sort member type issues have real indices");
  });
  run("SORT/current-duplicates-retained", [] {
    const std::string binary("a\0b", 3);
    const auto value = read(tables(d({{"SortColumns", l({b("name"), b("name"), b(binary), b(binary)})}})));
    const auto& table = value.incomplete;
    check(table.sort.size() == 4, "Sort duplicates are not deduplicated");
    sortText(table.sort[0], 0, "name"); sortText(table.sort[1], 1, "name");
    sortText(table.sort[2], 2, binary, true, true); sortText(table.sort[3], 3, binary, true, true);
    check(issue(value, Issue::DuplicateSortId, Field::SortId, Table::Incomplete, ColumnLayoutNoIndex, 1)
          && issue(value, Issue::DuplicateSortId, Field::SortId, Table::Incomplete, ColumnLayoutNoIndex, 3), "Duplicate comparison uses full exact bytes including NUL");
  });
  run("SORT/unknown-primary-before-known", [] {
    const auto value = read(tables(d({{"Column.name", l(slots())}, {"SortColumns", l({b("unregistered.plugin"), b("name")})}})));
    check(value.incomplete.sort.size() == 2 && value.incomplete.sortSource == Sort::SortColumns, "No active registry selection");
    sortText(value.incomplete.sort[0], 0, "unregistered.plugin"); sortText(value.incomplete.sort[1], 1, "name");
  });
  run("SORT/wrong-current-list-type-falls-to-legacy", [] {
    const auto value = read(tables(d({{"SortColumns", b("name")}, {"SortColumn", b("legacy")}})));
    const auto& table = value.incomplete;
    check(table.sortColumnsPresence == Presence::WrongType && table.sortColumnPresence == Presence::Present
          && table.sortSource == Sort::SortColumn && table.sort.size() == 1, "Only wrong current outer type permits legacy singleton");
    sortText(table.sort.front(), 0, "legacy");
    check(issue(value, Issue::Type, Field::SortColumns), "Wrong current outer type remains diagnosed after fallback");
  });
  run("SORT/legacy-singleton-ASCII", [] {
    for (const auto& id : {std::string("legacy"), std::string()}) {
      const auto value = read(tables(d({{"SortColumn", b(id)}})));
      check(value.incomplete.sortColumnsPresence == Presence::Missing && value.incomplete.sortSource == Sort::SortColumn
            && value.incomplete.sort.size() == 1, "Absent current list allows even empty legacy byte singleton");
      sortText(value.incomplete.sort[0], 0, id);
    }
  });
  run("SORT/malformed-legacy-global-required", [] {
    const auto value = read(tables(d({{"SortColumns", i(1)}, {"SortColumn", "le"}})));
    const auto& table = value.incomplete;
    check(table.sortColumnsPresence == Presence::WrongType && table.sortColumnPresence == Presence::WrongType
          && table.sortSource == Sort::LegacyGlobalRequired && table.sort.empty(), "Malformed source keys cannot invent global result");
    check(issue(value, Issue::Type, Field::SortColumns) && issue(value, Issue::Type, Field::SortColumn), "Selected fallback outer errors are distinct");
  });
  run("SORT/neither-sort-global-required", [] {
    const auto value = read(tables("de"));
    const auto& table = value.incomplete;
    check(table.sortColumnsPresence == Presence::Missing && table.sortColumnPresence == Presence::Missing
          && table.sortSource == Sort::LegacyGlobalRequired && table.sort.empty(), "Global sort remains unexecuted and unknown");
    check(value.issues.empty(), "Absent sort does not create diagnostic or guessed primary");
  });
  run("SORT/legacy-nonUTF8-charset-unresolved", [] {
    const std::string raw("\xff\0X", 3);
    const auto value = read(tables(d({{"SortColumn", b(raw)}})));
    check(value.incomplete.sortSource == Sort::SortColumn && value.incomplete.sort.size() == 1, "Legacy invalid UTF8 remains selected bytes");
    sortText(value.incomplete.sort[0], 0, raw, false, true);
    check(!issue(value, Issue::Encoding, Field::SortId, Table::Incomplete, ColumnLayoutNoIndex, 0), "Legacy default charset is not asserted to be UTF8");
    check(issue(value, Issue::Nul, Field::SortId, Table::Incomplete, ColumnLayoutNoIndex, 0), "Legacy NUL byte remains factual");
  });
  run("METADATA/rowheight-and-lastreset-valid", [] {
    const auto value = read(tables(d({{"RowHeight", i(4294967297LL)}, {"last.reset", b(std::string("v\0\xff", 3))}})));
    number(value.incomplete.rowHeight, 4294967297LL, 1);
    check(value.incomplete.lastReset == Presence::Present && value.issues.empty(), "Reset bytes are retained without decoding/version/clock action");
  });
  run("METADATA/rowheight-and-lastreset-wrong-type", [] {
    const auto value = read(tables(d({{"RowHeight", b("17")}, {"last.reset", i(1)}, {"Column.name", l(slots())}})));
    absent(value.incomplete.rowHeight, Presence::WrongType);
    check(value.incomplete.lastReset == Presence::WrongType && value.incomplete.columns.size() == 1, "Wrong table metadata does not hide columns");
    check(issue(value, Issue::Type, Field::RowHeight) && issue(value, Issue::Type, Field::LastReset), "Both metadata type issues remain independent");
  });
}

void ownershipAndEditCases() {
  run("OWNERSHIP/caller-destruction", [] {
    const auto raw = tables(d({{"Column.owned", l(slots())}, {"SortColumns", l({b("owned")})}}));
    auto value = read(raw); // read() destroys its original Document before returning.
    check(value.source->originalBytes() == raw, "Owned source survives caller Document destruction");
    text(onlyColumn(value.incomplete).id, "owned"); sortText(value.incomplete.sort[0], 0, "owned");
    const auto* rootTable = value.source->find("Table.MyTorrents");
    check(rootTable && rootTable->find("Column.owned"), "Owned raw tree remains usable after caller destruction");
  });
  run("OWNERSHIP/caller-edit-after-success", [] {
    const auto raw = tables(column()); auto caller = parsed(raw);
    auto result = readColumnLayouts(caller);
    check(result.has_value() && result->source->originalBytes().data() != caller.originalBytes().data(), "Snapshot initially isolated");
    check(caller.replaceValue("Table.MyTorrents", column({}, "changed")).has_value(), "Caller edit is valid supported operation");
    check(caller.serialize() != raw, "Caller really changed after successful read");
    check(result->source->serialize() == raw, "Caller edit cannot mutate owned source");
    text(onlyColumn(result->incomplete).id, "name"); number(result->incomplete.columns[0].width, 321, 321);
  });
  run("OWNERSHIP/result-copy-move", [] {
    const auto raw = tables(column());
    auto original = read(raw); auto copied = original; auto moved = std::move(copied);
    check(moved.source == original.source && moved.source->originalBytes() == raw, "Copy/move safely shares one immutable source");
    text(onlyColumn(moved.incomplete).id, "name");
    moved.incomplete.columns[0].id.bytes = "local changed result";
    text(onlyColumn(original.incomplete).id, "name");
    check(moved.source->serialize() == raw, "Projected ID storage mutation cannot change source bytes or sibling result");
  });
  run("EDIT/setInteger-same-encoded-length", [] {
    auto source = parsed(d({{"n", i(1)}, {"Table.MyTorrents", column()}}));
    const auto raw = source.originalBytes();
    check(source.setInteger("n", 2), "Same-length supported edit succeeds");
    check(source.originalBytes() == raw && source.serialize().size() == raw.size() && source.serialize() != raw,
          "Fixture separates immutable old bytes from changed same-length tree");
    fails(source, Error::EditedDocument);
  });
  run("EDIT/replace-selected-value", [] {
    auto source = parsed(tables(column()));
    check(source.replaceValue("Table.MyTorrents", "de").has_value(), "Selected replacement fixture valid");
    fails(source, Error::EditedDocument);
  });
  run("EDIT/replace-unselected-value", [] {
    auto source = parsed(d({{"opaque", i(1)}, {"Table.MyTorrents", "de"}}));
    check(source.replaceValue("opaque", i(2)).has_value(), "Opaque replacement fixture valid");
    fails(source, Error::EditedDocument);
  });
  run("EDIT/raw-tree-scalar-without-edit-marker", [] {
    auto source = parsed(d({{"opaque", i(1)}}));
    auto& node = const_cast<Node&>(*source.find("opaque"));
    node.integer = 2;
    check(!node.changed && !source.root().changed, "Fixture has no supported edit marker to rely on");
    fails(source, Error::EditedDocument);
  });
  run("EDIT/raw-tree-byte-span-or-child-shape", [] {
    {
      auto source = parsed(d({{"opaque", b("abc")}}));
      const std::string validForeignStorage = "abc";
      const_cast<Node&>(*source.find("opaque")).bytes = validForeignStorage;
      fails(source, Error::EditedDocument); // Valid memory, forbidden backing provenance.
    }
    {
      auto source = parsed(d({{"opaque", b("abc")}}));
      ++const_cast<Node&>(*source.find("opaque")).begin;
      fails(source, Error::EditedDocument);
    }
    {
      auto source = parsed(d({{"x", i(1)}, {"y", i(2)}}));
      auto& root = const_cast<Node&>(source.root());
      std::swap(root.children[0], root.children[2]);
      std::swap(root.children[1], root.children[3]);
      fails(source, Error::EditedDocument);
    }
    {
      auto source = parsed(d({{"x", i(1)}}));
      const_cast<Node&>(source.root()).children.pop_back();
      fails(source, Error::EditedDocument); // Odd dictionary children, never serialized.
    }
    {
      auto source = parsed(d({{"x", i(1)}}));
      const_cast<Node&>(*source.find("x")).children.emplace_back(Node::Type::Integer);
      fails(source, Error::EditedDocument); // Scalar with children, never serialized.
    }
    {
      auto source = parsed(d({{"x", i(1)}}));
      const_cast<Node&>(*source.find("x")).origin = std::make_shared<const std::string>("i1e");
      fails(source, Error::EditedDocument);
    }
  });
}

void documentLimitCases() {
  constexpr std::size_t cap = 4 * 1024 * 1024;
  run("HARD_DOCUMENT/bytes-at4MiB", [] { const auto value = read(exactBytes(cap)); check(value.issues.empty(), "Exactly ordinary input cap is admitted"); });
  run("HARD_DOCUMENT/bytes-over4MiB", [] { failsRaw(exactBytes(cap + 1), Error::Limit, {}, generousParser()); });
  run("HARD_DOCUMENT/nodes-at100000", [] { const auto value = read(exactNodes(100000)); check(value.issues.empty(), "Exactly node cap is admitted"); });
  run("HARD_DOCUMENT/nodes-over100000", [] { failsRaw(exactNodes(100001), Error::Limit, {}, generousParser()); });
  run("HARD_DOCUMENT/depth-at64", [] { const auto value = read(exactDepth(64)); check(value.issues.empty(), "Root-zero depth64 is admitted"); });
  run("HARD_DOCUMENT/depth-over64", [] { failsRaw(exactDepth(65), Error::Limit, {}, generousParser()); });
  run("LOWER_DOCUMENT/lower-input-bytes", [] {
    ColumnLayoutLimits limits; limits.document.inputBytes = 25;
    const auto value = read(exactBytes(25), limits);
    check(value.issues.empty(), "Lower input limit is inclusive");
    failsRaw(exactBytes(26), Error::Limit, limits);
  });
  run("LOWER_DOCUMENT/lower-string-bytes", [] {
    ColumnLayoutLimits limits; limits.document.stringBytes = 4;
    const auto value = read(d({{"x", b("1234")}}), limits);
    check(value.issues.empty(), "Lower per-string limit is inclusive");
    failsRaw(d({{"x", b("12345")}}), Error::Limit, limits);
  });
  run("LOWER_DOCUMENT/lower-node-and-depth-limits", [] {
    ColumnLayoutLimits nodes; nodes.document.nodes = 10;
    const auto atNodes = read(exactNodes(10), nodes);
    check(atNodes.issues.empty(), "Lower cumulative node count includes dictionary keys");
    failsRaw(exactNodes(11), Error::Limit, nodes);
    ColumnLayoutLimits depth; depth.document.depth = 4;
    const auto atDepth = read(exactDepth(4), depth);
    check(atDepth.issues.empty(), "Lower depth uses root-zero convention");
    failsRaw(exactDepth(5), Error::Limit, depth);
  });
}

void projectionLimitCases() {
  run("HARD_PROJECTION/columns1024-per-pane", [] {
    const auto value = read(tables(columns(1024), columns(1024)));
    check(value.incomplete.columns.size() == 1024 && value.complete.columns.size() == 1024,
          "Column cap applies independently to both panes, not combined");
    check(value.issues.empty(), "Missing fields in 2048 short records do not exhaust issue capacity");
  });
  run("HARD_PROJECTION/columns1025", [] {
    failsRaw(tables(columns(1025, true)), Error::Limit);
    failsRaw(tables("de", columns(1025, true)), Error::Limit);
  });
  run("HARD_PROJECTION/sortslots32", [] {
    const auto value = read(tables(d({{"SortColumns", sortList(32)}}), d({{"SortColumns", sortList(32)}})));
    check(value.incomplete.sort.size() == 32 && value.complete.sort.size() == 32, "Sort caps count every duplicate and malformed slot per pane");
    absent(value.incomplete.sort[1].id, Presence::WrongType);
    sortText(value.incomplete.sort[30], 30, "duplicate");
  });
  run("HARD_PROJECTION/sortslots33", [] {
    failsRaw(tables(d({{"SortColumns", sortList(33)}, {"SortColumn", b("legacy")}})), Error::Limit);
    failsRaw(tables("de", d({{"SortColumns", sortList(33)}})), Error::Limit);
  });
  run("HARD_PROJECTION/id256", [] {
    const std::string id(256, 'x');
    const auto value = read(tables(d({{"Column." + id, "le"}, {"SortColumns", l({b(id)})}})));
    text(onlyColumn(value.incomplete).id, id); sortText(value.incomplete.sort[0], 0, id);
  });
  run("HARD_PROJECTION/id257", [] {
    const std::string id(257, 'x');
    failsRaw(tables(column({}, id)), Error::Limit);
    failsRaw(tables(d({{"SortColumns", l({b(id)})}})), Error::Limit);
    failsRaw(tables(d({{"SortColumn", b(id)}})), Error::Limit);
  });
  run("HARD_PROJECTION/issues4096", [] {
    const auto value = read(tables(columns(1024, true, true), columns(1024, true, true)));
    check(value.issues.size() == 4096, "Two issues per malformed encoded-ID/non-list record fill exact total issue cap");
    check(value.incomplete.columns.size() == 1024 && value.complete.columns.size() == 1024, "Issue cap never truncates columns");
    check(issue(value, Issue::Encoding, Field::Id, Table::Incomplete, 0)
          && issue(value, Issue::Type, Field::Column, Table::Complete, 1023), "Both pane/source dimensions remain correct at issue boundary");
  });
  run("HARD_PROJECTION/issues-over4096", [] {
    failsRaw(tables(columns(1024, true, true, true), columns(1024, true, true)), Error::Limit);
  });
  run("LOWER_PROJECTION/columns-lower", [] {
    ColumnLayoutLimits limits; limits.columnsPerTable = 1;
    const auto value = read(tables(d({{"Column.", "le"}})), limits);
    check(value.incomplete.columns.size() == 1 && value.incomplete.hasColumnSettings, "Empty ID still counts as a column");
    failsRaw(tables(d({{"Column.", "le"}, {"Column.second", i(1)}})), Error::Limit, limits);
  });
  run("LOWER_PROJECTION/sortslots-lower", [] {
    ColumnLayoutLimits limits; limits.sortIdsPerTable = 1;
    const auto value = read(tables(d({{"SortColumns", l({b("same")})}})), limits);
    check(value.incomplete.sort.size() == 1, "Lower sort slot bound inclusive");
    failsRaw(tables(d({{"SortColumns", l({b("same"), b("same")})}})), Error::Limit, limits);
    failsRaw(tables(d({{"SortColumns", l({i(1), i(2)})}})), Error::Limit, limits);
    limits.sortIdsPerTable = 0;
    failsRaw(tables(d({{"SortColumn", b("legacy")}})), Error::Limit, limits);
    const auto empty = read(tables(d({{"SortColumns", "le"}})), limits);
    check(empty.incomplete.sort.empty(), "Zero selected sort capacity still admits valid empty current list");
  });
  run("LOWER_PROJECTION/idbytes-lower", [] {
    ColumnLayoutLimits limits; limits.idBytes = 3;
    const auto value = read(tables(d({{"Column.abc", "le"}, {"SortColumns", l({b("abc")})}})), limits);
    text(onlyColumn(value.incomplete).id, "abc"); sortText(value.incomplete.sort[0], 0, "abc");
    failsRaw(tables(column({}, "abcd")), Error::Limit, limits);
    failsRaw(tables(d({{"SortColumns", l({b("abcd")})}})), Error::Limit, limits);
    failsRaw(tables(d({{"SortColumn", b("abcd")}})), Error::Limit, limits);
  });
  run("LOWER_PROJECTION/issues-lower", [] {
    ColumnLayoutLimits limits; limits.issues = 0;
    const auto clean = read(tables(column()), limits);
    check(clean.issues.empty(), "Zero issue capacity permits valid input");
    failsRaw(tables(d({{"Column.bad", i(1)}})), Error::Limit, limits);
    limits.issues = 1;
    const auto at = read(tables(d({{"Column.bad", i(1)}})), limits);
    check(at.issues.size() == 1 && at.incomplete.columns.size() == 1, "Lower issue bound is inclusive");
    failsRaw(tables(d({{"Column.bad", i(1)}, {"Column.other", i(1)}})), Error::Limit, limits);
  });
  const std::array<const char*, 8> raisedNames{"input", "string", "nodes", "depth", "columns", "sort", "id", "issues"};
  for (std::size_t which = 0; which < raisedNames.size(); ++which) {
    run(std::string("RAISED_LIMIT/") + raisedNames[which], [which] {
      ColumnLayoutLimits limits;
      switch (which) {
        case 0: ++limits.document.inputBytes; break;
        case 1: ++limits.document.stringBytes; break;
        case 2: ++limits.document.nodes; break;
        case 3: ++limits.document.depth; break;
        case 4: ++limits.columnsPerTable; break;
        case 5: ++limits.sortIdsPerTable; break;
        case 6: ++limits.idBytes; break;
        case 7: ++limits.issues; break;
        default: throw std::logic_error("Unknown configured-cap test");
      }
      failsRaw("de", Error::Limit, limits);
    });
  }
  const std::array<std::pair<const char*, std::string>, 3> roots{{
      {"integer-root", i(1)}, {"list-root", "le"}, {"bytes-root", b("opaque")}}};
  for (const auto& [name, raw] : roots) {
    run(std::string("ROOT/") + name, [&raw] { failsRaw(raw, Error::Root); });
  }
}
} // namespace

int main() {
  try {
    tableCases();
    slotCases();
    rgbAndOpaqueCases();
    idAndOrderCases();
    sortAndMetadataCases();
    ownershipAndEditCases();
    documentLimitCases();
    projectionLimitCases();
    check(cases == 112, "Frozen 22-family plan defines exactly 112 named cases");
    std::cout << "PASS " << cases << " named column-layout cases, " << assertions
              << " assertions; generated pure requested-fact snapshots only\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << activeCase << ": " << error.what() << '\n';
    return 1;
  }
}
