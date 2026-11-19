#pragma once
#include "original_control.hpp"
#include "torrent_state.hpp"

namespace biglybtc::storage {
enum class OriginalStorageConversionStage {
  ResumePublished,IntentPublished,PayloadSynced,ControlPublished,TypePublished,ControlRetired,Completed
};
// Synchronous serialized worker primitive,not a command/admission authority.
// Caller authenticates the complete original layout,mapping and exclusive
// payload claim before entry and retains the lease/descriptor/IO lifetimes.
// This verifies the FD/IO identity,exact control owner and unverified prepared
// resume before any mutation. It changes storage metadata,never skip fields.
// Existing BiglyBTC.compactConversion inode intents retain their interpretation.
std::expected<void,Error> expandOwnedOriginalCompact(const ProfileLease&,
    const TorrentStateStore&,TorrentStateFile&,std::size_t index,int payload,
    OriginalFileIo&,const std::shared_ptr<OriginalControl>&,
    void (*observer)(OriginalStorageConversionStage)=nullptr);
std::expected<void,Error> unskipOwnedOriginalReordered(const ProfileLease&,
    const TorrentStateStore&,TorrentStateFile&,std::size_t index,int payload,
    OriginalFileIo&,const std::shared_ptr<OriginalControl>&,
    void (*observer)(OriginalStorageConversionStage)=nullptr,
    bool preservePreviousControl=false);
// preservePreviousControl uses Original's X-to-R previous-storage marker:
// reopen the same validated map as R without publishing an st-only rewrite.
// This mode performs no control write and requires no pending swap recovery.
}
