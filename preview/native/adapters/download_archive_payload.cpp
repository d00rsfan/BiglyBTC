// SPDX-License-Identifier: GPL-2.0-or-later
#include "download_archive_payload.hpp"

#include "download_archive_restore.hpp"
#include "file_operation_io.hpp"
#include "original_filename_policy.hpp"
#include "original_storage_defaults.hpp"
#include "torrent_state.hpp"
#include "../platform/utf8_path.hpp"
#include "biglybtc/original_file_links.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <ranges>
#include <set>

namespace biglybtc::storage::detail {
namespace {
namespace fs=std::filesystem;
using Paths=std::vector<fs::path>;

void require(bool condition,ErrorCode code,const char* message){
  if(!condition){throw Error{code,message};}
}

template<class T>
T take(std::expected<T,Error> result){
  if(!result){throw std::move(result.error());}
  return std::move(*result);
}

bool beneath(const fs::path& root,const fs::path& target){
  auto current=target.begin();
  for(const auto& part:root){
    if(current==target.end()||!sameFilesystemComponent(part,*current)){return false;}
    ++current;
  }
  return true;
}

bool samePath(const fs::path& a,const fs::path& b){
  return beneath(a,b)&&beneath(b,a);
}

bool overlaps(const fs::path& a,const fs::path& b){
  return beneath(a,b)||beneath(b,a);
}

void checkStop(std::stop_token stop){
  require(!stop.stop_requested(),ErrorCode::Cancelled,"Archive data removal was cancelled");
}

fs::path saveRoot(const core::SavedDownload& saved,const compat::OriginalLayout& layout){
  const auto resolved=take(resolveOriginalSaveRoot(saved,layout));
  require(!resolved.saveDirectory.empty()&&resolved.saveFile.has_value(),ErrorCode::InvalidFormat,
      "The archived download has no resolved save location");
  auto root=platform::pathFromUtf8(resolved.saveDirectory)/platform::pathFromUtf8(*resolved.saveFile);
  fileop::pathShape(root);
  return root;
}

Paths selectedFiles(const core::Document& state,const compat::OriginalLayout& layout,const fs::path& root){
  const auto* attributes=state.find("attributes");
  std::vector<std::string> names;
  names.reserve(layout.files.size());
  for(const auto& file:layout.files){
    names.push_back(platform::pathToUtf8(layout.multifile?root/platform::pathFromUtf8(file.path):root));
  }
  const auto links=compat::resolveOriginalFileLinks(attributes?attributes->find("filelinks2"):nullptr,names);
  require(links&&links->diagnostics.empty(),ErrorCode::InvalidFormat,"The original file links are ambiguous");
  Paths paths;
  paths.reserve(names.size());
  for(std::size_t i=0;i<names.size();++i){
    auto path=platform::pathFromUtf8(links->byIndex[i]?links->byIndex[i]->target:names[i]);
    fileop::pathShape(path);
    paths.push_back(std::move(path));
  }
  return paths;
}

fs::path archiveNamedFile(const core::ArchivedFile& file,const fs::path& root){
  require(file.path.has_value(),ErrorCode::InvalidFormat,"An archived file has no saved path");
  const auto path=platform::pathFromUtf8(*file.path);
  if(file.relative){
    require(!path.is_absolute()&&!path.has_root_path(),ErrorCode::UnsafePath,"An archived relative file has a rooted path");
    auto resolved=root/path;
    fileop::pathShape(resolved);
    require(beneath(root,resolved)&&!samePath(root,resolved),ErrorCode::UnsafePath,"An archived relative file leaves its saved directory");
    return resolved;
  }
  fileop::pathShape(path);
  return path;
}

core::SavedDownload archivedInventoryRow(const DownloadArchiveRestoreSource& source){
  const auto doc=core::Document::parse("d9:downloadsl"+source.inventoryRow.originalBytes()+"ee",compatibleConfigLimits());
  require(doc.has_value(),ErrorCode::InvalidFormat,"Invalid archived download inventory snapshot");
  const auto saved=core::readLibrary(*doc);
  require(saved&&saved->issues.empty()&&saved->downloads.size()==1&&saved->downloads.front().hash==source.identity.originalKey,
      ErrorCode::Conflict,"The archived payload no longer has one saved owner");
  return saved->downloads.front();
}

} // namespace

std::expected<ArchivePayloadSelection,Error> selectArchivePayload(
    const ProfileLease& lease,const DownloadArchiveRestoreSource& source,std::stop_token stop) try {
  checkStop(stop);
  require(source.state.bytes.has_value(),ErrorCode::Missing,"The archived torrent metadata is missing");
  const auto state=core::Document::parse(*source.state.bytes,compatibleConfigLimits());
  require(state.has_value(),ErrorCode::InvalidFormat,"The archived torrent metadata is unreadable");
  const auto layout=take(readOriginalLayout(lease,*state));
  const auto root=saveRoot(archivedInventoryRow(source),layout);
  const auto profile=platform::pathFromUtf8(fileop::profilePath(lease));
  require(!overlaps(root,profile),ErrorCode::UnsafePath,"The archive payload root overlaps the saved profile");
  const auto entries=take(readDownloadArchive(lease,stop));
  require(entries.source&&entries.source->document().originalBytes()==source.configuration.document().originalBytes()
      &&source.rowIndex<entries.stubs.size(),ErrorCode::Conflict,"The archive changed during payload selection");
  const auto& selected=entries.stubs[source.rowIndex];
  require(selected.hash==source.identity.originalKey&&selected.metadata.savePath.has_value()
      &&samePath(root,platform::pathFromUtf8(*selected.metadata.savePath)),
      ErrorCode::Conflict,"The archive save location differs from the original download");

  ArchivePayloadSelection result{root,{}};
  if(!layout.multifile){
    // The original removes the simple torrent's save location, even when the
    // file list mentions a relocated link elsewhere.
    if(const auto stamp=fileop::inspect(root)){
      require(stamp->links==1&&stamp->owned,ErrorCode::UnsafePath,"The archived payload file is linked or belongs to another user");
      result.files.push_back({root,*stamp});
    }
    return result;
  }

  const auto paths=selectedFiles(*state,layout,root);
  const auto& archived=selected.metadata.fileEntries;
  require(archived.size()==paths.size(),ErrorCode::Conflict,"Archived file map differs from original torrent metadata");
  std::set<std::pair<std::uint64_t,std::uint64_t>> identities;
  for(std::size_t index=0;index<paths.size();++index){
    checkStop(stop);
    const auto path=archiveNamedFile(archived[index],root);
    const auto length=archived[index].length;
    require(length&&*length!=std::numeric_limits<std::int64_t>::min()&&std::abs(*length)==layout.files[index].bytes
        &&samePath(path,paths[index]),ErrorCode::Conflict,"An archived file differs from the original torrent layout");
    if(!beneath(root,path)||samePath(root,path)){continue;}
    if(const auto stamp=fileop::inspect(path)){
      require(stamp->links==1&&stamp->owned,ErrorCode::UnsafePath,"The archived payload file is linked or belongs to another user");
      require(identities.emplace(stamp->device,stamp->inode).second,ErrorCode::Conflict,
          "Multiple archived entries refer to the same existing payload file");
      result.files.push_back({path,*stamp});
    }
  }
  return result;
} catch(const Error& error){
  return std::unexpected(error);
} catch(const fileop::Failure& failure){
  return std::unexpected(failure.error);
} catch(const std::filesystem::filesystem_error&){
  return std::unexpected(Error{ErrorCode::UnsafePath,"Invalid archived payload path"});
} catch(const std::bad_alloc&){
  return std::unexpected(Error{ErrorCode::Io,"Cannot prepare archived payload paths"});
}

std::expected<void,Error> verifyArchivePayloadReferences(
    const ProfileLease& lease,const core::TorrentHash& removed,const ArchivePayloadSelection& selection,std::stop_token stop) try {
  if(selection.files.empty()){return {};}
  const auto protect=[&](const fs::path& path){
    require(std::ranges::none_of(selection.files,[&](const auto& file){return overlaps(file.path,path);}),
        ErrorCode::Conflict,"An archived payload file is still used by a different download");
  };

  const auto current=take(readDownloadArchive(lease,stop));
  for(const auto& entry:current.stubs){
    checkStop(stop);
    if(entry.hash==removed){continue;}
    require(entry.hash&&entry.metadata.savePath&&entry.metadata.fileLengths.has_value(),ErrorCode::Conflict,
        "Another archived download has unresolved payload ownership");
    const auto root=platform::pathFromUtf8(*entry.metadata.savePath);
    fileop::pathShape(root);
    protect(root);
    for(const auto& file:entry.metadata.fileEntries){protect(archiveNamedFile(file,root));}
  }

  auto inventory=ConfigFile::load(lease,"downloads.config");
  if(!inventory&&inventory.error().code==ErrorCode::Missing){return {};}
  if(!inventory){return std::unexpected(inventory.error());}
  require(inventory->recoverySource()==RecoverySource::Primary,ErrorCode::Conflict,
      "Resolve library inventory recovery before removing archived payload");
  const auto library=core::readLibrary(inventory->document());
  require(library&&library->issues.empty(),ErrorCode::Conflict,"The saved library has unresolved payload ownership");
  if(library->downloads.empty()){return {};}
  const auto states=take(TorrentStateStore::open(lease));
  for(const auto& active:library->downloads){
    checkStop(stop);
    require(active.hash&&active.hash!=removed,ErrorCode::Conflict,
        "An archived payload has an active or unresolved download identity");
    const auto loaded=take(states.loadAttributes(lease,*active.hash));
    require(!loaded.recoveredBackup(),ErrorCode::Conflict,"An active download needs state recovery");
    const auto layout=take(readOriginalLayout(lease,loaded.document()));
    const auto root=saveRoot(active,layout);
    protect(root);
    for(const auto& file:selectedFiles(loaded.document(),layout,root)){protect(file);}
  }
  return {};
} catch(const Error& error){
  return std::unexpected(error);
} catch(const fileop::Failure& failure){
  return std::unexpected(failure.error);
} catch(const std::filesystem::filesystem_error&){
  return std::unexpected(Error{ErrorCode::UnsafePath,"Invalid saved payload reference"});
} catch(const std::bad_alloc&){
  return std::unexpected(Error{ErrorCode::Io,"Cannot verify archived payload ownership"});
}

} // namespace biglybtc::storage::detail
