#pragma once

#include "biglybtc/bencode.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace biglybtc::core {

// Native projection limits, not Java-format limits. Only reductions of these
// defaults are accepted, including when the caller used larger parser quotas.
// The complete opaque document is bounded once by document. Count/ID limits
// apply only to Column.* records and selected sort entries in the two selected
// table maps, not to unrelated opaque table/plugin contents elsewhere.
struct ColumnLayoutLimits {
  Limits document{};
  std::size_t columnsPerTable = 1024;
  std::size_t sortIdsPerTable = 32;
  std::size_t idBytes = 256;
  std::size_t issues = 4096;
};
enum class ColumnLayoutError { Limit, EditedDocument, Root, Memory };
enum class ColumnLayoutPresence { Missing, WrongType, Malformed, Present };
enum class ColumnLayoutTableId { Incomplete, Complete };
enum class ColumnLayoutOrigin { Absent, ExactTable, UserDefault };
enum class ColumnLayoutSortSource { LegacyGlobalRequired, SortColumns, SortColumn };
enum class ColumnLayoutDirection { Ascending, Descending };
enum class ColumnLayoutIssueCode { Type, Shape, Encoding, Nul, EmptyId, DuplicateSortId };
enum class ColumnLayoutField {
  Table, Column, Id, Visibility, Position, Width, AutoTooltip, SortOrder,
  UserData, Alignment, Foreground, Background, IconEnabled, SortColumns,
  SortColumn, SortId, RowHeight, LastReset
};
inline constexpr std::size_t ColumnLayoutNoIndex = std::numeric_limits<std::size_t>::max();
struct ColumnLayoutIssue {
  ColumnLayoutIssueCode code;
  ColumnLayoutTableId table;
  // A column's index among dictionary pairs, or NoIndex for table/sort issues.
  std::size_t sourceIndex = ColumnLayoutNoIndex;
  ColumnLayoutField field = ColumnLayoutField::Table;
  // Sort/RGB member index when applicable; never a Qt logical/visual index.
  std::size_t elementIndex = ColumnLayoutNoIndex;
};

// Raw stored signed64 and defined signed-low32 Number.intValue() facts. No
// visual range clamp, default, registry normalization or Java object is implied.
struct ColumnLayoutNumber {
  std::int64_t raw = 0;
  std::int32_t value = 0;
};
template<typename T>
struct ColumnLayoutValue {
  ColumnLayoutPresence presence = ColumnLayoutPresence::Missing;
  // Engaged exactly when presence==Present. WrongType/Malformed remain raw in
  // the owned source, rather than becoming guessed zero/default values.
  std::optional<T> value{};
};
using ColumnLayoutInteger = ColumnLayoutValue<ColumnLayoutNumber>;
struct ColumnLayoutText {
  std::string bytes;
  bool validUtf8 = true;
  bool embeddedNul = false;
};
using ColumnLayoutRgb = std::array<ColumnLayoutNumber, 3>;

