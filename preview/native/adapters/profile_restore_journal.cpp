// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_restore_journal.hpp"
#include "profile_backup.hpp"
#include "download_archive.hpp"
#include "profile_file_snapshot.hpp"
#include "file_operations.hpp"
#include "linear_to_compact.hpp"
#include "profile_transaction_pending.hpp"
#include "state_directory_listing.hpp"
#include "../platform/utf8_path.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <map>
#include <openssl/evp.h>
#include <ranges>
#include <vector>

namespace biglybtc::storage::detail {
namespace {
namespace fs=std::filesystem;
constexpr std::size_t MaximumEntries=100000;
constexpr unsigned MaximumDepth=32;
constexpr char Configuration[]="biglybt.config";

void require(bool condition, ErrorCode code, const char* message) {
  if (!condition) { throw Error{code,message}; }
}
template<class T>
T take(std::expected<T, Error> result) {
  if (!result) { throw std::move(result.error()); }
  if constexpr (!std::is_void_v<T>) { return std::move(*result); }
}
void checkpoint(std::stop_token stop) {
  require(!stop.stop_requested(),ErrorCode::Cancelled,"Profile restore cancelled");
}
std::string encoded(std::string_view value) {
  return std::to_string(value.size())+':'+std::string(value);
}
std::string integer(std::int64_t value) {
  return 'i'+std::to_string(value)+'e';
}
std::string identity(std::uint64_t value) {
  return encoded(std::to_string(value));
}

bool reserved(const fs::path& leaf) {
  for (const auto* name : {RestoreDirectory,RestoreRequest,RestoreRequestSaving,RestorePrevious,RestorePrior,RestorePriorSaving,RestoreApplied,".azlock",".lock"}) {
    if (sameFilesystemComponent(leaf,name)) { return true; }
  }
  return false;
}

bool privateFile(const StateStamp& stamp, std::uint64_t device) {
  return stamp.regular && stamp.owned && stamp.links==1 && stamp.device==device && stamp.size>=0;
}
bool privateDirectory(StateHandle handle, const StateStamp& stamp, std::uint64_t device, std::uint64_t mount) {
  return stamp.directory && stamp.owned && stamp.device==device && take(stateMountId(handle))==mount;
}

using Digest=std::array<unsigned char,32>;
Digest contentDigest(StateHandle directory,const fs::path& name,const StateStamp& expected,
    std::stop_token stop) {
  auto file=take(openStateFile(directory,name));
  require(sameStateSnapshot(expected,take(stateStamp(file.get()))),ErrorCode::Conflict,"Restore file changed before reading");
  std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> digest(EVP_MD_CTX_new(),EVP_MD_CTX_free);
  require(digest&&EVP_DigestInit_ex(digest.get(),EVP_sha256(),nullptr)==1,ErrorCode::Io,"Cannot start restore checksum");
  std::array<char,64*1024> buffer;
  std::int64_t remaining=expected.size;
  while (remaining>0) {
    checkpoint(stop);
    const auto count=readStateSequential(file.get(),buffer.data(),static_cast<std::size_t>(
        std::min<std::int64_t>(remaining,buffer.size())));
    require(count>0&&count<=remaining,ErrorCode::Conflict,"Restore file changed while reading");
    require(EVP_DigestUpdate(digest.get(),buffer.data(),static_cast<std::size_t>(count))==1,ErrorCode::Io,"Cannot checksum restore data");
    remaining-=count;
  }
  Digest result{};
  unsigned size=0;
  require(EVP_DigestFinal_ex(digest.get(),result.data(),&size)==1&&size==result.size(),ErrorCode::Io,"Cannot finish restore checksum");
  const auto live=take(stateStampAt(directory,name));
  require(live&&sameStateSnapshot(*live,expected)&&sameStateSnapshot(take(stateStamp(file.get())),expected),
      ErrorCode::Conflict,"Restore file changed while checksumming");
  return result;
}

struct RestoreItem {
  StateStamp identity;
  Digest digest{};
};
using RestoreContents=std::map<fs::path,RestoreItem,FilesystemPathLess>;

void captureRestoreContents(StateHandle directory,const fs::path& relative,std::uint64_t device,std::uint64_t mount,
    RestoreContents& items,unsigned depth,std::stop_token stop) {
  require(depth<=MaximumDepth,ErrorCode::Unsupported,"Too many nested restore directories");
  const auto before=take(stateStamp(directory));
  for (const auto& name : take(listStateDirectoryNames(directory,MaximumEntries,stop))) {
    if (name=="."||name=="..") { continue; }
    checkpoint(stop);
    require(items.size()<MaximumEntries&&!name.empty()&&!name.has_parent_path()&&name!="."&&name!=".."
        && (!relative.empty()||!reserved(name)),ErrorCode::UnsafePath,"Unexpected path in the staged restore");
    const auto stamp=take(stateStampAt(directory,name));
    require(stamp.has_value(),ErrorCode::Conflict,"Staged restore item disappeared");
    const auto path=relative/name;
    if (stamp->directory) {
      const auto child=take(openDirectoryAt(directory,name));
      require(sameStateDirectory(*stamp,take(stateStamp(child.get())))
          &&privateDirectory(child.get(),*stamp,device,mount),ErrorCode::UnsafePath,
          "Restore directories must remain on the profile volume");
      require(items.emplace(path,RestoreItem{*stamp}).second,ErrorCode::Conflict,"Staged restore paths collide");
      captureRestoreContents(child.get(),path,device,mount,items,depth+1,stop);
    } else {
      require(privateFile(*stamp,device),ErrorCode::UnsafePath,"Restore requires an ordinary owned file");
      require(items.emplace(path,RestoreItem{*stamp,contentDigest(directory,name,*stamp,stop)}).second,
          ErrorCode::Conflict,"Staged restore paths collide");
    }
  }
  require(sameStateSnapshot(before,take(stateStamp(directory))),ErrorCode::Conflict,"Staged restore directory changed");
}

std::string encodeRestoreItem(const fs::path& path,const RestoreItem& item) {
  const auto& stamp=item.identity;
  auto result='l'+encoded(platform::pathToUtf8(path))+identity(stamp.inode)+integer(stamp.directory?1:0);
  if (!stamp.directory) {
    result+=integer(stamp.size)+integer(stamp.modified.tv_sec)+integer(stamp.modified.tv_nsec);
    result+=encoded({reinterpret_cast<const char*>(item.digest.data()),item.digest.size()});
  }
  return result+'e';
}

void verifyStagedProfile(StateHandle stage) {
  const auto natural=take(stateStamp(stage));
  require(!take(stateStampAt(stage,".azlock")),ErrorCode::Conflict,"Staged backup contains a profile lock");
  bool recovery=false;
  std::expected<void,Error> acquisition;
  {
    const auto lease=take(ProfileLease::acquire(take(stateDirectoryPath(stage))));
    require(sameStateDirectory(natural,take(stateStamp(lease.directoryHandle()))),ErrorCode::Conflict,
        "Staged backup changed while being validated");
    recovery=hasPendingArchiveRestore(lease)||hasPendingFileOperation(lease)||hasPendingOriginalCompaction(lease)
        ||hasPendingCategoryDeletion(lease)||hasPendingTorrentIntake(lease);
    acquisition=requireNoPendingSubscriptionAcquisition(lease);
  }
  const auto lock=take(stateStampAt(stage,".azlock"));
  if (lock) { take(retireStateFile(stage,".azlock",*lock)); }
  require(!recovery,ErrorCode::Conflict,"Choose a backup without an interrupted profile operation");
  take(std::move(acquisition));
}

std::string restoreRequest(const StateStamp& staged,const RestoreContents& items) {
  auto result="d6:device"+identity(staged.device)+"5:inode"+identity(staged.inode)+"5:itemsl";
  for (const auto& [path,item] : items) {
    result+=encodeRestoreItem(path,item);
    require(result.size()<compatibleConfigLimits().inputBytes-12,ErrorCode::Unsupported,"The restore manifest is too large");
  }
  return result+"e7:versioni1ee";
}
struct RestorePlan {
  std::uint64_t device;
  std::uint64_t inode;
  RestoreContents items;
};

std::string_view bytes(const core::Node* node) {
  require(node&&node->type==core::Node::Type::Bytes,ErrorCode::InvalidFormat,"Invalid profile restore manifest value");
  return node->bytes;
}
std::int64_t number(const core::Node* node) {
  require(node&&node->type==core::Node::Type::Integer,ErrorCode::InvalidFormat,"Invalid profile restore manifest number");
  return node->integer;
}
std::uint64_t fileIdentity(const core::Node* node) {
  const auto value=bytes(node);
  std::uint64_t id=0;
  const auto result=std::from_chars(value.data(),value.data()+value.size(),id);
  require(result.ec==std::errc{}&&result.ptr==value.data()+value.size(),ErrorCode::InvalidFormat,"Invalid restore file identity");
  return id;
}

fs::path restorePath(std::string_view encoded) {
  require(!encoded.empty()&&!encoded.contains('\0'),ErrorCode::InvalidFormat,"Missing restore path");
  auto path=platform::pathFromUtf8(encoded);
  require(!path.empty()&&!path.has_root_path()&&path.lexically_normal()==path&&!reserved(*path.begin())
      &&std::distance(path.begin(),path.end())<=static_cast<std::ptrdiff_t>(MaximumDepth),ErrorCode::UnsafePath,"Restore path escaped the profile");
  require(std::ranges::none_of(path,[](const auto& name) { return name.empty()||name=="."||name==".."; }),
      ErrorCode::UnsafePath,"Invalid restore path component");
  return path;
}

RestorePlan readRestorePlan(const std::string& encoded,bool staged=true) {
  const auto doc=core::Document::parse(encoded,compatibleConfigLimits());
  require(doc&&doc->root().type==core::Node::Type::Dictionary&&number(doc->find("version"))==1,ErrorCode::InvalidFormat,
      "Invalid profile restore manifest");
  RestorePlan plan{fileIdentity(doc->find("device")),fileIdentity(doc->find("inode")),{}};
  const auto* records=doc->find("items");
  require(records&&records->type==core::Node::Type::List
      &&records->children.size()<=MaximumEntries,ErrorCode::InvalidFormat,"Invalid profile restore manifest entries");
  for (const auto& record:records->children) {
    require(record.type==core::Node::Type::List&&(record.children.size()==3||record.children.size()==7),
        ErrorCode::InvalidFormat,"Incomplete profile restore manifest entry");
    const auto& entry=record.children;
    auto path=restorePath(bytes(&entry[0]));
    RestoreItem item;
    auto& stamp=item.identity;
    stamp.device=plan.device;
    stamp.inode=fileIdentity(&entry[1]);
    const auto type=number(&entry[2]);
    require(type==(entry.size()==3?1:0)&&(staged||type==0),ErrorCode::InvalidFormat,"Invalid restore manifest entry type");
    stamp.directory=type==1;
    if (!stamp.directory) {
      stamp.size=number(&entry[3]);
      const auto seconds=number(&entry[4]);
      const auto nanos=number(&entry[5]);
      const auto digest=bytes(&entry[6]);
      require(stamp.size>=0&&nanos>=0&&nanos<1000000000&&digest.size()==item.digest.size()
          &&std::in_range<decltype(stamp.modified.tv_sec)>(seconds),ErrorCode::InvalidFormat,"Invalid restore file attributes");
      stamp.modified={static_cast<decltype(stamp.modified.tv_sec)>(seconds),static_cast<long>(nanos)};
      std::copy(digest.begin(),digest.end(),item.digest.begin());
    }
    require(plan.items.emplace(std::move(path),std::move(item)).second,ErrorCode::Conflict,"Restore manifest paths collide");
  }
  if (!staged) { return plan; }
  for (const auto& [path,item]:plan.items) {
    static_cast<void>(item);
    const auto parent=path.parent_path();
    if (!parent.empty()) {
      const auto parentRecord=plan.items.find(parent);
      require(parentRecord!=plan.items.end()&&parentRecord->second.identity.directory,
          ErrorCode::InvalidFormat,"Restore manifest is missing a parent directory");
    }
  }
  const auto configuration=plan.items.find(Configuration);
  require(configuration!=plan.items.end()&&!configuration->second.identity.directory,ErrorCode::InvalidFormat,
      "Restore manifest does not contain a main configuration");
  return plan;
}

struct RestoreVolume { std::uint64_t device, mount; };

std::optional<StateDescriptor> restoreParent(StateHandle root,const fs::path& relative,RestoreVolume volume,bool create,
    const RestoreContents* staged=nullptr) {
  auto parent=take(duplicateStateDirectory(root));
  fs::path traversed;
  for (const auto& part:relative) {
    traversed/=part;
    auto expected=take(stateStampAt(parent.get(),part));
    if (!expected && !create) { return {}; }
    auto child=expected ? take(openDirectoryAt(parent.get(),part)) : take(createDirectoryAt(parent.get(),part));
    const auto identity=take(stateStamp(child.get()));
    require((!expected||sameStateDirectory(*expected,identity))&&privateDirectory(child.get(),identity,volume.device,volume.mount),
        ErrorCode::UnsafePath,"Restore target directory changed or crossed a mount");
    if (staged) {
      const auto found=staged->find(traversed);
      require(found!=staged->end()&&sameStateIdentity(found->second.identity,identity),
          ErrorCode::Conflict,"Staged restore directory changed");
    }
    if (!expected) { take(syncStateDirectory(parent.get())); }
    parent=std::move(child);
  }
  return parent;
}

bool sameRestoreFile(const StateStamp& actual,const StateStamp& planned) {
  return privateFile(actual,planned.device)&&sameStateIdentity(actual,planned)&&actual.size==planned.size
      &&sameStateTimestamp(actual.modified,planned.modified);
}

struct RestoreFileLocation {
  StateDescriptor source;
  std::optional<StateDescriptor> target;
  std::optional<StateStamp> staged;
  std::optional<StateStamp> current;
};

RestoreFileLocation locateRestoreFile(StateHandle stage,StateHandle profile,const RestorePlan& plan,
    const fs::path& path,const RestoreItem& entry,RestoreVolume volume,bool create,std::stop_token stop) {
  auto sourceParent=restoreParent(stage,path.parent_path(),volume,false,&plan.items);
  require(sourceParent.has_value(),ErrorCode::Conflict,"Staged restore parent disappeared");
  auto target=restoreParent(profile,path.parent_path(),volume,create);
  const auto leaf=path.filename();
  auto staged=take(stateStampAt(sourceParent->get(),leaf));
  auto current=target ? take(stateStampAt(target->get(),leaf)) : std::optional<StateStamp>{};
  if (staged) {
    require(sameRestoreFile(*staged,entry.identity)&&contentDigest(sourceParent->get(),leaf,*staged,stop)==entry.digest,
        ErrorCode::Conflict,"Staged restore file no longer matches the manifest");
    require(!current||privateFile(*current,volume.device),ErrorCode::UnsafePath,"Restore cannot replace this profile item");
  } else {
    require(current&&sameRestoreFile(*current,entry.identity)&&contentDigest(target->get(),leaf,*current,stop)==entry.digest,
        ErrorCode::Conflict,"A partially restored file no longer matches the manifest");
  }
  return {std::move(*sourceParent),std::move(target),staged,current};
}

void requireJournalCurrent(StateHandle root,const char* leaf,const ProfileFileSnapshot& marker) {
  const auto current=take(stateStampAt(root,leaf));
  require(marker.stamp&&current&&sameStateSnapshot(*marker.stamp,*current),ErrorCode::Conflict,"Restore request changed");
}

RestorePlan prepareRestorePreimages(StateHandle profile,StateHandle stage,const RestorePlan& plan,RestoreVolume volume,
    std::stop_token stop) {
  auto prior=take(captureProfileFile(stage,RestorePrior,compatibleConfigLimits().inputBytes));
  const auto root=take(stateStamp(profile));
  if (prior.bytes) {
    auto recorded=readRestorePlan(*prior.bytes,false);
    require(recorded.device==root.device&&recorded.inode==root.inode,ErrorCode::Conflict,"Preimages belong to another profile");
    for (const auto& [path,item]:recorded.items) {
      static_cast<void>(item);
      const auto selected=plan.items.find(path);
      require(selected!=plan.items.end()&&!selected->second.identity.directory,ErrorCode::Conflict,
          "Preimage is outside the staged restore");
    }
    return recorded;
  }
  require(!take(stateStampAt(stage,RestorePrevious)),ErrorCode::Conflict,
      "Preserved files lack their restore preimages");
  RestoreContents before;
  for (const auto& [path,item]:plan.items) {
    checkpoint(stop);
    if (item.identity.directory) { continue; }
    const auto found=locateRestoreFile(stage,profile,plan,path,item,volume,false,stop);
    require(found.staged.has_value(),ErrorCode::Conflict,"A restore move lacks saved preimages");
    if (found.current) {
      before.emplace(path,RestoreItem{*found.current,
          contentDigest(found.target->get(),path.filename(),*found.current,stop)});
    }
  }
  const auto content=restoreRequest(root,before);
  require(core::Document::parse(content,compatibleConfigLimits()).has_value(),ErrorCode::Unsupported,
      "The restore preimages exceed profile limits");
  const auto interrupted=take(captureProfileFile(stage,RestorePriorSaving,compatibleConfigLimits().inputBytes));
  if (interrupted.bytes) {
    require(*interrupted.bytes==content&&interrupted.stamp,ErrorCode::Conflict,"An earlier restore preimage is incomplete");
    take(moveStateFileNoReplace(stage,RestorePriorSaving,stage,RestorePrior,*interrupted.stamp));
  } else {
    auto file=take(stageStateFile(stage,RestorePriorSaving,content,0600));
    const auto stamp=take(stateStamp(file.get()));
    take(publishStateFile(stage,RestorePriorSaving,RestorePrior,file.get(),false,&stamp));
  }
  take(syncStateDirectory(stage));
  return {root.device,root.inode,std::move(before)};
}

std::map<std::size_t,const RestoreItem*> previousRestoreFiles(const RestorePlan& plan,const RestorePlan& before) {
  const auto configuration=plan.items.find(Configuration);
  std::map<std::size_t,const RestoreItem*> result;
  std::size_t sequence=0;
  for (auto entry=plan.items.begin();entry!=plan.items.end();++entry,++sequence) {
    const auto found=before.items.find(entry->first);
    if (found!=before.items.end()) {
      result.emplace(entry==configuration?plan.items.size():sequence,&found->second);
    }
  }
  return result;
}

bool applyRestoreFiles(StateHandle profile,StateHandle stage,const RestorePlan& plan,const ProfileFileSnapshot& marker,
    RestoreVolume volume,std::stop_token stop) {
  for (const auto& [path,item]:plan.items) {
    checkpoint(stop);
    if (item.identity.directory) {
      require(restoreParent(stage,path,volume,false,&plan.items).has_value(),ErrorCode::Conflict,
          "Staged restore directory is missing");
      static_cast<void>(restoreParent(profile,path,volume,false));
    } else { static_cast<void>(locateRestoreFile(stage,profile,plan,path,item,volume,false,stop)); }
  }
  requireJournalCurrent(profile,RestoreRequest,marker);
  const auto before=prepareRestorePreimages(profile,stage,plan,volume,stop);
  const auto preimageRecord=take(captureProfileFile(stage,RestorePrior,compatibleConfigLimits().inputBytes));
  auto previous=restoreParent(stage,RestorePrevious,volume,true);
  const auto saved=previousRestoreFiles(plan,before);
  require(previous&&stateAccessRestricted(previous->get(),false),ErrorCode::UnsafePath,"Restore preservation folder is unsafe");
  for (const auto& [path,item]:plan.items) {
    if (!item.identity.directory) { continue; }
    checkpoint(stop);
    requireJournalCurrent(profile,RestoreRequest,marker);
    static_cast<void>(restoreParent(profile,path,volume,true));
  }

  const auto applyFile=[&](const fs::path& path,const RestoreItem& item,std::size_t sequence) {
    checkpoint(stop);
    auto location=locateRestoreFile(stage,profile,plan,path,item,volume,true,stop);
    require(location.target.has_value(),ErrorCode::Conflict,"Restore destination is unavailable");
    auto& target=*location.target;
    const auto leaf=path.filename();
    const auto previousName=std::to_string(sequence);
    const auto preserved=take(stateStampAt(previous->get(),previousName));
    const auto expected=saved.find(sequence);
    if (expected==saved.end()) {
      require(!preserved&&(!location.staged||!location.current),ErrorCode::Conflict,
          "Restore target appeared after the preimages were saved");
    } else {
      const auto& record=*expected->second;
      if (preserved) {
        require((!location.staged||!location.current)&&sameRestoreFile(*preserved,record.identity)
            &&contentDigest(previous->get(),previousName,*preserved,stop)==record.digest,ErrorCode::Conflict,
            "Previously saved profile file changed");
      } else {
        require(location.staged&&location.current&&sameRestoreFile(*location.current,record.identity)
            &&contentDigest(target.get(),leaf,*location.current,stop)==record.digest,ErrorCode::Conflict,
            "Profile file changed after its restore preimage was saved");
      }
    }
    if (!location.staged) { return; }
    requireJournalCurrent(profile,RestoreRequest,marker);
    requireJournalCurrent(stage,RestorePrior,preimageRecord);
    if (location.current) {
      take(moveStateFileNoReplace(target.get(),leaf,previous->get(),previousName,*location.current));
      take(syncStateDirectory(previous->get()));
      take(syncStateDirectory(target.get()));
    }
    take(moveStateFileNoReplace(location.source.get(),leaf,target.get(),leaf,*location.staged));
    take(syncStateDirectory(target.get()));
    take(syncStateDirectory(location.source.get()));
  };

  std::size_t sequence=0;
  const auto configuration=plan.items.find(Configuration);
  for (auto entry=plan.items.begin();entry!=plan.items.end();++entry,++sequence) {
    if (entry!=configuration&&!entry->second.identity.directory) { applyFile(entry->first,entry->second,sequence); }
  }
  applyFile(configuration->first,configuration->second,sequence);
  requireJournalCurrent(profile,RestoreRequest,marker);
  take(moveStateFileNoReplace(profile,RestoreRequest,profile,RestoreApplied,*marker.stamp));
  take(syncStateDirectory(profile));
  return true;
}

void cleanupAppliedRestore(StateHandle profile,const ProfileFileSnapshot& marker,RestoreVolume volume) {
  const auto plan=readRestorePlan(*marker.bytes);
  const auto stageStamp=take(stateStampAt(profile,RestoreDirectory));
  if (stageStamp) {
    auto stage=take(openDirectoryAt(profile,RestoreDirectory));
    require(stageStamp->inode==plan.inode&&privateDirectory(stage.get(),*stageStamp,volume.device,volume.mount),
        ErrorCode::Conflict,"Restore cleanup source changed");
    const auto preimages=take(captureProfileFile(stage.get(),RestorePrior,compatibleConfigLimits().inputBytes));
    const auto previousStamp=take(stateStampAt(stage.get(),RestorePrevious));
    if (previousStamp) {
      auto previous=take(openDirectoryAt(stage.get(),RestorePrevious));
      require(stateAccessRestricted(previous.get(),false)&&privateDirectory(previous.get(),*previousStamp,volume.device,volume.mount),
          ErrorCode::UnsafePath,"Restore preservation folder changed");
      require(preimages.bytes.has_value(),ErrorCode::Conflict,"Restore cleanup lacks its saved preimages");
      const auto before=readRestorePlan(*preimages.bytes,false);
      const auto root=take(stateStamp(profile));
      require(before.inode==root.inode&&before.device==root.device,ErrorCode::Conflict,"Restore cleanup belongs to another profile");
      const auto saved=previousRestoreFiles(plan,before);
      const auto names=take(listStateDirectoryNames(previous.get(),MaximumEntries,{}));
      std::vector<std::pair<fs::path,StateStamp>> retired;
      for (const auto& name:names) {
        if (name=="."||name=="..") { continue; }
        const auto value=platform::pathToUtf8(name);
        std::size_t index=0;
        const auto decoded=std::from_chars(value.data(),value.data()+value.size(),index);
        require(decoded.ec==std::errc{}&&decoded.ptr==value.data()+value.size()&&index<=plan.items.size(),
            ErrorCode::Conflict,"Unexpected file in restore preservation folder");
        const auto stamp=take(stateStampAt(previous.get(),name));
        const auto expected=saved.find(index);
        require(stamp&&expected!=saved.end()&&sameRestoreFile(*stamp,expected->second->identity)
            &&contentDigest(previous.get(),name,*stamp,{})==expected->second->digest,ErrorCode::Conflict,
            "Preserved profile file changed");
        retired.emplace_back(name,*stamp);
      }
      for (const auto& [name,stamp]:retired) {
        requireJournalCurrent(profile,RestoreApplied,marker);
        take(retireStateFile(previous.get(),name,stamp));
      }
      previous={};
      take(retireEmptyDirectoryAt(stage.get(),RestorePrevious,*previousStamp));
    }
    for (auto entry=plan.items.rbegin();entry!=plan.items.rend();++entry) {
      if (!entry->second.identity.directory) { continue; }
      auto parent=restoreParent(stage.get(),entry->first.parent_path(),volume,false,&plan.items);
      if (!parent) { continue; }
      const auto leaf=entry->first.filename();
      const auto found=take(stateStampAt(parent->get(),leaf));
      if (found) {
        require(sameStateIdentity(*found,entry->second.identity),ErrorCode::Conflict,"Staged restore cleanup folder changed");
        take(retireEmptyDirectoryAt(parent->get(),leaf,*found));
      }
    }
    if (preimages.stamp) {
      take(retireStateFile(stage.get(),RestorePrior,*preimages.stamp));
      take(syncStateDirectory(stage.get()));
    }
    stage={};
    take(retireEmptyDirectoryAt(profile,RestoreDirectory,*stageStamp));
  }
  requireJournalCurrent(profile,RestoreApplied,marker);
  take(retireStateFile(profile,RestoreApplied,*marker.stamp));
  take(syncStateDirectory(profile));
}
} // namespace

std::expected<void, Error> publishProfileRestoreRequest(StateHandle profile, const StateStamp& staged,
    std::stop_token stop) try {
  const auto stage=take(openDirectoryAt(profile,RestoreDirectory));
  const auto parent=take(stateStamp(profile));
  const auto mount=take(stateMountId(profile));
  require(sameStateDirectory(staged,take(stateStamp(stage.get())))
      &&privateDirectory(stage.get(),staged,parent.device,mount),ErrorCode::Conflict,"Staged profile restore changed");
  verifyStagedProfile(stage.get());
  RestoreContents items;
  captureRestoreContents(stage.get(),{},parent.device,mount,items,1,stop);
  const auto configuration=items.find(Configuration);
  require(configuration!=items.end()&&!configuration->second.identity.directory,ErrorCode::Missing,
      "Staged profile restore lacks its main configuration");
  const auto bytes=restoreRequest(staged,items);
  require(core::Document::parse(bytes,compatibleConfigLimits()).has_value(),ErrorCode::Unsupported,"The restore manifest exceeds profile limits");
  checkpoint(stop);
  auto marker=take(stageStateFile(profile,RestoreRequestSaving,bytes,0600));
  const auto stamp=take(stateStamp(marker.get()));
  take(publishStateFile(profile,RestoreRequestSaving,RestoreRequest,marker.get(),false,&stamp));
  take(syncStateDirectory(profile));
  return {};
} catch (const Error& error) {
  return std::unexpected(error);
} catch (const std::exception&) {
  return std::unexpected(Error{ErrorCode::Io,"Cannot publish the pending profile restore"});
}
}

