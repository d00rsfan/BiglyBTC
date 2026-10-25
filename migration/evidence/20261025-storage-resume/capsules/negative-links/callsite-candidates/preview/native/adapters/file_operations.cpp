#include "biglybtc/original_file_links.hpp"
#include "file_operations.hpp"
#include "file_operation_io.hpp"
#include "original_file_io.hpp"
#include "encoded_document.hpp"
#include "biglybtc/torrent_layout.hpp"
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <linux/fs.h>
#include <map>
#include <set>
#include <sys/syscall.h>
#include <unistd.h>

namespace biglybtc::storage {
namespace {
using namespace fileop;
namespace fs=std::filesystem;
using Node=core::Node;
constexpr std::size_t MaximumDocument=64*1024*1024,MaximumJournal=384*1024*1024,MaximumFiles=10000;
constexpr const char* JournalName="BiglyBTC.file-operation";
template<class F> auto guarded(F&& action)->decltype(action()){
  try{return action();}catch(const Failure& e){return std::unexpected(e.error);}catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Insufficient memory for file operation"});}
  catch(const std::exception& e){return std::unexpected(Error{ErrorCode::Io,e.what()});}
}
void checkpoint(std::stop_token stop){require(!stop.stop_requested(),ErrorCode::Cancelled,"File operation cancelled before mutation");}
std::string b(std::string_view value){return std::to_string(value.size())+':'+std::string(value);}
std::string number(std::int64_t value){return 'i'+std::to_string(value)+'e';}
core::Limits limits(std::size_t bytes=MaximumDocument){auto out=compatibleConfigLimits();out.inputBytes=out.stringBytes=bytes;return out;}
core::Document parse(std::string bytes,std::size_t maximum=MaximumDocument){auto doc=core::Document::parse(std::move(bytes),limits(maximum));require(doc&&doc->root().type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"File-operation input is not a bounded dictionary");return std::move(*doc);}
std::string encoded(const Node& node){auto out=core::encodeValue(node,limits(MaximumJournal));require(out.has_value(),ErrorCode::InvalidFormat,"Cannot encode original file-operation value");return std::move(*out);}
core::Document section(const Node* node){if(!node){return parse("de");}require(node->type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Original state section is not a dictionary");return parse(encoded(*node));}
void put(core::Document& doc,std::string_view key,std::string value){require(doc.replaceValue(key,std::move(value)).has_value(),ErrorCode::InvalidFormat,"File-operation structured edit exceeds limit");}
const Node& field(const Node& node,std::string_view key,Node::Type type){const auto* p=node.find(key);require(p&&p->type==type,ErrorCode::InvalidFormat,"Invalid file-operation journal field");return *p;}
std::string text(const Node* node){require(node&&node->type==Node::Type::Bytes,ErrorCode::InvalidFormat,"Expected original byte string");return std::string(node->bytes);}
std::int64_t integer(const Node* node){require(node&&node->type==Node::Type::Integer,ErrorCode::InvalidFormat,"Expected journal integer");return node->integer;}
std::uint64_t unsignedText(const Node* node){auto value=text(node);std::uint64_t out=0;const auto result=std::from_chars(value.data(),value.data()+value.size(),out);require(result.ec==std::errc{}&&result.ptr==value.data()+value.size(),ErrorCode::InvalidFormat,"Invalid unsigned journal value");return out;}
bool within(const fs::path& child,const fs::path& parent){auto a=child.begin(),z=parent.begin();for(;z!=parent.end();++z,++a){if(a==child.end()||*a!=*z){return false;}}return true;}
bool intersects(const fs::path&a,const fs::path&b){return within(a,b)||within(b,a);}
void ownedPayload(const struct stat& stamp,std::int64_t maximum){require(stamp.st_nlink==1&&stamp.st_size>=0&&stamp.st_size<=maximum,ErrorCode::UnsafePath,"Payload is linked or exceeds original file length");}
struct Edit {fs::path path;Raw before;std::optional<std::string> after;FileOperationStage stage;};
// Control files belong to profile state, never to payload previews or moves.
struct Control {fs::path path;Raw before;};
struct Payload {FileOperationEntry view;std::optional<struct stat> source,destination;std::string sha256;std::optional<struct stat> temporary;};
struct Plan {
  FileOperationPreview preview;
  fs::path profile,destinationParent;
  std::uint64_t profileDevice=0,profileInode=0,targetDevice=0,targetInode=0;
  std::vector<Payload> files;
  std::vector<Control> controls;
  std::vector<Edit> edits;
  std::string scopeSelection;
  bool attempted=false,journalCreated=false;
};
void profileIdentity(const ProfileLease& lease,const Plan& plan){const auto path=profilePath(lease);struct stat s{};require(path==plan.profile&&::fstat(lease.directoryHandle(),&s)==0&&s.st_dev==plan.profileDevice&&s.st_ino==plan.profileInode,ErrorCode::Conflict,"File-operation lease/profile identity changed");}
std::string stampText(const struct stat& s){auto d=parse("de");put(d,"dev",b(std::to_string(s.st_dev)));put(d,"ino",b(std::to_string(s.st_ino)));put(d,"size",number(s.st_size));put(d,"mode",number(s.st_mode));put(d,"uid",number(s.st_uid));put(d,"links",number(s.st_nlink));put(d,"ms",number(s.st_mtim.tv_sec));put(d,"mn",number(s.st_mtim.tv_nsec));put(d,"cs",number(s.st_ctim.tv_sec));put(d,"cn",number(s.st_ctim.tv_nsec));return d.serialize();}
struct stat readStamp(const Node& n){require(n.type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid journal stat");struct stat s{};s.st_dev=unsignedText(n.find("dev"));s.st_ino=unsignedText(n.find("ino"));s.st_size=integer(n.find("size"));s.st_mode=static_cast<mode_t>(integer(n.find("mode")));s.st_uid=static_cast<uid_t>(integer(n.find("uid")));s.st_nlink=static_cast<nlink_t>(integer(n.find("links")));s.st_mtim={integer(n.find("ms")),integer(n.find("mn"))};s.st_ctim={integer(n.find("cs")),integer(n.find("cn"))};require(S_ISREG(s.st_mode)&&s.st_uid==::geteuid()&&s.st_size>=0&&s.st_nlink>=1&&s.st_nlink<=2&&s.st_mtim.tv_nsec>=0&&s.st_mtim.tv_nsec<1000000000,ErrorCode::InvalidFormat,"Unsafe journal file stamp");return s;}
std::string canonical(const Plan& p,bool stable){
  auto d=parse("de",MaximumJournal);put(d,"kind",number(static_cast<int>(p.preview.kind)));put(d,"hash",b(p.preview.hash.hex()));put(d,"profile",b(p.profile.string()));put(d,"source",b(p.preview.sourceRoot.string()));put(d,"target",b(p.preview.targetRoot.string()));put(d,"parent",b(p.destinationParent.string()));put(d,"td",b(std::to_string(p.targetDevice)));put(d,"ti",b(std::to_string(p.targetInode)));put(d,"selection",b(p.scopeSelection));
  std::string rows="l";for(const auto& f:p.files){auto row=parse("de");put(row,"i",number(static_cast<std::int64_t>(f.view.index)));put(row,"s",b(f.view.source.string()));put(row,"d",b(f.view.destination.string()));put(row,"bytes",number(f.view.logicalBytes));
    if(f.source){put(row,"dev",b(std::to_string(f.source->st_dev)));put(row,"ino",b(std::to_string(f.source->st_ino)));if(!stable){put(row,"stamp",stampText(*f.source));}}
    if(f.destination){put(row,"dd",b(std::to_string(f.destination->st_dev)));put(row,"di",b(std::to_string(f.destination->st_ino)));if(!stable){put(row,"destination",stampText(*f.destination));}}
    rows+=row.serialize();}put(d,"files",rows+'e');
  if(!p.controls.empty()){std::string controls="l";for(const auto& c:p.controls){auto item=parse("de",MaximumJournal);put(item,"path",b(c.path.string()));if(!stable&&c.before.bytes){put(item,"before",b(*c.before.bytes));put(item,"stamp",stampText(*c.before.stamp));}controls+=item.serialize();}put(d,"controls",controls+'e');}
  if(!stable){std::string edits="l";for(const auto& e:p.edits){auto item=parse("de",MaximumJournal);put(item,"path",b(e.path.string()));put(item,"stage",number(static_cast<int>(e.stage)));if(e.before.bytes){put(item,"before",b(*e.before.bytes));put(item,"stamp",stampText(*e.before.stamp));}if(e.after){put(item,"after",b(*e.after));}edits+=item.serialize();}put(d,"edits",edits+'e');}
  return d.serialize();
}
std::string journal(const Plan& p){
  auto d=parse(canonical(p,false),MaximumJournal);put(d,"version","i1e");put(d,"pd",b(std::to_string(p.profileDevice)));put(d,"pi",b(std::to_string(p.profileInode)));put(d,"scope",b(p.preview.approvalScopeDigest));put(d,"cas",b(p.preview.executionCasDigest));
  std::string runtime="l";for(const auto& f:p.files){auto n=parse("de");put(n,"sha",b(f.sha256));if(f.temporary){put(n,"temp",stampText(*f.temporary));}runtime+=n.serialize();}put(d,"runtime",runtime+'e');const auto body=d.serialize();return "d7:payload"+b(body)+"6:sha256"+b(digest(body))+"7:versioni1ee";
}
void saveJournal(Plan& p){const auto path=p.profile/JournalName;auto old=read(path,MaximumJournal);
  require(bool(old.bytes)==p.journalCreated,ErrorCode::Conflict,"File-operation journal appeared/disappeared unexpectedly");
  if(old.bytes){auto outer=parse(*old.bytes,MaximumJournal);const auto body=text(outer.find("payload"));require(digest(body)==text(outer.find("sha256")),ErrorCode::Conflict,"Existing operation journal changed");auto prior=parse(body,MaximumJournal);require(text(prior.find("scope"))==p.preview.approvalScopeDigest&&text(prior.find("cas"))==p.preview.executionCasDigest,ErrorCode::Conflict,"Existing journal belongs to another operation");}
  publish(path,old,journal(p),MaximumJournal);p.journalCreated=true;
}
void hashMatches(const EncodedDocument& doc,const core::TorrentHash& hash){
  const auto* info=doc.document().find("info");require(info&&info->type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Original metadata info is missing");const auto raw=doc.document().originalBytes().substr(info->begin,info->end-info->begin);const bool v1=info->find("pieces")!=nullptr;const auto key=hash.bytes();std::array<unsigned char,EVP_MAX_MD_SIZE> out{};unsigned size=0;
  require(EVP_Digest(raw.data(),raw.size(),out.data(),&size,key.size()==20&&v1?EVP_sha1():EVP_sha256(),nullptr)==1&&size>=key.size()&&std::equal(key.begin(),key.end(),reinterpret_cast<const std::byte*>(out.data())),ErrorCode::InvalidFormat,"Active metadata does not match selected torrent hash");
}
EncodedDocument envelope(const Raw& raw){require(raw.bytes.has_value(),ErrorCode::Missing,"Selected active metadata is absent");auto doc=EncodedDocument::parse(*raw.bytes,limits());require(doc&&doc->document().root().type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid original active metadata");return std::move(*doc);}
void hydrate(EncodedDocument& state,const Node* cached){
  if(!cached){return;}for(const auto& [from,to]:{std::pair{"attributes","attributes"},{"azp","azureus_properties"},{"azpp","azureus_private_properties"}}){if(const auto* n=cached->find(from)){require(n->type==Node::Type::Dictionary&&state.replaceValue(to,encoded(*n)).has_value(),ErrorCode::InvalidFormat,"Cannot hydrate original cached state");}}
}
void markRecheck(EncodedDocument& state,core::Document attributes){
  auto resume=section(state.document().find("resume"));auto data=section(resume.find("data"));if(const auto* pieces=data.find("resume data")){require(pieces->type==Node::Type::Bytes&&pieces->bytes.size()<=1000000,ErrorCode::InvalidFormat,"Invalid original piece flags");put(data,"resume data",b(std::string(pieces->bytes.size(),'\2')));}
  put(data,"valid","i0e");put(data,"blocks","de");put(resume,"data",data.serialize());put(attributes,"resumecomplete","i1e");require(state.replaceValue("resume",resume.serialize()).has_value()&&state.replaceValue("attributes",attributes.serialize()).has_value(),ErrorCode::InvalidFormat,"Cannot invalidate original resume data");
}
std::string serialize(const EncodedDocument& d){auto out=d.serialize();require(out.has_value(),ErrorCode::InvalidFormat,"Cannot encode original state");return std::move(*out);}
const Node* cacheEntry(const EncodedDocument* cache,const core::TorrentHash& hash){
  if(!cache){return nullptr;}const auto* rows=cache->document().find("state");if(!rows){return nullptr;}require(rows->type==Node::Type::List,ErrorCode::InvalidFormat,"Invalid state cache list");const Node* found=nullptr;
  for(const auto& row:rows->children){const auto* key=row.find("hash");if(key&&key->type==Node::Type::Bytes&&key->bytes.size()==hash.bytes().size()&&std::equal(hash.bytes().begin(),hash.bytes().end(),reinterpret_cast<const std::byte*>(key->bytes.data()))){found=&row;}}return found;
}
OriginalStorageKind storageKind(const Node& attrs,std::size_t index,std::size_t count){
  const auto& stores=field(attrs,"storetypes",Node::Type::List);require(stores.children.size()==count&&index<count,ErrorCode::InvalidFormat,"Storage types do not match metadata");const auto value=text(&stores.children[index]);
  if(value=="L"){return OriginalStorageKind::Linear;}if(value=="C"){return OriginalStorageKind::Compact;}if(value=="R"){return OriginalStorageKind::Reordered;}if(value=="X"){return OriginalStorageKind::ReorderedCompact;}fail(ErrorCode::Unsupported,"Unknown original storage type");
}
std::int64_t physicalMaximum(const compat::OriginalLayout& layout,const Node& attrs,std::size_t index){
  const auto& f=layout.files[index];require(f.offset>=0&&f.bytes>=0&&layout.pieceLength>0&&layout.pieceLength<=UINT32_MAX,ErrorCode::InvalidFormat,"Original storage geometry is invalid");auto bound=originalMaximumPhysicalSize({static_cast<std::uint64_t>(f.offset),static_cast<std::uint64_t>(f.bytes),static_cast<std::uint32_t>(layout.pieceLength)},storageKind(attrs,index,layout.files.size()));require(bound&&*bound<=INT64_MAX,ErrorCode::InvalidFormat,"Original physical storage bound overflows");return static_cast<std::int64_t>(*bound);
}
std::vector<fs::path> controlPaths(const fs::path& profile,const core::TorrentHash& hash,std::size_t count){
  std::vector<fs::path> paths;paths.reserve(count*4);const auto parent=profile/"active"/hash.hex();
  for(std::size_t i=0;i<count;++i){for(const auto kind:{OriginalStorageKind::Compact,OriginalStorageKind::Reordered}){const auto path=parent/originalControlLeaf(i,kind);paths.push_back(path);paths.emplace_back(path.string()+".bak");}}
  return paths;
}
void checkControls(const Plan& p,bool recovering=false){
  for(const auto& c:p.controls){require(!inspect(fs::path(c.path.string()+".saving")),ErrorCode::Conflict,"Original control recovery is pending");const auto now=read(c.path,MaximumDocument);if(recovering&&p.preview.kind!=FileOperationKind::Relocate&&p.preview.kind!=FileOperationKind::Relink&&!now.bytes){continue;}require(now.bytes==c.before.bytes&&(!c.before.stamp||(now.stamp&&unchanged(*now.stamp,*c.before.stamp))),ErrorCode::Conflict,"Original control file changed after preparation");}
}
void payloadPath(const fs::path& path,const fs::path& profile){
  pathShape(path);require(!intersects(path,profile),ErrorCode::UnsafePath,"Payload link overlaps the held profile");for(const auto& part:path){require(part!=".git"&&!part.native().starts_with(".BiglyBTC."),ErrorCode::UnsafePath,"Payload link overlaps a reserved control path");}
}
fs::path relocatedPath(const core::SavedDownload& saved,const compat::OriginalLayout& layout,std::size_t index,const fs::path& source,const fs::path& parent,std::string_view suffix){
  const auto saveParent=fs::path(saved.saveDirectory),root=saveParent/ *saved.saveFile;
  if(within(source,root)||(!layout.multifile&&source.parent_path()==saveParent)){return parent/source.lexically_relative(saveParent);}
  // External authenticated links become normal metadata paths at the selected
  // destination. Never reproduce an absolute external path using '..'.
  auto result=parent/(layout.multifile?fs::path(*saved.saveFile)/layout.files[index].path:fs::path(*saved.saveFile));if(!suffix.empty()&&source.filename().native().ends_with(suffix)){result+=suffix;}return result;
}
std::vector<fs::path> mapping(const core::SavedDownload& saved,const compat::OriginalLayout& layout,const Node& attrs){
  require(!saved.legacySavePath&&saved.saveFile&&fs::path(*saved.saveFile).filename()==fs::path(*saved.saveFile)&&!saved.saveFile->empty(),ErrorCode::Unsupported,"Legacy save-root mapping needs its qualified adapter");
  // Deprecated filelinks is opaque: the pinned original reads filelinks2 only.
  for(const auto* key:{"dnd_pfx","dnd_sf","dnd_al"}){if(const auto* n=attrs.find(key)){require(n->type==Node::Type::Bytes&&n->bytes.size()<=4096,ErrorCode::InvalidFormat,"Invalid original DND policy field");}}
  const auto& stores=field(attrs,"storetypes",Node::Type::List);require(stores.children.size()==layout.files.size(),ErrorCode::InvalidFormat,"Storage types do not match metadata");std::vector<fs::path> out;out.reserve(layout.files.size());
  for(std::size_t i=0;i<layout.files.size();++i){static_cast<void>(storageKind(attrs,i,layout.files.size()));out.push_back(fs::path(saved.saveDirectory)/(layout.multifile?fs::path(*saved.saveFile)/layout.files[i].path:fs::path(*saved.saveFile)));pathShape(out.back());}
  std::vector<std::string> originalLinkBases; originalLinkBases.reserve(out.size());
  for (const auto& path:out) { originalLinkBases.push_back(path.native()); }
  const auto links=compat::resolveOriginalFileLinks(attrs.find("filelinks2"),originalLinkBases);
  if(!links){fail(links.error().code==compat::OriginalFileLinksErrorCode::Memory?ErrorCode::Io:
      links.error().code==compat::OriginalFileLinksErrorCode::AmbiguousBase?ErrorCode::UnsafePath:ErrorCode::InvalidFormat,
      links.error().message.data());}
  for(std::size_t i=0;i<out.size();++i){if(links->byIndex[i]){const fs::path target(links->byIndex[i]->target);pathShape(target);out[i]=target;}}
  return out;
}
}
namespace {
bool moving(FileOperationKind kind){return kind==FileOperationKind::Relocate||kind==FileOperationKind::Relink;}
std::optional<EncodedDocument> cacheDocument(const Raw& raw){if(!raw.bytes){return {};}
  auto d=EncodedDocument::parse(*raw.bytes,limits());require(d&&d->compressed()&&d->document().root().type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"State cache must be a valid original compressed dictionary");return std::move(*d);}
std::string selectedRows(const Node* list,const core::TorrentHash& hash,bool pause,std::string_view hashKey="torrent_hash"){
  if(!list){return "le";}require(list->type==Node::Type::List,ErrorCode::InvalidFormat,"Original selected rows are not a list");std::string out="l";const auto key=hash.bytes();
  for(const auto& row:list->children){const auto* n=pause&&row.type==Node::Type::Bytes?&row:row.find(pause?std::string_view("hash"):hashKey);if(n&&n->type==Node::Type::Bytes&&n->bytes.size()==key.size()&&std::equal(key.begin(),key.end(),reinterpret_cast<const std::byte*>(n->bytes.data()))){out+=encoded(row);}}return out+'e';
}
std::string withoutSelected(const Node* list,const core::TorrentHash& hash,bool pause,std::optional<std::string_view> replacement={},std::string_view hashKey="torrent_hash"){
  require(list&&list->type==Node::Type::List,ErrorCode::InvalidFormat,"Original list is missing");std::string out="l";const auto key=hash.bytes();bool replaced=false;
  for(const auto& row:list->children){const auto* n=pause&&row.type==Node::Type::Bytes?&row:row.find(pause?std::string_view("hash"):hashKey);const bool match=n&&n->type==Node::Type::Bytes&&n->bytes.size()==key.size()&&std::equal(key.begin(),key.end(),reinterpret_cast<const std::byte*>(n->bytes.data()));
    if(!match){out+=encoded(row);}else if(replacement&&!replaced){out+=*replacement;replaced=true;}}
  if(replacement&&!replaced){fail(ErrorCode::Conflict,"Selected original row disappeared");}return out+'e';
}
Plan prepare(const ProfileLease& lease,const FileOperationRequest& request,std::stop_token stop){
  checkpoint(stop);Plan p;p.profile=profilePath(lease);p.preview.kind=request.kind;p.preview.hash=request.hash;struct stat profile{};require(::fstat(lease.directoryHandle(),&profile)==0,ErrorCode::Io,"Cannot inspect held profile");p.profileDevice=profile.st_dev;p.profileInode=profile.st_ino;
  require(request.kind>=FileOperationKind::RemoveKeepData&&request.kind<=FileOperationKind::Relink&&(request.hash.bytes().size()==20||request.hash.bytes().size()==32),ErrorCode::InvalidFormat,"Invalid file-operation request");
  require(!inspect(p.profile/JournalName)&&!inspect(p.profile/(std::string(JournalName)+".BiglyBTC.fileop-saving")),ErrorCode::Conflict,"File operation recovery is required before preparing another operation");
  require(!inspect(p.profile/"BiglyBTC.torrent-import"),ErrorCode::Conflict,"Torrent import recovery is pending");
  const auto inventoryRaw=read(p.profile/"downloads.config",MaximumDocument);require(inventoryRaw.bytes.has_value(),ErrorCode::Missing,"Saved inventory is absent");auto inventory=parse(*inventoryRaw.bytes);const auto& rows=field(inventory.root(),"downloads",Node::Type::List);auto library=core::readLibrary(inventory);require(library&&!library->legacyMap&&rows.children.size()<=MaximumFiles,ErrorCode::Unsupported,"File operations require bounded modern inventory");
  std::optional<core::SavedDownload> saved;for(const auto& row:library->downloads){if(row.hash&&*row.hash==request.hash){require(!saved,ErrorCode::Conflict,"Duplicate selected torrent identity");saved=row;}}
  require(saved&&saved->persistent&&!saved->legacySavePath&&saved->saveFile,ErrorCode::Unsupported,"Selected original save mapping is unavailable");
  auto saveParent=fs::path(saved->saveDirectory);pathShape(saveParent);p.preview.sourceRoot=saveParent/ *saved->saveFile;pathShape(p.preview.sourceRoot);require(!intersects(p.preview.sourceRoot,p.profile),ErrorCode::UnsafePath,"Payload may not contain or reside inside the held profile");
  const auto statePath=p.profile/"active"/(request.hash.hex()+".dat");const auto stateRaw=read(statePath,MaximumDocument);auto state=envelope(stateRaw);hashMatches(state,request.hash);
  require(!inspect(fs::path(statePath.string()+".saving"))&&!inspect(fs::path(statePath.string()+".bak.saving")),ErrorCode::Conflict,"Active state has unresolved temporary data");
  const auto cachePath=p.profile/"active/cache.dat";const auto cacheRaw=read(cachePath,MaximumDocument);auto cache=cacheDocument(cacheRaw);hydrate(state,cacheEntry(cache?&*cache:nullptr,request.hash));
  auto layout=compat::originalLayout(state.document(),MaximumFiles);require(layout.has_value(),ErrorCode::Unsupported,"Unsupported original payload layout");auto attrs=section(state.document().find("attributes"));auto paths=mapping(*saved,*layout,attrs.root());
  const auto* completion=attrs.find("BiglyBTC.completion");if(completion){require(completion->type==Node::Type::Dictionary&&completion->children.empty(),ErrorCode::Conflict,"Completion rename recovery must finish before file operation");}
  if(const auto* conversion=attrs.find("BiglyBTC.compactConversion")){require(conversion->type==Node::Type::List&&conversion->children.empty(),ErrorCode::Conflict,"Compact conversion recovery must finish before file operation");}
  if(request.kind==FileOperationKind::RemoveDeleteData){if(const auto* flags=attrs.find("flags")){require(flags->type==Node::Type::Integer,ErrorCode::InvalidFormat,"Malformed original deletion-protection flags");require((flags->integer&0x40)==0,ErrorCode::Unsupported,"Original torrent flags prohibit deleting payload on removal");}}
  if(moving(request.kind)){
    require(request.destinationParent.has_value(),ErrorCode::InvalidFormat,"Destination parent is required");p.destinationParent=*request.destinationParent;pathShape(p.destinationParent);auto destination=directory(p.destinationParent);struct stat target{};
    require(::fstat(destination.get(),&target)==0&&target.st_uid==::geteuid(),ErrorCode::UnsafePath,"Destination parent must be owned");p.targetDevice=target.st_dev;p.targetInode=target.st_ino;
    p.preview.targetRoot=p.destinationParent/ *saved->saveFile;pathShape(p.preview.targetRoot);require(!intersects(p.preview.sourceRoot,p.preview.targetRoot)&&!intersects(p.preview.targetRoot,p.profile),ErrorCode::Conflict,"Source, destination and profile paths overlap");
    if(request.kind==FileOperationKind::Relocate){struct stat exists{};require(::fstatat(destination.get(),saved->saveFile->c_str(),&exists,AT_SYMLINK_NOFOLLOW)!=0&&errno==ENOENT,ErrorCode::Conflict,"Destination name already exists; relocation never merges or overwrites");}
    p.preview.requiresRecheck=true;
  }else{require(!request.destinationParent,ErrorCode::InvalidFormat,"Removal does not accept a destination");}
  const auto suffix=attrs.find("incompfilesuffix")?text(attrs.find("incompfilesuffix")):std::string{};require(suffix.find('/')==suffix.npos&&suffix.find('\n')==suffix.npos&&suffix.size()<=200,ErrorCode::Unsupported,"Unsupported incomplete suffix");
  std::set<fs::path> unique;std::set<std::pair<dev_t,ino_t>> inodes;
  for(std::size_t index=0;index<layout->files.size();++index){checkpoint(stop);if(layout->files[index].padding){continue;}Payload f;f.view.index=index;f.view.logicalBytes=layout->files[index].bytes;f.view.source=paths[index];f.source=inspect(f.view.source);
    const auto base=saveParent/(layout->multifile?fs::path(*saved->saveFile)/layout->files[index].path:fs::path(*saved->saveFile));
    if(!f.source&&paths[index]==base&&!suffix.empty()){f.view.source+=suffix;f.source=inspect(f.view.source);}
    payloadPath(f.view.source,p.profile);
    if(f.source){ownedPayload(*f.source,physicalMaximum(*layout,attrs.root(),index));require(inodes.emplace(f.source->st_dev,f.source->st_ino).second,ErrorCode::UnsafePath,"Selected files share an inode");}
    require(unique.insert(f.view.source).second,ErrorCode::UnsafePath,"Selected files share a path");
    if(moving(request.kind)){f.view.destination=relocatedPath(*saved,*layout,index,f.view.source,p.destinationParent,suffix);payloadPath(f.view.destination,p.profile);if(request.kind==FileOperationKind::Relink){f.destination=inspect(f.view.destination);if(f.destination){ownedPayload(*f.destination,physicalMaximum(*layout,attrs.root(),index));require(inodes.emplace(f.destination->st_dev,f.destination->st_ino).second,ErrorCode::UnsafePath,"Relink target shares a selected inode");}}
      else{require(!inspect(f.view.destination),ErrorCode::Conflict,"A relocation payload destination already exists");}
      if(request.kind==FileOperationKind::Relocate&&f.source&&f.source->st_dev!=p.targetDevice){p.preview.crossDevice=true;}}
    if(moving(request.kind)){require(unique.insert(f.view.destination).second,ErrorCode::UnsafePath,"Selected destination shares a path");}
    const auto& visible=request.kind==FileOperationKind::Relink?f.destination:f.source;f.view.present=visible.has_value();if(visible){f.view.device=visible->st_dev;f.view.inode=visible->st_ino;f.view.actualBytes=visible->st_size;++p.preview.presentFiles;require(static_cast<std::uint64_t>(visible->st_size)<=UINT64_MAX-p.preview.presentBytes,ErrorCode::InvalidFormat,"Payload byte sum overflow");p.preview.presentBytes+=static_cast<std::uint64_t>(visible->st_size);}else{++p.preview.missingFiles;}
    p.files.push_back(std::move(f));
  }
  if(request.kind==FileOperationKind::Relink){require(p.preview.presentFiles!=0,ErrorCode::Missing,"Relink destination has no original payload files");}
  // No accidental shared data: inspect every other saved root and supported
  // explicit physical map, including identical inode aliases.
  if(request.kind!=FileOperationKind::RemoveKeepData){for(const auto& other:library->downloads){
    if(other.hash&&*other.hash==request.hash){continue;}checkpoint(stop);require(other.hash&&other.saveFile&&!other.legacySavePath,ErrorCode::Unsupported,"Cannot prove independence from another unresolved library mapping");
    const fs::path otherRoot=fs::path(other.saveDirectory)/ *other.saveFile;pathShape(otherRoot);
    require(!intersects(otherRoot,p.preview.sourceRoot)&&(!moving(request.kind)||!intersects(otherRoot,p.preview.targetRoot)),ErrorCode::Conflict,"Payload roots overlap another library torrent");
    auto otherRaw=read(p.profile/"active"/(other.hash->hex()+".dat"),MaximumDocument);auto otherState=envelope(otherRaw);hashMatches(otherState,*other.hash);hydrate(otherState,cacheEntry(cache?&*cache:nullptr,*other.hash));auto otherLayout=compat::originalLayout(otherState.document(),MaximumFiles);require(otherLayout.has_value(),ErrorCode::Unsupported,"Another library payload map is unsupported");auto otherAttrs=section(otherState.document().find("attributes"));auto otherPaths=mapping(other,*otherLayout,otherAttrs.root());
    for(const auto& path:otherPaths){for(const auto& selected:p.files){require(!intersects(path,selected.view.source)&&(selected.view.destination.empty()||!intersects(path,selected.view.destination)),ErrorCode::Conflict,"Payload path is shared by another torrent");}if(auto info=inspect(path)){require(!inodes.contains({info->st_dev,info->st_ino}),ErrorCode::Conflict,"Payload inode belongs to another torrent");}}
  }}
  if(request.kind!=FileOperationKind::RemoveKeepData){for(const auto& row:library->downloads){const fs::path archive(row.torrentFile);pathShape(archive);for(const auto& f:p.files){require(f.view.source!=archive&&(f.view.destination.empty()||f.view.destination!=archive),ErrorCode::UnsafePath,"Payload operation aliases a retained torrent archive");}}}
  const auto& selectedRow=rows.children[saved->sourceIndex];auto updatedRow=parse(encoded(selectedRow));std::string selection;
  for(const auto* key:{"file_priorities","file_priorities_c"}){if(const auto* priorities=selectedRow.find(key)){selection+=b(key)+encoded(*priorities);}}for(const auto* key:{"storetypes","filelinks2","incompfilesuffix","dnd_pfx","dnd_sf","dnd_al","flags"}){if(const auto* n=attrs.find(key)){selection+=b(key)+encoded(*n);}}
  // Include priority/skip and actual path maps in the stable scope, while
  // excluding force/startup/pause/counters changed by legitimate retirement.
  p.scopeSelection=selection;
  if(cache){const auto* entries=cache->document().find("state");if(entries){require(cache->replaceValue("state",withoutSelected(entries,request.hash,false,std::nullopt,"hash")).has_value(),ErrorCode::InvalidFormat,"Cannot remove selected state cache entry");const auto after=serialize(*cache);if(after!=*cacheRaw.bytes){p.edits.push_back({cachePath,cacheRaw,after,FileOperationStage::CachePublished});}}}
  if(moving(request.kind)){
    put(updatedRow,"save_dir",b(p.destinationParent.string()));put(updatedRow,"save_file",b(*saved->saveFile));std::string links="l";
    for(const auto& f:p.files){const auto& original=layout->files[f.view.index];const auto base=p.destinationParent/(layout->multifile?fs::path(*saved->saveFile)/original.path:fs::path(*saved->saveFile));if(base!=f.view.destination){links+=b(std::to_string(f.view.index)+'\n'+base.string()+'\n'+f.view.destination.string());}}
    put(attrs,"filelinks2",links+'e');put(attrs,"canosavedir",b(p.preview.targetRoot.string()+'\n'+p.preview.targetRoot.string()));markRecheck(state,std::move(attrs));const auto newState=serialize(state);
    const auto backup=fs::path(statePath.string()+".bak");p.edits.push_back({backup,read(backup,MaximumDocument),newState,FileOperationStage::StatePublished});p.edits.push_back({statePath,stateRaw,newState,FileOperationStage::StatePublished});
    put(inventory,"downloads",withoutSelected(&rows,request.hash,false,updatedRow.serialize()));
  }else{
    put(inventory,"downloads",withoutSelected(&rows,request.hash,false));if(const auto* pauses=inventory.find("pause_data")){put(inventory,"pause_data",withoutSelected(pauses,request.hash,true));}
  }
  p.edits.push_back({p.profile/"downloads.config",inventoryRaw,inventory.serialize(),FileOperationStage::InventoryPublished});
  if(!moving(request.kind)){p.edits.push_back({statePath,stateRaw,std::nullopt,FileOperationStage::StateRemoved});const auto backup=fs::path(statePath.string()+".bak");auto raw=read(backup,MaximumDocument);if(raw.bytes){p.edits.push_back({backup,std::move(raw),std::nullopt,FileOperationStage::StateRemoved});}}
  for(const auto& path:controlPaths(p.profile,request.hash,layout->files.size())){require(!inspect(fs::path(path.string()+".saving")),ErrorCode::Conflict,"Original control recovery is pending");p.controls.push_back({path,read(path,MaximumDocument)});}
  const auto material=canonical(p,true);p.preview.approvalScopeDigest=digest(material);p.preview.executionCasDigest=digest(canonical(p,false));
  for(const auto& f:p.files){p.preview.files.push_back(f.view);}checkpoint(stop);return p;
}
}
namespace {
fs::path temporaryPath(const Plan& p,const Payload& f){return f.view.destination.parent_path()/(".BiglyBTC.fileop."+p.preview.executionCasDigest.substr(0,16)+'.'+std::to_string(f.view.index));}
void targetIdentity(const Plan& p){if(!moving(p.preview.kind)){return;}auto fd=directory(p.destinationParent);struct stat current{};require(::fstat(fd.get(),&current)==0&&current.st_uid==::geteuid()&&current.st_dev==p.targetDevice&&current.st_ino==p.targetInode,ErrorCode::Conflict,"Destination parent identity changed");}
void readyDestination(Plan& p,Payload& f){
  if(!f.source){return;}targetIdentity(p);auto present=inspect(f.view.destination);
  if(present){const bool ours=identity(*present,*f.source)||(f.temporary&&identity(*present,*f.temporary));require(ours&&present->st_size==f.source->st_size&&digestFile(f.view.destination,*present)==f.sha256,ErrorCode::Conflict,"Destination appeared or changed during relocation");return;}
  auto source=inspect(f.view.source);require(source&&identity(*source,*f.source)&&digestFile(f.view.source,*f.source)==f.sha256,ErrorCode::Conflict,"Relocation source changed");auto from=directory(f.view.source.parent_path());auto to=directory(f.view.destination.parent_path(),true);
  if(::linkat(from.get(),f.view.source.filename().c_str(),to.get(),f.view.destination.filename().c_str(),0)==0){sync(to.get());const auto linked=inspect(f.view.destination);require(linked&&identity(*linked,*f.source),ErrorCode::Conflict,"Linked destination identity changed");return;}
  require(errno==EXDEV||errno==EPERM||errno==EOPNOTSUPP,ErrorCode::Conflict,"Cannot link relocation destination without overwrite");
  const auto temporary=temporaryPath(p,f);auto prior=inspect(temporary);Fd output(-1);
  if(prior){require(f.temporary&&identity(*prior,*f.temporary)&&prior->st_nlink==1,ErrorCode::Conflict,"An unrecorded relocation temporary requires explicit review");output=Fd(::openat(to.get(),temporary.filename().c_str(),O_WRONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));struct stat opened{};require(output.get()>=0&&::fstat(output.get(),&opened)==0&&identity(opened,*prior),ErrorCode::Conflict,"Relocation temporary changed before open");
    // Only this exact journal-recorded scratch inode is restarted, never a
    // source/target file or an unknown preexisting temporary.
    require(::ftruncate(output.get(),0)==0,ErrorCode::Io,"Cannot restart owned relocation scratch");
  }else{output=Fd(::openat(to.get(),temporary.filename().c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));require(output.get()>=0,ErrorCode::Conflict,"Cannot exclusively create relocation scratch");struct stat made{};require(::fstat(output.get(),&made)==0&&made.st_nlink==1&&made.st_uid==::geteuid(),ErrorCode::UnsafePath,"Unsafe relocation scratch");f.temporary=made;sync(to.get());saveJournal(p);}
  Fd input(::openat(from.get(),f.view.source.filename().c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));struct stat opened{};require(input.get()>=0&&::fstat(input.get(),&opened)==0&&identity(opened,*f.source)&&opened.st_size==f.source->st_size,ErrorCode::Conflict,"Copy source changed before open");
  std::array<char,65536> bytes{};std::uint64_t copied=0;for(;;){auto n=::read(input.get(),bytes.data(),bytes.size());if(n<0&&errno==EINTR){continue;}require(n>=0,ErrorCode::Io,"Cannot read relocation source");if(!n){break;}require(static_cast<std::uint64_t>(n)<=static_cast<std::uint64_t>(f.source->st_size)-copied,ErrorCode::Conflict,"Relocation source grew");std::size_t pos=0;while(pos<static_cast<std::size_t>(n)){auto written=::write(output.get(),bytes.data()+pos,static_cast<std::size_t>(n)-pos);if(written<0&&errno==EINTR){continue;}require(written>0,ErrorCode::Io,"Cannot write relocation scratch");pos+=static_cast<std::size_t>(written);}copied+=static_cast<std::uint64_t>(n);}
  require(copied==static_cast<std::uint64_t>(f.source->st_size),ErrorCode::Conflict,"Relocation source shortened");require(::fchmod(output.get(),f.source->st_mode&0777)==0,ErrorCode::Io,"Cannot preserve payload permissions");sync(output.get());auto scratch=inspect(temporary);require(scratch&&f.temporary&&identity(*scratch,*f.temporary)&&digestFile(temporary,*scratch)==f.sha256&&digestFile(f.view.source,*f.source)==f.sha256,ErrorCode::Conflict,"Relocation copy/source validation failed");f.temporary=*scratch;saveJournal(p);
  require(::syscall(SYS_renameat2,to.get(),temporary.filename().c_str(),to.get(),f.view.destination.filename().c_str(),RENAME_NOREPLACE)==0,ErrorCode::Conflict,"Relocation destination appeared before publication");sync(to.get());
}
std::optional<std::string> mergedDocument(const Plan& p,const Edit& edit,const Raw& current){
  if(!current.bytes){require(!edit.before.bytes,ErrorCode::Conflict,"Original document disappeared");return edit.after;}
  if(current.bytes==edit.after){return current.bytes;}
  if(edit.path==p.profile/"downloads.config"){
    require(edit.before.bytes&&edit.after,ErrorCode::InvalidFormat,"Invalid inventory journal");auto before=parse(*edit.before.bytes),after=parse(*edit.after),now=parse(*current.bytes);
    const auto* a=before.find("downloads"),*z=after.find("downloads"),*n=now.find("downloads");const auto old=selectedRows(a,p.preview.hash,false),next=selectedRows(z,p.preview.hash,false),actual=selectedRows(n,p.preview.hash,false);
    require(actual==old||actual==next,ErrorCode::Conflict,"Selected inventory row changed during recovery");
    if(actual!=next){std::optional<std::string> replacement;if(next!="le"){auto parsed=core::Document::parse(next,limits());require(parsed&&parsed->root().children.size()==1,ErrorCode::InvalidFormat,"Invalid updated target row");replacement=encoded(parsed->root().children.front());}put(now,"downloads",withoutSelected(n,p.preview.hash,false,replacement?std::optional<std::string_view>(*replacement):std::nullopt));}
    const auto oldPauses=selectedRows(before.find("pause_data"),p.preview.hash,true),nextPauses=selectedRows(after.find("pause_data"),p.preview.hash,true),actualPauses=selectedRows(now.find("pause_data"),p.preview.hash,true);
    require(actualPauses==oldPauses||actualPauses==nextPauses,ErrorCode::Conflict,"Selected pause intent changed during recovery");
    if(actualPauses!=nextPauses){require(nextPauses=="le",ErrorCode::InvalidFormat,"Unexpected new pause intent in file operation");put(now,"pause_data",withoutSelected(now.find("pause_data"),p.preview.hash,true));}return now.serialize();
  }
  if(edit.path==p.profile/"active/cache.dat"){
    auto before=cacheDocument(edit.before),after=cacheDocument({edit.after,std::nullopt}),now=cacheDocument(current);require(before&&after&&now,ErrorCode::InvalidFormat,"Invalid cache journal");const auto old=selectedRows(before->document().find("state"),p.preview.hash,false,"hash"),next=selectedRows(after->document().find("state"),p.preview.hash,false,"hash"),actual=selectedRows(now->document().find("state"),p.preview.hash,false,"hash");
    require(actual==old||actual==next,ErrorCode::Conflict,"Selected cache state changed during recovery");if(actual!=next){require(next=="le"&&now->replaceValue("state",withoutSelected(now->document().find("state"),p.preview.hash,false,std::nullopt,"hash")).has_value(),ErrorCode::InvalidFormat,"Cannot sparsely remove original cache row");}return serialize(*now);
  }
  require(current.bytes==edit.before.bytes&&current.stamp&&edit.before.stamp&&unchanged(*current.stamp,*edit.before.stamp),ErrorCode::Conflict,"Selected active state changed during recovery");return edit.after;
}
void publishEdit(const Plan& p,const Edit& edit){
  const auto current=read(edit.path,MaximumDocument);auto next=mergedDocument(p,edit,current);if(next==current.bytes){return;}require(next.has_value(),ErrorCode::InvalidFormat,"Invalid document publication");publish(edit.path,current,*next,MaximumDocument);
}
FileOperationResult execute(const ProfileLease& lease,Plan& p,bool recovered,void(*observer)(FileOperationStage)){
  profileIdentity(lease,p);targetIdentity(p);checkControls(p,recovered);const auto notify=[&](FileOperationStage stage){if(observer){observer(stage);}};
  if(p.preview.kind==FileOperationKind::Relocate){for(auto& f:p.files){readyDestination(p,f);if(f.source){notify(FileOperationStage::DestinationPublished);}}}
  if(p.preview.kind==FileOperationKind::Relink){for(const auto& f:p.files){const auto now=inspect(f.view.destination);require(bool(now)==bool(f.destination)&&(!now||(identity(*now,*f.destination)&&digestFile(f.view.destination,*f.destination)==f.sha256)),ErrorCode::Conflict,"Relink target changed after confirmation");}}
  for(const auto& e:p.edits){if(e.after){publishEdit(p,e);notify(e.stage);}}
  FileOperationResult result{p.preview.kind,p.preview.hash,0,0,recovered};
  if(p.preview.kind==FileOperationKind::Relocate||p.preview.kind==FileOperationKind::RemoveDeleteData){for(const auto& f:p.files){if(!f.source){continue;}const auto now=inspect(f.view.source);if(now){require(identity(*now,*f.source)&&digestFile(f.view.source,*f.source)==f.sha256,ErrorCode::Conflict,"Source changed before final removal");erase(f.view.source,*now);}if(p.preview.kind==FileOperationKind::Relocate){++result.filesMoved;}else{++result.filesDeleted;}notify(FileOperationStage::SourceRemoved);}}
  for(const auto& e:p.edits){if(!e.after){const auto current=read(e.path,MaximumDocument);if(current.bytes){require(current.bytes==e.before.bytes&&current.stamp&&e.before.stamp&&unchanged(*current.stamp,*e.before.stamp),ErrorCode::Conflict,"Active cleanup source changed");erase(e.path,*current.stamp);}notify(FileOperationStage::StateRemoved);}}
  if(!moving(p.preview.kind)){for(const auto& c:p.controls){const auto current=read(c.path,MaximumDocument);if(current.bytes){require(current.bytes==c.before.bytes&&current.stamp&&c.before.stamp&&unchanged(*current.stamp,*c.before.stamp),ErrorCode::Conflict,"Original control changed before removal");erase(c.path,*current.stamp);}notify(FileOperationStage::StateRemoved);}}
  const auto raw=read(p.profile/JournalName,MaximumJournal);require(raw.stamp.has_value(),ErrorCode::Conflict,"File-operation journal disappeared");erase(p.profile/JournalName,*raw.stamp);notify(FileOperationStage::Completed);return result;
}
}
namespace {
core::TorrentHash decodeHash(std::string_view hex){require(hex.size()==40||hex.size()==64,ErrorCode::InvalidFormat,"Invalid journal torrent identity");std::string raw;raw.reserve(hex.size()/2);for(std::size_t i=0;i<hex.size();i+=2){unsigned value=0;const auto parsed=std::from_chars(hex.data()+i,hex.data()+i+2,value,16);require(parsed.ec==std::errc{}&&parsed.ptr==hex.data()+i+2,ErrorCode::InvalidFormat,"Invalid journal hash digits");raw+=static_cast<char>(value);}auto key=core::TorrentHash::parse(raw);require(key.has_value(),ErrorCode::InvalidFormat,"Invalid journal hash");return *key;}
bool digestShape(std::string_view value){return value.size()==64&&std::ranges::all_of(value,[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');});}
void validateCurrentIndependence(const Plan& p){
  if(p.preview.kind==FileOperationKind::RemoveKeepData){return;}
  const auto raw=read(p.profile/"downloads.config",MaximumDocument);require(raw.bytes.has_value(),ErrorCode::Conflict,"Recovery inventory disappeared");auto doc=parse(*raw.bytes);auto library=core::readLibrary(doc);require(library&&!library->legacyMap,ErrorCode::InvalidFormat,"Invalid current recovery inventory");auto cache=cacheDocument(read(p.profile/"active/cache.dat",MaximumDocument));
  for(const auto& other:library->downloads){if(other.hash&&*other.hash==p.preview.hash){continue;}
    require(other.hash&&other.saveFile&&!other.legacySavePath,ErrorCode::Unsupported,"Another recovery payload mapping is unresolved");const fs::path root=fs::path(other.saveDirectory)/ *other.saveFile;pathShape(root);require(!intersects(root,p.preview.sourceRoot)&&(p.preview.targetRoot.empty()||!intersects(root,p.preview.targetRoot)),ErrorCode::Conflict,"Another torrent now overlaps operation scope");
    auto state=envelope(read(p.profile/"active"/(other.hash->hex()+".dat"),MaximumDocument));hashMatches(state,*other.hash);hydrate(state,cacheEntry(cache?&*cache:nullptr,*other.hash));auto layout=compat::originalLayout(state.document(),MaximumFiles);require(layout.has_value(),ErrorCode::Unsupported,"Another recovery file layout is unsupported");auto attrs=section(state.document().find("attributes"));auto paths=mapping(other,*layout,attrs.root());
    for(const auto& path:paths){const auto actual=inspect(path);for(const auto& f:p.files){require(!intersects(path,f.view.source)&&(f.view.destination.empty()||!intersects(path,f.view.destination)),ErrorCode::Conflict,"Another torrent now shares a selected path");if(actual){require((!f.source||!identity(*actual,*f.source))&&(!f.destination||!identity(*actual,*f.destination))&&(!f.temporary||!identity(*actual,*f.temporary)),ErrorCode::Conflict,"Another torrent now shares selected inode");}}}
  }
}
Plan decode(const Raw& journalRaw){
  require(journalRaw.bytes&&journalRaw.stamp&&journalRaw.stamp->st_nlink==1&&(journalRaw.stamp->st_mode&0077)==0,ErrorCode::UnsafePath,"Recovery journal must be owned private single-link data");auto outer=parse(*journalRaw.bytes,MaximumJournal);require(integer(outer.find("version"))==1,ErrorCode::Unsupported,"Unsupported file-operation journal version");const auto bytes=text(outer.find("payload"));require(digest(bytes)==text(outer.find("sha256")),ErrorCode::InvalidFormat,"Recovery journal checksum mismatch");auto d=parse(bytes,MaximumJournal);Plan p;
  const auto kind=integer(d.find("kind"));require(kind>=0&&kind<=3,ErrorCode::InvalidFormat,"Invalid journal operation");p.preview.kind=static_cast<FileOperationKind>(kind);p.preview.hash=decodeHash(text(d.find("hash")));p.profile=text(d.find("profile"));pathShape(p.profile);p.preview.sourceRoot=text(d.find("source"));pathShape(p.preview.sourceRoot);p.preview.targetRoot=text(d.find("target"));p.destinationParent=text(d.find("parent"));p.targetDevice=unsignedText(d.find("td"));p.targetInode=unsignedText(d.find("ti"));p.scopeSelection=text(d.find("selection"));p.profileDevice=unsignedText(d.find("pd"));p.profileInode=unsignedText(d.find("pi"));p.preview.approvalScopeDigest=text(d.find("scope"));p.preview.executionCasDigest=text(d.find("cas"));require(digestShape(p.preview.approvalScopeDigest)&&digestShape(p.preview.executionCasDigest),ErrorCode::InvalidFormat,"Invalid journal digests");
  require(!intersects(p.profile,p.preview.sourceRoot),ErrorCode::UnsafePath,"Recovery payload overlaps profile");if(moving(p.preview.kind)){pathShape(p.preview.targetRoot);pathShape(p.destinationParent);require(p.preview.targetRoot.parent_path()==p.destinationParent&&!intersects(p.preview.sourceRoot,p.preview.targetRoot)&&!intersects(p.profile,p.preview.targetRoot),ErrorCode::UnsafePath,"Recovery destination overlaps protected paths");}
  const auto& files=field(d.root(),"files",Node::Type::List);const auto& runtime=field(d.root(),"runtime",Node::Type::List);require(files.children.size()<=MaximumFiles&&runtime.children.size()==files.children.size(),ErrorCode::InvalidFormat,"Invalid recovery file count");std::set<std::size_t> indices;std::set<fs::path> paths;
  for(std::size_t i=0;i<files.children.size();++i){const auto& n=files.children[i];Payload f;const auto index=integer(n.find("i"));require(index>=0&&static_cast<std::uint64_t>(index)<MaximumFiles&&indices.insert(static_cast<std::size_t>(index)).second,ErrorCode::InvalidFormat,"Duplicate recovery file index");f.view.index=static_cast<std::size_t>(index);f.view.source=text(n.find("s"));pathShape(f.view.source);require(paths.insert(f.view.source).second,ErrorCode::InvalidFormat,"Duplicate recovery source");f.view.destination=text(n.find("d"));f.view.logicalBytes=integer(n.find("bytes"));require(f.view.logicalBytes>=0,ErrorCode::InvalidFormat,"Invalid original file length");if(const auto* s=n.find("stamp")){f.source=readStamp(*s);require(f.source->st_nlink==1,ErrorCode::InvalidFormat,"Invalid recovery source stamp");}
    if(const auto* s=n.find("destination")){f.destination=readStamp(*s);require(f.destination->st_nlink==1,ErrorCode::InvalidFormat,"Invalid relink stamp");}
    if(moving(p.preview.kind)){pathShape(f.view.destination);require(within(f.view.destination,p.destinationParent)&&!intersects(f.view.destination,p.profile),ErrorCode::UnsafePath,"Recovery destination escapes selected parent");}
    f.sha256=text(runtime.children[i].find("sha"));if((f.source&&p.preview.kind!=FileOperationKind::RemoveKeepData)||(f.destination&&p.preview.kind==FileOperationKind::Relink)){require(digestShape(f.sha256),ErrorCode::InvalidFormat,"Missing recovery content digest");}if(const auto* t=runtime.children[i].find("temp")){f.temporary=readStamp(*t);require(f.temporary->st_nlink==1,ErrorCode::InvalidFormat,"Recovery scratch must have one link");}p.files.push_back(std::move(f));
  }
  if(const auto* controls=d.find("controls")){require(controls->type==Node::Type::List&&controls->children.size()<=MaximumFiles*4,ErrorCode::InvalidFormat,"Invalid recovery control count");std::set<fs::path> seen;for(const auto& n:controls->children){Control c;c.path=text(n.find("path"));pathShape(c.path);require(seen.insert(c.path).second,ErrorCode::InvalidFormat,"Duplicate recovery control");if(const auto* before=n.find("before")){c.before.bytes=text(before);c.before.stamp=readStamp(field(n,"stamp",Node::Type::Dictionary));require(c.before.stamp->st_nlink==1&&c.before.stamp->st_size==static_cast<std::int64_t>(c.before.bytes->size())&&c.before.bytes->size()<=MaximumDocument,ErrorCode::InvalidFormat,"Invalid recovery control stamp");}p.controls.push_back(std::move(c));}}
  const auto& edits=field(d.root(),"edits",Node::Type::List);require(edits.children.size()>=2&&edits.children.size()<=5,ErrorCode::InvalidFormat,"Invalid recovery document count");std::set<fs::path> names;const auto active=p.profile/"active"/(p.preview.hash.hex()+".dat");
  for(const auto& n:edits.children){Edit e;e.path=text(n.find("path"));pathShape(e.path);const bool cache=e.path==p.profile/"active/cache.dat",inventory=e.path==p.profile/"downloads.config",state=e.path==active||e.path==fs::path(active.string()+".bak");require((cache||inventory||state)&&names.insert(e.path).second,ErrorCode::UnsafePath,"Recovery document is not an exact fixed owned path");if(const auto* before=n.find("before")){e.before.bytes=text(before);e.before.stamp=readStamp(field(n,"stamp",Node::Type::Dictionary));require(e.before.stamp->st_nlink==1,ErrorCode::InvalidFormat,"Recovery document is linked");}if(const auto* after=n.find("after")){e.after=text(after);}const auto stage=integer(n.find("stage"));e.stage=static_cast<FileOperationStage>(stage);require((cache&&e.after&&e.stage==FileOperationStage::CachePublished)||(inventory&&e.after&&e.stage==FileOperationStage::InventoryPublished)||(state&&((moving(p.preview.kind)&&e.after&&e.stage==FileOperationStage::StatePublished)||(!moving(p.preview.kind)&&!e.after&&e.stage==FileOperationStage::StateRemoved))),ErrorCode::InvalidFormat,"Invalid recovery document stage");p.edits.push_back(std::move(e));}
  require(names.contains(active)&&names.contains(p.profile/"downloads.config"),ErrorCode::InvalidFormat,"Recovery lacks target state/inventory");require(digest(canonical(p,false))==p.preview.executionCasDigest&&digest(canonical(p,true))==p.preview.approvalScopeDigest,ErrorCode::InvalidFormat,"Recovery scope/execution binding mismatch");
  const auto stateEdit=std::ranges::find(p.edits,active,&Edit::path);auto state=envelope(stateEdit->before);hashMatches(state,p.preview.hash);const auto cache=std::ranges::find(p.edits,p.profile/"active/cache.dat",&Edit::path);auto cacheDoc=cache==p.edits.end()?std::optional<EncodedDocument>{}:cacheDocument(cache->before);hydrate(state,cacheEntry(cacheDoc?&*cacheDoc:nullptr,p.preview.hash));
  const auto inventory=std::ranges::find(p.edits,p.profile/"downloads.config",&Edit::path);require(inventory->before.bytes.has_value(),ErrorCode::InvalidFormat,"Recovery original inventory missing");auto inv=parse(*inventory->before.bytes);auto library=core::readLibrary(inv);require(library&&!library->legacyMap,ErrorCode::InvalidFormat,"Invalid recovery original library");std::optional<core::SavedDownload> selected;for(const auto& row:library->downloads){if(row.hash&&*row.hash==p.preview.hash){require(!selected,ErrorCode::InvalidFormat,"Duplicate recovery target");selected=row;}}
  require(selected&&selected->saveFile&&!selected->legacySavePath&&p.preview.sourceRoot==fs::path(selected->saveDirectory)/ *selected->saveFile&&(!moving(p.preview.kind)||p.preview.targetRoot==p.destinationParent/ *selected->saveFile),ErrorCode::InvalidFormat,"Recovery paths differ from original row");auto layout=compat::originalLayout(state.document(),MaximumFiles);require(layout.has_value(),ErrorCode::Unsupported,"Unsupported recovery original layout");auto attrs=section(state.document().find("attributes"));const auto map=mapping(*selected,*layout,attrs.root());const auto suffix=attrs.find("incompfilesuffix")?text(attrs.find("incompfilesuffix")):std::string{};
  if(p.preview.kind==FileOperationKind::RemoveDeleteData){if(const auto* flags=attrs.find("flags")){require(flags->type==Node::Type::Integer&&(flags->integer&0x40)==0,ErrorCode::Unsupported,"Original flags prohibit payload deletion");}}
  for(const auto& f:p.files){require(f.view.index<layout->files.size()&&!layout->files[f.view.index].padding&&f.view.logicalBytes==layout->files[f.view.index].bytes,ErrorCode::InvalidFormat,"Recovery file geometry changed");payloadPath(f.view.source,p.profile);const auto expected=map[f.view.index];const auto base=fs::path(selected->saveDirectory)/(layout->multifile?fs::path(*selected->saveFile)/layout->files[f.view.index].path:fs::path(*selected->saveFile));require(f.view.source==expected||(expected==base&&!suffix.empty()&&f.view.source==fs::path(base.string()+suffix)),ErrorCode::UnsafePath,"Recovery source differs from original file mapping");if(moving(p.preview.kind)){require(f.view.destination==relocatedPath(*selected,*layout,f.view.index,f.view.source,p.destinationParent,suffix),ErrorCode::UnsafePath,"Recovery destination differs from approved mapping");}}
  if(!p.controls.empty()){const auto expected=controlPaths(p.profile,p.preview.hash,layout->files.size());require(expected.size()==p.controls.size(),ErrorCode::InvalidFormat,"Recovery control inventory changed");for(std::size_t i=0;i<expected.size();++i){require(expected[i]==p.controls[i].path,ErrorCode::UnsafePath,"Recovery control is not an exact original indexed state leaf");}}else{for(std::size_t i=0;i<layout->files.size();++i){require(storageKind(attrs.root(),i,layout->files.size())==OriginalStorageKind::Linear,ErrorCode::InvalidFormat,"Legacy journal cannot omit nonlinear control inventory");}}
  for(const auto& f:p.files){const auto maximum=physicalMaximum(*layout,attrs.root(),f.view.index);if(f.source){ownedPayload(*f.source,maximum);}if(f.destination){ownedPayload(*f.destination,maximum);}}
  p.journalCreated=true;return p;
}
}
struct PreparedFileOperation::Impl {Plan plan;explicit Impl(Plan p):plan(std::move(p)){}};
PreparedFileOperation::PreparedFileOperation(std::unique_ptr<Impl> p):impl_(std::move(p)){}
PreparedFileOperation::PreparedFileOperation(PreparedFileOperation&&) noexcept=default;
PreparedFileOperation& PreparedFileOperation::operator=(PreparedFileOperation&&) noexcept=default;
PreparedFileOperation::~PreparedFileOperation()=default;
const FileOperationPreview& PreparedFileOperation::preview()const noexcept{return impl_->plan.preview;}
std::expected<PreparedFileOperation,Error> prepareFileOperation(const ProfileLease& lease,const FileOperationRequest& request,std::stop_token stop){return guarded([&]()->std::expected<PreparedFileOperation,Error>{return PreparedFileOperation(std::make_unique<PreparedFileOperation::Impl>(prepare(lease,request,stop)));});}
std::expected<FileOperationConfirmation,Error> confirmFileOperation(const PreparedFileOperation& p,std::string_view accepted,bool deleteConfirmed){return guarded([&]()->std::expected<FileOperationConfirmation,Error>{require(accepted==p.preview().approvalScopeDigest,ErrorCode::Conflict,"File-operation approved scope changed");require(p.preview().kind!=FileOperationKind::RemoveDeleteData||deleteConfirmed,ErrorCode::Conflict,"Explicit payload deletion confirmation is required");return FileOperationConfirmation{std::string(accepted),deleteConfirmed};});}
std::expected<FileOperationResult,Error> applyFileOperation(const ProfileLease& lease,PreparedFileOperation&& prepared,const FileOperationConfirmation& confirmation,void(*observer)(FileOperationStage),std::stop_token stop){
  return guarded([&]()->std::expected<FileOperationResult,Error>{require(bool(prepared.impl_),ErrorCode::Conflict,"Prepared file operation was moved/consumed");auto& p=prepared.impl_->plan;checkpoint(stop);require(!p.attempted&&confirmation.approvalScopeDigest==p.preview.approvalScopeDigest&&(p.preview.kind!=FileOperationKind::RemoveDeleteData||confirmation.deleteConfirmed),ErrorCode::Conflict,"File-operation confirmation is stale or missing");profileIdentity(lease,p);targetIdentity(p);require(!hasPendingFileOperation(lease),ErrorCode::Conflict,"Existing file operation needs recovery");
    checkControls(p);for(const auto& e:p.edits){check(e.path,e.before,MaximumDocument);}for(auto& f:p.files){checkpoint(stop);const auto& expected=p.preview.kind==FileOperationKind::Relink?f.destination:f.source;const auto& path=p.preview.kind==FileOperationKind::Relink?f.view.destination:f.view.source;const auto now=inspect(path);require(bool(now)==bool(expected)&&(!now||unchanged(*now,*expected)),ErrorCode::Conflict,"Payload changed after preparation");if(expected&&p.preview.kind!=FileOperationKind::RemoveKeepData){f.sha256=digestFile(path,*expected,stop);}}
    checkpoint(stop);p.attempted=true;saveJournal(p);if(observer){observer(FileOperationStage::JournalSynced);}return execute(lease,p,false,observer);
  });
}
bool hasPendingFileOperation(const ProfileLease& lease) noexcept {
  try{const auto profile=fs::path(profilePath(lease));return inspect(profile/JournalName).has_value()||inspect(profile/(std::string(JournalName)+".BiglyBTC.fileop-saving")).has_value();}catch(...){return true;}
}
std::expected<std::optional<FileOperationResult>,Error> recoverFileOperation(const ProfileLease& lease,void(*observer)(FileOperationStage)){
  return guarded([&]()->std::expected<std::optional<FileOperationResult>,Error>{const auto profile=fs::path(profilePath(lease));auto raw=read(profile/JournalName,MaximumJournal);if(!raw.bytes){require(!hasPendingFileOperation(lease),ErrorCode::Conflict,"Unpublished file-operation journal requires explicit review");return std::nullopt;}auto p=decode(raw);profileIdentity(lease,p);validateCurrentIndependence(p);
    // Check every affected document before any further data mutation. Sparse
    // target row/pauses/cache merges tolerate unrelated owner checkpoints.
    for(const auto& e:p.edits){const auto now=read(e.path,MaximumDocument);if(e.after){static_cast<void>(mergedDocument(p,e,now));}else if(now.bytes){require(now.bytes==e.before.bytes&&now.stamp&&e.before.stamp&&unchanged(*now.stamp,*e.before.stamp),ErrorCode::Conflict,"Removal state changed during recovery");}}
    return std::optional<FileOperationResult>{execute(lease,p,true,observer)};
  });
}
} // namespace biglybtc::storage
