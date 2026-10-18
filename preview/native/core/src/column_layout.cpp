#include "biglybtc/column_layout.hpp"

#include <new>
#include <string_view>
#include <utility>

namespace biglybtc::core {
namespace {
// Field source: BiglyBT cb8c6cb9bb97916cbc7e80338a017200ffc3e71f,
// TableColumnManager/TableColumnImpl. This projects requested saved facts, not
// registered-column loading, defaults, integrity normalization or UI behavior.
struct Failure { ColumnLayoutError error; };
using Presence = ColumnLayoutPresence;
constexpr std::string_view ColumnPrefix = "Column.";
constexpr std::size_t MaximumDepth = Limits{}.depth;

void validateLimits(const ColumnLayoutLimits& limits) {
  const ColumnLayoutLimits maximum;
  if (limits.document.inputBytes > maximum.document.inputBytes
      || limits.document.stringBytes > maximum.document.stringBytes
      || limits.document.nodes > maximum.document.nodes
      || limits.document.depth > maximum.document.depth
      || limits.columnsPerTable > maximum.columnsPerTable
      || limits.sortIdsPerTable > maximum.sortIdsPerTable
      || limits.idBytes > maximum.idBytes || limits.issues > maximum.issues) {
    throw Failure{ColumnLayoutError::Limit};
  }
}

// Inspect no caller byte contents here. Even a replaced valid-memory bytes
// view is checked against its exact original span only after bounded parsing.
void validateInput(const Document& document, const Limits& limits) {
  const auto bytes = document.originalBytes().size();
  if (bytes > limits.inputBytes) { throw Failure{ColumnLayoutError::Limit}; }
  struct Frame { const Node* node; std::size_t next; };
  std::array<Frame, MaximumDepth + 1> stack{};
  std::size_t depth = 0;
  std::size_t nodes = 0;
  const auto enter = [&](const Node& node) {
    if (nodes == limits.nodes) { throw Failure{ColumnLayoutError::Limit}; }
    ++nodes;
    if (node.children.size() > limits.nodes - nodes) {
      throw Failure{ColumnLayoutError::Limit};
    }
    if (node.changed || node.origin || node.begin > node.end || node.end > bytes) {
      throw Failure{ColumnLayoutError::EditedDocument};
    }
    switch (node.type) {
      case Node::Type::Integer:
      case Node::Type::Bytes:
        if (!node.children.empty()) { throw Failure{ColumnLayoutError::EditedDocument}; }
        break;
      case Node::Type::List: break;
      case Node::Type::Dictionary:
        if (node.children.size() % 2 != 0) { throw Failure{ColumnLayoutError::EditedDocument}; }
        break;
      default: throw Failure{ColumnLayoutError::EditedDocument};
    }
    if (node.type == Node::Type::Bytes && node.bytes.size() > limits.stringBytes) {
      throw Failure{ColumnLayoutError::Limit};
    }
  };
  enter(document.root());
  stack[0] = {&document.root(), 0};
  for (;;) {
    auto& frame = stack[depth];
    if (frame.next == frame.node->children.size()) {
      if (depth == 0) { break; }
      --depth;
      continue;
    }
    if (depth == limits.depth) { throw Failure{ColumnLayoutError::Limit}; }
    const auto index = frame.next++;
    const auto& child = frame.node->children[index];
    enter(child);
    if (frame.node->type == Node::Type::Dictionary && index % 2 == 0
        && child.type != Node::Type::Bytes) {
      throw Failure{ColumnLayoutError::EditedDocument};
    }
    ++depth;
    stack[depth] = {&child, 0};
  }
}

void compareInput(const Document& input, const Document& parsed) {
  struct Frame { const Node* input; const Node* parsed; std::size_t next; };
  std::array<Frame, MaximumDepth + 1> stack{};
  std::size_t depth = 0;
  const auto compare = [&](const Node& supplied, const Node& bounded) {
    if (supplied.type != bounded.type || supplied.begin != bounded.begin
        || supplied.end != bounded.end || supplied.integer != bounded.integer
        || supplied.children.size() != bounded.children.size()) {
      throw Failure{ColumnLayoutError::EditedDocument};
    }
    if (bounded.type == Node::Type::Bytes) {
      // Subtraction is ONLY between pointers into the fresh parser's own raw
      // buffer. Never subtract/order/dereference an untrusted supplied pointer.
      const auto offset = static_cast<std::size_t>(bounded.bytes.data() - parsed.originalBytes().data());
      if (supplied.bytes.size() != bounded.bytes.size()
          || supplied.bytes.data() != input.originalBytes().data() + offset) {
        throw Failure{ColumnLayoutError::EditedDocument};
      }
    } else if (!supplied.bytes.empty()) {
      throw Failure{ColumnLayoutError::EditedDocument};
    }
  };
  compare(input.root(), parsed.root());
  stack[0] = {&input.root(), &parsed.root(), 0};
  for (;;) {
    auto& frame = stack[depth];
    if (frame.next == frame.parsed->children.size()) {
      if (depth == 0) { break; }
      --depth;
      continue;
    }
    // Both trees were already depth/count bounded; the fresh parser controls
    // this walk, rather than any supplied shape or view.
    if (depth == MaximumDepth) { throw Failure{ColumnLayoutError::EditedDocument}; }
    const auto index = frame.next++;
    const auto& supplied = frame.input->children[index];
    const auto& bounded = frame.parsed->children[index];
    compare(supplied, bounded);
    ++depth;
    stack[depth] = {&supplied, &bounded, 0};
  }
}

ColumnLayoutNumber number(std::int64_t raw) {
  const auto low = static_cast<std::uint32_t>(raw);
  const auto value = low <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())
      ? static_cast<std::int32_t>(low)
      : -1 - static_cast<std::int32_t>(std::numeric_limits<std::uint32_t>::max() - low);
  return {raw, value};
}
bool utf8(std::string_view bytes) {
  for (std::size_t i = 0; i < bytes.size();) {
    const auto first = static_cast<unsigned char>(bytes[i]);
    if (first < 0x80) { ++i; continue; }
    const std::size_t length = first >= 0xc2 && first <= 0xdf ? 2
        : first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
    if (length == 0 || length > bytes.size() - i) { return false; }
    for (std::size_t j = 1; j < length; ++j) {
      const auto next = static_cast<unsigned char>(bytes[i + j]);
      if (next < 0x80 || next > 0xbf) { return false; }
    }
    const auto second = static_cast<unsigned char>(bytes[i + 1]);
    if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0)
        || (first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90)) { return false; }
    i += length;
  }
  return true;
}
Presence presence(const Node* node, Node::Type type) {
  return !node ? Presence::Missing : node->type == type ? Presence::Present : Presence::WrongType;
}
const Node* slot(const Node* list, std::size_t index) {
  return list && index < list->children.size() ? &list->children[index] : nullptr;
}
struct Selection {
  const Node* exact = nullptr;
  const Node* selected = nullptr;
  ColumnLayoutOrigin origin = ColumnLayoutOrigin::Absent;
  std::size_t columns = 0;
  std::size_t sorts = 0;
};
Selection select(const Node& root, std::string_view exactKey, std::string_view defaultKey) {
  Selection result;
  result.exact = root.find(exactKey);
  if (result.exact) {
    result.selected = result.exact;
    result.origin = ColumnLayoutOrigin::ExactTable;
  } else if (const auto* defaults = root.find(defaultKey)) {
    result.selected = defaults;
    result.origin = ColumnLayoutOrigin::UserDefault;
  }
  return result;
}
void preflight(Selection& selection, const ColumnLayoutLimits& limits) {
  if (!selection.selected || selection.selected->type != Node::Type::Dictionary) { return; }
  const auto& table = *selection.selected;
  for (std::size_t i = 0; i < table.children.size(); i += 2) {
    const auto key = table.children[i].bytes;
    if (!key.starts_with(ColumnPrefix)) { continue; }
    if (selection.columns == limits.columnsPerTable || key.size() - ColumnPrefix.size() > limits.idBytes) {
      throw Failure{ColumnLayoutError::Limit};
    }
    ++selection.columns;
  }
  const auto checkId = [&](const Node& node) {
    if (node.type == Node::Type::Bytes && node.bytes.size() > limits.idBytes) {
      throw Failure{ColumnLayoutError::Limit};
    }
  };
  if (const auto* current = table.find("SortColumns"); current && current->type == Node::Type::List) {
    selection.sorts = current->children.size();
    if (selection.sorts > limits.sortIdsPerTable) { throw Failure{ColumnLayoutError::Limit}; }
    for (const auto& id : current->children) { checkId(id); }
  } else if (const auto* legacy = table.find("SortColumn"); legacy && legacy->type == Node::Type::Bytes) {
    if (limits.sortIdsPerTable == 0) { throw Failure{ColumnLayoutError::Limit}; }
    selection.sorts = 1;
    checkId(*legacy);
  }
}

