// SPDX-License-Identifier: GPL-2.0-or-later
#include "verified_existing_scope.hpp"
#include "selected_existing_scope.hpp"
#include "intake_destination_snapshot.hpp"
#include "file_operation_digest.hpp"
#include "original_payload_io.hpp"
#include "original_storage_defaults.hpp"
#include "state_file_io.hpp"
#include "../platform/utf8_path.hpp"
#include "biglybtc/existing_piece_goal.hpp"
#include "biglybtc/torrent_intake_options.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace biglybtc::storage {
namespace {
namespace fs = std::filesystem;
using Node = core::Node;
using fileop::Failure;
using fileop::require;
using platform::pathFromUtf8;
using platform::pathToUtf8;
using detail::StateDescriptor;
using detail::StateStamp;
using detail::StateHandle;
using detail::FilesystemPathLess;
using detail::stateStamp;
using detail::stateStampAt;
using detail::sameStateSnapshot;
using detail::sameStateIdentity;
using detail::OriginalPayloadAccess;

constexpr std::size_t MaximumInventory = 64 * 1024 * 1024;

template<class T> T take(std::expected<T, Error> result) {
  if (!result) { throw Failure{result.error()}; }
  return std::move(*result);
}

std::string b(std::string_view value) { return std::to_string(value.size()) + ':' + std::string(value); }
std::string i(std::int64_t value) { return 'i' + std::to_string(value) + 'e'; }

core::Document parse(std::string bytes) {
  auto limits = compatibleConfigLimits();
  limits.inputBytes = MaximumInventory;
  limits.stringBytes = MaximumInventory;
  auto result = core::Document::parse(std::move(bytes), limits);
  require(result && result->root().type == Node::Type::Dictionary, ErrorCode::InvalidFormat,
      "Intake document is not a complete bounded dictionary");
  return std::move(*result);
}

void put(core::Document& document, std::string_view key, std::string encoded) {
  require(document.replaceValue(key, std::move(encoded)).has_value(), ErrorCode::InvalidFormat,
      "Intake structured edit exceeds its limits");
}

void uniqueKeys(const Node& node, std::size_t depth = 0) {
  require(depth <= 128, ErrorCode::InvalidFormat, "Existing scope exceeds depth limit");
  if (node.type == Node::Type::Dictionary) {
    require(node.children.size() % 2 == 0, ErrorCode::InvalidFormat, "Unpaired scope dictionary entry");
    std::set<std::string_view> keys;
    for (std::size_t n = 0; n < node.children.size(); n += 2) {
      const auto& key = node.children[n];
      require(key.type == Node::Type::Bytes && keys.insert(key.bytes).second,
          ErrorCode::InvalidFormat, "Duplicate or invalid scope dictionary key");
      uniqueKeys(node.children[n + 1], depth + 1);
    }
  } else if (node.type == Node::Type::List) {
    for (const auto& child : node.children) { uniqueKeys(child, depth + 1); }
  }
}

namespace existing_scope {
constexpr std::size_t MaximumFiles = 4096, MaximumDirectories = 8192, MaximumBytes = 4 * 1024 * 1024;
using Dirs = std::map<std::string, std::pair<std::int64_t, std::int64_t>, std::less<>>;

template<class T> std::int64_t number(T value) {
  require(std::in_range<std::int64_t>(value), ErrorCode::Unsupported, "Existing scope stat exceeds signed journal range");
  return static_cast<std::int64_t>(value);
}

std::string stamp(const StateStamp& value) {
  require(value.regular && value.owned && value.links == 1 && value.size >= 0,
      ErrorCode::UnsafePath, "Existing scope needs complete owned regular single-link leaves");
  return "l" + i(number(value.device)) + i(number(value.inode)) + i(value.size) + i(value.mode)
      + i(number(value.owner)) + i(number(value.links)) + i(value.modified.tv_sec) + i(value.modified.tv_nsec)
      + i(value.changed.tv_sec) + i(value.changed.tv_nsec) + 'e';
}

void pathShape(const fs::path& path) {
  const auto& name = path.native();
  require(path.is_absolute() && path.lexically_normal() == path && !detail::filesystemRoot(path).empty()
      && name.size() <= 4096 && name.find('\0') == name.npos && name.find('\n') == name.npos,
      ErrorCode::UnsafePath, "File operation requires absolute normalized paths without newlines");
  for (const auto& part : path.relative_path()) {
    require(!part.empty() && part != "." && part != ".." && part.native().size() <= 255,
        ErrorCode::UnsafePath, "Unsafe path component");
  }
}

bool inside(const fs::path& path, const fs::path& root) {
  auto part = path.begin();
  for (const auto& parent : root) {
    if (part == path.end() || !detail::sameFilesystemComponent(*part, parent)) { return false; }
    ++part;
  }
  return true;
}

void rejectReserved(const fs::path& path) {
  static const fs::path Git(".git"), BiglyBTC(".BiglyBTC");
  for (const auto& part : path.relative_path()) {
    const auto prefix = fs::path(part.native().substr(0, BiglyBTC.native().size()));
    require(!detail::sameFilesystemComponent(part, Git) && !detail::sameFilesystemComponent(prefix, BiglyBTC),
        ErrorCode::UnsafePath, "Existing payload overlaps reserved paths");
  }
}

struct DirectoryObservation {
  StateDescriptor handle;
  StateStamp stamp;
  fs::path missing;
};

template<class Visit> DirectoryObservation observeDirectory(const fs::path& path, bool allowMissing, Visit visit) {
  pathShape(path);
  auto prefix = detail::filesystemRoot(path);
  auto current = take(detail::openFilesystemRoot(prefix));
  auto held = take(stateStamp(current.get()));
  visit(prefix, held);
  if (prefix == path) { return {std::move(current), held, {}}; }
  const auto relative = prefix == path.root_path() ? path.relative_path() : path.lexically_relative(prefix);
  require(!relative.empty(), ErrorCode::UnsafePath, "Invalid scoped filesystem root");
  for (const auto& part : relative) {
    prefix /= part;
    const auto named = take(stateStampAt(current.get(), part));
    if (allowMissing && !named) { return {std::move(current), held, std::move(prefix)}; }
    require(named && named->directory, allowMissing ? ErrorCode::UnsafePath : ErrorCode::Conflict,
        "Scope ancestor is missing or symbolic");
    auto next = take(detail::openDirectoryAt(current.get(), part));
    held = take(stateStamp(next.get()));
    const auto again = take(stateStampAt(current.get(), part));
    require(held.directory && held.links && again && sameStateIdentity(held, *named) && sameStateIdentity(*again, *named),
        ErrorCode::Conflict, "Scope ancestor changed while opening");
    visit(prefix, held);
    current = std::move(next);
  }
  return {std::move(current), held, {}};
}

void observe(Dirs& dirs, const fs::path& path, const StateStamp& stamp) {
  const auto identity = std::pair{number(stamp.device), number(stamp.inode)};
  const auto [found, inserted] = dirs.emplace(pathToUtf8(path), identity);
  require(inserted || found->second == identity, ErrorCode::Conflict, "Scope directory changed during capture");
  require(dirs.size() <= MaximumDirectories, ErrorCode::Unsupported, "Scope directory count exceeds limit");
}

fs::path profilePath(const ProfileLease& lease) {
  require(lease.valid(), ErrorCode::Busy, "Existing scope requires held profile lease");
  auto path = take(detail::stateDirectoryPath(lease.directoryHandle()));
  const auto named = observeDirectory(path, false, [](const auto&, const auto&) {});
  const auto held = take(stateStamp(lease.directoryHandle()));
  require(held.owned && sameStateIdentity(held, named.stamp), ErrorCode::Conflict,
      "Held profile path identity changed");
  return path;
}

StateStamp payloadStamp(StateHandle parent, const fs::path& name, OriginalPayloadHandle file) {
  const auto held = take(stateStamp(file));
  const auto named = take(stateStampAt(parent, name));
  require(named && held.regular && sameStateSnapshot(*named, held), ErrorCode::Conflict,
      "Scope payload changed during observation");
  return held;
}

const Node& field(const Node& node, std::string_view key, Node::Type type) {
  const auto* value = node.find(key);
  require(value && value->type == type, ErrorCode::InvalidFormat, "Invalid existing scope field");
  return *value;
}

std::int64_t integerField(const Node& node, std::string_view key) { return field(node, key, Node::Type::Integer).integer; }

std::string encoded(const Node& node) {
  const auto value = core::encodeValue(node, compatibleConfigLimits());
  require(value.has_value(), ErrorCode::InvalidFormat, "Invalid bounded existing scope");
  return *value;
}

template<std::size_t N> std::string bytes(const std::optional<std::array<std::byte, N>>& value) {
  return value ? std::string(reinterpret_cast<const char*>(value->data()), N) : std::string{};
}
} // namespace existing_scope
} // namespace