struct SavedColumnLayout {
  std::size_t sourceIndex = 0; // Pair index in the selected table dictionary.
  ColumnLayoutText id; // Exact Column. suffix, without normalization/aliases.
  ColumnLayoutPresence presence = ColumnLayoutPresence::Missing; // List value.
  std::size_t listLength = 0; // Extra list tail remains only in source.
  // Executable source order is visibility, POSITION, WIDTH. The old Java
  // loadSettings comment incorrectly swaps position and width.
  ColumnLayoutInteger visibility;  // List[0], intValue()==1.
  ColumnLayoutInteger position;    // List[1], including -1/-2 unchanged.
  ColumnLayoutInteger width;       // List[2], no Qt/column min/max applied.
  ColumnLayoutInteger autoTooltip; // List[3], intValue()==1.
  ColumnLayoutInteger sortOrder;   // List[4], negative leaves initialized order.
  ColumnLayoutPresence userData = ColumnLayoutPresence::Missing; // List[5].
  ColumnLayoutInteger alignment;   // List[6].
  ColumnLayoutValue<ColumnLayoutRgb> foreground; // List[7], exactly3 integers.
  ColumnLayoutValue<ColumnLayoutRgb> background; // List[8], exactly3 integers.
  ColumnLayoutInteger iconEnabled; // List[9], intValue()!=0.
  // DECODED REQUESTED slot facts only, not effective registered-column
  // properties. Legacy position, ensureIntegrity, initialized/default values
  // and registered min/max constraints may change effective Java properties.
  std::optional<bool> visible{};
  std::optional<bool> automaticTooltip{};
  std::optional<bool> iconReferenceEnabled{};
  // Decoded requested direction only. Engaged for stored numeric sortOrder
  // with narrowed value>=0.
  // No per-column initialized/default direction is fabricated for other cases.
  std::optional<ColumnLayoutDirection> direction{};
  bool legacyPositionRequired = false;
  bool legacyWidthRequired = false;
  bool legacyTooltipRequired = false;
};
struct ColumnLayoutSortEntry {
  std::size_t sourceIndex = 0; // Selected sort-list index (0 for legacy singleton).
  ColumnLayoutValue<ColumnLayoutText> id;
};
struct SavedTableLayout {
  // Missing exact table may select the matching TableDefault.* map. Present
  // empty exact table wins; wrong exact type blocks default fallback.
  ColumnLayoutPresence exactPresence = ColumnLayoutPresence::Missing;
  ColumnLayoutOrigin origin = ColumnLayoutOrigin::Absent;
  ColumnLayoutPresence presence = ColumnLayoutPresence::Missing; // Selected map.
  bool hasColumnSettings = false; // Any Column.* key, even a malformed value.
  std::vector<SavedColumnLayout> columns; // Source dictionary order, not sorted.
  ColumnLayoutPresence sortColumnsPresence = ColumnLayoutPresence::Missing;
  ColumnLayoutPresence sortColumnPresence = ColumnLayoutPresence::Missing;
  ColumnLayoutSortSource sortSource = ColumnLayoutSortSource::LegacyGlobalRequired;
  // Full ordered selected list, including malformed/duplicate entries. Valid
  // empty SortColumns remains empty and does not select SortColumn/global.
  // Legacy SortColumn bytes are preserved; JVM default charset is unresolved.
  std::vector<ColumnLayoutSortEntry> sort;
  ColumnLayoutInteger rowHeight;
  // last.reset and userData are not copied or interpreted beyond type presence.
  // Their complete values, unknown fields and list tails remain in source.
  ColumnLayoutPresence lastReset = ColumnLayoutPresence::Missing;
};
struct ColumnLayouts {
  std::shared_ptr<const Document> source;
  SavedTableLayout incomplete; // Exact MyTorrents, fallback TableDefault.MyTorrents.
  SavedTableLayout complete;   // Exact MySeeders, fallback TableDefault.MySeeders.
  std::vector<ColumnLayoutIssue> issues;
};

// Pure, synchronous projection of ONLY the two ordinary classic table maps.
// No manager/registry/default migration, global config lookup, I/O, UI, clock,
// sorting, geometry application or writer. Unknown tables/plugin columns/tails/
// userData survive once in the complete original immutable source document.
//
// Validate configured caps, raw bytes and the supplied tree under ordinary
// document maxima with a bounded iterative walk before serialization/recursive
// comparison or owned parsing. Reject edited or
// raw/tree-inconsistent input. Fresh bounded parsing isolates ownership from
// the caller and enforces limits even for a Document parsed with larger caps.
// Result byte fields own their copies; there are no borrowed result pointers.
// Later caller mutation/destruction cannot change a successful result.
//
// Wrong root fails. Malformed selected maps/records/fields are retained and
// diagnosed independently; an existing malformed exact map never falls back.
// Missing fields carry presence/fallback facts without a diagnostic per field.
// Exceeding any count/size/issue cap fails the whole projection with Limit;
// no truncation/partial successful result. Allocation failure returns Memory.
// Only existing Document parser policy is supported (duplicate dicts rejected).
std::expected<ColumnLayouts, ColumnLayoutError> readColumnLayouts(
    const Document& document, ColumnLayoutLimits limits = {});

} // namespace biglybtc::core