class Reader final {
public:
  Reader(const ColumnLayoutLimits& limits, std::vector<ColumnLayoutIssue>& issues)
      : limits_(limits), issues_(issues) {}
  void table(ColumnLayoutTableId table) { table_ = table; index_ = ColumnLayoutNoIndex; }
  void row(std::size_t index) { index_ = index; }
  void issue(ColumnLayoutIssueCode code, ColumnLayoutField field,
      std::size_t element = ColumnLayoutNoIndex) {
    if (issues_.size() == limits_.issues) { throw Failure{ColumnLayoutError::Limit}; }
    issues_.push_back({code, table_, index_, field, element});
  }
  Presence type(const Node* node, Node::Type expected, ColumnLayoutField field) {
    const auto state = presence(node, expected);
    if (state == Presence::WrongType) { issue(ColumnLayoutIssueCode::Type, field); }
    return state;
  }
  ColumnLayoutInteger integer(const Node* node, ColumnLayoutField field) {
    ColumnLayoutInteger result;
    result.presence = type(node, Node::Type::Integer, field);
    if (result.presence == Presence::Present) { result.value = number(node->integer); }
    return result;
  }
  ColumnLayoutText text(std::string_view bytes, ColumnLayoutField field,
      std::size_t element = ColumnLayoutNoIndex, bool explicitUtf8 = true) {
    // Entire selected count/ID preflight precedes any copied strings. Keep this
    // local check too, so allocation stays bounded if new callers are added.
    if (bytes.size() > limits_.idBytes) { throw Failure{ColumnLayoutError::Limit}; }
    const bool valid = utf8(bytes);
    const bool nul = bytes.find('\0') != std::string_view::npos;
    if (bytes.empty()) { issue(ColumnLayoutIssueCode::EmptyId, field, element); }
    if (!valid && explicitUtf8) { issue(ColumnLayoutIssueCode::Encoding, field, element); }
    if (nul) { issue(ColumnLayoutIssueCode::Nul, field, element); }
    return {std::string(bytes), valid, nul};
  }
  ColumnLayoutValue<ColumnLayoutRgb> rgb(const Node* node, ColumnLayoutField field) {
    ColumnLayoutValue<ColumnLayoutRgb> result;
    result.presence = type(node, Node::Type::List, field);
    if (result.presence != Presence::Present) { return result; }
    if (node->children.size() != 3) {
      result.presence = Presence::Malformed;
      issue(ColumnLayoutIssueCode::Shape, field);
      return result;
    }
    ColumnLayoutRgb values{};
    bool valid = true;
    for (std::size_t i = 0; i < values.size(); ++i) {
      if (node->children[i].type != Node::Type::Integer) {
        valid = false;
        issue(ColumnLayoutIssueCode::Type, field, i);
      } else { values[i] = number(node->children[i].integer); }
    }
    if (valid) { result.value = values; }
    else { result.presence = Presence::Malformed; }
    return result;
  }
private:
  const ColumnLayoutLimits& limits_;
  std::vector<ColumnLayoutIssue>& issues_;
  ColumnLayoutTableId table_ = ColumnLayoutTableId::Incomplete;
  std::size_t index_ = ColumnLayoutNoIndex;
};

