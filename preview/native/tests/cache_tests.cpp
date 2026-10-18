#include "biglybtc/state_cache.hpp"

#include <cstdlib>
#include <iostream>

namespace {
using namespace biglybtc::core;
void check(bool value, const char* description) {
  if (!value) { std::cerr << description << '\n'; std::exit(1); }
}
std::string bytes(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
Document document(std::string input) {
  auto value = Document::parse(std::move(input));
  check(value.has_value(), "Test input must be valid bencoding");
  return std::move(*value);
}
std::string entry(std::string_view identity, std::string_view name, int size) {
  return "d4:hash" + bytes(identity) + "4:name" + bytes(name) + "4:sizei" + std::to_string(size) + "ee";
}
StateCache owned() {
  auto source = document("d5:statel" + entry(std::string(20, '\0'), "owned", 9) + "ee");
  auto result = readStateCache(source);
  check(result.has_value(), "Owned snapshot read");
  return std::move(*result);
}
}

int main() {
  const std::string id(20, '\xff');
  const auto identity = TorrentHash::parse(id)->hex();
  const auto original = "d7:unknown4:" + std::string("\0\xff\x80\1", 4)
      + "5:statel" + entry(id, "old", 1) + entry(id, "new", 200) + "ee";
  auto source = document(original);
  auto value = readStateCache(source);
  check(value && value->issues.empty() && value->torrents.size() == 1, "Duplicate identity keeps exactly one cache view");
  check(value->torrents.at(identity).name == "new" && value->torrents.at(identity).bytes == 200,
      "Original Java last cache entry wins");
  check(source.serialize() == original, "Read preserves unknown bytes and original key order");
  check(!readStateCache(source, 1), "Cache list entry count is bounded before coalescing");
  value = readStateCache(source, 2, 2);
  check(value && value->torrents.empty() && value->issues.size() == 2, "Text allocation bounds report indexed entry issues");
  auto future = document("d5:stateld4:hash" + bytes(id) + "8:utf8name0:4:sizei0e2:fci1e2:tti3e4:privi-1e10:attributesd11:displayname7:renamedeeee");
  value = readStateCache(future);
  check(value && value->issues.empty(), "Actual cached optional field shapes");
  const auto& info = value->torrents.at(identity);
  check(info.utf8Name == "" && info.displayName == "renamed" && info.files == 1 && info.type == 3
      && info.bytes == 0 && info.privateTorrent, "Original empty UTF8/custom name/integer/private semantics");
  auto malformed = document("d5:stateli7ed4:hash3:baded4:hash" + bytes(id) + "4:sizei-1eeee");
  value = readStateCache(malformed);
  check(value && value->torrents.empty() && value->issues.size() == 3
      && value->issues[2].sourceIndex == 2 && value->issues[2].error == CacheError::Integer,
      "Malformed entries stay indexed and cannot become valid rows");
  check(!readStateCache(document("le")), "Root must be a dictionary");
  check(!readStateCache(document("d5:statei1ee")), "State collection must be a list");
  check(readStateCache(document("de"))->torrents.empty(), "Missing optional cache list is empty");
  const auto snapshot = owned();
  check(snapshot.torrents.begin()->second.name == "owned", "Snapshot outlives its original document");
  std::cout << "PASS bounded original cache fields, duplicates, unknown bytes, malformed entries and owned lifetimes\n";
}
