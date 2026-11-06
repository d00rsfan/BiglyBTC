// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/bencode.hpp"
#include <optional>
#include <variant>

namespace biglybtc::core {
// Download-list history, distinct from Statistics/LongTermStats saved_history.
inline constexpr std::string_view DownloadHistoryActiveFile="dlhistorya.config";
inline constexpr std::string_view DownloadHistoryDeadFile="dlhistoryd.config";
enum class SavedDownloadHistoryError {Format,Root,Records,Record,Field,Text,DuplicateUid,MissingUid,ExistingUid,Limit,Memory};
struct SavedDownloadHistoryLimits {
  Limits document{};
  std::size_t records=4096,textBytes=1024*1024,fieldBytes=65536,tagsPerRecord=256,hashBytes=4096;
};
struct SavedDownloadHistoryRecord {
  std::size_t sourceIndex=0;
  std::int64_t uid=0;
  // Original reader accepts arbitrary byte arrays; these are NOT peer/storage
  // identity capabilities. Real normal h is20B and v is32B; old hints are kept.
  std::optional<std::string> hash,hashV2;
  std::string name,saveLocation;
  std::optional<std::int64_t> rawSize;
  std::int64_t size=0,addTime=0,rawCompleteTime=0,rawRemoveTime=0;
  std::int64_t completeTime=0,removeTime=0; // Original negative -> zero projection.
  bool tagsPresent=false;
  std::vector<std::string> tags; // Preserve original list order/duplicates.
};
struct SavedDownloadHistory {
  std::shared_ptr<const Document> source;
  bool recordsPresent=false;
  std::vector<SavedDownloadHistoryRecord> records;
};
// Strict whole-document projection: no skipped malformed rows or duplicate-UID
// last-wins. Invalid known UTF8 refuses instead of approximating Java replacement.
// Unknown root/record fields remain owning raw source bytes. No version field is
// invented; the original records document is unversioned. Input is decoded
// bencode only; filesystem/gzip/primary/backup selection belongs to a held owner.
std::expected<SavedDownloadHistory,SavedDownloadHistoryError> readSavedDownloadHistory(
    std::string_view,SavedDownloadHistoryLimits={});

struct DownloadHistoryNewRecord {
  std::int64_t uid=0;
  std::optional<std::string> hash,hashV2;
  std::string name,saveLocation;
  std::int64_t size=0,addTime=0,completeTime=0,removeTime=0;
  std::vector<std::string> tags;
};
struct DownloadHistoryAppend {DownloadHistoryNewRecord record;};
struct DownloadHistorySetName {std::int64_t uid;std::string value;};
struct DownloadHistorySetSaveLocation {std::int64_t uid;std::string value;};
struct DownloadHistorySetCompleteTime {std::int64_t uid;std::int64_t value;};
struct DownloadHistorySetRemoved {
  std::int64_t uid,time;
  // Original setRemoved only replaces tags when supplied list is nonempty.
  std::vector<std::string> tags;
};
struct DownloadHistoryErase {std::int64_t uid;};
struct DownloadHistoryClear {};
using SavedDownloadHistoryEdit=std::variant<DownloadHistoryAppend,DownloadHistorySetName,
    DownloadHistorySetSaveLocation,DownloadHistorySetCompleteTime,
    DownloadHistorySetRemoved,DownloadHistoryErase,DownloadHistoryClear>;
struct SavedDownloadHistoryEditResult {SavedDownloadHistory value;bool changed=false;};
// Value-only selected edit. Appending an existing UID refuses (no accidental
// collision overwrite). Missing erase is a no-op; other missing edits refuse.
// Negative completion/removal requests project zero. Semantic no-ops retain
// historical raw negatives/absent size and the entire byte string exactly.
// Active->dead publication, clocks, tags capture, original events and disk CAS
// are NOT performed by these edits or by their owning result.
std::expected<SavedDownloadHistoryEditResult,SavedDownloadHistoryError> editSavedDownloadHistory(
    std::string_view,const SavedDownloadHistoryEdit&,SavedDownloadHistoryLimits={});

// Exact original Java getUID inputs. Its date source is getLongAttribute with
// PARAM_DOWNLOAD_ADDED_TIME, distinct from record.addTime's getLongParameter.
// The signed Java RHS is not masked to32 bits. This helper supplies no ownership.
std::int64_t originalDownloadHistoryUid(std::optional<std::string_view> torrentGetHash,
    std::int64_t uidAddedAttributeMillis)noexcept;
std::int64_t originalDownloadHistoryCompleteTime(std::int64_t lastCompleteAttribute,
    std::int64_t completedParameter)noexcept;
bool originalDownloadHistoryMonitored(bool persistent,std::uint64_t flags)noexcept;
} // namespace biglybtc::core