SavedColumnLayout column(std::string_view id, const Node& raw,
    std::size_t index, Reader& reader) {
  reader.row(index);
  SavedColumnLayout result;
  result.sourceIndex = index;
  result.id = reader.text(id, ColumnLayoutField::Id);
  result.presence = reader.type(&raw, Node::Type::List, ColumnLayoutField::Column);
  const auto* list = result.presence == Presence::Present ? &raw : nullptr;
  if (list) { result.listLength = list->children.size(); }
  result.visibility = reader.integer(slot(list, 0), ColumnLayoutField::Visibility);
  result.position = reader.integer(slot(list, 1), ColumnLayoutField::Position);
  result.width = reader.integer(slot(list, 2), ColumnLayoutField::Width);
  result.autoTooltip = reader.integer(slot(list, 3), ColumnLayoutField::AutoTooltip);
  result.sortOrder = reader.integer(slot(list, 4), ColumnLayoutField::SortOrder);
  result.userData = reader.type(slot(list, 5), Node::Type::Dictionary, ColumnLayoutField::UserData);
  result.alignment = reader.integer(slot(list, 6), ColumnLayoutField::Alignment);
  result.foreground = reader.rgb(slot(list, 7), ColumnLayoutField::Foreground);
  result.background = reader.rgb(slot(list, 8), ColumnLayoutField::Background);
  result.iconEnabled = reader.integer(slot(list, 9), ColumnLayoutField::IconEnabled);
  if (result.visibility.value) { result.visible = result.visibility.value->value == 1; }
  if (result.autoTooltip.value) { result.automaticTooltip = result.autoTooltip.value->value == 1; }
  if (result.iconEnabled.value) { result.iconReferenceEnabled = result.iconEnabled.value->value != 0; }
  if (result.sortOrder.value && result.sortOrder.value->value >= 0) {
    result.direction = result.sortOrder.value->value == 1
        ? ColumnLayoutDirection::Ascending : ColumnLayoutDirection::Descending;
  }
  result.legacyPositionRequired = result.position.presence != Presence::Present;
  result.legacyWidthRequired = result.width.presence != Presence::Present;
  result.legacyTooltipRequired = result.autoTooltip.presence != Presence::Present;
  return result;
}
void sortEntry(SavedTableLayout& table, const Node& node, std::size_t index,
    Reader& reader, bool explicitUtf8) {
  ColumnLayoutSortEntry result;
  result.sourceIndex = index;
  result.id.presence = presence(&node, Node::Type::Bytes);
  if (result.id.presence == Presence::WrongType) {
    reader.issue(ColumnLayoutIssueCode::Type, ColumnLayoutField::SortId, index);
  }
  if (result.id.presence == Presence::Present) {
    result.id.value = reader.text(node.bytes, ColumnLayoutField::SortId, index, explicitUtf8);
    for (const auto& previous : table.sort) {
      if (previous.id.value && previous.id.value->bytes == result.id.value->bytes) {
        reader.issue(ColumnLayoutIssueCode::DuplicateSortId, ColumnLayoutField::SortId, index);
        break;
      }
    }
  }
  table.sort.push_back(std::move(result));
}
void readTable(const Selection& selection, SavedTableLayout& result,
    ColumnLayoutTableId tableId, Reader& reader) {
  reader.table(tableId);
  result.exactPresence = presence(selection.exact, Node::Type::Dictionary);
  result.origin = selection.origin;
  result.presence = reader.type(selection.selected, Node::Type::Dictionary, ColumnLayoutField::Table);
  if (result.presence != Presence::Present) { return; }
  const auto& table = *selection.selected;
  result.columns.reserve(selection.columns);
  for (std::size_t i = 0; i < table.children.size(); i += 2) {
    const auto key = table.children[i].bytes;
    if (!key.starts_with(ColumnPrefix)) { continue; }
    result.hasColumnSettings = true;
    result.columns.push_back(column(key.substr(ColumnPrefix.size()), table.children[i + 1], i / 2, reader));
  }
  reader.row(ColumnLayoutNoIndex);
  const auto* current = table.find("SortColumns");
  const auto* legacy = table.find("SortColumn");
  result.sortColumnsPresence = reader.type(current, Node::Type::List, ColumnLayoutField::SortColumns);
  // Record presence but do not interpret/diagnose the unused legacy branch
  // when a current list exists, including empty/current malformed-ID lists.
  result.sortColumnPresence = presence(legacy, Node::Type::Bytes);
  result.sort.reserve(selection.sorts);
  if (result.sortColumnsPresence == Presence::Present) {
    result.sortSource = ColumnLayoutSortSource::SortColumns;
    for (std::size_t i = 0; i < current->children.size(); ++i) {
      sortEntry(result, current->children[i], i, reader, true);
    }
  } else if (result.sortColumnPresence == Presence::Present) {
    result.sortSource = ColumnLayoutSortSource::SortColumn;
    sortEntry(result, *legacy, 0, reader, false);
  } else if (result.sortColumnPresence == Presence::WrongType) {
    reader.issue(ColumnLayoutIssueCode::Type, ColumnLayoutField::SortColumn);
  }
  result.rowHeight = reader.integer(table.find("RowHeight"), ColumnLayoutField::RowHeight);
  result.lastReset = reader.type(table.find("last.reset"), Node::Type::Bytes, ColumnLayoutField::LastReset);
}
} // namespace

