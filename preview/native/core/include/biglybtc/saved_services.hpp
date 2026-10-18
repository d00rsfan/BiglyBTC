#pragma once

#include "biglybtc/bencode.hpp"

#include <limits>
#include <optional>

namespace biglybtc::core {

// Native projection caps, not limits of the original Java file formats. Only
// reductions of these defaults are accepted. The retained source is separately
// bounded by document; textBytes charges copied projected strings/binary fields.
struct SavedServiceLimits {
  Limits document{};
  std::size_t rows = 10000;
  std::size_t textBytes = 1024 * 1024;
  std::size_t fieldBytes = 65536;
  std::size_t issues = 100000;
};
enum class SavedServiceError { Root, List, Limit, Memory, EditedDocument };
enum class SavedIssueCode { Missing, Type, Encoding, Nul, Unknown, DuplicateIdentity, HashLength };
enum class SavedField {
  Records, Record, Hash, Persistent, Passive, Status, DateAdded, TorrentFile,
  BackupTime, Stats, Completed, Announces, Uploaded, Downloaded, Scrapes,
  BytesIn, BytesOut, ResourceType, File, Attributes, Category, Properties,
  Tags, Recursive, Fingerprint, CacheFile
};
inline constexpr std::size_t SavedRootIndex = std::numeric_limits<std::size_t>::max();
struct SavedServiceIssue {
  SavedIssueCode code;
  std::size_t sourceIndex;
  SavedField field;
};
enum class SavedTextEncoding { Utf8, Latin1 };
struct SavedText {
  std::string bytes;
  SavedTextEncoding encoding = SavedTextEncoding::Utf8;
  bool validEncoding = true;
  bool embeddedNul = false;
};
enum class SavedHostingDisposition { PersistentHost, StatsBackup };
struct SavedHostingStats {
  std::optional<std::int64_t> completed, announces, uploaded, downloaded;
  std::optional<std::int64_t> scrapes, bytesIn, bytesOut;
};
struct SavedHostingRecord {
  std::size_t sourceIndex = 0;
  std::optional<std::string> hash;
  // Missing typed flags have the documented source defaults true/false.
  // Malformed flags remain unknown; disposition is not service activation.
  std::optional<bool> persistent, passive;
  std::optional<SavedHostingDisposition> disposition;
  std::optional<std::int64_t> rawStatus;
  std::optional<std::int32_t> status;
  std::optional<std::int64_t> dateAdded, backupTime;
  std::optional<SavedText> torrentFile; // ISO-8859-1 bytes; inert path.
  std::optional<SavedHostingStats> stats;
  bool duplicateIdentity = false;
};
struct SavedHosting {
  std::shared_ptr<const Document> source;
  bool listPresent = false;
  std::vector<SavedHostingRecord> records;
  std::vector<SavedServiceIssue> issues;
};
enum class SavedShareKind { File = 1, Directory = 2, DirectoryContents = 3 };
struct SavedShareRecord {
  std::size_t sourceIndex = 0;
  std::optional<std::int64_t> rawType;
  std::optional<std::int32_t> type;
  std::optional<SavedShareKind> kind;
  std::optional<SavedText> file; // Full stored path, not a basename or an opened file.
  std::optional<SavedText> category;
  std::optional<bool> persistent; // Absent props/persistent means false.
  std::optional<SavedText> tagUids; // Unparsed comma-separated IDs, not tag names.
  std::optional<bool> recursive; // Directory-contents only.
  std::optional<std::string> fingerprint; // ihash is NOT a torrent infohash.
  std::optional<SavedText> cacheFile; // Inert relative cache text, never resolved.
  bool duplicateIdentity = false;
};
struct SavedShares {
  std::shared_ptr<const Document> source;
  bool listPresent = false;
  std::vector<SavedShareRecord> records;
  std::vector<SavedServiceIssue> issues;
};

// Pure, synchronous, read-only projections of unedited parsed documents. Each
// result privately clones the document tree with immutable shared raw backing;
// later ordinary edits to the caller's Document do not alter a result. Every
// list entry has its original sourceIndex, even when malformed. Unknown fields
// and duplicate list records survive. Document::parse still rejects duplicate
// dictionary keys. Missing lists differ from valid empty lists; wrong roots or
// list types fail. UTF-8/NUL diagnostics retain the supplied bytes unmodified.
// per_key is retained ONLY in source, never copied into a projection field.
// No clock, path validation/resolution, filesystem, managers or network services.
std::expected<SavedHosting, SavedServiceError> readSavedHosting(
    const Document& document, SavedServiceLimits limits = {});
std::expected<SavedShares, SavedServiceError> readSavedShares(
    const Document& document, SavedServiceLimits limits = {});

} // namespace biglybtc::core