struct VerifiedExistingScope::Data {
  struct Leaf {fs::path path;std::string stamp;};
  std::string encoded;
  existing_scope::Dirs directories;
  std::map<std::size_t,Leaf> files;
  bool mapped=false;
};
const std::string& VerifiedExistingScope::encoded() const noexcept {return data_->encoded;}
bool VerifiedExistingScope::mapped() const noexcept {return data_->mapped;}
std::expected<VerifiedExistingScope,Error> VerifiedExistingScope::decode(const Node& node) {
  try {
    using namespace existing_scope;
    require(node.type==Node::Type::Dictionary&&node.children.size()==20,ErrorCode::InvalidFormat,"Unknown existing scope fields");uniqueKeys(node);
    const auto version=integerField(node,"version");require(version==1||version==2,ErrorCode::Unsupported,"Unknown existing scope version");
    const auto swarm=integerField(node,"swarm");require(swarm==1||swarm==2,ErrorCode::InvalidFormat,"Invalid existing scope selection");
    require(field(node,"selected",Node::Type::Bytes).bytes.size()==20,ErrorCode::InvalidFormat,"Invalid existing selected identity");
    for(const auto* key:{"info","layers"}) {const auto value=field(node,key,Node::Type::Bytes).bytes;
      require(value.size()==64&&std::ranges::all_of(value,[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');}),ErrorCode::InvalidFormat,"Invalid existing subject digest");}
    const auto v1=field(node,"v1",Node::Type::Bytes).bytes.size(),v2=field(node,"v2",Node::Type::Bytes).bytes.size();
    require((v1==0||v1==20)&&(v2==0||v2==32)&&(swarm==1?v1==20:v2==32),ErrorCode::InvalidFormat,"Invalid existing full identities");
    const auto root=pathFromUtf8(field(node,"root",Node::Type::Bytes).bytes);pathShape(root);
    auto data=std::make_shared<Data>();data->encoded=existing_scope::encoded(node);data->mapped=version==2;
    require(data->encoded.size()<=MaximumBytes,ErrorCode::Unsupported,"Existing scope exceeds journal bound");
    const auto& directories=field(node,"dirs",Node::Type::List);
    require(!directories.children.empty()&&directories.children.size()<=MaximumDirectories,ErrorCode::Unsupported,"Existing scope directory bound");
    std::string previous;
    for(const auto& entry:directories.children) {
      require(entry.type==Node::Type::Dictionary&&entry.children.size()==6,ErrorCode::InvalidFormat,"Invalid existing directory record");
      const auto path=std::string(field(entry,"path",Node::Type::Bytes).bytes);pathShape(pathFromUtf8(path));
      const auto dev=integerField(entry,"dev"),ino=integerField(entry,"ino");
      require(dev>=0&&ino>0&&(previous.empty()||previous<path),ErrorCode::InvalidFormat,"Duplicate/unsorted existing directory identity");
      data->directories.emplace(path,std::pair{dev,ino});previous=path;
    }
    const auto& files=field(node,"files",Node::Type::List);
    require(!files.children.empty()&&files.children.size()<=MaximumFiles,ErrorCode::Unsupported,"Existing scope file bound");
    std::int64_t previousIndex=-1;std::set<fs::path,FilesystemPathLess> paths;
    for(const auto& entry:files.children) {
      require(entry.type==Node::Type::Dictionary&&entry.children.size()==6,ErrorCode::InvalidFormat,"Invalid existing leaf record");
      const auto index=integerField(entry,"index");
      require(index>=0&&index<100000&&index>previousIndex&&std::in_range<std::size_t>(index),ErrorCode::InvalidFormat,"Duplicate/invalid existing file index");
      const auto path=pathFromUtf8(field(entry,"path",Node::Type::Bytes).bytes);pathShape(path);
      require((version==2||inside(path,root))&&paths.insert(path).second,ErrorCode::InvalidFormat,"Existing scope path is outside its root or duplicated");
      const auto& values=field(entry,"stamp",Node::Type::List);
      require(values.children.size()==10&&std::ranges::all_of(values.children,[](const auto& v){return v.type==Node::Type::Integer;}),ErrorCode::InvalidFormat,"Invalid existing leaf stamp");
      for(const auto n:{0U,1U,2U,3U,4U,5U}) {require(values.children[n].integer>=0,ErrorCode::InvalidFormat,"Negative existing stat identity");}
      require(values.children[1].integer>0&&values.children[5].integer==1&&values.children[7].integer>=0&&values.children[7].integer<1000000000
          &&values.children[9].integer>=0&&values.children[9].integer<1000000000,ErrorCode::InvalidFormat,"Invalid existing stat identity/time");
      data->files.emplace(static_cast<std::size_t>(index),Data::Leaf{path,existing_scope::encoded(values)});previousIndex=index;
    }
    return VerifiedExistingScope{std::move(data)};
  }catch(const Failure& failure){return std::unexpected(failure.error);}
  catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot decode existing payload scope"});}
}
std::expected<VerifiedExistingScope,Error> VerifiedExistingScope::capture(const ProfileLease& lease,
    const core::Document& source,const compat::OriginalLayout& layout,const OriginalTorrentIdentity& identity,
    const fs::path& directory,std::string_view leaf,std::span<const fs::path> exactPaths) {
  try {
    using namespace existing_scope;
    require(core::validTorrentIntakeSaveName(leaf),ErrorCode::UnsafePath,"Existing scope requires a safe destination leaf");
    const auto root=directory/pathFromUtf8(leaf);pathShape(root);const auto profile=profilePath(lease);
    require(!inside(root,profile)&&!inside(profile,root),ErrorCode::UnsafePath,"Existing payload overlaps the held profile");
    rejectReserved(root);
    const bool mapped=!exactPaths.empty();require(!mapped||exactPaths.size()==layout.files.size(),ErrorCode::InvalidFormat,"Existing mapped scope differs from original indices");
    Dirs dirs;std::string files="l";std::size_t count=0;std::set<fs::path,FilesystemPathLess> actualPaths;
    require(!layout.files.empty()&&layout.files.size()<=100000,ErrorCode::Unsupported,"Existing original file bound");
    for(std::size_t index=0;index<layout.files.size();++index) {
      const auto& file=layout.files[index];if(file.padding){require(!mapped||exactPaths[index].empty(),ErrorCode::InvalidFormat,"Virtual padding cannot have a mapped scope path");continue;}
      require(++count<=MaximumFiles&&file.bytes>=0,ErrorCode::Unsupported,"Existing real file bound");
      const auto path=mapped?exactPaths[index]:(layout.multifile?root/pathFromUtf8(file.path):root);pathShape(path);
      require((mapped||inside(path,root))&&!inside(path,profile)&&!inside(profile,path)&&actualPaths.insert(path).second,ErrorCode::UnsafePath,"Existing leaf escapes or aliases its approved scope");
      rejectReserved(path);
      const auto parent=observeDirectory(path.parent_path(),false,
          [&](const fs::path& prefix,const StateStamp& value){observe(dirs,prefix,value);});
      const auto payload=take(detail::openOriginalPayloadAt(parent.handle.get(),path.filename(),OriginalPayloadAccess::ReadOnly));
      const auto held=payloadStamp(parent.handle.get(),path.filename(),payload.get());
      require(held.size==file.bytes,ErrorCode::Conflict,"Existing payload is short or changed");
      files+="d"+b("index")+i(static_cast<std::int64_t>(index))+b("path")+b(pathToUtf8(path))+b("stamp")+stamp(held)+'e';
      require(files.size()<=MaximumBytes,ErrorCode::Unsupported,"Existing scope file paths exceed bound");
    }
    files+='e';std::string directories="l";
    for(const auto& [path,id]:dirs){directories+="d"+b("dev")+i(id.first)+b("ino")+i(id.second)+b("path")+b(path)+'e';}directories+='e';
    const auto rawInfo=take(originalTorrentInfoSubject(source));std::string layers;
    if(const auto* value=source.find("piece layers")){layers=existing_scope::encoded(*value);}
    const auto key=identity.originalKey.bytes();
    auto encoded="d"+b("dirs")+directories+b("files")+files+b("info")+b(fileop::digest(rawInfo))
        +b("layers")+b(fileop::digest(layers))+b("root")+b(pathToUtf8(root))
        +b("selected")+b(std::string_view(reinterpret_cast<const char*>(key.data()),key.size()))
        +b("swarm")+i(identity.selected==OriginalSelectedHash::V1?1:2)+b("v1")+b(bytes(identity.full.v1))+b("v2")+b(bytes(identity.full.v2))+b("version")+i(mapped?2:1)+'e';
    require(encoded.size()<=MaximumBytes,ErrorCode::Unsupported,"Existing scope exceeds journal bound");
    auto document=parse(std::move(encoded));auto result=take(decode(document.root()));
    for(const auto& [index,file]:result.data_->files){
      const auto parent=observeDirectory(file.path.parent_path(),false,[](const auto&,const auto&){});
      const auto fd=take(detail::openOriginalPayloadAt(parent.handle.get(),file.path.filename(),OriginalPayloadAccess::ReadOnly));
      const auto proof=take(result.bind(index,file.path,fd.get()));
      auto valid=proof();
      if(!valid){throw Failure{valid.error()};}
    }
    return result;
  }catch(const Failure& failure){return std::unexpected(failure.error);}
  catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot capture existing payload scope"});}
}
std::expected<void,Error> VerifiedExistingScope::validate(const ProfileLease& lease,const core::Document& source,
    const compat::OriginalLayout& layout,const OriginalTorrentIdentity& identity,const fs::path& directory,std::string_view leaf,std::span<const fs::path> exactPaths) const {
  const auto now=capture(lease,source,layout,identity,directory,leaf,exactPaths);if(!now){return std::unexpected(now.error());}
  if(now->encoded()!=encoded()){return std::unexpected(Error{ErrorCode::Conflict,"Approved existing payload subject/path/identity changed"});}return {};
}
std::expected<VerifiedExistingScope::Proof,Error> VerifiedExistingScope::bind(std::size_t index,const fs::path& path,OriginalPayloadHandle descriptor) const {
  try {
    const auto found=data_->files.find(index);
    require(found!=data_->files.end()&&found->second.path==path,ErrorCode::Conflict,"Descriptor is outside approved existing file indices");
    Proof proof=[data=data_,index,path,descriptor]() -> std::expected<void,Error> {
      try {
        using namespace existing_scope;
        const auto parent=observeDirectory(path.parent_path(),false,[&](const fs::path& prefix,const StateStamp& value){
          const auto directory=data->directories.find(pathToUtf8(prefix));
          require(directory!=data->directories.end()&&directory->second==std::pair{number(value.device),number(value.inode)},
              ErrorCode::Conflict,"Approved existing directory identity changed");
        });
        require(detail::isOwnedReadOnlyOriginalPayload(descriptor),ErrorCode::Conflict,"Approved existing descriptor permits writes");
        const auto held=payloadStamp(parent.handle.get(),path.filename(),descriptor);
        require(stamp(held)==data->files.at(index).stamp,ErrorCode::Conflict,"Approved payload metadata changed");
        return {};
      }catch(const Failure& failure){return std::unexpected(failure.error);}
      catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot validate approved existing descriptor"});}
    };
    auto valid=proof();
    if(!valid){return std::unexpected(valid.error());}
    return proof;
  }catch(const Failure& failure){return std::unexpected(failure.error);}
  catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot bind approved existing descriptor"});}
}

std::expected<std::string, Error> captureIntakeDestinationSnapshot(const core::Document& source,
    const compat::OriginalLayout& layout, const core::TorrentHash& hash, std::span<const fs::path> paths,
    std::span<const std::uint64_t> maxima) {
  try {
    using namespace existing_scope;
    require(paths.size() == layout.files.size(), ErrorCode::InvalidFormat,
        "Destination snapshot differs from original indices");
    require(maxima.empty() || maxima.size() == paths.size(), ErrorCode::InvalidFormat,
        "Destination physical bound count differs");
    Dirs directories;
    std::string records = "l";
    for (std::size_t index = 0; index < paths.size(); ++index) {
      const auto& path = paths[index];
      const auto& file = layout.files[index];
      if (file.padding) {
        require(path.empty(), ErrorCode::InvalidFormat, "Virtual padding cannot have a destination snapshot");
        continue;
      }
      pathShape(path);
      const auto parent = observeDirectory(path.parent_path(), true,
          [&](const fs::path& name, const StateStamp& value) { observe(directories, name, value); });
      auto missing = parent.missing;
      std::string observation;
      if (missing.empty()) {
        const auto named = take(stateStampAt(parent.handle.get(), path.filename()));
        if (!named) {
          missing = path;
        } else {
          const auto maximum = maxima.empty() ? static_cast<std::uint64_t>(file.bytes) : maxima[index];
          require(named->regular && named->owned && named->links == 1 && named->size >= 0
              && static_cast<std::uint64_t>(named->size) <= maximum, ErrorCode::UnsafePath,
              "Existing indexed target is not safe ordinary data");
          const auto payload = take(detail::openOriginalPayloadAt(parent.handle.get(), path.filename(),
              OriginalPayloadAccess::ReadOnly));
          const auto held = payloadStamp(parent.handle.get(), path.filename(), payload.get());
          require(sameStateSnapshot(held, *named), ErrorCode::Conflict, "Destination leaf changed while observing");
          observation = "d5:stamp" + stamp(held) + 'e';
        }
      }
      if (!missing.empty()) {
        require(parent.stamp.owned, ErrorCode::UnsafePath, "Missing destination requires an owned ancestor");
        observation = "d7:missing" + b(pathToUtf8(missing)) + 'e';
      }
      records += "d5:index" + i(static_cast<std::int64_t>(index)) + "4:path" + b(pathToUtf8(path))
          + "8:snapshot" + observation + 'e';
      require(records.size() <= MaximumBytes, ErrorCode::Unsupported, "Destination snapshot byte bound");
    }
    records += 'e';
    std::string dirs = "l";
    for (const auto& [name, id] : directories) { dirs += "l" + b(name) + i(id.first) + i(id.second) + 'e'; }
    dirs += 'e';
    const auto rawInfo = take(originalTorrentInfoSubject(source));
    return fileop::digest("d4:dirs" + dirs + "5:files" + records + "4:info" + b(fileop::digest(rawInfo))
        + "8:selected" + b(hash.hex()) + 'e');
  } catch (const Failure& failure) { return std::unexpected(failure.error); }
  catch (...) { return std::unexpected(Error{ErrorCode::Io, "Cannot capture indexed destination snapshot"}); }
}

std::expected<SelectedExistingScope,Error> SelectedExistingScope::decode(const Node& node){
  const auto* version=node.find("version"),*digest=node.find("digest");
  if(node.type!=Node::Type::Dictionary||node.children.size()!=4||!version||version->type!=Node::Type::Integer||version->integer!=1
      ||!digest||digest->type!=Node::Type::Bytes||digest->bytes.size()!=64||!std::ranges::all_of(digest->bytes,[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');})){
    return std::unexpected(Error{ErrorCode::InvalidFormat,"Invalid versioned selected-existing scope digest"});}
  return SelectedExistingScope(std::string(digest->bytes));
}
std::expected<SelectedExistingScope,Error> SelectedExistingScope::capture(const ProfileLease& lease,
    const core::Document& source,const compat::OriginalLayout& layout,const OriginalTorrentIdentity& identity,
    std::span<const compat::FilePolicy> policies,std::span<const OriginalStorageKind> kinds,
    std::span<const fs::path> paths){
  try{
    using namespace existing_scope;
    require(lease.valid()&&layout.pieceLength>0&&layout.bytes>0&&layout.files.size()<=4096&&!layout.files.empty()&&policies.size()==layout.files.size()
        &&kinds.size()==layout.files.size()&&paths.size()==layout.files.size(),ErrorCode::InvalidFormat,"Invalid bounded selected-existing domain");
    const auto goal=core::existingPieceGoal(layout,policies,core::ExistingVerificationGoal::SelectedFiles,
        static_cast<std::size_t>(layout.bytes/layout.pieceLength+(layout.bytes%layout.pieceLength!=0)));
    require(goal.has_value(),ErrorCode::InvalidFormat,"Invalid selected-existing piece goal");
    const fs::path profile=profilePath(lease);std::set<fs::path,FilesystemPathLess> unique;std::vector<std::uint64_t> maxima;std::string types="l",choices="l";
    for(std::size_t n=0;n<paths.size();++n){const auto& file=layout.files[n];const auto kind=kinds[n];
      const auto maximum=take(originalMaximumPhysicalSize({static_cast<std::uint64_t>(file.offset),static_cast<std::uint64_t>(file.bytes),static_cast<std::uint32_t>(layout.pieceLength)},kind));maxima.push_back(maximum);
      const auto code=kind==OriginalStorageKind::Linear?'L':kind==OriginalStorageKind::Compact?'C':kind==OriginalStorageKind::Reordered?'R':'X';types+=b(std::string(1,code));
      choices+="l"+i(policies[n].skipped?1:0)+i(policies[n].priority)+'e';if(file.padding){require(paths[n].empty(),ErrorCode::InvalidFormat,"Virtual padding cannot own selected-existing data");continue;}
      pathShape(paths[n]);require(!existing_scope::inside(paths[n],profile)&&!existing_scope::inside(profile,paths[n])&&unique.insert(paths[n]).second,ErrorCode::UnsafePath,"Selected-existing payload overlaps profile or aliases a real index");
      rejectReserved(paths[n]);
    }
    for(const auto& path:unique){for(auto ancestor=path.parent_path();ancestor!=ancestor.root_path()&&!ancestor.empty();ancestor=ancestor.parent_path()){require(!unique.contains(ancestor),ErrorCode::UnsafePath,"Selected-existing file is another file's ancestor");}}
    types+='e';choices+='e';auto attrs=parse("de");put(attrs,"storetypes",types);
    const auto storage=take(resolveOriginalStorageTypes(lease,identity.originalKey,layout,attrs.root()));
    require(!storage.unsupportedConversion&&std::ranges::equal(storage.actual,kinds),ErrorCode::Conflict,"Selected-existing physical kinds differ from original controls");
    std::string controls="l";existing_scope::Dirs controlDirs;std::size_t controlBytes=0;
    const auto controlPath=profile/"active"/identity.originalKey.hex();
    // An absent control directory grants nothing. Creating the ordinary active
    // state directory during intake cannot change the meaning of absent maps.
    const auto controlParent=observeDirectory(controlPath,true,[&](const fs::path& path,const StateStamp& value){
      observe(controlDirs,path,value);
    });
    std::map<std::string,std::string,std::less<>> candidates;
    if(!controlParent.missing.empty()) {
      controlDirs.clear();
      require(storage.markers.empty(),ErrorCode::Conflict,"Selected controls lost their directory");
    } else {
      for(const auto& marker:storage.markers) {
        const auto directory=controlParent.handle.get();
        const auto handle=take(detail::openStateFile(directory,marker.leaf));
        const auto before=take(stateStamp(handle.get()));
        require(before.device==marker.device&&before.inode==marker.inode
            &&before.size>=0&&static_cast<std::uint64_t>(before.size)==marker.bytes
            &&before.modified.tv_sec==marker.modifiedSeconds&&before.modified.tv_nsec==marker.modifiedNanoseconds
            &&before.changed.tv_sec==marker.changedSeconds&&before.changed.tv_nsec==marker.changedNanoseconds,
            ErrorCode::Conflict,"Selected control changed before capture");
        require(marker.bytes<=16*1024*1024&&marker.bytes<=MaximumInventory-controlBytes,
            ErrorCode::Unsupported,"Selected control byte bound");
        std::string value(static_cast<std::size_t>(marker.bytes),'\0');
        for(std::size_t offset=0;offset<value.size();) {
          const auto count=detail::readStateAt(handle.get(),value.data()+offset,value.size()-offset,offset);
          require(count>0,ErrorCode::Io,"Cannot read complete selected control");
          offset+=static_cast<std::size_t>(count);
        }
        const auto after=payloadStamp(directory,marker.leaf,handle.get());
        require(sameStateSnapshot(before,after),ErrorCode::Conflict,"Selected control changed during capture");
        controlBytes+=value.size();
        controls+="d5:index"+i(static_cast<std::int64_t>(marker.index))+"4:leaf"+b(marker.leaf)
            +"5:stamp"+stamp(before)+"6:digest"+b(fileop::digest(value))+'e';
        candidates.emplace(marker.leaf,std::move(value));
      }
    }
    for(std::size_t n=0;n<kinds.size();++n){if(kinds[n]==OriginalStorageKind::Linear||layout.files[n].padding){continue;}
      const auto leaf=originalControlLeaf(n,kinds[n]);const auto primary=candidates.find(leaf),backup=candidates.find(leaf+".bak");
      const OriginalFileGeometry geometry{static_cast<std::uint64_t>(layout.files[n].offset),static_cast<std::uint64_t>(layout.files[n].bytes),static_cast<std::uint32_t>(layout.pieceLength)};
      const auto valid=[&](auto candidate){return candidate!=candidates.end()&&OriginalFileIo::validateControl(geometry,kinds[n],candidate->second).has_value();};
      const auto selected=valid(primary)?primary:valid(backup)?backup:candidates.end();
      require(selected!=candidates.end()||(primary==candidates.end()&&backup==candidates.end()),ErrorCode::InvalidFormat,"No valid original selected control candidate");
      if(selected!=candidates.end()){const auto document=parse(selected->second);if(const auto* redo=document.find("BiglyBTC.reorderedCompletion")){
        require(redo->type==Node::Type::Dictionary&&redo->children.empty(),ErrorCode::Conflict,"Selected read-only verification cannot recover native completion redo");}}
    }
    controls+='e';std::string dirs="l";for(const auto& [path,id]:controlDirs){dirs+="l"+b(path)+i(id.first)+i(id.second)+'e';}dirs+='e';
    const auto rawInfo=take(originalTorrentInfoSubject(source));std::string layers;if(const auto* value=source.find("piece layers")){layers=existing_scope::encoded(*value);}
    const auto observed=take(captureIntakeDestinationSnapshot(source,layout,identity.originalKey,paths,maxima));
    auto encoded="d4:dirs"+dirs+"5:files"+b(observed)+"8:controls"+controls+"7:choices"+choices+"5:kinds"+types
        +"4:info"+b(fileop::digest(rawInfo))+"6:layers"+b(fileop::digest(layers))+"8:selected"+b(identity.originalKey.hex())
        +"5:swarm"+i(identity.selected==OriginalSelectedHash::V1?1:2)+"2:v1"+b(existing_scope::bytes(identity.full.v1))+"2:v2"+b(existing_scope::bytes(identity.full.v2))+"7:versioni1ee";
    return SelectedExistingScope(fileop::digest(encoded));
  }catch(const Failure& error){return std::unexpected(error.error);}
  catch(...){return std::unexpected(Error{ErrorCode::Io,"Cannot capture selected-existing scope"});}
}
std::expected<void,Error> SelectedExistingScope::validate(const ProfileLease& lease,const core::Document& source,
    const compat::OriginalLayout& layout,const OriginalTorrentIdentity& identity,
    std::span<const compat::FilePolicy> policies,std::span<const OriginalStorageKind> kinds,std::span<const fs::path> paths)const{
  const auto current=capture(lease,source,layout,identity,policies,kinds,paths);if(!current){return std::unexpected(current.error());}
  if(current->digest()!=digest()){return std::unexpected(Error{ErrorCode::Conflict,"Approved selected-existing source/map/policy/control observations changed"});}return {};
}

} // namespace biglybtc::storage
