#include "biglybtc/overall_document.hpp"

#include <new>
#include <string>
#include <utility>

namespace biglybtc::core {
namespace {
struct Failure { OverallDocumentError error; };
void validateLimits(const Limits& limits) {
  constexpr Limits maximum;
  if (limits.inputBytes > maximum.inputBytes || limits.stringBytes > maximum.stringBytes
      || limits.nodes > maximum.nodes || limits.depth > maximum.depth) {
    throw Failure{OverallDocumentError::Limit};
  }
}
bool limitError(const ParseError& error) {
  return error.code == "input_limit" || error.code == "string_limit"
      || error.code == "node_limit" || error.code == "depth_limit";
}
OverallDocument parse(std::string_view bytes, const Limits& limits) {
  validateLimits(limits);
  if (bytes.size() > limits.inputBytes) { throw Failure{OverallDocumentError::Limit}; }
  auto parsed = Document::parse(std::string(bytes), limits);
  if (!parsed) {
    throw Failure{limitError(parsed.error()) ? OverallDocumentError::Limit : OverallDocumentError::InvalidDocument};
  }
  if (parsed->root().type != Node::Type::Dictionary) { throw Failure{OverallDocumentError::InvalidDocument}; }
  const auto* all = parsed->find("all");
  if (all && all->type != Node::Type::Dictionary) { throw Failure{OverallDocumentError::InvalidAll}; }
  OverallDocument result;
  result.hasAll = all != nullptr;
  if (all) {
    for (std::size_t i = 0; i < SavedStatisticKeys.size(); ++i) {
      if (const auto* field = all->find(SavedStatisticKeys[i])) {
        if (field->type == Node::Type::Integer) {
          result.values[i] = field->integer;
          result.presence[i] = OverallFieldPresence::Integer;
        } else { result.presence[i] = OverallFieldPresence::WrongType; }
      }
    }
  }
  result.source = std::make_shared<const Document>(std::move(*parsed));
  return result;
}
void append(std::string& output, std::string_view value, const Limits& limits) {
  if (value.size() > limits.inputBytes - output.size()) { throw Failure{OverallDocumentError::Limit}; }
  output.append(value);
}
void raw(std::string& output, const Document& document, const Node& node, const Limits& limits) {
  // Nodes come exclusively from the fresh bounded parser above. No public tree
  // supplied by a caller or arbitrary byte view is traversed by this codec.
  append(output, std::string_view(document.originalBytes()).substr(node.begin, node.end - node.begin), limits);
}
std::optional<std::size_t> fieldIndex(std::string_view key) {
  for (std::size_t i = 0; i < SavedStatisticKeys.size(); ++i) {
    if (key == SavedStatisticKeys[i]) { return i; }
  }
  return std::nullopt;
}
void integer(std::string& output, std::int64_t value, const Limits& limits) {
  append(output, "i", limits);
  append(output, std::to_string(value), limits);
  append(output, "e", limits);
}
} // namespace

std::expected<OverallDocument, OverallDocumentError> readOverallDocument(std::string_view bytes, Limits limits) {
  try { return parse(bytes, limits); }
  catch (const Failure& failure) { return std::unexpected(failure.error); }
  catch (const std::bad_alloc&) { return std::unexpected(OverallDocumentError::Memory); }
}

std::expected<OverallDocument, OverallDocumentError> prepareOverallDocument(
    std::string_view pristineBytes, const OverallValues& values, Limits limits) {
  try {
    const auto baseline = parse(pristineBytes, limits);
    const auto& document = *baseline.source;
    const auto* all = document.find("all");
    std::string replacement;
    append(replacement, "d", limits);
    std::array<bool, SavedStatisticKeys.size()> seen{};
    if (all) {
      for (std::size_t i = 0; i < all->children.size(); i += 2) {
        const auto& key = all->children[i];
        const auto& value = all->children[i + 1];
        raw(replacement, document, key, limits);
        if (const auto index = fieldIndex(key.bytes)) {
          integer(replacement, values[*index], limits);
          seen[*index] = true;
        } else { raw(replacement, document, value, limits); }
      }
    }
    for (std::size_t i = 0; i < seen.size(); ++i) {
      if (seen[i]) { continue; }
      append(replacement, std::to_string(SavedStatisticKeys[i].size()), limits);
      append(replacement, ":", limits);
      append(replacement, SavedStatisticKeys[i], limits);
      integer(replacement, values[i], limits);
    }
    append(replacement, "e", limits);
    std::string encoded;
    append(encoded, "d", limits);
    const auto& root = document.root();
    for (std::size_t i = 0; i < root.children.size(); i += 2) {
      raw(encoded, document, root.children[i], limits);
      if (root.children[i].bytes == "all") { append(encoded, replacement, limits); }
      else { raw(encoded, document, root.children[i + 1], limits); }
    }
    if (!all) {
      append(encoded, "3:all", limits);
      append(encoded, replacement, limits);
    }
    append(encoded, "e", limits);
    // Recheck whole result, including appended key count and increased depth.
    // No publication occurs here, so failure leaves the input wholly unchanged.
    return parse(encoded, limits);
  } catch (const Failure& failure) { return std::unexpected(failure.error); }
  catch (const std::bad_alloc&) { return std::unexpected(OverallDocumentError::Memory); }
}
} // namespace biglybtc::core