std::expected<ColumnLayouts, ColumnLayoutError> readColumnLayouts(
    const Document& document, ColumnLayoutLimits limits) {
  try {
    validateLimits(limits);
    validateInput(document, limits.document);
    auto parsed = Document::parse(document.originalBytes(), limits.document);
    if (!parsed) {
      const auto& code = parsed.error().code;
      const bool limit = code == "input_limit" || code == "string_limit"
          || code == "node_limit" || code == "depth_limit";
      return std::unexpected(limit ? ColumnLayoutError::Limit : ColumnLayoutError::EditedDocument);
    }
    compareInput(document, *parsed);
    if (parsed->root().type != Node::Type::Dictionary) { return std::unexpected(ColumnLayoutError::Root); }
    ColumnLayouts result;
    result.source = std::make_shared<const Document>(std::move(*parsed));
    auto incomplete = select(result.source->root(), "Table.MyTorrents", "TableDefault.MyTorrents");
    auto complete = select(result.source->root(), "Table.MySeeders", "TableDefault.MySeeders");
    // Preflight both panes before constructing any projected records/IDs.
    preflight(incomplete, limits);
    preflight(complete, limits);
    Reader reader(limits, result.issues);
    readTable(incomplete, result.incomplete, ColumnLayoutTableId::Incomplete, reader);
    readTable(complete, result.complete, ColumnLayoutTableId::Complete, reader);
    return result;
  } catch (const Failure& failure) {
    return std::unexpected(failure.error);
  } catch (const std::bad_alloc&) {
    return std::unexpected(ColumnLayoutError::Memory);
  }
}

} // namespace biglybtc::core
