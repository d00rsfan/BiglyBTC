#include "biglybtc/bencode.hpp"

#include <charconv>
#include <algorithm>
#include <unordered_set>
#include <utility>

namespace biglybtc::core {
namespace {

class Parser {
public:
  Parser(std::string_view input, Limits limits) : input_(input), limits_(limits) {}

  std::expected<Node, ParseError> parse(std::size_t depth = 0) {
    if (depth > limits_.depth) {
      return fail("depth_limit");
    }
    if (nodes_ == limits_.nodes) {
      return fail("node_limit");
    }
    ++nodes_;
    if (position_ == input_.size()) {
      return fail("unexpected_end");
    }
    const auto start = position_;
    const char marker = input_[position_];
    if (marker == 'i') {
      ++position_;
      const auto end = input_.find('e', position_);
      if (end == std::string_view::npos) {
        return fail("unterminated_integer");
      }
      const auto text = input_.substr(position_, end - position_);
      if (text.empty() || text == "-0" || (text.size() > 1 && text.front() == '0')
          || (text.size() > 2 && text[0] == '-' && text[1] == '0')) {
        return fail("noncanonical_integer");
      }
      Node result{Node::Type::Integer};
      const auto conversion = std::from_chars(text.data(), text.data() + text.size(), result.integer);
      if (conversion.ec != std::errc{} || conversion.ptr != text.data() + text.size()) {
        return fail("invalid_integer");
      }
      position_ = end + 1;
      result.begin = start;
      result.end = position_;
      return result;
    }
    if (marker >= '0' && marker <= '9') {
      const auto colon = input_.find(':', position_);
      if (colon == std::string_view::npos) {
        return fail("missing_string_length");
      }
      const auto text = input_.substr(position_, colon - position_);
      if (text.size() > 1 && text.front() == '0') {
        return fail("noncanonical_length");
      }
      std::size_t length = 0;
      const auto conversion = std::from_chars(text.data(), text.data() + text.size(), length);
      if (conversion.ec != std::errc{} || conversion.ptr != text.data() + text.size()) {
        return fail("invalid_string_length");
      }
      if (length > limits_.stringBytes) {
        return fail("string_limit");
      }
      position_ = colon + 1;
      if (length > input_.size() - position_) {
        return fail("truncated_string");
      }
      Node result{Node::Type::Bytes};
      result.bytes = input_.substr(position_, length);
      position_ += length;
      result.begin = start;
      result.end = position_;
      return result;
    }
    if (marker != 'l' && marker != 'd') {
      return fail("invalid_marker");
    }
    Node result{marker == 'l' ? Node::Type::List : Node::Type::Dictionary};
    ++position_;
    std::unordered_set<std::string_view> keys;
    while (position_ < input_.size() && input_[position_] != 'e') {
      auto child = parse(depth + 1);
      if (!child) {
        return std::unexpected(child.error());
      }
      if (marker == 'd') {
        if (child->type != Node::Type::Bytes) {
          return fail("non_string_key");
        }
        if (!keys.insert(child->bytes).second) {
          return fail("duplicate_key");
        }
        result.children.push_back(std::move(*child));
        child = parse(depth + 1);
        if (!child) {
          return std::unexpected(child.error());
        }
      }
      result.children.push_back(std::move(*child));
    }
    if (position_ == input_.size()) {
      return fail("unterminated_container");
    }
    ++position_;
    result.begin = start;
    result.end = position_;
    return result;
  }

