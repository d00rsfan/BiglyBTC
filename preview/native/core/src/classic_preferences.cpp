#include "biglybtc/classic_preferences.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <new>
#include <type_traits>
#include <utility>

namespace biglybtc::core {
namespace {
constexpr std::string_view Namespace = "BiglyBTC.ClassicColumns";
constexpr std::string_view Key = "BiglyBTC.ClassicColumns.v1";
constexpr Limits WholeLimits{};
constexpr Limits PrivateLimits{32768, 32768, 1024, 8};
using Error = ClassicPreferenceError;
struct Failure { Error error; };
[[noreturn]] void fail(Error error) { throw Failure{error}; }
struct Shape { std::size_t nodes = 0; std::size_t depth = 0; };

// No supplied bytes are dereferenced in this walk. In particular, a forged
// string_view is not ordered/subtracted against a real allocation.
Shape preflight(const Document& document) {
  const auto bytes = document.originalBytes().size();
  if (bytes > WholeLimits.inputBytes) { fail(Error::Limit); }
  struct Frame { const Node* node; std::size_t next; };
  std::array<Frame, WholeLimits.depth + 1> stack{};
  Shape shape;
  std::size_t depth = 0;
  const auto enter = [&](const Node& node) {
    if (shape.nodes == WholeLimits.nodes) { fail(Error::Limit); }
    ++shape.nodes;
    if (node.children.size() > WholeLimits.nodes - shape.nodes) { fail(Error::Limit); }
    if (node.changed || node.origin || node.begin > node.end || node.end > bytes) { fail(Error::EditedDocument); }
    switch (node.type) {
      case Node::Type::Integer:
      case Node::Type::Bytes:
        if (!node.children.empty()) { fail(Error::EditedDocument); }
        break;
      case Node::Type::List: break;
      case Node::Type::Dictionary:
        if (node.children.size() % 2 != 0) { fail(Error::EditedDocument); }
        break;
      default: fail(Error::EditedDocument);
    }
    if (node.type == Node::Type::Bytes && node.bytes.size() > WholeLimits.stringBytes) { fail(Error::Limit); }
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
    if (depth == WholeLimits.depth) { fail(Error::Limit); }
    const auto index = frame.next++;
    const auto& child = frame.node->children[index];
    enter(child);
    if (frame.node->type == Node::Type::Dictionary && index % 2 == 0 && child.type != Node::Type::Bytes) {
      fail(Error::EditedDocument);
    }
    ++depth;
    if (depth > shape.depth) { shape.depth = depth; }
    stack[depth] = {&child, 0};
  }
  return shape;
}

void compare(const Document& input, const Document& parsed) {
  struct Frame { const Node* input; const Node* parsed; std::size_t next; };
  std::array<Frame, WholeLimits.depth + 1> stack{};
  std::size_t depth = 0;
  const auto equal = [&](const Node& supplied, const Node& bounded) {
    if (supplied.type != bounded.type || supplied.begin != bounded.begin || supplied.end != bounded.end
        || supplied.integer != bounded.integer || supplied.children.size() != bounded.children.size()) {
      fail(Error::EditedDocument);
    }
    if (bounded.type == Node::Type::Bytes) {
      const auto offset = static_cast<std::size_t>(bounded.bytes.data() - parsed.originalBytes().data());
      if (supplied.bytes.size() != bounded.bytes.size()
          || supplied.bytes.data() != input.originalBytes().data() + offset) { fail(Error::EditedDocument); }
    } else if (!supplied.bytes.empty()) { fail(Error::EditedDocument); }
  };
  equal(input.root(), parsed.root());
  stack[0] = {&input.root(), &parsed.root(), 0};
  for (;;) {
    auto& frame = stack[depth];
    if (frame.next == frame.parsed->children.size()) {
      if (depth == 0) { break; }
      --depth;
      continue;
    }
    if (depth == WholeLimits.depth) { fail(Error::EditedDocument); }
    const auto index = frame.next++;
    const auto& supplied = frame.input->children[index];
    const auto& bounded = frame.parsed->children[index];
    equal(supplied, bounded);
    ++depth;
    stack[depth] = {&supplied, &bounded, 0};
  }
}

Document checked(const Document& document, Shape& shape) {
  shape = preflight(document);
  auto parsed = Document::parse(document.originalBytes(), WholeLimits);
  if (!parsed) {
    const auto& code = parsed.error().code;
    fail(code == "input_limit" || code == "string_limit" || code == "node_limit" || code == "depth_limit"
        ? Error::Limit : Error::EditedDocument);
  }
  compare(document, *parsed);
  if (parsed->root().type != Node::Type::Dictionary) { fail(Error::Invalid); }
  return std::move(*parsed);
}

Shape privateShape(const Node& root) {
  if (root.end - root.begin > PrivateLimits.inputBytes) { fail(Error::Limit); }
  struct Frame { const Node* node; std::size_t next; };
  std::array<Frame, PrivateLimits.depth + 1> stack{};
  Shape shape{1, 0};
  std::size_t depth = 0;
  stack[0] = {&root, 0};
  for (;;) {
    auto& frame = stack[depth];
    if (frame.next == frame.node->children.size()) {
      if (depth == 0) { break; }
      --depth;
      continue;
    }
    if (depth == PrivateLimits.depth || shape.nodes == PrivateLimits.nodes) { fail(Error::Limit); }
    const auto& child = frame.node->children[frame.next++];
    ++shape.nodes;
    ++depth;
    if (depth > shape.depth) { shape.depth = depth; }
    stack[depth] = {&child, 0};
  }
  return shape;
}

std::optional<std::size_t> id(std::string_view value) {
  for (std::size_t i = 0; i < ClassicPreferenceIds.size(); ++i) {
    if (value == ClassicPreferenceIds[i]) { return i; }
  }
  return std::nullopt;
}
void fields(const Node& node, std::initializer_list<std::string_view> allowed) {
  if (node.type != Node::Type::Dictionary) { fail(Error::Invalid); }
  for (std::size_t i = 0; i < node.children.size(); i += 2) {
    bool known = false;
    for (const auto name : allowed) { if (node.children[i].bytes == name) { known = true; break; } }
    if (!known) { fail(Error::Unsupported); }
  }
}
const Node& required(const Node& node, std::string_view name, Node::Type type) {
  const auto* result = node.find(name);
  if (!result || result->type != type) { fail(Error::Invalid); }
  return *result;
}
bool boolean(const Node& node, std::string_view name) {
  const auto value = required(node, name, Node::Type::Integer).integer;
  if (value != 0 && value != 1) { fail(Error::Invalid); }
  return value == 1;
}
void validatePane(const ClassicPanePreferences& pane) {
  std::array<bool, 13> seen{};
  for (const auto& value : pane.order) {
    const auto index = id(value);
    if (!index || seen[*index]) { fail(Error::Invalid); }
    seen[*index] = true;
  }
  if (!id(pane.sortId) || pane.columns[0].width) { fail(Error::Invalid); }
  for (std::size_t i = 1; i < pane.columns.size(); ++i) {
    const auto width = pane.columns[i].width;
    if (!width || *width < 16 || *width > 8192) { fail(Error::Invalid); }
  }
}
ClassicPanePreferences pane(const Node& node) {
  fields(node, {"columns", "order", "sort"});
  const auto& columns = required(node, "columns", Node::Type::Dictionary);
  ClassicPanePreferences result;
  for (std::size_t i = 0; i < columns.children.size(); i += 2) {
    if (!id(columns.children[i].bytes)) { fail(Error::Unsupported); }
  }
  if (columns.children.size() != 2 * ClassicPreferenceIds.size()) { fail(Error::Invalid); }
  for (std::size_t i = 0; i < ClassicPreferenceIds.size(); ++i) {
    const auto& column = required(columns, ClassicPreferenceIds[i], Node::Type::Dictionary);
    if (i == 0) { fields(column, {"visible"}); }
    else { fields(column, {"visible", "width"}); }
    result.columns[i].visible = boolean(column, "visible");
    if (i != 0) {
      const auto width = required(column, "width", Node::Type::Integer).integer;
      if (width < 16 || width > 8192) { fail(Error::Invalid); }
      result.columns[i].width = static_cast<int>(width);
    }
  }
  const auto& order = required(node, "order", Node::Type::List);
  if (order.children.size() != ClassicPreferenceIds.size()) { fail(Error::Invalid); }
  for (std::size_t i = 0; i < order.children.size(); ++i) {
    if (order.children[i].type != Node::Type::Bytes) { fail(Error::Invalid); }
    result.order[i] = order.children[i].bytes;
  }
  const auto& sort = required(node, "sort", Node::Type::Dictionary);
  fields(sort, {"id", "ascending"});
  result.sortId = required(sort, "id", Node::Type::Bytes).bytes;
  result.ascending = boolean(sort, "ascending");
  validatePane(result);
  return result;
}
std::optional<ClassicPreferences> read(const Document& document, Shape* shape = nullptr) {
  const Node* value = nullptr;
  for (std::size_t i = 0; i < document.root().children.size(); i += 2) {
    const auto key = document.root().children[i].bytes;
    if (!key.starts_with(Namespace)) { continue; }
    if (key != Key) { fail(Error::Unsupported); }
    value = &document.root().children[i + 1];
  }
  if (!value) { return std::nullopt; }
  const auto measured = privateShape(*value);
  if (shape) { *shape = measured; }
  fields(*value, {"version", "incomplete", "complete"});
  if (required(*value, "version", Node::Type::Integer).integer != 1) { fail(Error::Unsupported); }
  return ClassicPreferences{pane(required(*value, "incomplete", Node::Type::Dictionary)),
                            pane(required(*value, "complete", Node::Type::Dictionary))};
}

void bytes(std::string& out, std::string_view value) { out += std::to_string(value.size()); out += ':'; out += value; }
void integer(std::string& out, int value) { out += 'i'; out += std::to_string(value); out += 'e'; }
void encodePane(std::string& out, const ClassicPanePreferences& pane) {
  out += 'd'; bytes(out, "columns"); out += 'd';
  for (std::size_t i = 0; i < ClassicPreferenceIds.size(); ++i) {
    bytes(out, ClassicPreferenceIds[i]); out += 'd'; bytes(out, "visible"); integer(out, pane.columns[i].visible ? 1 : 0);
    if (i != 0) { bytes(out, "width"); integer(out, *pane.columns[i].width); }
    out += 'e';
  }
  out += 'e'; bytes(out, "order"); out += 'l';
  for (const auto& value : pane.order) { bytes(out, value); }
  out += 'e'; bytes(out, "sort"); out += 'd'; bytes(out, "id"); bytes(out, pane.sortId);
  bytes(out, "ascending"); integer(out, pane.ascending ? 1 : 0); out += "ee";
}
std::string encode(const ClassicPreferences& prefs) {
  std::string out = "d"; bytes(out, "version"); integer(out, 1);
  bytes(out, "incomplete"); encodePane(out, prefs.incomplete);
  bytes(out, "complete"); encodePane(out, prefs.complete); out += 'e';
  return out;
}
} // namespace

std::expected<void, ClassicPreferenceError> validateClassicPreferences(const ClassicPreferences& prefs) {
  try { validatePane(prefs.incomplete); validatePane(prefs.complete); return {}; }
  catch (const Failure& failure) { return std::unexpected(failure.error); }
  catch (const std::bad_alloc&) { return std::unexpected(Error::Memory); }
}
std::expected<std::optional<ClassicPreferences>, ClassicPreferenceError> readClassicPreferences(const Document& document) {
  try {
    Shape shape;
    const auto bounded = checked(document, shape);
    return read(bounded);
  } catch (const Failure& failure) { return std::unexpected(failure.error); }
  catch (const std::bad_alloc&) { return std::unexpected(Error::Memory); }
}
std::expected<void, ClassicPreferenceError> writeClassicPreferences(Document& document, const ClassicPreferences& prefs) {
  try {
    Shape whole;
    const auto bounded = checked(document, whole);
    Shape oldShape;
    const auto existing = read(bounded, &oldShape); // Unknown private data never authorizes overwrite.
    validatePane(prefs.incomplete); validatePane(prefs.complete);
    auto encoded = encode(prefs);
    auto value = Document::parse(encoded, PrivateLimits);
    if (!value) { fail(Error::Limit); }
    const auto newShape = privateShape(value->root());
    const auto* oldValue = bounded.find(Key);
    const auto oldBytes = oldValue ? oldValue->end - oldValue->begin : 0;
    const auto baseBytes = bounded.originalBytes().size() - oldBytes;
    const auto keyBytes = oldValue ? 0 : std::to_string(Key.size()).size() + 1 + Key.size();
    const auto baseNodes = whole.nodes - (existing ? oldShape.nodes : 0);
    const auto addedNodes = newShape.nodes + (oldValue ? 0 : 1);
    if (keyBytes > WholeLimits.inputBytes - baseBytes
        || encoded.size() > WholeLimits.inputBytes - baseBytes - keyBytes
        || addedNodes > WholeLimits.nodes - baseNodes || newShape.depth + 1 > WholeLimits.depth) { fail(Error::Limit); }
    Document candidate = document; // Preserve lower inherited edit quotas too.
    const auto result = candidate.replaceValue(Key, std::move(encoded));
    if (!result) { fail(result.error() == EditError::Limit ? Error::Limit : Error::Invalid); }
    static_assert(std::is_nothrow_move_assignable_v<Document>);
    document = std::move(candidate); // Only publication; all fallible work is over.
    return {};
  } catch (const Failure& failure) { return std::unexpected(failure.error); }
  catch (const std::bad_alloc&) { return std::unexpected(Error::Memory); }
}
} // namespace biglybtc::core
