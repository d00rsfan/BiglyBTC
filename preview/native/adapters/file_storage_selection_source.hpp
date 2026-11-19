#pragma once
#include "file_operations.hpp"
#include "file_operation_io.hpp"
#include "original_file_io.hpp"
#include <vector>

namespace biglybtc::storage::detail {
// Values from the existing complete file-operation planner. This read-only
// snapshot carries no PreparedFileOperation or mutation/recovery authority.
struct StorageSelectionFile {
  std::size_t index=0;
  std::filesystem::path path;
  std::optional<struct stat> stamp;
  OriginalFileGeometry geometry;
  OriginalStorageKind requested=OriginalStorageKind::Linear,physical=OriginalStorageKind::Linear;
};
struct StorageSelectionControl {std::filesystem::path path;fileop::Raw before;};
struct StorageSelectionSource {
  std::uint64_t profileDevice=0,profileInode=0,pieceCount=0;
  std::string row,requestedAttributes,infoSha256,filenameConversions;
  bool decoderShowAll=false;
  std::vector<StorageSelectionFile> files;
  std::vector<StorageSelectionControl> controls;
};
// Same-policy request: no DND move or metadata edit is executed. Existing
// compact inode intents may be observed solely for a group owner's cold replay.
std::expected<StorageSelectionSource,Error> readStorageSelectionSource(
    const ProfileLease&,const FileOperationRequest&);
}