namespace biglybtc::storage {

std::expected<bool, Error> applyPendingProfileRestore(const ProfileLease& lease,std::stop_token stop) try {
  using namespace detail;
  checkpoint(stop);
  auto access=take(lease.sourceAccess());
  const auto root=lease.directoryHandle();
  const auto ready=take(captureProfileFile(root,RestoreRequest,compatibleConfigLimits().inputBytes));
  const auto applied=take(captureProfileFile(root,RestoreApplied,compatibleConfigLimits().inputBytes));
  if (!ready.bytes&&!applied.bytes) { return false; }
  auto mutation=take(lease.sourceAccess(true));
  const auto stamp=take(stateStamp(root));
  const RestoreVolume volume{stamp.device,take(stateMountId(root))};
  if (!ready.bytes) {
    cleanupAppliedRestore(root,applied,volume);
    return false;
  }
  require(!applied.bytes&&!take(stateStampAt(root,RestoreRequestSaving)),ErrorCode::Conflict,"Multiple profile restore requests");
  require(!hasPendingArchiveRestore(lease)&&!hasPendingFileOperation(lease)&&!hasPendingOriginalCompaction(lease)
      &&!hasPendingCategoryDeletion(lease)&&!hasPendingTorrentIntake(lease),
      ErrorCode::Conflict,"Finish pending profile recovery before applying the backup restore");
  take(requireNoPendingSubscriptionAcquisition(lease));
  const auto plan=readRestorePlan(*ready.bytes);
  auto stage=take(openDirectoryAt(root,RestoreDirectory));
  const auto staged=take(stateStamp(stage.get()));
  require(staged.inode==plan.inode&&staged.device==plan.device&&privateDirectory(stage.get(),staged,volume.device,volume.mount)
      &&stateAccessRestricted(stage.get(),false),ErrorCode::Conflict,"The staged backup directory was replaced");
  const auto completed=applyRestoreFiles(root,stage.get(),plan,ready,volume,stop);
  stage={};
  const auto marker=take(captureProfileFile(root,RestoreApplied,compatibleConfigLimits().inputBytes));
  require(marker.bytes&&marker.bytes==ready.bytes,ErrorCode::Conflict,"Completed restore receipt changed");
  cleanupAppliedRestore(root,marker,volume);
  return completed;
} catch (const Error& error) {
  return std::unexpected(error);
} catch (const std::exception&) {
  return std::unexpected(Error{ErrorCode::Io,"Cannot complete the pending profile restore"});
}

} // namespace biglybtc::storage
