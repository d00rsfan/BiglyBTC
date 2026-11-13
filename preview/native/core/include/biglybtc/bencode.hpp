#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace biglybtc::core {

struct Limits {
  std::size_t inputBytes = 4 * 1024 * 1024;
  std::size_t stringBytes = 4 * 1024 * 1024;
  std::size_t nodes = 100000;
  std::size_t depth = 64;
};

struct ParseError {
  std::string code;
  std::size_t offset;
};

enum class EditError { NotDictionary, InvalidValue, Limit };

struct Node {
  enum class Type { Integer, Bytes, List, Dictionary } type;
  explicit Node(Type value) : type(value) {}
  std::int64_t integer = 0;
  // Borrowed from the owning Document's immutable backing buffer.
  std::string_view bytes;
  // Dictionary children alternate byte-string key and value; order is retained.
  std::vector<Node> children;
  std::size_t begin = 0;
  std::size_t end = 0;
  bool changed = false;
  // A root replacement owns a separate immutable source for its whole subtree.
  std::shared_ptr<const std::string> origin;
  const Node* find(std::string_view key) const;
};

std::expected<std::string, ParseError> encodeValue(const Node& node, Limits limits = {});

class Document final {
public:
  static std::expected<Document, ParseError> parse(std::string input, Limits limits = {});
  const Node& root() const { return *root_; }
  const std::string& originalBytes() const { return *original_; }
  const Node* find(std::string_view key) const;
  bool setInteger(std::string_view key, std::int64_t value);
  std::expected<void, EditError> replaceValue(std::string_view key, std::string encoded);
  // Keep the immutable source bytes for ConfigFile compare-and-save.
  std::expected<bool, EditError> eraseValue(std::string_view key);
  std::string serialize() const;
  // Parsed snapshots share immutable nodes. Edits detach before mutation;
  // borrowed strings continue to own their original immutable backing bytes.
  std::size_t storageBytes() const noexcept;
private:
  Document(std::shared_ptr<const std::string> original, Node root, Limits limits)
      : original_(std::move(original)), root_(std::make_shared<Node>(std::move(root))), limits_(limits) {}
  std::shared_ptr<const std::string> original_;
  std::shared_ptr<Node> root_;
  void detachRoot();
  Limits limits_;
  void encode(const Node& node, std::string& output, const std::string& source) const;
};

} // namespace biglybtc::core
