// SPDX-License-Identifier: GPL-2.0-or-later
#include "manual_tag_identity.hpp"
#include "original_identity_digest.hpp"
#include "torrent_state.hpp"
#include "file_operations.hpp"
#include <new>
#include <thread>
#include <cerrno>
#include <sys/stat.h>
#include <fcntl.h>
namespace biglybtc::storage {
struct ManualTagIdentityScope::Data {
 ManualTagSnapshot source;core::TorrentHash runtime;OriginalTorrentIdentity identity;
 std::string inventory,state;std::uint64_t revision=0;std::thread::id thread;
};
namespace {
Error conflict(){return {ErrorCode::Conflict,"Canonical tag identity source is stale or ambiguous"};}
struct Source {OriginalTorrentIdentity identity;std::string inventory,state;};
std::expected<Source,Error> readSource(const ManualTagStore& store,const ProfileLease& lease,
 const ManualTagSnapshot& snapshot,const core::TorrentHash& runtime){
 if(!snapshot.valid()||!snapshot.writable()||!store.matchesSnapshotLineage(snapshot,lease)||hasPendingFileOperation(lease)){return std::unexpected(conflict());}
 for(const char* pending : {"tag.config.saving", "downloads.config.saving"}) {
  struct stat status{};
  if(::fstatat(lease.directoryHandle(),pending,&status,AT_SYMLINK_NOFOLLOW)==0||errno!=ENOENT) {
   return std::unexpected(conflict());
  }
 }
 auto tags=ConfigFile::load(lease,"tag.config");
 if(!tags){if(tags.error().code!=ErrorCode::Missing||!snapshot.missing()){return std::unexpected(conflict());}}
 else if(snapshot.missing()||tags->recoverySource()!=RecoverySource::Primary||!snapshot.rawBytes()||tags->document().originalBytes()!=*snapshot.rawBytes()){return std::unexpected(conflict());}
 auto inventory=ConfigFile::load(lease,"downloads.config");if(!inventory){return std::unexpected(inventory.error());}
 if(inventory->recoverySource()!=RecoverySource::Primary){return std::unexpected(conflict());}
 auto library=core::readLibrary(inventory->document());if(!library||!library->issues.empty()||library->downloads.size()>100000){return std::unexpected(conflict());}
 unsigned matches=0;for(const auto& row:library->downloads){if(!row.hash){return std::unexpected(conflict());}if(*row.hash==runtime){if(!row.persistent){return std::unexpected(conflict());}++matches;}}
 if(matches!=1){return std::unexpected(conflict());}
 auto states=TorrentStateStore::open(lease);if(!states){return std::unexpected(states.error());}auto state=states->load(lease,runtime);if(!state||state->recoveredBackup()){return std::unexpected(conflict());}
 auto identity=digestOriginalTorrentIdentity(state->document());if(!identity){return std::unexpected(identity.error());}
 auto key=validateOriginalTorrentKey(*identity,runtime,true);if(!key){return std::unexpected(key.error());}
 for(const auto& row:library->downloads){if(*row.hash==runtime){continue;}
  if(row.hash->bytes().size()!=20||*row.hash==identity->originalKey){return std::unexpected(Error{ErrorCode::Unsupported,"Other tag subject owner has an unproved alias"});}
 }
 if(!store.matchesSnapshotLineage(snapshot,lease)){return std::unexpected(conflict());}
 return Source{std::move(*identity),inventory->document().originalBytes(),state->document().originalBytes()};
}
bool same(const Source& a,const Source& b){return a.identity.originalKey==b.identity.originalKey&&a.identity.selected==b.identity.selected&&a.identity.full==b.identity.full&&a.inventory==b.inventory&&a.state==b.state;}
}
std::optional<core::TorrentHash> ManualTagIdentityScope::runtimeHash()const{return data_?std::optional{data_->runtime}:std::nullopt;}
std::optional<core::TorrentHash> ManualTagIdentityScope::canonicalHash()const{return data_?std::optional{data_->identity.originalKey}:std::nullopt;}
bool ManualTagIdentityScope::current(const ManualTagStore& store,const ProfileLease& lease,const ManualTagSnapshot& snapshot)const noexcept{
 return data_&&data_->thread==std::this_thread::get_id()&&lease.sourceRevision()==data_->revision
   &&store.matchesSnapshotLineage(data_->source,lease)&&store.matchesSnapshotLineage(snapshot,lease)
   &&snapshot.generation()==data_->source.generation()&&snapshot.sameSource(data_->source);
}
std::expected<ManualTagIdentityScope,Error> bindManualTagIdentity(const ManualTagStore& store,const ProfileLease& lease,
 const ManualTagSnapshot& snapshot,const core::TorrentHash& runtime){
 try{
  if(!store.matchesSnapshotLineage(snapshot,lease)){return std::unexpected(conflict());}auto access=lease.sourceAccess();if(!access){return std::unexpected(access.error());}
  const auto revision=lease.sourceRevision();auto first=readSource(store,lease,snapshot,runtime);if(!first){return std::unexpected(first.error());}
  auto second=readSource(store,lease,snapshot,runtime);if(!second){return std::unexpected(second.error());}
  if(lease.sourceRevision()!=revision||!same(*first,*second)){return std::unexpected(conflict());}
  auto data=std::make_shared<ManualTagIdentityScope::Data>(ManualTagIdentityScope::Data{snapshot,runtime,std::move(first->identity),std::move(first->inventory),std::move(first->state),revision,std::this_thread::get_id()});return ManualTagIdentityScope(std::move(data));
 }catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Canonical tag identity scope memory unavailable"});}
}
std::expected<void,Error> ManualTagIdentityScope::revalidate(const ManualTagStore& store,const ProfileLease& lease,const ManualTagSnapshot& snapshot)const{
 try{
  if(!current(store,lease,snapshot)){return std::unexpected(conflict());}auto access=lease.sourceAccess();if(!access){return std::unexpected(access.error());}
  auto actual=readSource(store,lease,snapshot,data_->runtime);if(!actual){return std::unexpected(actual.error());}
  if(!current(store,lease,snapshot)||actual->identity.originalKey!=data_->identity.originalKey||actual->identity.selected!=data_->identity.selected
     ||actual->identity.full!=data_->identity.full||actual->inventory!=data_->inventory||actual->state!=data_->state){return std::unexpected(conflict());}return {};
 }catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Canonical tag identity validation memory unavailable"});}
}
}
