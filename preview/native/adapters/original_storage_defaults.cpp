#include "original_storage_defaults.hpp"
#include "original_filename_policy.hpp"
#include "file_io.hpp"

#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <exception>
#include <new>
#include <utility>
#include <sys/stat.h>
#include <unistd.h>

namespace biglybtc::storage {
namespace {
namespace fs = std::filesystem;
using Node = core::Node;
using Kind = OriginalStorageKind;
using Fd = detail::Descriptor;
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
      value.native().find_first_of(std::string("\0\n",2))==std::string::npos,
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
  require(!value.empty() && !value.is_absolute() && value.filename()==value &&
      value!="." && value!=".." && value.native().size()<=255 &&
      value.native().find_first_of(std::string("\0\n",2))==std::string::npos,
      ErrorCode::UnsafePath,"Original save filename is not one safe component");
}
// Return absent at the first missing component. Existing ancestors are opened
// descriptor-relatively and every named/opened directory identity is compared.
std::optional<struct stat> observe(const fs::path& path) {
  auto current=Fd(::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC));
  require(current.get()>=0,ErrorCode::Io,"Cannot inspect filesystem root");
  if (path==path.root_path()) { struct stat s{}; require(::fstat(current.get(),&s)==0,ErrorCode::Io,"Cannot inspect root"); return s; }
  auto parts=path.relative_path();
  for (auto it=parts.begin();it!=parts.end();++it) {
    struct stat named{};
    if (::fstatat(current.get(),it->c_str(),&named,AT_SYMLINK_NOFOLLOW)!=0) {
      require(errno==ENOENT,ErrorCode::UnsafePath,"Original save path is inaccessible"); return {};
    }
    require(!S_ISLNK(named.st_mode),ErrorCode::UnsafePath,"Symbolic original save paths are not authorized");
    auto next=it; ++next;
    if (next==parts.end()) {
      require(S_ISDIR(named.st_mode)||S_ISREG(named.st_mode),ErrorCode::UnsafePath,"Original save path is not a file/directory");
      require(!S_ISREG(named.st_mode)||named.st_uid==::geteuid(),ErrorCode::UnsafePath,"Original save file must be owned");
      return named;
    }
    require(S_ISDIR(named.st_mode),ErrorCode::UnsafePath,"Original save ancestor is not a directory");
    auto opened=Fd(::openat(current.get(),it->c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
    struct stat actual{},again{};
    require(opened.get()>=0 && ::fstat(opened.get(),&actual)==0 &&
        ::fstatat(current.get(),it->c_str(),&again,AT_SYMLINK_NOFOLLOW)==0 &&
        actual.st_dev==named.st_dev && actual.st_ino==named.st_ino &&
        again.st_dev==named.st_dev && again.st_ino==named.st_ino,
        ErrorCode::Conflict,"Original save directory changed during inspection");
    current=std::move(opened);
  }
  return {};
}
core::SavedDownload resolved(const core::SavedDownload& saved,const fs::path& root) {
  leaf(root.filename());
  auto copy=saved; copy.saveDirectory=clean(root.parent_path()).string();
  copy.saveFile=root.filename().string(); copy.legacySavePath=false; return copy;
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
std::optional<struct stat> marker(int directory,const std::string& name) {
  struct stat s{};
  if (::fstatat(directory,name.c_str(),&s,AT_SYMLINK_NOFOLLOW)!=0) {
    require(errno==ENOENT,ErrorCode::UnsafePath,"Cannot inspect original storage control"); return {};
  }
  require(S_ISREG(s.st_mode)&&s.st_uid==::geteuid()&&s.st_nlink==1&&s.st_size>=0&&s.st_size<=16*1024*1024,
      ErrorCode::UnsafePath,"Original storage control is unsafe or oversized");
  return s;
}
void directoryIdentity(int parent,const std::string& name,int child) {
  struct stat named{},opened{};
  require(::fstatat(parent,name.c_str(),&named,AT_SYMLINK_NOFOLLOW)==0&&
      ::fstat(child,&opened)==0&&S_ISDIR(named.st_mode)&&named.st_uid==::geteuid()&&
      named.st_dev==opened.st_dev&&named.st_ino==opened.st_ino,
      ErrorCode::Conflict,"Original control directory changed");
}
Fd childDirectory(int parent,const std::string& name) {
  struct stat s{};
  if (::fstatat(parent,name.c_str(),&s,AT_SYMLINK_NOFOLLOW)!=0) {
    require(errno==ENOENT,ErrorCode::UnsafePath,"Cannot inspect original control directory"); return Fd(-1);
  }
  require(S_ISDIR(s.st_mode)&&s.st_uid==::geteuid(),ErrorCode::UnsafePath,"Unsafe original control directory");
  auto child=Fd(::openat(parent,name.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
  require(child.get()>=0,ErrorCode::UnsafePath,"Cannot open original control directory");
  directoryIdentity(parent,name,child.get()); return child;
}
}

std::expected<core::SavedDownload,Error> resolveOriginalSaveRoot(const core::SavedDownload& saved,
    const compat::OriginalLayout& layout) {
  return guarded([&]() -> std::expected<core::SavedDownload,Error> {
    const auto path=clean(fs::path(saved.saveDirectory));
    if (saved.saveFile) { leaf(fs::path(*saved.saveFile));const auto directory=observe(path);
      require(!directory||S_ISDIR(directory->st_mode),ErrorCode::UnsafePath,"Original save parent is not a directory");
      return resolved(saved,path/ *saved.saveFile); }
    leaf(fs::path(layout.name)); const auto state=observe(path);
    if (!layout.multifile) { return resolved(saved,state&&S_ISDIR(state->st_mode)?path/layout.name:path); }
    require(!state||S_ISDIR(state->st_mode),ErrorCode::UnsafePath,"Multifile original save path is not a directory");
    return resolved(saved,state&&path.filename()==fs::path(layout.name)?path:path/layout.name);
  });
}
std::expected<core::SavedDownload,Error> restoreOriginalSaveRoot(const core::SavedDownload& saved,
    const compat::OriginalLayout& layout,const fs::path& approvedRoot) {
  return guarded([&]() -> std::expected<core::SavedDownload,Error> {
    const auto path=clean(fs::path(saved.saveDirectory)),root=clean(approvedRoot);
    if (saved.saveFile) { leaf(fs::path(*saved.saveFile)); require(root==path/ *saved.saveFile,ErrorCode::Conflict,"Captured original root differs from saved fields"); }
    else {
      leaf(fs::path(layout.name));
      require(root==path/layout.name||((!layout.multifile||path.filename()==fs::path(layout.name))&&root==path),
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
          struct stat candidate{};require(::fstatat(lease.directoryHandle(),name,&candidate,AT_SYMLINK_NOFOLLOW)!=0&&errno==ENOENT,
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
    if (active.get()<0) { return out; }
    const auto name=hash.hex(); auto controls=childDirectory(active.get(),name);
    if (controls.get()<0) { directoryIdentity(lease.directoryHandle(),"active",active.get()); return out; }
    for (std::size_t i=0;i<layout.files.size();++i) {
      bool compact=false,reordered=false;
      for (const bool r:{false,true}) {
        const auto base=originalControlLeaf(i,r?Kind::Reordered:Kind::Compact);
        for (const auto* suffix:{".saving",".bak.saving"}) {
          struct stat pending{};
          require(::fstatat(controls.get(),(base+suffix).c_str(),&pending,AT_SYMLINK_NOFOLLOW)!=0&&errno==ENOENT,
              ErrorCode::Conflict,"Original storage control recovery is pending");
        }
        for (const auto* suffix:{"",".bak"}) {
          const auto leafName=base+suffix; const auto s=marker(controls.get(),leafName); if (!s) { continue; }
          (r?reordered:compact)=true;
          out.markers.push_back({i,leafName,static_cast<std::uint64_t>(s->st_dev),static_cast<std::uint64_t>(s->st_ino),
              static_cast<std::uint64_t>(s->st_size),s->st_mtim.tv_sec,s->st_mtim.tv_nsec,s->st_ctim.tv_sec,s->st_ctim.tv_nsec});
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
