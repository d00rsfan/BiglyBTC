#include "../platform/utf8_path.hpp"
#include "original_storage_defaults.hpp"
#include "biglybtc/file_policy_restoration.hpp"
#include "original_filename_policy.hpp"
#include "state_file_io.hpp"

#include <algorithm>
#include <limits>
#include <exception>
#include <new>
#include <utility>

namespace biglybtc::storage {
namespace {
namespace fs = std::filesystem;
using Node = core::Node;
using Kind = OriginalStorageKind;
using detail::StateDescriptor;
using detail::StateHandle;
using detail::StateStamp;
using detail::stateStamp;
using detail::stateStampAt;
using detail::sameStateIdentity;
using platform::pathFromUtf8;
using platform::pathToUtf8;
struct Failure { Error error; };
void require(bool value, ErrorCode code, const char* message) {
  if (!value) { throw Failure{{code, message}}; }
}
template<class F> auto guarded(F&& action) -> decltype(action()) {
  try { return action(); }
  catch (const Failure& e) { return std::unexpected(e.error); }
  catch (const std::bad_alloc&) { return std::unexpected(Error{ErrorCode::Io,"Original defaults allocation failed"}); }
  catch (const std::exception& e) { return std::unexpected(Error{ErrorCode::Io,e.what()}); }
}
template<class T> T take(std::expected<T,Error>&& value) {
  if (!value) { throw Failure{value.error()}; }
  return std::move(*value);
}
fs::path clean(fs::path value) {
  require(value.is_absolute() && value.native().size()<=4096 &&
      value.native().find('\0')==value.native().npos && value.native().find('\n')==value.native().npos,
      ErrorCode::UnsafePath,"Original save path must be bounded and absolute");
  for (const auto& part:value.relative_path()) {
    require(part!="." && part!=".." && part.native().size()<=255,
        ErrorCode::UnsafePath,"Unsafe original save path component");
  }
  while (value!=value.root_path() && value.filename().empty()) { value=value.parent_path(); }
  require(value.lexically_normal()==value,ErrorCode::UnsafePath,"Original save path is not normalized");
  return value;
}
void leaf(const fs::path& value) {
  require(!value.empty() && !value.has_root_path() && value.filename()==value &&
      value!="." && value!=".." && value.native().size()<=255 &&
      value.native().find('\0')==value.native().npos && value.native().find('\n')==value.native().npos,
      ErrorCode::UnsafePath,"Original save filename is not one safe component");
}
// Return absent at the first missing component. Existing ancestors are opened
// descriptor-relatively and every named/opened directory identity is compared.
std::optional<StateStamp> observe(const fs::path& path) {
  const auto root=detail::filesystemRoot(path);
  auto current=take(detail::openFilesystemRoot(root));
  if (path==root) { return take(stateStamp(current.get())); }
  const auto parts=root==path.root_path()?path.relative_path():path.lexically_relative(root);
  require(!parts.empty(),ErrorCode::UnsafePath,"Original save path is outside its filesystem root");
  for (auto it=parts.begin();it!=parts.end();++it) {
    const auto named=stateStampAt(current.get(),*it);
    require(named.has_value(),ErrorCode::UnsafePath,"Original save path is inaccessible");
    if (!*named) { return {}; }
    if (std::next(it)==parts.end()) {
      require((*named)->directory||(*named)->regular,ErrorCode::UnsafePath,"Original save path is not a regular file or directory");
      require(!(*named)->regular||(*named)->owned,ErrorCode::UnsafePath,"Original save file must be owned");
      return **named;
    }
    require((*named)->directory,ErrorCode::UnsafePath,"Original save ancestor is not a directory");
    auto opened=detail::openDirectoryAt(current.get(),*it);
    require(opened.has_value(),ErrorCode::Conflict,"Original save directory changed during inspection");
    const auto actual=stateStamp(opened->get());
    const auto again=stateStampAt(current.get(),*it);
    require(actual&&again&&*again&&sameStateIdentity(*actual,**named)&&sameStateIdentity(**again,**named),
        ErrorCode::Conflict,"Original save directory changed during inspection");
    current=std::move(*opened);
  }
  return {};
}

core::SavedDownload resolved(const core::SavedDownload& saved,const fs::path& root) {
  leaf(root.filename());
  auto copy=saved; copy.saveDirectory=pathToUtf8(clean(root.parent_path()));
  copy.saveFile=pathToUtf8(root.filename()); copy.legacySavePath=false; return copy;
}
std::int64_t integer(const Node& n,std::string_view key,std::int64_t fallback) {
  const auto* value=n.find(key); if (!value) { return fallback; }
  require(value->type==Node::Type::Integer,ErrorCode::InvalidFormat,"Invalid original storage default integer");
  return value->integer;
}
Kind kind(const Node& node) {
  require(node.type==Node::Type::Bytes,ErrorCode::InvalidFormat,"Original storage type is not bytes");
  if (node.bytes=="L") { return Kind::Linear; } if (node.bytes=="C") { return Kind::Compact; }
  if (node.bytes=="R") { return Kind::Reordered; } if (node.bytes=="X") { return Kind::ReorderedCompact; }
  throw Failure{{ErrorCode::Unsupported,"Unknown original physical storage type"}};
}
std::optional<StateStamp> marker(StateHandle directory,const std::string& name) {
  const auto stamp=stateStampAt(directory,name);
  require(stamp.has_value(),ErrorCode::UnsafePath,"Cannot inspect original storage control");
  if (!*stamp) { return {}; }
  const auto& s=**stamp;
  require(s.regular&&s.owned&&s.links==1&&s.size>=0&&s.size<=16*1024*1024,
      ErrorCode::UnsafePath,"Original storage control is unsafe or oversized");
  return s;
}
void directoryIdentity(StateHandle parent,const std::string& name,StateHandle child) {
  const auto named=stateStampAt(parent,name);
  const auto opened=stateStamp(child);
  require(named&&*named&&opened&&(*named)->directory&&(*named)->owned&&sameStateIdentity(**named,*opened),
      ErrorCode::Conflict,"Original control directory changed");
}
StateDescriptor childDirectory(StateHandle parent,const std::string& name) {
  const auto named=stateStampAt(parent,name);
  require(named.has_value(),ErrorCode::UnsafePath,"Cannot inspect original control directory");
  if (!*named) { return {}; }
  require((*named)->directory&&(*named)->owned,ErrorCode::UnsafePath,"Unsafe original control directory");
  auto child=detail::openStateDirectory(parent,name);
  require(child.has_value(),ErrorCode::UnsafePath,"Cannot open original control directory");
  directoryIdentity(parent,name,child->get());
  return std::move(*child);
}
}

std::expected<core::SavedDownload,Error> resolveOriginalSaveRoot(const core::SavedDownload& saved,
    const compat::OriginalLayout& layout) {
  return guarded([&]() -> std::expected<core::SavedDownload,Error> {
    const auto path=clean(pathFromUtf8(saved.saveDirectory));
    if (saved.saveFile) { leaf(pathFromUtf8(*saved.saveFile));const auto directory=observe(path);
      require(!directory||directory->directory,ErrorCode::UnsafePath,"Original save parent is not a directory");
      return resolved(saved,path/pathFromUtf8(*saved.saveFile)); }
    leaf(pathFromUtf8(layout.name)); const auto state=observe(path);
    if (!layout.multifile) { return resolved(saved,state&&state->directory?path/pathFromUtf8(layout.name):path); }
    require(!state||state->directory,ErrorCode::UnsafePath,"Multifile original save path is not a directory");
    return resolved(saved,state&&path.filename()==pathFromUtf8(layout.name)?path:path/pathFromUtf8(layout.name));
  });
}
std::expected<core::SavedDownload,Error> restoreOriginalSaveRoot(const core::SavedDownload& saved,
    const compat::OriginalLayout& layout,const fs::path& approvedRoot) {
  return guarded([&]() -> std::expected<core::SavedDownload,Error> {
    const auto path=clean(pathFromUtf8(saved.saveDirectory)),root=clean(approvedRoot);
    if (saved.saveFile) { leaf(pathFromUtf8(*saved.saveFile)); require(root==path/pathFromUtf8(*saved.saveFile),ErrorCode::Conflict,"Captured original root differs from saved fields"); }
    else {
      leaf(pathFromUtf8(layout.name));
      require(root==path/pathFromUtf8(layout.name)||((!layout.multifile||path.filename()==pathFromUtf8(layout.name))&&root==path),
          ErrorCode::Conflict,"Captured root is not an original save-path interpretation");
    }
    return resolved(saved,root);
  });
}

std::expected<ResolvedOriginalStorageTypes,Error> resolveOriginalStorageTypes(const ProfileLease& lease,
    const core::TorrentHash& hash,const compat::OriginalLayout& layout,const Node& attributes) {
  return guarded([&]() -> std::expected<ResolvedOriginalStorageTypes,Error> {
    require(lease.valid()&&attributes.type==Node::Type::Dictionary&&layout.files.size()<=100000,
        ErrorCode::InvalidFormat,"Invalid original storage-default input");
    ResolvedOriginalStorageTypes out; const auto* stores=attributes.find("storetypes");
    require(!stores||stores->type==Node::Type::List,ErrorCode::InvalidFormat,"Original storage types are not a list");
    out.synthesized=!stores||stores->children.empty();
    if (!out.synthesized) {
      require(stores->children.size()==layout.files.size(),ErrorCode::InvalidFormat,"Original storage count differs from metadata");
      for (const auto& n:stores->children) { out.requested.push_back(kind(n)); }
    } else {
      bool reorder=false; std::int64_t threshold=10;
      auto config=ConfigFile::load(lease,"biglybt.config");
      if (!config) {
        require(config.error().code==ErrorCode::Missing,config.error().code,"Cannot read original storage defaults");
        for(const auto* name:{"biglybt.config","biglybt.config.bak","biglybt.config.saving"}) {
          const auto candidate=stateStampAt(lease.directoryHandle(),name);
          require(candidate&&!*candidate,
              ErrorCode::InvalidFormat,"Present original settings have no readable storage defaults");
        }
      }
      else {
        require(config->recoverySource()!=RecoverySource::Saving,ErrorCode::Conflict,"Original storage defaults need recovery");
        const auto& root=config->document().root();
        const auto enabled=integer(root,"Enable reorder storage mode",0);
        require(enabled>=std::numeric_limits<std::int32_t>::min()&&enabled<=std::numeric_limits<std::int32_t>::max(),
            ErrorCode::InvalidFormat,"Original reorder boolean exceeds Java int");
        reorder=enabled!=0;
        threshold=integer(root,"Reorder storage mode min MB",10);
      }
      require(threshold>=std::numeric_limits<std::int32_t>::min()&&threshold<=std::numeric_limits<std::int32_t>::max(),
          ErrorCode::InvalidFormat,"Original reorder threshold exceeds Java int");
      auto local=integer(attributes,"reordermb",-1);
      require(local>=std::numeric_limits<std::int32_t>::min()&&local<=std::numeric_limits<std::int32_t>::max(),
          ErrorCode::InvalidFormat,"Original per-download reorder threshold exceeds Java int");
      if (reorder&&local<0) { local=threshold; out.reorderMinimumMiB=local; }
      for (const auto& file:layout.files) {
        require(file.bytes>=0,ErrorCode::InvalidFormat,"Negative original file length");
        out.requested.push_back(reorder&&file.bytes/(1024*1024)>=local?Kind::Reordered:Kind::Linear);
      }
    }
    out.actual=out.requested;
    auto active=childDirectory(lease.directoryHandle(),"active");
    if (active.get()==detail::InvalidStateHandle) { return out; }
    const auto name=hash.hex(); auto controls=childDirectory(active.get(),name);
    if (controls.get()==detail::InvalidStateHandle) { directoryIdentity(lease.directoryHandle(),"active",active.get()); return out; }
    for (std::size_t i=0;i<layout.files.size();++i) {
      bool compact=false,reordered=false;
      for (const bool r:{false,true}) {
        const auto base=originalControlLeaf(i,r?Kind::Reordered:Kind::Compact);
        for (const auto* suffix:{".saving",".bak.saving"}) {
          const auto pending=stateStampAt(controls.get(),base+suffix);
          require(pending&&!*pending,
              ErrorCode::Conflict,"Original storage control recovery is pending");
        }
        for (const auto* suffix:{"",".bak"}) {
          const auto leafName=base+suffix; const auto s=marker(controls.get(),leafName); if (!s) { continue; }
          (r?reordered:compact)=true;
          out.markers.push_back({i,leafName,s->device,s->inode,
              static_cast<std::uint64_t>(s->size),s->modified.tv_sec,s->modified.tv_nsec,s->changed.tv_sec,s->changed.tv_nsec});
        }
      }
      require(!(compact&&reordered),ErrorCode::Conflict,"Both compact and reordered control families exist");
      const auto desired=out.requested[i];
      if (compact) { out.actual[i]=Kind::Compact; out.unsupportedConversion|=desired==Kind::Reordered||desired==Kind::ReorderedCompact; }
      else if (reordered) {
        out.actual[i]=desired==Kind::Reordered?Kind::Reordered:Kind::ReorderedCompact;
        out.unsupportedConversion|=desired==Kind::Linear||desired==Kind::Compact;
      }
    }
    directoryIdentity(active.get(),name,controls.get()); directoryIdentity(lease.directoryHandle(),"active",active.get());
    return out;
  });
}
std::expected<OriginalFilePolicies,Error> readOriginalFilePolicies(
    const Node& saved,std::span<const OriginalStorageKind> requested) {
  return guarded([&]() -> std::expected<OriginalFilePolicies,Error> {
    auto projected=compat::originalFilePolicy(saved,requested.size());
    require(projected.has_value(),ErrorCode::InvalidFormat,"Invalid original saved file-policy projection");
    std::vector<compat::FilePolicy> initial(requested.size());
    for(std::size_t index=0;index<requested.size();++index){
      const auto kind=requested[index];
      require(kind>=Kind::Linear&&kind<=Kind::ReorderedCompact,ErrorCode::InvalidFormat,"Invalid constructor storage kind");
      initial[index].skipped=kind==Kind::Compact||kind==Kind::ReorderedCompact;
    }
    auto restored=compat::originalRestoredFilePolicy(*projected,initial);
    require(restored.has_value(),ErrorCode::InvalidFormat,"Incomplete original restored file-policy census");
    return OriginalFilePolicies{std::move(*projected),std::move(*restored)};
  });
}

std::expected<void,Error> applyOriginalStorageDefaults(core::Document& attributes,
    const ResolvedOriginalStorageTypes& resolved,bool physicalKinds) {
  return guarded([&]() -> std::expected<void,Error> {
    require(attributes.root().type==Node::Type::Dictionary&&resolved.requested.size()==resolved.actual.size(),
        ErrorCode::InvalidFormat,"Invalid resolved original storage types");
    std::string list="l";
    for (const auto kind:physicalKinds?resolved.actual:resolved.requested) {
      require(kind>=Kind::Linear&&kind<=Kind::ReorderedCompact,ErrorCode::InvalidFormat,"Invalid resolved storage kind");
      list+=kind==Kind::Linear?"1:L":kind==Kind::Compact?"1:C":kind==Kind::Reordered?"1:R":"1:X";
    }
    list+='e'; require(attributes.replaceValue("storetypes",list).has_value(),ErrorCode::InvalidFormat,"Cannot materialize original storage types");
    if (resolved.reorderMinimumMiB) {
      require(attributes.replaceValue("reordermb",'i'+std::to_string(*resolved.reorderMinimumMiB)+'e').has_value(),
          ErrorCode::InvalidFormat,"Cannot capture original reorder threshold");
    }
    return {};
  });
}
#include "original_filename_policy.inc"
} // namespace biglybtc::storage