  std::size_t position() const { return position_; }
private:
  std::unexpected<ParseError> fail(std::string code) const {
    return std::unexpected(ParseError{std::move(code), position_});
  }
  std::string_view input_;
  Limits limits_;
  std::size_t position_ = 0;
  std::size_t nodes_ = 0;
};

std::size_t encodedSize(const Node& node) {
  if (!node.changed) { return node.end - node.begin; }
  if (node.type == Node::Type::Integer) { return std::to_string(node.integer).size() + 2; }
  std::size_t total = 2;
  for (const auto& child : node.children) { total += encodedSize(child); }
  return total;
}

std::size_t nodeCount(const Node& node) {
  std::size_t total = 1;
  for (const auto& child : node.children) { total += nodeCount(child); }
  return total;
}

std::size_t treeDepth(const Node& node) {
  std::size_t maximum = 0;
  for (const auto& child : node.children) { maximum = std::max(maximum, 1 + treeDepth(child)); }
  return maximum;
}

class ValueEncoder final {
public:
  explicit ValueEncoder(Limits limits) : limits_(limits) {}
  std::expected<void, ParseError> encode(const Node& node, std::size_t depth = 0) {
    if (depth > limits_.depth || nodes_ == limits_.nodes) { return fail("encode_structure_limit"); }
    ++nodes_;
    if (node.type == Node::Type::Integer) {
      return append("i" + std::to_string(node.integer) + "e");
    }
    if (node.type == Node::Type::Bytes) {
      if (node.bytes.size() > limits_.stringBytes) { return fail("encode_string_limit"); }
      auto prefix = append(std::to_string(node.bytes.size()) + ":");
      if (!prefix) { return prefix; }
      return append(node.bytes);
    }
    const bool dictionary = node.type == Node::Type::Dictionary;
    if (!dictionary && node.type != Node::Type::List) { return fail("encode_invalid_type"); }
    if (dictionary && node.children.size() % 2 != 0) { return fail("encode_invalid_dictionary"); }
    auto marker = append(dictionary ? "d" : "l");
    if (!marker) { return marker; }
    std::unordered_set<std::string_view> keys;
    for (std::size_t i = 0; i < node.children.size(); ++i) {
      const auto& child = node.children[i];
      if (dictionary && i % 2 == 0
          && (child.type != Node::Type::Bytes || !keys.insert(child.bytes).second)) {
        return fail("encode_invalid_key");
      }
      auto encoded = encode(child, depth + 1);
      if (!encoded) { return encoded; }
    }
    return append("e");
  }
  std::string take() { return std::move(output_); }
private:
  std::unexpected<ParseError> fail(std::string code) const {
    return std::unexpected(ParseError{std::move(code), output_.size()});
  }
  std::expected<void, ParseError> append(std::string_view value) {
    if (value.size() > limits_.inputBytes - output_.size()) { return fail("encode_input_limit"); }
    output_.append(value);
    return {};
  }
  Limits limits_;
  std::size_t nodes_ = 0;
  std::string output_;
};

} // namespace

const Node* Node::find(std::string_view key) const {
  if (type != Type::Dictionary) { return nullptr; }
  for (std::size_t i = 0; i + 1 < children.size(); i += 2) {
    if (children[i].type == Type::Bytes && children[i].bytes == key) { return &children[i + 1]; }
  }
  return nullptr;
}

std::expected<std::string, ParseError> encodeValue(const Node& node, Limits limits) {
  ValueEncoder encoder(limits);
  const auto result = encoder.encode(node);
  if (!result) { return std::unexpected(result.error()); }
  return encoder.take();
}

std::expected<Document, ParseError> Document::parse(std::string input, Limits limits) {
  if (input.size() > limits.inputBytes) {
    return std::unexpected(ParseError{"input_limit", 0});
  }
  auto original = std::make_shared<const std::string>(std::move(input));
  Parser parser(*original, limits);
  auto parsed = parser.parse();
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  if (parser.position() != original->size()) {
    return std::unexpected(ParseError{"trailing_data", parser.position()});
  }
  return Document(std::move(original), std::move(*parsed), limits);
}

const Node* Document::find(std::string_view key) const {
  return root_.find(key);
}

bool Document::setInteger(std::string_view key, std::int64_t value) {
  if (root_.type != Node::Type::Dictionary) {
    return false;
  }
  for (std::size_t i = 0; i < root_.children.size(); i += 2) {
    if (root_.children[i].bytes == key) {
      auto& existing = root_.children[i + 1];
      if (existing.type != Node::Type::Integer) {
        return false;
      }
      if (existing.integer != value) {
        const auto total = encodedSize(root_);
        const auto base = total - encodedSize(existing);
        const auto replacement = std::to_string(value).size() + 2;
        if (base > limits_.inputBytes || replacement > limits_.inputBytes - base) { return false; }
        existing.integer = value;
        existing.changed = true;
        root_.changed = true;
      }
      return true;
    }
  }
  return false;
}

std::expected<void, EditError> Document::replaceValue(std::string_view key, std::string encoded) {
  if (root_.type != Node::Type::Dictionary) { return std::unexpected(EditError::NotDictionary); }
  Node* existing = nullptr;
  for (std::size_t i = 0; i < root_.children.size(); i += 2) {
    if (root_.children[i].bytes == key) { existing = &root_.children[i + 1]; break; }
  }
  if (existing && !existing->changed) {
    const auto& source = existing->origin ? *existing->origin : *original_;
    if (std::string_view(source).substr(existing->begin, existing->end - existing->begin) == encoded) {
      return {};
    }
  }
  auto value = Document::parse(std::move(encoded), limits_);
  if (!value) {
    return std::unexpected(value.error().code.ends_with("_limit") ? EditError::Limit : EditError::InvalidValue);
  }
  if (limits_.depth == 0 || treeDepth(value->root_) >= limits_.depth || key.size() > limits_.stringBytes) {
    return std::unexpected(EditError::Limit);
  }
  const auto prefix = std::to_string(key.size()) + ":";
  const auto count = nodeCount(root_) - (existing ? nodeCount(*existing) : 0);
  const auto added = nodeCount(value->root_) + (existing ? 0 : 1);
  const auto base = encodedSize(root_) - (existing ? encodedSize(*existing) : 0);
  if (count > limits_.nodes || added > limits_.nodes - count || base > limits_.inputBytes
      || (!existing && (key.size() > limits_.inputBytes || prefix.size() > limits_.inputBytes - key.size()))) {
    return std::unexpected(EditError::Limit);
  }
  const auto keySize = existing ? 0 : prefix.size() + key.size();
  if (keySize > limits_.inputBytes - base || value->original_->size() > limits_.inputBytes - base - keySize) {
    return std::unexpected(EditError::Limit);
  }
  value->root_.origin = value->original_;
  if (existing) {
    *existing = std::move(value->root_);
  } else {
    auto keyDocument = Document::parse(prefix + std::string(key), limits_);
    if (!keyDocument) { return std::unexpected(EditError::InvalidValue); }
    keyDocument->root_.origin = keyDocument->original_;
    // Reserve before either append so allocation failure leaves an even, intact
    // dictionary. Node's moves are noexcept with these standard containers.
    root_.children.reserve(root_.children.size() + 2);
    root_.children.push_back(std::move(keyDocument->root_));
    root_.children.push_back(std::move(value->root_));
  }
  root_.changed = true;
  return {};
}

void Document::encode(const Node& node, std::string& output, const std::string& source) const {
  const auto& origin = node.origin ? *node.origin : source;
  if (!node.changed) {
    output.append(origin, node.begin, node.end - node.begin);
    return;
  }
  if (node.type == Node::Type::Integer) {
    output += 'i';
    output += std::to_string(node.integer);
    output += 'e';
    return;
  }
  output += node.type == Node::Type::Dictionary ? 'd' : 'l';
  for (const auto& child : node.children) {
    encode(child, output, origin);
  }
  output += 'e';
}

std::string Document::serialize() const {
  std::string output;
  output.reserve(encodedSize(root_));
  encode(root_, output, *original_);
  return output;
}

} // namespace biglybtc::core
