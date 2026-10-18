#include "biglybtc/saved_services.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
using namespace biglybtc::core;
std::size_t assertions = 0;
void require(bool condition, std::string_view message) {
  ++assertions;
  if (!condition) { throw std::runtime_error(std::string(message)); }
}
std::string bytes(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
std::string integer(std::int64_t value) { return "i" + std::to_string(value) + 'e'; }
std::string map(std::initializer_list<std::pair<std::string_view, std::string>> fields) {
  std::string value = "d";
  for (const auto& [key, field] : fields) { value += bytes(key) + field; }
  return value + 'e';
}
std::string hostingInput(std::string entry) { return map({{"torrents", "l" + entry + 'e'}}); }
std::string sharingInput(std::string entry) { return map({{"resources", "l" + entry + 'e'}}); }
Document parse(std::string input) {
  auto document = Document::parse(std::move(input));
  require(document.has_value(), "synthetic fixture is not valid bencode");
  return std::move(*document);
}
SavedHosting hosting(std::string input) {
  auto document = parse(std::move(input));
  auto result = readSavedHosting(document);
  require(result.has_value(), "hosting projection failed");
  return std::move(*result);
}
SavedShares shares(std::string input) {
  auto document = parse(std::move(input));
  auto result = readSavedShares(document);
  require(result.has_value(), "sharing projection failed");
  return std::move(*result);
}
std::string fixture(const std::string& directory, std::string_view name) {
  // Only test fixture names supplied by this source are opened. No serialized
  // path, ifile, torrent_file or per_key is passed to any filesystem API.
  std::ifstream file(directory + '/' + std::string(name), std::ios::binary);
  require(file.is_open(), "explicit synthetic fixture file is missing");
  std::string data{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  require(!file.bad(), "synthetic fixture read failed");
  return data;
}
template<class Snapshot>
bool issue(const Snapshot& snapshot, SavedIssueCode code, std::size_t row, SavedField field) {
  return std::any_of(snapshot.issues.begin(), snapshot.issues.end(), [&](const auto& item) {
    return item.code == code && item.sourceIndex == row && item.field == field;
  });
}
const Node& entry(const Document& source, std::string_view key, std::size_t index) {
  const auto* list = source.find(key);
  require(list && list->type == Node::Type::List && index < list->children.size(), "retained source list/index mismatch");
  return list->children[index];
}
std::string minimalHost(std::int64_t status = 1) {
  return hostingInput(map({{"hash", bytes(std::string(20, 'H'))}, {"status", integer(status)}}));
}
std::string minimalShare() {
  return sharingInput(map({{"type", "i3e"}, {"file", "3:abc"}, {"recursive", "i0e"}}));
}
void limitedHosting(const Document& document, SavedServiceLimits limits, bool accepted) {
  auto result = readSavedHosting(document, limits);
  require(result.has_value() == accepted, "hosting limit acceptance differs");
  if (!accepted) { require(result.error() == SavedServiceError::Limit, "hosting limit error differs"); }
}
void limitedShares(const Document& document, SavedServiceLimits limits, bool accepted) {
  auto result = readSavedShares(document, limits);
  require(result.has_value() == accepted, "sharing limit acceptance differs");
  if (!accepted) { require(result.error() == SavedServiceError::Limit, "sharing limit error differs"); }
}
} // namespace

int main(int argc, char* argv[]) {
  std::string_view active = "arguments";
  std::size_t cases = 0;
  try {
    require(argc == 2, "supply only the explicit saved-services fixture directory");
    const std::string directory = argv[1];
    const auto trackerRaw = fixture(directory, "tracker-mixed.benc");
    const auto sharingRaw = fixture(directory, "sharing-mixed.benc");
    const auto run = [&](std::string_view name, auto body) { active = name; body(); ++cases; };

    run("missing-versus-empty", [&] {
      auto absent = hosting(fixture(directory, "missing-lists.benc"));
      auto empty = hosting(fixture(directory, "tracker-empty.benc"));
      require(!absent.listPresent && absent.records.empty(), "missing tracker list became present");
      require(issue(absent, SavedIssueCode::Missing, SavedRootIndex, SavedField::Records), "missing tracker list has no root diagnostic");
      require(empty.listPresent && empty.records.empty() && empty.issues.empty(), "empty tracker list is not a valid empty snapshot");
      auto absentShares = shares(fixture(directory, "missing-lists.benc"));
      auto emptyShares = shares(fixture(directory, "sharing-empty.benc"));
      require(!absentShares.listPresent && absentShares.records.empty(), "missing resources became present");
      require(issue(absentShares, SavedIssueCode::Missing, SavedRootIndex, SavedField::Records), "missing resources has no root diagnostic");
      require(emptyShares.listPresent && emptyShares.records.empty() && emptyShares.issues.empty(), "empty resources is not a valid empty snapshot");
    });

    run("persistent-hosts-versus-stat-backups", [&] {
      auto saved = hosting(trackerRaw);
      require(saved.records.size() == 5 && saved.listPresent, "tracker list slots were lost");
      for (std::size_t i = 0; i < saved.records.size(); ++i) { require(saved.records[i].sourceIndex == i, "tracker source index changed"); }
      const auto& first = saved.records[0];
      require(first.persistent == true && first.passive == false, "missing tracker flag defaults differ");
      require(first.disposition == SavedHostingDisposition::PersistentHost, "persistent host was misclassified");
      require(first.rawStatus == 0 && first.status == 0, "saved Failed0 was normalized like live recovery");
      require(!first.dateAdded && !first.backupTime && !first.stats, "missing saved metadata was fabricated");
      const auto& second = saved.records[1];
      require(second.rawStatus == 4294967298LL && second.status == 2, "tracker status low32 was lost");
      require(second.persistent == true && second.passive == true, "explicit tracker flags differ");
      require(second.dateAdded == 1700000000000LL, "saved date units/value changed");
      require(second.stats.has_value(), "saved statistics disappeared");
      require(second.stats->completed == 7 && second.stats->announces == 8, "saved statistic values differ");
      require(second.stats->uploaded == 1099511627776LL && second.stats->downloaded == -9, "signed full-width counters changed");
      require(!second.stats->scrapes && !second.stats->bytesIn && !second.stats->bytesOut, "optional saved counters were invented");
      const auto& backup = saved.records[2];
      require(backup.persistent == false && backup.disposition == SavedHostingDisposition::StatsBackup, "nonpersistent record became a hosted torrent");
      require(!backup.status && !backup.rawStatus, "backup status was fabricated");
      require(!issue(saved, SavedIssueCode::Missing, 2, SavedField::Status), "backup incorrectly requires host status");
      require(backup.backupTime == 1700000000001LL, "backup timestamp changed");
      require(backup.stats && backup.stats->scrapes == 11 && backup.stats->bytesIn == 12 && backup.stats->bytesOut == 13, "optional saved counters disappeared");
      const auto& wide = saved.records[3];
      require(wide.persistent == false && wide.passive == false, "Long flags were narrowed before equality to1");
      require(wide.disposition == SavedHostingDisposition::StatsBackup, "wide false flag became a live host");
      require(wide.rawStatus == -4294967295LL && wide.status == 1, "negative Long status narrowing differs");
      require(wide.backupTime == std::numeric_limits<std::int64_t>::min(), "minimum saved timestamp changed");
      require(wide.duplicateIdentity && issue(saved, SavedIssueCode::DuplicateIdentity, 3, SavedField::Hash), "duplicate tracker hash was not retained/diagnosed");
      const auto& unknown = saved.records[4];
      require(unknown.rawStatus == std::numeric_limits<std::int64_t>::max() && unknown.status == -1, "maximum Long status narrowing differs");
      require(issue(saved, SavedIssueCode::Unknown, 4, SavedField::Status), "unknown saved status was treated as known");
      require(unknown.hash && unknown.hash->size() == 32, "32-byte tracker identity changed");
      require(saved.source && saved.source->serialize() == trackerRaw, "tracker raw source changed");
    });

    run("full-width-tracker-booleans", [&] {
      for (const auto value : {std::int64_t{-1}, std::int64_t{0}, std::int64_t{1}, std::int64_t{2}, std::int64_t{4294967297LL}, std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max()}) {
        auto saved = hosting(hostingInput(map({{"hash", bytes(std::string(20, 'B'))}, {"status", "i1e"}, {"persistent", integer(value)}, {"passive", integer(value)}})));
        require(saved.records[0].persistent == (value == 1) && saved.records[0].passive == (value == 1), "tracker Long boolean rule differs");
      }
    });

    run("malformed-tracker-rows-preserve-following-slots", [&] {
      auto saved = hosting(fixture(directory, "tracker-malformed-rows.benc"));
      require(saved.records.size() == 4, "malformed tracker list slot was dropped");
      for (std::size_t i = 0; i < saved.records.size(); ++i) { require(saved.records[i].sourceIndex == i, "malformed tracker source index changed"); }
      require(issue(saved, SavedIssueCode::Type, 0, SavedField::Record), "nonmap tracker row lacks a diagnostic");
      for (const auto field : {SavedField::Hash, SavedField::Persistent, SavedField::Passive, SavedField::Status, SavedField::DateAdded, SavedField::Stats}) {
        require(issue(saved, SavedIssueCode::Type, 1, field), "mistyped tracker field lacks a diagnostic");
      }
      require(!saved.records[1].persistent && !saved.records[1].passive && !saved.records[1].disposition, "malformed flag became a default/activation");
      require(saved.records[2].hash == std::string(20, 'V') && saved.records[2].status == 1, "valid tracker row after malformed input was lost");
      require(saved.records[3].stats && saved.records[3].stats->completed == 1, "partial statistics were dropped wholesale");
      for (const auto field : {SavedField::Announces, SavedField::Uploaded, SavedField::Downloaded}) {
        require(issue(saved, SavedIssueCode::Missing, 3, field), "missing required saved-stat field lacks a diagnostic");
      }
    });

    run("share-kinds-and-inert-fingerprints", [&] {
      auto saved = shares(sharingRaw);
      require(saved.records.size() == 6 && saved.listPresent, "sharing list slots were lost");
      for (std::size_t i = 0; i < saved.records.size(); ++i) { require(saved.records[i].sourceIndex == i, "sharing source index changed"); }
      const auto& first = saved.records[0];
      require(first.kind == SavedShareKind::File && first.type == 1, "file resource kind differs");
      require(first.file && first.file->bytes == "/not-opened/文件/ש.bin", "full Unicode share path was reduced or changed");
      require(first.file->encoding == SavedTextEncoding::Utf8 && first.file->validEncoding && !first.file->embeddedNul, "valid Unicode path flags differ");
      require(first.category && first.category->bytes == "Music", "saved category disappeared");
      require(first.persistent == true, "case-insensitive share persistent flag differs");
      require(first.tagUids && first.tagUids->bytes == "7,-2,9223372036854775807", "tag IDs were resolved/coerced");
      std::string fingerprint;
      for (int i = 0; i < 16; ++i) { fingerprint.push_back(static_cast<char>(i)); }
      require(first.fingerprint == fingerprint, "filesystem fingerprint was parsed as torrent hash");
      require(first.cacheFile && first.cacheFile->bytes == "../../cache/$(never-execute).torrent", "inert cache path was resolved/changed");
      const auto& second = saved.records[1];
      require(second.rawType == 4294967298LL && second.type == 2 && second.kind == SavedShareKind::Directory, "resource type low32 differs");
      require(second.persistent == false && !second.recursive, "absent share props or directory recursive state was fabricated");
      require(second.cacheFile && second.cacheFile->bytes == "/not-opened/external.torrent", "absolute cache path was followed/coerced");
      const auto& contents = saved.records[2];
      require(contents.kind == SavedShareKind::DirectoryContents && contents.recursive == true, "directory-contents flags differ");
      require(!contents.fingerprint && !contents.cacheFile, "directory-contents cache metadata was fabricated");
      require(contents.tagUids && contents.tagUids->bytes == "1,not-a-number,-2,1", "raw tag ID text was parsed as names/numbers");
      const auto& duplicate = saved.records[3];
      require(duplicate.duplicateIdentity && duplicate.file && duplicate.file->bytes == first.file->bytes, "duplicate sharing path was collapsed");
      require(issue(saved, SavedIssueCode::DuplicateIdentity, 3, SavedField::File), "duplicate sharing path lacks a diagnostic");
      require(duplicate.persistent == false, "persistent flag was trimmed before comparison");
      const auto& unknown = saved.records[4];
      require(unknown.type == 99 && !unknown.kind, "unknown resource type activated an invented kind");
      require(issue(saved, SavedIssueCode::Unknown, 4, SavedField::ResourceType), "unknown resource kind lacks native diagnostic");
      require(!unknown.recursive, "unknown kind was silently treated as directory contents");
      require(saved.records[5].fingerprint == std::string(20, 'I'), "20-byte fingerprint was converted into torrent identity");
      require(saved.source && saved.source->serialize() == sharingRaw, "sharing raw source changed");
    });

    run("share-boolean-rules", [&] {
      for (const auto value : {std::int64_t{-1}, std::int64_t{0}, std::int64_t{1}, std::int64_t{2}, std::int64_t{4294967297LL}}) {
        auto saved = shares(sharingInput(map({{"type", "i3e"}, {"file", "3:raw"}, {"recursive", integer(value)}})));
        require(saved.records[0].recursive == (value == 1), "recursive Long was narrowed or treated as nonzero");
      }
      for (const auto& [text, expected] : std::initializer_list<std::pair<std::string_view, bool>>{{"true", true}, {"TRUE", true}, {"TrUe", true}, {"false", false}, {" true ", false}, {"1", false}, {"", false}}) {
        auto saved = shares(sharingInput(map({{"type", "i3e"}, {"file", "3:raw"}, {"recursive", "i0e"}, {"props", map({{"persistent", bytes(text)}})}})));
        require(saved.records[0].persistent == expected, "share persistent string comparison differs");
      }
    });

    run("malformed-share-rows-preserve-following-slots", [&] {
      auto saved = shares(fixture(directory, "sharing-malformed-rows.benc"));
      require(saved.records.size() == 5, "malformed sharing list slot was dropped");
      for (std::size_t i = 0; i < saved.records.size(); ++i) { require(saved.records[i].sourceIndex == i, "malformed sharing source index changed"); }
      require(issue(saved, SavedIssueCode::Type, 0, SavedField::Record), "nonmap sharing row lacks a diagnostic");
      for (const auto field : {SavedField::ResourceType, SavedField::File, SavedField::Attributes, SavedField::Properties}) {
        require(issue(saved, SavedIssueCode::Type, 1, field), "mistyped sharing field lacks a diagnostic");
      }
      require(issue(saved, SavedIssueCode::Missing, 2, SavedField::File), "missing share path lacks a diagnostic");
      require(issue(saved, SavedIssueCode::Type, 2, SavedField::Recursive), "mistyped recursive flag lacks a diagnostic");
      require(issue(saved, SavedIssueCode::Missing, 3, SavedField::Fingerprint), "missing fingerprint lacks a diagnostic");
      require(issue(saved, SavedIssueCode::Missing, 3, SavedField::CacheFile), "missing cache reference lacks a diagnostic");
      require(saved.records[4].kind == SavedShareKind::Directory && saved.records[4].file && saved.records[4].file->bytes == "/not-opened/valid", "valid sharing row after malformed input was lost");
    });

    run("opaque-latin1-utf8-and-nul", [&] {
      auto hosted = hosting(trackerRaw);
      const auto& path = hosted.records[1].torrentFile;
      require(path && path->bytes == std::string("/not-opened/") + std::string("\xff\x80", 2) + ".torrent", "tracker Latin1 bytes changed");
      require(path->encoding == SavedTextEncoding::Latin1 && path->validEncoding && !path->embeddedNul, "Latin1 path was treated as invalid UTF8");
      auto shared = shares(sharingRaw);
      const auto& row = shared.records[5];
      require(row.file && row.file->bytes == std::string("/not-opened/") + std::string("\xff\0", 2) + "../../escape", "binary/NUL share path changed");
      require(!row.file->validEncoding && row.file->embeddedNul, "invalid UTF8/NUL path flags differ");
      require(issue(shared, SavedIssueCode::Encoding, 5, SavedField::File) && issue(shared, SavedIssueCode::Nul, 5, SavedField::File), "binary path lacks separate encoding/NUL diagnostics");
      require(row.category && !row.category->validEncoding && row.category->bytes == std::string("\xff", 1), "binary category was dropped/coerced");
      require(row.tagUids && !row.tagUids->validEncoding, "invalid tag text lost its encoding status");
      require(row.cacheFile && !row.cacheFile->validEncoding && row.cacheFile->embeddedNul, "inert binary cache path flags differ");
      auto nul = hosting(hostingInput(map({{"hash", bytes(std::string(20, 'N'))}, {"status", "i1e"}, {"torrent_file", bytes(std::string("a\0b", 3))}})));
      require(nul.records[0].torrentFile && nul.records[0].torrentFile->validEncoding && nul.records[0].torrentFile->embeddedNul, "Latin1 NUL flag differs");
      require(issue(nul, SavedIssueCode::Nul, 0, SavedField::TorrentFile), "Latin1 NUL lacks a diagnostic");
    });

    run("utf8-scalar-boundaries-native-policy", [&] {
      const std::pair<std::string, bool> values[]{{"ASCII", true}, {std::string("\xc2\x80", 2), true},
          {std::string("\xe0\xa0\x80", 3), true}, {std::string("\xf0\x90\x80\x80", 4), true},
          {std::string("\xf4\x8f\xbf\xbf", 4), true}, {std::string("\xc0\x80", 2), false},
          {std::string("\xed\xa0\x80", 3), false}, {std::string("\xf4\x90\x80\x80", 4), false},
          {std::string("\x80", 1), false}, {std::string("\xf0\x9f\x98", 3), false}, {std::string("a\0b", 3), true}};
      for (const auto& [value, valid] : values) {
        auto saved = shares(sharingInput(map({{"type", "i3e"}, {"file", bytes(value)}, {"recursive", "i0e"}})));
        require(saved.records[0].file && saved.records[0].file->bytes == value, "UTF8 test bytes were rewritten");
        require(saved.records[0].file->validEncoding == valid, "UTF8 scalar validity differs");
      }
    });

    run("raw-unknown-personal-data-and-private-clone", [&] {
      auto document = parse(sharingRaw);
      auto saved = readSavedShares(document);
      require(saved.has_value() && saved->source && saved->source.get() != &document, "snapshot does not privately own its document");
      const auto& first = entry(*saved->source, "resources", 0);
      require(first.find("per_key") && first.find("per_key")->type == Node::Type::Bytes, "opaque personal bytes were lost from raw source");
      const auto* props = first.find("props");
      require(props && props->find("user_data") && props->find("user_data")->find("nested") && props->find("user_data")->find("nested")->integer == 1, "typed unknown properties were lost");
      const auto* attrs = first.find("attributes");
      require(attrs && attrs->find("Future Attribute") && attrs->find("Future Attribute")->type == Node::Type::List, "unknown typed attribute was coerced");
      require(saved->source->serialize() == sharingRaw, "unknown raw fields/order were reencoded");
      require(document.replaceValue("test.caller.edit", "i7e").has_value(), "ordinary caller edit failed");
      require(saved->source->serialize() == sharingRaw && !saved->source->find("test.caller.edit"), "caller edit changed retained source clone");
      auto rejected = readSavedShares(document);
      require(!rejected && rejected.error() == SavedServiceError::EditedDocument, "edited sharing input was projected as raw original");
      auto moved = std::move(*saved);
      require(moved.records.size() == 6 && moved.source->serialize() == sharingRaw, "moved snapshot lost ownership");
      auto hostingDocument = parse(trackerRaw);
      auto hosted = readSavedHosting(hostingDocument);
      require(hosted.has_value(), "hosting clone failed");
      require(hostingDocument.replaceValue("test.caller.edit", "i8e").has_value(), "ordinary hosting caller edit failed");
      require(hosted->source->serialize() == trackerRaw, "caller edit changed hosting source clone");
      auto hostingRejected = readSavedHosting(hostingDocument);
      require(!hostingRejected && hostingRejected.error() == SavedServiceError::EditedDocument, "edited hosting input was accepted");
    });

    run("tracker-hash-length-is-diagnostic-not-truncation", [&] {
      for (const std::size_t length : {0U, 19U, 20U, 21U, 31U, 32U, 33U}) {
        const std::string hash(length, '\xff');
        auto saved = hosting(hostingInput(map({{"hash", bytes(hash)}, {"status", "i1e"}})));
        require(saved.records[0].hash == hash, "unusual tracker hash bytes were lost/truncated");
        require(issue(saved, SavedIssueCode::HashLength, 0, SavedField::Hash) == (length != 20 && length != 32), "hash-length diagnostic differs");
      }
      auto duplicateDictionary = Document::parse("d8:torrentsle8:torrentslee");
      require(!duplicateDictionary && duplicateDictionary.error().code == "duplicate_key", "duplicate dictionary key was silently accepted");
    });

    run("wrong-root-and-list-failures", [&] {
      auto root = parse("le");
      auto badHost = readSavedHosting(root);
      auto badShares = readSavedShares(root);
      require(!badHost && badHost.error() == SavedServiceError::Root, "tracker wrong-root error differs");
      require(!badShares && badShares.error() == SavedServiceError::Root, "sharing wrong-root error differs");
      auto hostList = parse(map({{"torrents", "i1e"}}));
      auto shareList = parse(map({{"resources", "3:bad"}}));
      badHost = readSavedHosting(hostList);
      badShares = readSavedShares(shareList);
      require(!badHost && badHost.error() == SavedServiceError::List, "tracker wrong-list error differs");
      require(!badShares && badShares.error() == SavedServiceError::List, "sharing wrong-list error differs");
      for (const auto& malformed : {std::string("d"), std::string("d8:torrentsl"), minimalHost() + 'x'}) {
        require(!Document::parse(malformed), "malformed/trailing bencode was accepted");
      }
    });

    run("row-field-text-and-issue-bounds", [&] {
      auto document = parse(minimalHost());
      SavedServiceLimits limits;
      limits.rows = 0;
      limitedHosting(document, limits, false);
      limits.rows = 1;
      limitedHosting(document, limits, true);
      limits.fieldBytes = 19;
      limitedHosting(document, limits, false);
      limits.fieldBytes = 20;
      limits.textBytes = 19;
      limitedHosting(document, limits, false);
      limits.textBytes = 20;
      limitedHosting(document, limits, true);
      limits.issues = 0;
      limitedHosting(document, limits, true);
      auto unknown = parse(minimalHost(7));
      limitedHosting(unknown, limits, false);
      limits.issues = 1;
      limitedHosting(unknown, limits, true);
      auto shareDocument = parse(minimalShare());
      SavedServiceLimits shareLimits;
      shareLimits.fieldBytes = 3;
      shareLimits.textBytes = 3;
      limitedShares(shareDocument, shareLimits, true);
      shareLimits.fieldBytes = 2;
      limitedShares(shareDocument, shareLimits, false);
      shareLimits.fieldBytes = 3;
      shareLimits.textBytes = 2;
      limitedShares(shareDocument, shareLimits, false);
      SavedServiceLimits zero;
      zero.rows = zero.fieldBytes = zero.textBytes = zero.issues = 0;
      auto emptyHost = parse("d8:torrentslee");
      auto emptyShares = parse("d9:resourceslee");
      limitedHosting(emptyHost, zero, true);
      limitedShares(emptyShares, zero, true);
    });

    run("document-bounds-include-unknown-fields", [&] {
      const auto raw = minimalHost();
      auto document = parse(raw);
      SavedServiceLimits limits;
      limits.document.inputBytes = raw.size();
      limits.document.stringBytes = 20;
      limits.document.nodes = 8;
      limits.document.depth = 3;
      limitedHosting(document, limits, true);
      auto low = limits;
      --low.document.inputBytes;
      limitedHosting(document, low, false);
      low = limits;
      --low.document.stringBytes;
      limitedHosting(document, low, false);
      low = limits;
      --low.document.nodes;
      limitedHosting(document, low, false);
      low = limits;
      --low.document.depth;
      limitedHosting(document, low, false);
      auto unknown = parse(map({{"torrents", "le"}, {"opaque", bytes(std::string(100, 'U'))}}));
      SavedServiceLimits unknownLimits;
      unknownLimits.document.stringBytes = 99;
      limitedHosting(unknown, unknownLimits, false);
      require(document.serialize() == raw, "failed bounds checks mutated input");
    });

    run("native-caps-cannot-be-raised", [&] {
      auto document = parse(minimalHost());
      const auto reject = [&](auto increase) {
        SavedServiceLimits limits;
        increase(limits);
        limitedHosting(document, limits, false);
      };
      reject([](auto& value) { ++value.rows; });
      reject([](auto& value) { ++value.textBytes; });
      reject([](auto& value) { ++value.fieldBytes; });
      reject([](auto& value) { ++value.issues; });
      reject([](auto& value) { ++value.document.inputBytes; });
      reject([](auto& value) { ++value.document.stringBytes; });
      reject([](auto& value) { ++value.document.nodes; });
      reject([](auto& value) { ++value.document.depth; });
    });

    run("independent-owned-projections-and-repeated-refresh", [&] {
      auto document = parse(trackerRaw);
      auto first = readSavedHosting(document);
      auto second = readSavedHosting(document);
      require(first && second && first->source.get() != second->source.get(), "independent snapshots share a mutable document object");
      first->records[0].hash = "changed-local-projection";
      require(second->records[0].hash == std::string(20, 'A'), "editing one projection changed another");
      require(first->source->serialize() == trackerRaw && second->source->serialize() == trackerRaw && document.serialize() == trackerRaw, "local projection edit mutated raw source");
      for (int iteration = 0; iteration < 4; ++iteration) {
        auto repeated = readSavedHosting(document);
        require(repeated && repeated->records.size() == 5 && repeated->source->serialize() == trackerRaw, "repeated refresh changed source/rows");
        auto shared = shares(sharingRaw);
        require(shared.records.size() == 6 && shared.source->serialize() == sharingRaw, "repeated sharing projection changed source/rows");
      }
    });

    run("cumulative-text-bounds-across-fields-and-rows", [&] {
      const auto sharedRow = [&](std::string_view path) {
        return map({{"type", "i1e"}, {"file", bytes(path)}, {"ihash", "2:fp"}, {"ifile", "4:c:01"},
            {"attributes", map({{"Category", "2:ab"}})}, {"props", map({{"tags", "3:1,2"}, {"persistent", "4:true"}})}});
      };
      // Each row copies3 file +2 fingerprint +4 cache +2 category +3 tag
      // bytes. The boolean's input text is validated, not copied as text.
      auto shared = parse(sharingInput(sharedRow("one") + sharedRow("two")));
      SavedServiceLimits shareLimits;
      shareLimits.rows = 2;
      shareLimits.fieldBytes = 4;
      shareLimits.textBytes = 28;
      limitedShares(shared, shareLimits, true);
      shareLimits.textBytes = 27;
      limitedShares(shared, shareLimits, false);
      const auto hostedRow = [&](char identity) {
        return map({{"hash", bytes(std::string(20, identity))}, {"status", "i1e"}, {"torrent_file", "3:raw"}});
      };
      auto hosted = parse(hostingInput(hostedRow('A') + hostedRow('B')));
      SavedServiceLimits hostLimits;
      hostLimits.rows = 2;
      hostLimits.fieldBytes = 20;
      hostLimits.textBytes = 46;
      limitedHosting(hosted, hostLimits, true);
      hostLimits.textBytes = 45;
      limitedHosting(hosted, hostLimits, false);
    });

    run("malformed-persistent-property-remains-unknown", [&] {
      const auto check = [&](std::string encoded, SavedIssueCode expected) {
        auto raw = sharingInput(map({{"type", "i3e"}, {"file", "3:raw"}, {"recursive", "i0e"},
            {"props", map({{"persistent", std::move(encoded)}})}}));
        auto saved = shares(raw);
        require(!saved.records[0].persistent.has_value(), "malformed persistent property became a boolean");
        require(issue(saved, expected, 0, SavedField::Persistent), "malformed persistent property lacks typed diagnostic");
        require(saved.source->serialize() == raw, "malformed persistent bytes were rewritten");
      };
      check(bytes(std::string("\xff", 1)), SavedIssueCode::Encoding);
      check(bytes(std::string("true\0", 5)), SavedIssueCode::Nul);
      check("i1e", SavedIssueCode::Type);
    });

    run("short-path-duplicates-stay-owned-and-indexed", [&] {
      const auto row = map({{"type", "i3e"}, {"file", "1:x"}, {"recursive", "i0e"}});
      const auto raw = sharingInput(row + row + row + row);
      auto saved = shares(raw);
      require(saved.records.size() == 4, "short duplicate paths were collapsed");
      for (std::size_t index = 0; index < 4; ++index) {
        require(saved.records[index].sourceIndex == index && saved.records[index].file && saved.records[index].file->bytes == "x", "short duplicate identity/index changed");
        require(saved.records[index].duplicateIdentity == (index != 0), "short duplicate marker differs");
        require(issue(saved, SavedIssueCode::DuplicateIdentity, index, SavedField::File) == (index != 0), "short duplicate diagnostic differs");
      }
      auto moved = std::move(saved);
      moved.records[0].file->bytes = "local-only-edit";
      for (std::size_t index = 1; index < 4; ++index) { require(moved.records[index].file->bytes == "x", "short projected strings alias another row"); }
      require(moved.source->serialize() == raw, "short-path local edit changed raw document");
    });

    std::cout << "PASS saved-services " << cases << " cases / " << assertions
        << " assertions; synthetic original-format inputs, zero Java equality credit; no service/path activation\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL saved-services " << active << ": " << error.what() << '\n';
    return 1;
  }
}
