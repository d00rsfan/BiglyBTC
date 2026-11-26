#include "../platform/utf8_path.hpp"
#include <biglybtc/saved_dnd.hpp>
#include "biglybtc/file_selection_intent.hpp"
#include "biglybtc/post_skip_existence.hpp"
#include "biglybtc/file_storage_selection.hpp"
#include "biglybtc/original_file_links.hpp"
#include "biglybtc/file_policy.hpp"
#include "file_operations.hpp"
#include "file_storage_selection_source.hpp"
#include "original_selection_group.hpp"
#include "torrent_intake.hpp"
#include "category_store.hpp"
#include "application_settings.hpp"
#include "linear_to_compact.hpp"
#include "original_torrent_identity.hpp"
#include "file_operation_io.hpp"
#include "original_file_io.hpp"
#include "original_storage_defaults.hpp"
#include "original_filename_policy.hpp"
#include "encoded_document.hpp"
#include "biglybtc/torrent_layout.hpp"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <thread>

namespace biglybtc::storage {
namespace {
using namespace fileop;
using platform::pathFromUtf8;
using platform::pathToUtf8;
namespace fs=std::filesystem;
using Node=core::Node;
constexpr std::size_t MaximumDocument=64*1024*1024,MaximumJournal=384*1024*1024,MaximumFiles=10000;
constexpr const char* JournalName="BiglyBTC.file-operation";
template<class F> auto guarded(F&& action)->decltype(action()){
  try{return action();}catch(const Failure& e){return std::unexpected(e.error);}catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Insufficient memory for file operation"});}
  catch(const std::exception& e){return std::unexpected(Error{ErrorCode::Io,e.what()});}
}
template<class T> T expectedValue(std::expected<T,Error>&& value){if(!value){throw Failure{value.error()};}return std::move(*value);}
void acquisitionAvailable(const ProfileLease& lease){auto allowed=requireNoPendingSubscriptionAcquisition(lease);if(!allowed){throw Failure{allowed.error()};}}
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
bool within(const fs::path& child,const fs::path& parent){auto a=child.begin(),z=parent.begin();for(;z!=parent.end();++z,++a){if(a==child.end()||!detail::sameFilesystemComponent(*a,*z)){return false;}}return true;}
bool intersects(const fs::path&a,const fs::path&b){return within(a,b)||within(b,a);}
fs::path relativeWithin(const fs::path& child,const fs::path& parent){
  require(within(child,parent),ErrorCode::UnsafePath,"File-operation path escapes its parent");
  auto part=child.begin();
  for(auto entry=parent.begin();entry!=parent.end();++entry){++part;}
  fs::path relative;
  for(;part!=child.end();++part){relative/=*part;}
  return relative;
}
void ownedPayload(const FileStamp& stamp,std::int64_t maximum){require(stamp.links==1&&stamp.size>=0&&stamp.size<=maximum,ErrorCode::UnsafePath,"Payload is linked or exceeds original file length");}
struct Edit {fs::path path;Raw before;std::optional<std::string> after;FileOperationStage stage;};
// Control files belong to profile state, never to payload previews or moves.
struct Control {fs::path path;Raw before;};
struct Archive {fs::path path;Raw before;bool internalState=false;Raw backup;};
struct Trash {fs::path root;std::string deletionDate;std::array<std::pair<std::uint64_t,std::uint64_t>,3> directories{};};
struct Replacement {fs::path root;std::string sha256;std::uint64_t directoryDevice=0,directoryInode=0;};
struct Payload {FileOperationEntry view;std::optional<FileStamp> source,destination;std::string sha256;std::optional<FileStamp> temporary;std::string allocationToken;bool selectionRemove=false,selectionAllocate=false;fs::path selectionAnchor;std::uint64_t selectionDevice=0,selectionInode=0;};
struct Plan {
  FileOperationPreview preview;
  fs::path profile,destinationParent;
  std::uint64_t profileDevice=0,profileInode=0,targetDevice=0,targetInode=0;
  std::vector<Payload> files;
  detail::StorageSelectionSource storageSource; // Memory-only read snapshot,never journalled.
  std::vector<Control> controls;
  std::vector<OriginalFileGeometry> controlGeometry;
  std::vector<Edit> edits;
  // Memory-only prepared owner and exact read-only inputs. Journal bytes never
  // reconstruct either a live OFD or permission to resume a removal.
  std::shared_ptr<const ProfileLease> preparedLease;
  std::thread::id preparedWorker;
  std::vector<Control> removalGuards;
  std::optional<Raw> ownedRemovalJournal;
  std::string removalInfoSha256,removalBeforeState;
  std::optional<core::SavedDownload> removalBeforeSaved;
  std::array<std::uint64_t,2> removalActiveDirectory{};
  std::vector<Control> removalPublished;
  std::int64_t removalEventMillis=0;
  std::chrono::steady_clock::time_point removalEventEnd;
  std::optional<Archive> archive;
  std::optional<Trash> trash;
  std::optional<Replacement> replacement;
  std::optional<std::string> filenameConversions;
  std::optional<bool> decoderShowAll;
  std::string scopeSelection;
  std::optional<Raw> reviewedJournal; // Memory-only exact old journal CAS; never serialized as authority.
  bool attempted=false,journalCreated=false,postSkip=false;
  PayloadDigestCache payloadDigests; // Empty on decode; at most eight pinned fds.
};
bool selectionRetiresControl(const Plan&,const fs::path&);
bool effectMoves(const Payload&);
void profileIdentity(const ProfileLease& lease,const Plan& plan){
  if(plan.preparedLease){require(plan.preparedWorker==std::this_thread::get_id()&&lease.sharesOwnership(*plan.preparedLease),ErrorCode::Conflict,"File-operation prepared owner/worker changed");}
  const auto path=pathFromUtf8(profilePath(lease));const auto s=heldFileStamp(lease.directoryHandle());require(path==plan.profile&&s.device==plan.profileDevice&&s.inode==plan.profileInode,ErrorCode::Conflict,"File-operation lease/profile identity changed");}
std::string stampText(const FileStamp& s){auto d=parse("de");put(d,"dev",b(std::to_string(s.device)));put(d,"ino",b(std::to_string(s.inode)));put(d,"size",number(s.size));put(d,"mode",number(s.mode));put(d,"uid",number(s.owner));put(d,"links",number(s.links));put(d,"ms",number(s.modified.tv_sec));put(d,"mn",number(s.modified.tv_nsec));put(d,"cs",number(s.changed.tv_sec));put(d,"cn",number(s.changed.tv_nsec));return d.serialize();}
FileStamp readStamp(const Node& n){
  require(n.type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid journal stat");
  FileStamp stamp;
  stamp.device=unsignedText(n.find("dev"));stamp.inode=unsignedText(n.find("ino"));stamp.size=integer(n.find("size"));
  const auto mode=integer(n.find("mode")),owner=integer(n.find("uid")),links=integer(n.find("links"));
  require(std::in_range<unsigned>(mode)&&owner>=0&&links>=0,ErrorCode::InvalidFormat,"Invalid journal attributes");
  stamp.mode=static_cast<unsigned>(mode);stamp.owner=static_cast<std::uint64_t>(owner);stamp.links=static_cast<std::uint64_t>(links);
  const auto timestamp=[&](std::string_view seconds,std::string_view nanos){
    const auto s=integer(n.find(seconds)),ns=integer(n.find(nanos));
    require(std::in_range<decltype(timespec{}.tv_sec)>(s)&&ns>=0&&ns<1000000000,ErrorCode::InvalidFormat,"Invalid journal timestamp");
    return timespec{static_cast<decltype(timespec{}.tv_sec)>(s),static_cast<decltype(timespec{}.tv_nsec)>(ns)};
  };
  stamp.modified=timestamp("ms","mn");stamp.changed=timestamp("cs","cn");
  require(validJournalFileStamp(stamp),ErrorCode::InvalidFormat,"Unsafe journal file stamp");
  stamp.owned=true;stamp.regular=true;return stamp;
}
std::string canonical(const Plan& p,bool stable){
  auto d=parse("de",MaximumJournal);put(d,"kind",number(static_cast<int>(p.preview.kind)));put(d,"hash",b(p.preview.hash.hex()));put(d,"profile",b(pathToUtf8(p.profile)));put(d,"source",b(pathToUtf8(p.preview.sourceRoot)));put(d,"target",b(pathToUtf8(p.preview.targetRoot)));put(d,"parent",b(pathToUtf8(p.destinationParent)));put(d,"td",b(std::to_string(p.targetDevice)));put(d,"ti",b(std::to_string(p.targetInode)));put(d,"selection",b(p.scopeSelection));
  if(!p.removalInfoSha256.empty()){put(d,"fullInfoSha256",b(p.removalInfoSha256));}
  if(p.postSkip){put(d,"postSkip",number(1));}
  std::string rows="l";for(const auto& f:p.files){auto row=parse("de");put(row,"i",number(static_cast<std::int64_t>(f.view.index)));put(row,"s",b(pathToUtf8(f.view.source)));put(row,"d",b(pathToUtf8(f.view.destination)));put(row,"bytes",number(f.view.logicalBytes));
    if(f.source){put(row,"dev",b(std::to_string(f.source->device)));put(row,"ino",b(std::to_string(f.source->inode)));if(!stable){put(row,"stamp",stampText(*f.source));}}
    if(f.destination){put(row,"dd",b(std::to_string(f.destination->device)));put(row,"di",b(std::to_string(f.destination->inode)));if(!stable){put(row,"destination",stampText(*f.destination));}}
    if(p.postSkip){put(row,"remove",number(f.selectionRemove));put(row,"allocate",number(f.selectionAllocate));if(f.selectionRemove||f.selectionAllocate){put(row,"anchor",b(pathToUtf8(f.selectionAnchor)));put(row,"anchorDevice",b(std::to_string(f.selectionDevice)));put(row,"anchorInode",b(std::to_string(f.selectionInode)));}}
    rows+=row.serialize();}put(d,"files",rows+'e');
  if(p.replacement){auto r=parse("de");put(r,"root",b(pathToUtf8(p.replacement->root)));put(r,"sha",b(p.replacement->sha256));put(r,"bytes",number(p.files.front().destination->size));if(p.preview.unsealedPreimageDirectory){put(r,"unsealed",b(p.preview.unsealedPreimageDirectory->string()));}put(d,"replacement",r.serialize());}
  if(p.filenameConversions){put(d,"filenameConversions",b(*p.filenameConversions));}
  if(p.decoderShowAll){put(d,"decoderShowAll",number(*p.decoderShowAll?1:0));}
  if(p.trash){auto t=parse("de");put(t,"root",b(pathToUtf8(p.trash->root)));if(!stable){put(t,"date",b(p.trash->deletionDate));}put(d,"trash",t.serialize());}
  if(!p.controls.empty()){std::string controls="l";for(const auto& c:p.controls){auto item=parse("de",MaximumJournal);put(item,"path",b(pathToUtf8(c.path)));if(!stable&&c.before.bytes){put(item,"before",b(*c.before.bytes));put(item,"stamp",stampText(*c.before.stamp));}controls+=item.serialize();}put(d,"controls",controls+'e');}
  if(!stable&&!p.removalGuards.empty()){std::string guards="l";for(const auto& c:p.removalGuards){auto item=parse("de",MaximumJournal);put(item,"path",b(pathToUtf8(c.path)));if(c.before.bytes){put(item,"before",b(*c.before.bytes));put(item,"stamp",stampText(*c.before.stamp));}guards+=item.serialize();}put(d,"removalGuards",guards+'e');}
  if(!stable){std::string edits="l";for(const auto& e:p.edits){auto item=parse("de",MaximumJournal);put(item,"path",b(pathToUtf8(e.path)));put(item,"stage",number(static_cast<int>(e.stage)));if(e.before.bytes){put(item,"before",b(*e.before.bytes));put(item,"stamp",stampText(*e.before.stamp));}if(e.after){put(item,"after",b(*e.after));}edits+=item.serialize();}put(d,"edits",edits+'e');}
  if(p.archive){const auto& a=*p.archive;auto row=parse("de",MaximumJournal);put(row,"path",b(pathToUtf8(a.path)));put(row,"internal",number(a.internalState));
    // Selected internal state already has exact execution CAS below; legitimate
    // retirement checkpoints must not change the visible archive choice.
    if(stable&&!a.internalState&&a.before.stamp){put(row,"dev",b(std::to_string(a.before.stamp->device)));put(row,"ino",b(std::to_string(a.before.stamp->inode)));put(row,"sha256",b(digest(*a.before.bytes)));}
    if(!stable&&a.before.bytes){put(row,"before",b(*a.before.bytes));put(row,"stamp",stampText(*a.before.stamp));}
    if(!a.internalState){auto backup=parse("de",MaximumJournal);if(a.backup.bytes){if(stable){put(backup,"dev",b(std::to_string(a.backup.stamp->device)));put(backup,"ino",b(std::to_string(a.backup.stamp->inode)));put(backup,"sha256",b(digest(*a.backup.bytes)));}else{put(backup,"before",b(*a.backup.bytes));put(backup,"stamp",stampText(*a.backup.stamp));}}put(row,"backup",backup.serialize());}put(d,"archive",row.serialize());}
  return d.serialize();
}
std::string journal(const Plan& p){
  auto d=parse(canonical(p,false),MaximumJournal);put(d,"version","i1e");put(d,"pd",b(std::to_string(p.profileDevice)));put(d,"pi",b(std::to_string(p.profileInode)));put(d,"scope",b(p.preview.approvalScopeDigest));put(d,"cas",b(p.preview.executionCasDigest));
  if(p.trash&&p.trash->directories[0].second){std::string dirs="l";for(const auto& [dev,ino]:p.trash->directories){auto row=parse("de");put(row,"dev",b(std::to_string(dev)));put(row,"ino",b(std::to_string(ino)));dirs+=row.serialize();}put(d,"trashDirectories",dirs+'e');}
  if(p.replacement&&p.replacement->directoryInode){auto r=parse("de");put(r,"dev",b(std::to_string(p.replacement->directoryDevice)));put(r,"ino",b(std::to_string(p.replacement->directoryInode)));put(d,"preimageDirectory",r.serialize());}
  std::string runtime="l";for(const auto& f:p.files){auto n=parse("de");put(n,"sha",b(f.sha256));if(!f.allocationToken.empty()){put(n,"allocationToken",b(f.allocationToken));}if(f.temporary){put(n,"temp",stampText(*f.temporary));}runtime+=n.serialize();}put(d,"runtime",runtime+'e');const auto body=d.serialize();return "d7:payload"+b(body)+"6:sha256"+b(digest(body))+"7:versioni1ee";
}
void saveJournal(Plan& p){const auto path=p.profile/JournalName;auto old=read(path,MaximumJournal);
  require(bool(old.bytes)==p.journalCreated,ErrorCode::Conflict,"File-operation journal appeared/disappeared unexpectedly");
  if(old.bytes){auto outer=parse(*old.bytes,MaximumJournal);const auto body=text(outer.find("payload"));require(digest(body)==text(outer.find("sha256")),ErrorCode::Conflict,"Existing operation journal changed");auto prior=parse(body,MaximumJournal);require(text(prior.find("scope"))==p.preview.approvalScopeDigest&&text(prior.find("cas"))==p.preview.executionCasDigest,ErrorCode::Conflict,"Existing journal belongs to another operation");}
  publish(path,old,journal(p),MaximumJournal);p.journalCreated=true;
  if(p.preview.kind==FileOperationKind::RemoveKeepData){p.ownedRemovalJournal=read(path,MaximumJournal);}
}
OriginalTorrentIdentity fullIdentity(const EncodedDocument& doc,const core::TorrentHash& hash){
  const auto subject=originalTorrentInfoSubject(doc.document());require(subject.has_value(),subject?ErrorCode::Io:subject.error().code,subject?"Original info subject":subject.error().message.c_str());
  const auto* info=doc.document().find("info");OriginalFullInfoHashes full;
  const auto compute=[&](const EVP_MD* algorithm,std::span<std::byte> output){std::array<unsigned char,EVP_MAX_MD_SIZE> digest{};unsigned size=0;require(EVP_Digest(subject->data(),subject->size(),digest.data(),&size,algorithm,nullptr)==1&&size==output.size(),ErrorCode::InvalidFormat,"Cannot compute complete original info digest");std::copy_n(reinterpret_cast<const std::byte*>(digest.data()),output.size(),output.begin());};
  if(info->find("pieces")){full.v1.emplace();compute(EVP_sha1(),*full.v1);}if(info->find("file tree")){full.v2.emplace();compute(EVP_sha256(),*full.v2);}
  const auto identity=selectOriginalTorrentIdentity(doc.document(),{*subject,std::move(full)});require(identity.has_value(),identity?ErrorCode::Io:identity.error().code,identity?"Original selected identity":identity.error().message.c_str());
  const auto matched=validateOriginalTorrentKey(*identity,hash,true);require(matched.has_value(),matched?ErrorCode::Io:matched.error().code,matched?"Original saved identity":matched.error().message.c_str());return *identity;
}
void hashMatches(const EncodedDocument& doc,const core::TorrentHash& hash){(void)fullIdentity(doc,hash);}
EncodedDocument envelope(const Raw& raw){require(raw.bytes.has_value(),ErrorCode::Missing,"Selected active metadata is absent");auto doc=EncodedDocument::parse(*raw.bytes,limits());require(doc&&doc->document().root().type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid original active metadata");return std::move(*doc);}
void archiveShape(const Plan& p,const Archive& a){
  pathShape(a.path);const auto active=p.profile/"active"/(p.preview.hash.hex()+".dat");
  require(a.internalState==(a.path==active||a.path==pathFromUtf8(pathToUtf8(active)+".bak")),ErrorCode::UnsafePath,"Archive internal-state alias changed");
  require(p.preview.kind==FileOperationKind::RemoveKeepData||p.preview.kind==FileOperationKind::RemoveDeleteData||p.preview.kind==FileOperationKind::RemoveRecycleData,ErrorCode::Unsupported,"Archive deletion is available only when removing a torrent");
  if(!a.internalState){
    require(!within(a.path,p.profile)||(!within(a.path,p.profile/"active")&&a.path.extension()==".torrent"),ErrorCode::UnsafePath,"Archive aliases protected profile data");
    require(!intersects(a.path,p.preview.sourceRoot),ErrorCode::UnsafePath,"Archive aliases the selected payload root");
  }
  const auto metadata=[&](const Raw& raw){if(raw.bytes){require(raw.stamp&&raw.stamp->links==1&&raw.stamp->size==static_cast<std::int64_t>(raw.bytes->size()),ErrorCode::UnsafePath,"Archive must be private owned single-link metadata");hashMatches(envelope(raw),p.preview.hash);}};
  metadata(a.before);metadata(a.backup);
  require(!a.internalState||!a.backup.bytes,ErrorCode::InvalidFormat,"Internal archive cannot add an unreviewed backup");
}
void checkArchive(const Archive& a,bool recovered){
  const auto one=[&](const fs::path& path,const Raw& before){const auto current=read(path,MaximumDocument);if(current.bytes){check(path,before,MaximumDocument);}else{require(recovered||!before.bytes,ErrorCode::Conflict,"Reviewed torrent archive disappeared");}};
  one(a.path,a.before);if(!a.internalState){one(pathFromUtf8(pathToUtf8(a.path)+".bak"),a.backup);}
}
void archiveReference(const Plan& p,const core::SavedDownload& other){
  if(!p.archive){return;}const auto& a=*p.archive;const fs::path primary=pathFromUtf8(other.torrentFile);
  // Original archive deletion owns both the named metadata and its exact .bak.
  // Neither may become another torrent's reviewed deletion scope, even if the
  // backup was absent when that other library record was loaded.
  for(const auto& path:{primary,pathFromUtf8(pathToUtf8(primary)+".bak")}){
    pathShape(path);
    require(path!=a.path&&(a.internalState||path!=pathFromUtf8(pathToUtf8(a.path)+".bak")),ErrorCode::Conflict,"Torrent archive is referenced by another library item");
    const auto stamp=inspect(path);require(!stamp||((!a.before.stamp||!identity(*stamp,*a.before.stamp))&&(!a.backup.stamp||!identity(*stamp,*a.backup.stamp))),ErrorCode::Conflict,"Torrent archive inode is shared by another library item");
  }
}
void archivePayload(const Plan& p,const fs::path& path){
  if(!p.archive||p.archive->internalState){return;}const auto& a=*p.archive;
  require(!intersects(a.path,path)&&!intersects(pathFromUtf8(pathToUtf8(a.path)+".bak"),path),ErrorCode::UnsafePath,"Torrent archive aliases payload data");
  const auto stamp=inspect(path);require(!stamp||((!a.before.stamp||!identity(*stamp,*a.before.stamp))&&(!a.backup.stamp||!identity(*stamp,*a.backup.stamp))),ErrorCode::UnsafePath,"Torrent archive inode aliases payload data");
}
void hydrate(EncodedDocument& state,const Node* cached){
  if(!cached){return;}for(const auto& [from,to]:{std::pair{"attributes","attributes"},{"azp","azureus_properties"},{"azpp","azureus_private_properties"}}){if(const auto* n=cached->find(from)){require(n->type==Node::Type::Dictionary&&state.replaceValue(to,encoded(*n)).has_value(),ErrorCode::InvalidFormat,"Cannot hydrate original cached state");}}
}
void markRecheck(EncodedDocument& state,core::Document attributes){
  auto resume=section(state.document().find("resume"));auto data=section(resume.find("data"));if(const auto* pieces=data.find("resume data")){require(pieces->type==Node::Type::Bytes&&pieces->bytes.size()<=1000000,ErrorCode::InvalidFormat,"Invalid original piece flags");put(data,"resume data",b(std::string(pieces->bytes.size(),'\2')));}
  put(data,"valid","i0e");put(data,"blocks","de");put(resume,"data",data.serialize());put(attributes,"resumecomplete","i1e");require(state.replaceValue("resume",resume.serialize()).has_value()&&state.replaceValue("attributes",attributes.serialize()).has_value(),ErrorCode::InvalidFormat,"Cannot invalidate original resume data");
}
std::string serialize(const EncodedDocument& d){auto out=d.serialize();require(out.has_value(),ErrorCode::InvalidFormat,"Cannot encode original state");return std::move(*out);}
const Node* cacheEntry(const EncodedDocument* cache,const core::TorrentHash& hash,bool unique=false){
  if(!cache){return nullptr;}const auto* rows=cache->document().find("state");if(!rows){return nullptr;}require(rows->type==Node::Type::List,ErrorCode::InvalidFormat,"Invalid state cache list");const Node* found=nullptr;
  for(const auto& row:rows->children){const auto* key=row.find("hash");if(key&&key->type==Node::Type::Bytes&&key->bytes.size()==hash.bytes().size()&&std::equal(hash.bytes().begin(),hash.bytes().end(),reinterpret_cast<const std::byte*>(key->bytes.data()))){require(!unique||!found,ErrorCode::Conflict,"Duplicate selected state-cache identity");found=&row;}}return found;
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
  for(std::size_t i=0;i<count;++i){for(const auto kind:{OriginalStorageKind::Compact,OriginalStorageKind::Reordered}){const auto path=parent/originalControlLeaf(i,kind);paths.push_back(path);paths.push_back(fs::path(path).concat(".bak"));}}
  return paths;
}
enum class CompletionRecord { None, Cleared, Pending, Malformed };
CompletionRecord completionRecord(const std::optional<std::string>& raw){
  if(!raw){return CompletionRecord::None;}constexpr std::string_view key="BiglyBTC.reorderedCompletion";
  const auto d=core::Document::parse(*raw,limits(MaximumDocument));
  if(!d||d->root().type!=Node::Type::Dictionary){return raw->find(key)==std::string::npos?CompletionRecord::None:CompletionRecord::Malformed;}
  const Node* record=nullptr;for(std::size_t i=0;i<d->root().children.size();i+=2){if(d->root().children[i].bytes==key){if(record){return CompletionRecord::Malformed;}record=&d->root().children[i+1];}}
  if(!record){return CompletionRecord::None;}if(record->type!=Node::Type::Dictionary){return CompletionRecord::Malformed;}
  return record->children.empty()?CompletionRecord::Cleared:CompletionRecord::Pending;
}
void validateCompletionControls(const std::vector<Control>& controls,std::span<const OriginalFileGeometry> geometry){
  if(controls.empty()){return;}require(controls.size()==geometry.size()*4,ErrorCode::InvalidFormat,"Original control geometry inventory differs");
  for(std::size_t i=0;i<geometry.size();++i){for(std::size_t family=0;family<2;++family){const auto& primary=controls[i*4+family*2];const auto& backup=controls[i*4+family*2+1];
    const auto a=completionRecord(primary.before.bytes),b=completionRecord(backup.before.bytes);
    require(a!=CompletionRecord::Pending&&a!=CompletionRecord::Malformed,ErrorCode::Conflict,"Pending or malformed reordered completion must settle before file operation");
    if(b==CompletionRecord::Pending||b==CompletionRecord::Malformed){const auto kind=family?OriginalStorageKind::Reordered:OriginalStorageKind::Compact;
      require(primary.before.bytes&&OriginalFileIo::validateControl(geometry[i],kind,*primary.before.bytes).has_value(),ErrorCode::Conflict,"Authoritative backup reordered completion must settle before file operation");}
  }}
}
void captureControlGeometry(Plan& p,const compat::OriginalLayout& layout){
  for(const auto& file:layout.files){require(layout.pieceLength>0&&layout.pieceLength<=UINT32_MAX&&file.offset>=0&&file.bytes>=0,ErrorCode::InvalidFormat,"Invalid control geometry");p.controlGeometry.push_back({static_cast<std::uint64_t>(file.offset),static_cast<std::uint64_t>(file.bytes),static_cast<std::uint32_t>(layout.pieceLength)});}
}
void checkControls(const Plan& p,bool recovering=false){
  validateCompletionControls(p.controls,p.controlGeometry);std::vector<Control> current;current.reserve(p.controls.size());
  for(const auto& c:p.controls){require(!inspect(pathFromUtf8(pathToUtf8(c.path)+".saving")),ErrorCode::Conflict,"Original control recovery is pending");auto now=read(c.path,MaximumDocument);
    // An authenticated removal journal can already have removed its primary.
    // Its accepted cleared beforeimage remains the authority for stale backup
    // cleanup; move/relink never receive this exception.
    if(recovering&&selectionRetiresControl(p,c.path)&&!now.bytes){current.push_back(c);continue;}
    if(recovering&&p.preview.kind!=FileOperationKind::Relocate&&p.preview.kind!=FileOperationKind::Relink&&p.preview.kind!=FileOperationKind::SetFilePolicies&&!now.bytes){current.push_back(c);continue;}
    current.push_back({c.path,std::move(now)});
  }
  validateCompletionControls(current,p.controlGeometry);
  for(std::size_t i=0;i<current.size();++i){const auto& now=current[i].before;const auto& expected=p.controls[i].before;if(recovering&&selectionRetiresControl(p,current[i].path)&&!now.bytes){continue;}require(now.bytes==expected.bytes&&(!expected.stamp||(now.stamp&&unchanged(*now.stamp,*expected.stamp))),ErrorCode::Conflict,"Original control file changed after preparation");}
}
void payloadPath(const fs::path& path,const fs::path& profile){
  pathShape(path);require(!intersects(path,profile),ErrorCode::UnsafePath,"Payload link overlaps the held profile");
  static const fs::path git(".git"), biglybtc(".BiglyBTC.");
  for(const auto& part:path){
    const auto prefix=fs::path(part.native().substr(0,biglybtc.native().size()));
    require(!detail::sameFilesystemComponent(part,git)&&!detail::sameFilesystemComponent(prefix,biglybtc),
        ErrorCode::UnsafePath,"Payload link overlaps a reserved control path");
  }
}
fs::path relocatedPath(const core::SavedDownload& saved,const compat::OriginalLayout& layout,std::size_t index,const fs::path& source,const fs::path& parent,std::string_view suffix){
  const auto saveParent=pathFromUtf8(saved.saveDirectory),root=saveParent/pathFromUtf8(*saved.saveFile);
  if(within(source,root)||(!layout.multifile&&source.parent_path()==saveParent)){return parent/relativeWithin(source,saveParent);}
  // External authenticated links become normal metadata paths at the selected
  // destination. Never reproduce an absolute external path using '..'.
  auto result=parent/(layout.multifile?pathFromUtf8(*saved.saveFile)/pathFromUtf8(layout.files[index].path):pathFromUtf8(*saved.saveFile));if(!suffix.empty()&&pathToUtf8(source.filename()).ends_with(suffix)){result+=pathFromUtf8(suffix);}return result;
}
std::vector<fs::path> mapping(const core::SavedDownload& saved,const compat::OriginalLayout& layout,const Node& attrs){
  require(!saved.legacySavePath&&saved.saveFile&&pathFromUtf8(*saved.saveFile).filename()==pathFromUtf8(*saved.saveFile)&&!saved.saveFile->empty(),ErrorCode::Unsupported,"Legacy save-root mapping needs its qualified adapter");
  // Deprecated filelinks is opaque: the pinned original reads filelinks2 only.
  for(const auto* key:{"dnd_pfx","dnd_sf","dnd_al"}){if(const auto* n=attrs.find(key)){require(n->type==Node::Type::Bytes&&n->bytes.size()<=4096,ErrorCode::InvalidFormat,"Invalid original DND policy field");}}
  const auto& stores=field(attrs,"storetypes",Node::Type::List);require(stores.children.size()==layout.files.size(),ErrorCode::InvalidFormat,"Storage types do not match metadata");std::vector<fs::path> out;out.reserve(layout.files.size());
  for(std::size_t i=0;i<layout.files.size();++i){static_cast<void>(storageKind(attrs,i,layout.files.size()));out.push_back(pathFromUtf8(saved.saveDirectory)/(layout.multifile?pathFromUtf8(*saved.saveFile)/pathFromUtf8(layout.files[i].path):pathFromUtf8(*saved.saveFile)));pathShape(out.back());}
  std::vector<std::string> originalLinkBases; originalLinkBases.reserve(out.size());
  for (const auto& path:out) { originalLinkBases.push_back(pathToUtf8(path)); }
  const auto links=compat::resolveOriginalFileLinks(attrs.find("filelinks2"),originalLinkBases);
  if(!links){fail(links.error().code==compat::OriginalFileLinksErrorCode::Memory?ErrorCode::Io:
      links.error().code==compat::OriginalFileLinksErrorCode::AmbiguousBase?ErrorCode::UnsafePath:ErrorCode::InvalidFormat,
      links.error().message.data());}
  for(std::size_t i=0;i<out.size();++i){if(links->byIndex[i]){const fs::path target=pathFromUtf8(links->byIndex[i]->target);pathShape(target);out[i]=target;}}
  return out;
}
}
namespace {
bool selecting(FileOperationKind kind){return kind==FileOperationKind::SetFilePolicies;}
bool fileMapping(FileOperationKind kind){return kind==FileOperationKind::MoveFile||kind==FileOperationKind::RetargetFile||kind==FileOperationKind::RevertFile||kind==FileOperationKind::ReplaceFile;}
bool replacementRootFor(const fs::path& root,const fs::path& parent,std::string_view leaf){
  if(root.parent_path()!=parent){return false;}const auto name=pathToUtf8(root.filename());if(name==leaf){return true;}
  const auto prefix=std::string(leaf)+".retry.";if(!name.starts_with(prefix)){return false;}const auto suffix=std::string_view(name).substr(prefix.size());unsigned n=0;
  const auto parsed=std::from_chars(suffix.data(),suffix.data()+suffix.size(),n);return parsed.ec==std::errc{}&&parsed.ptr==suffix.data()+suffix.size()&&n>=1&&n<=1024&&std::to_string(n)==suffix;
}
std::optional<FileStamp> namedEntry(const fs::path& path){
  const auto parent=directory(path.parent_path(),false,true);
  if(parent.get()==detail::InvalidStateHandle){return {};}
  return expectedValue(detail::stateStampAt(parent.get(),path.filename()));
}
bool directoryExists(const fs::path& path){return namedEntry(path).has_value();}
bool retargeting(FileOperationKind kind){return kind==FileOperationKind::Relink||kind==FileOperationKind::RetargetFile;}
bool recycling(FileOperationKind kind){return kind==FileOperationKind::RemoveRecycleData;}
bool deleting(FileOperationKind kind){return kind==FileOperationKind::RemoveDeleteData||recycling(kind);}
bool relocating(FileOperationKind kind){return kind==FileOperationKind::Relocate||selecting(kind)||kind==FileOperationKind::MoveFile||kind==FileOperationKind::RevertFile||kind==FileOperationKind::ReplaceFile||recycling(kind);}

std::vector<compat::FilePolicy> selectionPolicies(const Node& row,const compat::OriginalLayout& layout,bool validate=true){
  auto values=compat::originalFilePolicy(row,layout.files.size());
  require(values.has_value(),ErrorCode::InvalidFormat,"Invalid original file selection");
  std::set<std::int32_t> levels;
  for(std::size_t i=0;i<values->size();++i){const auto& value=(*values)[i];
    require(!validate||((!layout.files[i].padding||value.skipped)&&value.priority!=std::numeric_limits<std::int32_t>::min()),ErrorCode::Unsupported,"Unsupported selected padding or original priority");
    if(!value.skipped&&!layout.files[i].padding){levels.insert(value.priority);}
  }
  require(!validate||levels.size()<=7,ErrorCode::Unsupported,"More than seven original file priority ranks");
  return std::move(*values);
}

constexpr std::string_view SelectionIntentKey="BiglyBTC.selection.intent.v1";
constexpr std::string_view SelectionChangedIntentKey="BiglyBTC.selection.intent.v2";
struct SelectionStorageIntent {
  compat::OriginalFileSelectionIntent intent;
  std::vector<std::int32_t> requested,desired;
  std::vector<std::uint8_t> converted;
};
SelectionStorageIntent selectionStorageIntent(const Node& node,const compat::OriginalLayout& layout,
    const Node& attributes,const Node& requested,const Node& savedRow,const std::vector<compat::FilePolicy>& before,
    const std::vector<compat::FilePolicy>& after,bool changedPolicy=false){
  auto read=compat::readOriginalFileSelectionIntent(node,layout.files.size());
  require(read.has_value(),ErrorCode::InvalidFormat,"Invalid explicit storage-selection intent");
  require(!read->skipped&&changedPolicy!=std::ranges::equal(before,after),ErrorCode::InvalidFormat,"Storage intent version differs from its policy-change role");
  require(requested.type==Node::Type::List&&requested.children.size()==layout.files.size(),ErrorCode::InvalidFormat,"Storage intent lacks complete requested kinds");
  for(const auto* key:{"dnd_pfx","dnd_sf","dnd_al"}){if(const auto* n=attributes.find(key)){require(n->type==Node::Type::Bytes&&n->bytes.empty(),ErrorCode::Unsupported,"This storage selection requires a DND relocation owner");}}
  std::vector<OriginalStorageKind> constructorKinds;for(const auto& n:requested.children){const auto kind=text(&n);require(kind=="L"||kind=="C"||kind=="R"||kind=="X",ErrorCode::InvalidFormat,"Unknown requested kind for current policy");constructorKinds.push_back(kind=="L"?OriginalStorageKind::Linear:kind=="C"?OriginalStorageKind::Compact:kind=="R"?OriginalStorageKind::Reordered:OriginalStorageKind::ReorderedCompact);}
  const auto canonical=expectedValue(readOriginalFilePolicies(savedRow,constructorKinds));
  SelectionStorageIntent result;result.intent=std::move(*read);result.converted.resize(layout.files.size());
  for(std::size_t i=0;i<layout.files.size();++i){const auto kind=text(&requested.children[i]);
    require(kind=="L"||kind=="C"||kind=="R"||kind=="X",ErrorCode::InvalidFormat,"Unknown explicit requested storage kind");
    result.requested.push_back(kind=="L"?1:kind=="C"?2:kind=="R"?3:4);
    if(result.intent.changed[i]){require(!layout.files[i].padding&&!after[i].skipped,ErrorCode::InvalidFormat,"Storage intent includes unselected or padding files");}
    else{const bool captured=(kind=="C"||kind=="X")&&!before[i].skipped&&after[i].skipped&&before[i].priority==0&&after[i].priority==0&&canonical.restored[i]==after[i];require(before[i]==after[i]||captured,ErrorCode::InvalidFormat,"Storage intent changes an unselected file policy");}
  }
  auto plan=compat::originalSkipStoragePlan(result.requested,result.intent.changed,false);
  require(plan&& !plan->empty(),ErrorCode::InvalidFormat,"Storage intent requires an actual C/X unskip request");
  result.desired=result.requested;
  for(const auto& step:*plan){for(std::size_t i=0;i<step.files.size();++i){if(step.files[i]){
    const auto& file=layout.files[i];require(layout.pieceLength>0&&file.offset>=0&&file.bytes>=0
        ,ErrorCode::Unsupported,"Unsupported selection storage geometry");
    result.desired[i]=step.target;result.converted[i]=1;
  }}}
  return result;
}
std::string selectionStorageTypes(const SelectionStorageIntent& intent){
  std::string value="l";for(const auto kind:intent.desired){value+=kind==1?"1:L":kind==2?"1:C":kind==3?"1:R":"1:X";}return value+'e';
}
std::string selectionLinks(const core::SavedDownload& saved,const compat::OriginalLayout& layout,
    const std::vector<fs::path>& paths,const std::vector<Payload>& changes){
  std::string links="l";
  for(std::size_t i=0;i<layout.files.size();++i){if(layout.files[i].padding){continue;}
    const auto base=pathFromUtf8(saved.saveDirectory)/(layout.multifile?pathFromUtf8(*saved.saveFile)/pathFromUtf8(layout.files[i].path):pathFromUtf8(*saved.saveFile));
    auto target=paths[i];const auto moved=std::lower_bound(changes.begin(),changes.end(),i,[](const Payload& file,std::size_t index){return file.view.index<index;});
    if(moved!=changes.end()&&moved->view.index==i){target=moved->view.destination;}
    if(target!=base){links+=b(std::to_string(i)+'\n'+pathToUtf8(base)+'\n'+pathToUtf8(target));}
  }
  return links+'e';
}
void validateSelectionStorageBoundary(const Plan& p,const SelectionStorageIntent& intent,
    const core::SavedDownload& saved,const compat::OriginalLayout& layout,const Node& row,const std::vector<fs::path>& paths,std::string_view suffix){
  require(p.postSkip||p.files.empty(),ErrorCode::Unsupported,"Legacy identity selection cannot move payload files");
  const auto expectedControls=controlPaths(p.profile,p.preview.hash,layout.files.size());
  require(p.controls.size()==expectedControls.size(),ErrorCode::InvalidFormat,"Storage intent lacks its complete control inventory");
  for(std::size_t i=0;i<expectedControls.size();++i){require(p.controls[i].path==expectedControls[i],ErrorCode::InvalidFormat,"Storage intent control index differs");}
  std::set<fs::path,detail::FilesystemPathLess> prohibited;
  const auto parent=p.profile/"active"/p.preview.hash.hex();
  for(std::size_t i=0;i<intent.converted.size();++i){if(!intent.converted[i]){continue;}
    for(const auto kind:{OriginalStorageKind::Compact,OriginalStorageKind::Reordered}){
      const auto path = parent / originalControlLeaf(i, kind);
      prohibited.insert(path);
      prohibited.insert(fs::path(path).concat(".bak"));
    }
    auto source=paths[i];auto current=inspect(source);if(p.postSkip){const auto original=std::ranges::find(p.files,i,[](const Payload& f){return f.view.index;});require(original!=p.files.end(),ErrorCode::InvalidFormat,"Missing selected file census");source=original->view.source;current=original->source;}
    const auto base=pathFromUtf8(saved.saveDirectory)/(layout.multifile?pathFromUtf8(*saved.saveFile)/pathFromUtf8(layout.files[i].path):pathFromUtf8(*saved.saveFile));
    if(!current&&source==base&&!suffix.empty()){source+=suffix;current=inspect(source);}
    if(current){ownedPayload(*current,layout.files[i].bytes);require(layout.files[i].bytes<=layout.pieceLength-layout.files[i].offset%layout.pieceLength,ErrorCode::Unsupported,"Present selected data requires the packed conversion owner");}
    else if(const auto* allocated=row.find("allocated")){
      const auto proof=std::ranges::find(p.files,i,[](const Payload& f){return f.view.index;});require(static_cast<std::uint32_t>(integer(allocated))!=1||(p.postSkip&&proof!=p.files.end()&&proof->selectionAllocate),ErrorCode::Unsupported,"Previously allocated selected data is missing; selection cannot recreate it");
    }
  }
  for(const auto& control:p.controls){require(!prohibited.contains(control.path)||!control.before.bytes,ErrorCode::Unsupported,"This file selection requires recovery of existing storage controls");}
}

bool selectionUtf8(std::string_view value){
  for(std::size_t i=0;i<value.size();){
    const auto head=static_cast<unsigned char>(value[i++]);if(head<128){continue;}
    unsigned more=0;std::uint32_t scalar=0,minimum=0;
    if(head>=0xc2&&head<=0xdf){more=1;scalar=head&31;minimum=0x80;}
    else if(head>=0xe0&&head<=0xef){more=2;scalar=head&15;minimum=0x800;}
    else if(head>=0xf0&&head<=0xf4){more=3;scalar=head&7;minimum=0x10000;}else{return false;}
    if(more>value.size()-i){return false;}
    while(more--){const auto next=static_cast<unsigned char>(value[i++]);if((next&0xc0)!=0x80){return false;}scalar=(scalar<<6)|(next&63);}
    if(scalar<minimum||scalar>0x10ffff||(scalar>=0xd800&&scalar<=0xdfff)){return false;}
  }return true;
}

std::int64_t originalFileDownloaded(const Node& attrs,std::size_t index,std::int64_t bytes){
  const auto* all=attrs.find("filedownloaded");if(!all){return 0;}
  require(all->type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Malformed original downloaded-file map");
  const auto* list=all->find("downloaded");if(!list){return 0;}
  require(list->type==Node::Type::List,ErrorCode::InvalidFormat,"Malformed original downloaded-file list");
  if(index>=list->children.size()){return 0;}
  const auto amount=integer(&list->children[index]);
  require(amount>=0&&amount<=bytes,ErrorCode::InvalidFormat,"Original per-file progress exceeds geometry");return amount;
}

// The caller authenticates existing source/destination descriptors. This pure
// projection follows DiskManagerUtil.setSkippedInternalSupport: dnd_sf has
// precedence even when empty; unrelated external links are not relocated.
fs::path selectionDestination(const core::SavedDownload& saved,const compat::OriginalLayout& layout,
    const Node& attrs,std::size_t index,const fs::path& source,
    const compat::FilePolicy& before,const compat::FilePolicy& after){
  if(before.skipped==after.skipped){return source;}
  const auto base=pathFromUtf8(saved.saveDirectory)/(layout.multifile?pathFromUtf8(*saved.saveFile)/pathFromUtf8(layout.files[index].path):pathFromUtf8(*saved.saveFile));
  const auto* sub=attrs.find("dnd_sf");const auto* alt=attrs.find("dnd_al");
  if(!sub&&!alt){return source;}
  auto name=pathToUtf8(source.filename());
  if(after.skipped){
    if(source.parent_path()!=base.parent_path()){return source;}
    const bool incomplete=originalFileDownloaded(attrs,index,layout.files[index].bytes)!=layout.files[index].bytes;
    for(const auto* key:{"dnd_pfx","incompfilesuffix"}){
      if(const auto* field=attrs.find(key)){const auto value=text(field);
        require(selectionUtf8(value)&&value.find('/')==value.npos&&value.find('\0')==value.npos&&value.find('\n')==value.npos,ErrorCode::UnsafePath,"Unsafe original DND filename decoration");
        if(incomplete&&!(std::string_view(key)=="dnd_pfx"?name.starts_with(value):name.ends_with(value))){
          name=std::string_view(key)=="dnd_pfx"?value+name:name+value;
        }
      }
    }
  }
  if(sub){
    const auto directoryName=text(sub);
    require(selectionUtf8(directoryName)&&directoryName!="."&&directoryName!=".."&&pathFromUtf8(directoryName).filename()==pathFromUtf8(directoryName)
        &&directoryName.find('\0')==directoryName.npos&&directoryName.find('\n')==directoryName.npos,
        ErrorCode::UnsafePath,"Original DND subfolder must be a safe leaf");
    if(after.skipped){return base.parent_path()/pathFromUtf8(directoryName)/pathFromUtf8(name);}
    // Original unskip restores the parent and keeps the current name. Prefix
    // removal belongs to actual completion, not to the selection command.
    return source.parent_path().filename()==pathFromUtf8(directoryName)?source.parent_path().parent_path()/pathFromUtf8(name):source;
  }
  const auto alternateText=text(alt);require(selectionUtf8(alternateText),ErrorCode::UnsafePath,"Original DND path is not UTF-8");
  const fs::path alternate=pathFromUtf8(alternateText);pathShape(alternate);
  const auto root=pathFromUtf8(saved.saveDirectory)/pathFromUtf8(*saved.saveFile);
  if(after.skipped){
    // Original uses String.startsWith. Component containment is deliberately
    // stricter: a sibling sharing the textual prefix never grants authority.
    require(layout.multifile,ErrorCode::Unsupported,"Alternate DND relocation needs a multifile save root");
    require(within(base.parent_path(),root),ErrorCode::UnsafePath,"Original DND base escapes save root");
    const auto relative=relativeWithin(base.parent_path(),root);
    return alternate/relative/pathFromUtf8(name);
  }
  return within(source,alternate)?base.parent_path()/name:source;
}

void selectionTargetIndependent(const fs::path& target,std::size_t index,
    const std::vector<fs::path>& paths,const core::SavedDownload& saved,
    const compat::OriginalLayout& layout,std::string_view suffix){
  for(std::size_t other=0;other<paths.size();++other){if(other==index||layout.files[other].padding){continue;}
    require(!intersects(target,paths[other]),ErrorCode::Conflict,"DND destination overlaps another selected payload path");
    const auto base=pathFromUtf8(saved.saveDirectory)/(layout.multifile?pathFromUtf8(*saved.saveFile)/pathFromUtf8(layout.files[other].path):pathFromUtf8(*saved.saveFile));
    if(paths[other]==base&&!suffix.empty()){
      require(!intersects(target,pathFromUtf8(pathToUtf8(base)+std::string(suffix))),ErrorCode::Conflict,"DND destination overlaps another incomplete filename");
    }
  }
}

bool moving(FileOperationKind kind){return kind==FileOperationKind::Relocate||kind==FileOperationKind::Relink||selecting(kind)||fileMapping(kind);}
#include "linux_trash.inc"
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
core::LibrarySnapshot parsedInventory(const core::Document& d){
  auto parsed=core::readLibrary(d);require(parsed.has_value()&&parsed->downloads.size()<=MaximumFiles,ErrorCode::InvalidFormat,"Invalid file-operation inventory");
  for(const auto& issue:parsed->issues){require(!issue.pauseEntry&&parsed->legacyMap,ErrorCode::InvalidFormat,"Malformed file-operation inventory");const auto& key=d.root().children.at(issue.sourceIndex*2);const auto& n=d.root().children.at(issue.sourceIndex*2+1);require(key.bytes=="pause_data"||(!n.find("torrent")&&!n.find("torrent_hash")&&!n.find("save_dir")&&!n.find("path")),ErrorCode::InvalidFormat,"Malformed legacy file-operation row");}
  std::set<std::string> hashes;for(const auto& row:parsed->downloads){if(row.hash){require(hashes.insert(row.hash->hex()).second,ErrorCode::Conflict,"Duplicate file-operation inventory hash");}}
  return std::move(*parsed);
}
const Node& inventoryRow(const core::Document& d,const core::SavedDownload& saved){return saved.legacyKey?d.root().children.at(saved.sourceIndex*2+1):d.find("downloads")->children.at(saved.sourceIndex);}
std::string inventorySelection(const core::Document& d,const core::TorrentHash& hash){
  const auto parsed=parsedInventory(d);std::string result="l";for(const auto& row:parsed.downloads){if(row.hash&&*row.hash==hash){result+=encoded(inventoryRow(d,row));}}return result+'e';
}
void editInventory(core::Document& d,const core::TorrentHash& hash,std::optional<std::string_view> replacement={}){
  const auto parsed=parsedInventory(d);if(!parsed.legacyMap){put(d,"downloads",withoutSelected(d.find("downloads"),hash,false,replacement));return;}
  std::optional<std::size_t> selected;for(const auto& row:parsed.downloads){if(row.hash&&*row.hash==hash){selected=row.sourceIndex;}}
  require(selected.has_value()||!replacement,ErrorCode::Conflict,"Legacy target disappeared before edit");std::string result="d";
  for(std::size_t i=0;i<d.root().children.size()/2;++i){if(selected&&i==*selected){if(replacement){result+=encoded(d.root().children[i*2])+std::string(*replacement);}}else{result+=encoded(d.root().children[i*2])+encoded(d.root().children[i*2+1]);}}
  d=parse(result+'e');
}
void restoreStorageProjection(core::Document& attrs,const Plan& p,const compat::OriginalLayout& layout){
  const auto scope=parse('d'+p.scopeSelection+'e',MaximumJournal);const auto* actual=scope.find("BiglyBTC.resolved.storetypes"),*requested=scope.find("BiglyBTC.requested.storetypes");
  if(!actual&&!requested){return;}require(actual&&requested&&actual->type==Node::Type::List&&requested->type==Node::Type::List&&actual->children.size()==layout.files.size()&&requested->children.size()==layout.files.size(),ErrorCode::InvalidFormat,"Invalid captured storage projection");
  const auto* original=attrs.find("storetypes");require(!original||original->type==Node::Type::List,ErrorCode::InvalidFormat,"Invalid captured original stores");
  const bool defaults=!original||original->children.empty();if(!defaults){require(encoded(*original)==encoded(*requested),ErrorCode::InvalidFormat,"Captured requested stores differ from original");}
  std::map<fs::path,std::pair<std::size_t,bool>> expectedMarkers;const auto parent=p.profile/"active"/p.preview.hash.hex();
  for(std::size_t i=0;i<layout.files.size();++i){for(const bool r:{false,true}){const auto leaf=originalControlLeaf(i,r?OriginalStorageKind::Reordered:OriginalStorageKind::Compact);expectedMarkers.emplace(parent/leaf,std::pair{i,r});expectedMarkers.emplace(parent/(leaf+".bak"),std::pair{i,r});}}
  std::vector<std::pair<bool,bool>> presence(layout.files.size());for(const auto& control:p.controls){const auto found=expectedMarkers.find(control.path);require(found!=expectedMarkers.end(),ErrorCode::InvalidFormat,"Captured control is not an original indexed leaf");if(control.before.bytes){auto& flags=presence[found->second.first];(found->second.second?flags.second:flags.first)=true;}}
  for(std::size_t i=0;i<layout.files.size();++i){const auto wanted=text(&requested->children[i]),physical=text(&actual->children[i]);require(wanted=="L"||wanted=="C"||wanted=="R"||wanted=="X",ErrorCode::InvalidFormat,"Invalid captured requested storage kind");if(defaults){require(wanted=="L"||wanted=="R",ErrorCode::InvalidFormat,"Original defaults cannot invent compact policy");}
    const auto [c,r]=presence[i];require(!(c&&r),ErrorCode::InvalidFormat,"Captured control families conflict");const auto expected=c?"C":r?(wanted=="R"?"R":"X"):wanted;require(physical==expected,ErrorCode::InvalidFormat,"Captured physical stores differ from control inventory");
  }
  put(attrs,"storetypes",encoded(*actual));
}
void checkRemovalGuards(const Plan& p){
  for(const auto& c:p.removalGuards){check(c.path,c.before,MaximumDocument);}
  if(p.ownedRemovalJournal){check(p.profile/JournalName,*p.ownedRemovalJournal,MaximumJournal);}
}
bool journalIsKeepData(const Raw& raw){
  require(raw.bytes.has_value(),ErrorCode::Missing,"Removal journal is absent");
  const auto outer=parse(*raw.bytes,MaximumJournal);require(integer(outer.find("version"))==1,ErrorCode::Unsupported,"Unknown operation journal");
  const auto body=text(outer.find("payload"));require(digest(body)==text(outer.find("sha256")),ErrorCode::InvalidFormat,"Operation journal checksum mismatch");
  const auto value=parse(body,MaximumJournal);return integer(value.find("kind"))==static_cast<int>(FileOperationKind::RemoveKeepData);
}
fs::path temporaryPath(const Plan&,const Payload&);
#include "file_selection_effects.inc"
std::vector<compat::FilePolicy> selectionMovementBefore(std::span<const compat::FilePolicy> before,std::span<const compat::FilePolicy> after,const Node* mask){
  std::vector<compat::FilePolicy> result(before.begin(),before.end());if(mask){auto selected=compat::readOriginalFileSelectionIntent(*mask,before.size());require(selected.has_value(),ErrorCode::InvalidFormat,"Invalid explicit selection movement mask");for(std::size_t n=0;n<before.size();++n){if(!selected->changed[n]){result[n]=after[n];}else{require(after[n].skipped==selected->skipped,ErrorCode::InvalidFormat,"Selection mask differs from requested flags");}}}return result;
}
Plan prepare(const ProfileLease& lease,const FileOperationRequest& request,std::stop_token stop,bool storageSnapshot=false){
  checkpoint(stop);Plan p;
  if(request.kind==FileOperationKind::RemoveKeepData){p.preparedLease=expectedValue(lease.retain());p.preparedWorker=std::this_thread::get_id();}
  p.profile=pathFromUtf8(profilePath(lease));p.preview.kind=request.kind;p.preview.hash=request.hash;const auto profile=heldFileStamp(lease.directoryHandle());p.profileDevice=profile.device;p.profileInode=profile.inode;
  require(request.kind>=FileOperationKind::RemoveKeepData&&request.kind<=FileOperationKind::ReplaceFile&&(request.hash.bytes().size()==20||request.hash.bytes().size()==32),ErrorCode::InvalidFormat,"Invalid file-operation request");
  require(storageSnapshot||(!inspect(p.profile/JournalName)&&!inspect(p.profile/(std::string(JournalName)+".BiglyBTC.fileop-saving"))),ErrorCode::Conflict,"File operation recovery is required before preparing another operation");
  require(!inspect(p.profile/"BiglyBTC.torrent-import"),ErrorCode::Conflict,"Torrent import recovery is pending");
  const auto inventoryRaw=read(p.profile/"downloads.config",MaximumDocument);require(inventoryRaw.bytes.has_value(),ErrorCode::Missing,"Saved inventory is absent");auto inventory=parse(*inventoryRaw.bytes);auto library=parsedInventory(inventory);
  std::optional<core::SavedDownload> saved;for(const auto& row:library.downloads){if(row.hash&&*row.hash==request.hash){require(!saved,ErrorCode::Conflict,"Duplicate selected torrent identity");saved=row;}}
  require(saved&&saved->persistent,ErrorCode::Unsupported,"Selected original save mapping is unavailable");const auto rawSaved=*saved;
  const auto statePath=p.profile/"active"/(request.hash.hex()+".dat");const auto stateRaw=read(statePath,MaximumDocument);auto state=envelope(stateRaw);
  require(!inspect(pathFromUtf8(pathToUtf8(statePath)+".saving"))&&!inspect(pathFromUtf8(pathToUtf8(statePath)+".bak.saving")),ErrorCode::Conflict,"Active state has unresolved temporary data");
  const auto cachePath=p.profile/"active/cache.dat";const auto cacheRaw=read(cachePath,MaximumDocument);auto cache=cacheDocument(cacheRaw);hydrate(state,cacheEntry(cache?&*cache:nullptr,request.hash,request.kind==FileOperationKind::RemoveKeepData));hashMatches(state,request.hash);
  if(request.kind==FileOperationKind::RemoveKeepData){
    p.removalInfoSha256=digest(expectedValue(originalTorrentInfoSubject(state.document())));
    p.removalBeforeState=serialize(state);p.removalBeforeSaved=rawSaved;
    auto active=directory(p.profile/"active");const auto activeStamp=heldFileStamp(active.get());p.removalActiveDirectory={static_cast<std::uint64_t>(activeStamp.device),static_cast<std::uint64_t>(activeStamp.inode)};
    require(!hasPendingCategoryDeletion(lease),ErrorCode::Conflict,"Category recovery prevents Keep Data removal");
    // A cache backup is an original recovery candidate, never a substitute for
    // an absent primary during a removal. Preserve all foreign/sibling rows.
    const auto backup=read(p.profile/"active/cache.dat.bak",MaximumDocument);
    require(cacheRaw.bytes||!backup.bytes,ErrorCode::Conflict,"State cache recovery requires review before removal");
    for(const auto& path:{p.profile/"downloads.config.saving",p.profile/"downloads.config.bak.saving",p.profile/"active/cache.dat.saving",p.profile/"active/cache.dat.bak.saving",pathFromUtf8(pathToUtf8(statePath)+".saving"),pathFromUtf8(pathToUtf8(statePath)+".bak.saving"),p.profile/"BiglyBTC.torrent-import"}){
      auto raw=read(path,MaximumDocument);require(!raw.bytes,ErrorCode::Conflict,"Profile recovery candidate prevents removal");p.removalGuards.push_back({path,std::move(raw)});
    }
    // Filename/storage defaults are inputs too; no callback or SDK wait occurs
    // while Host holds the source turn around the final reprepare and apply.
    for(const auto* name:{"biglybt.config","biglybt.config.bak","biglybt.config.saving"}){const auto path=p.profile/name;auto raw=read(path,MaximumDocument);if(std::string_view(name)=="biglybt.config.saving"){require(!raw.bytes,ErrorCode::Conflict,"Settings recovery prevents removal");}p.removalGuards.push_back({path,std::move(raw)});}
    const fs::path reference(rawSaved.torrentFile);
    if(!request.deleteTorrentFile&&reference!=statePath&&reference!=pathFromUtf8(pathToUtf8(statePath)+".bak")){
      for(const auto& path:{reference,pathFromUtf8(pathToUtf8(reference)+".bak")}){
        auto raw=read(path,MaximumDocument);if(raw.bytes){auto archive=envelope(raw);hashMatches(archive,request.hash);require(expectedValue(originalTorrentInfoSubject(archive.document()))==expectedValue(originalTorrentInfoSubject(state.document())),ErrorCode::Conflict,"Referenced archive differs from retained full info");}
        p.removalGuards.push_back({path,std::move(raw)});
      }
    }
  }
  const auto filenameContext=expectedValue(readOriginalFilenameContext(lease));p.filenameConversions=filenameContext.conversions;p.decoderShowAll=filenameContext.decoderShowAll;auto layout=compat::originalLayout(state.document(),MaximumFiles,*p.filenameConversions,*p.decoderShowAll);require(layout.has_value(),ErrorCode::Unsupported,"Unsupported original payload layout");auto attrs=section(state.document().find("attributes"));const auto originalAttributes=attrs;
  if(request.kind==FileOperationKind::RemoveKeepData){if(const auto* networks=attrs.find("networks")){require(networks->type==Node::Type::List&&networks->children.size()==1&&networks->children[0].type==Node::Type::Bytes&&networks->children[0].bytes=="Public",ErrorCode::Unsupported,"Keep Data removal requires an exact Public saved network");}}
  auto root=resolveOriginalSaveRoot(*saved,*layout);require(root.has_value(),root?ErrorCode::Io:root.error().code,root?"Original root resolution":root.error().message.c_str());saved=std::move(*root);
  auto saveParent=pathFromUtf8(saved->saveDirectory);pathShape(saveParent);p.preview.sourceRoot=saveParent/pathFromUtf8(*saved->saveFile);pathShape(p.preview.sourceRoot);require(!intersects(p.preview.sourceRoot,p.profile),ErrorCode::UnsafePath,"Payload may not contain or reside inside the held profile");
  auto defaults=resolveOriginalStorageTypes(lease,request.hash,*layout,attrs.root());require(defaults.has_value(),defaults?ErrorCode::Io:defaults.error().code,defaults?"Storage default resolution":defaults.error().message.c_str());
  auto wanted=attrs;require(applyOriginalStorageDefaults(wanted,*defaults).has_value(),ErrorCode::InvalidFormat,"Cannot capture requested storage defaults");
  require(applyOriginalStorageDefaults(attrs,*defaults,true).has_value(),ErrorCode::InvalidFormat,"Cannot capture physical storage defaults");auto paths=mapping(*saved,*layout,attrs.root());
  const auto* completion=attrs.find("BiglyBTC.completion");if(completion){require(completion->type==Node::Type::Dictionary&&completion->children.empty(),ErrorCode::Conflict,"Completion rename recovery must finish before file operation");}
  if(const auto* conversion=attrs.find("BiglyBTC.compactConversion")){require(conversion->type==Node::Type::List&&(storageSnapshot||conversion->children.empty()),ErrorCode::Conflict,"Compact conversion recovery must finish before file operation");
    if(storageSnapshot){std::set<std::size_t> indices;require(conversion->children.size()<=layout->files.size(),ErrorCode::InvalidFormat,"Excess compact intents");
      for(const auto& item:conversion->children){const auto index=integer(item.find("index"));require(item.type==Node::Type::Dictionary&&index>=0&&static_cast<std::size_t>(index)<layout->files.size()&&indices.insert(static_cast<std::size_t>(index)).second&&integer(item.find("device"))>=0&&integer(item.find("inode"))>0,ErrorCode::InvalidFormat,"Malformed compact snapshot intent");}}
}
  if(deleting(request.kind)){if(const auto* flags=attrs.find("flags")){require(flags->type==Node::Type::Integer,ErrorCode::InvalidFormat,"Malformed original deletion-protection flags");require((flags->integer&0x40)==0,ErrorCode::Unsupported,"Original torrent flags prohibit deleting payload on removal");}}
  const bool oneFile=fileMapping(request.kind);
  require(oneFile==request.fileIndex.has_value(),ErrorCode::InvalidFormat,"Exactly one original file index is required for a file mapping");
  require((request.kind==FileOperationKind::MoveFile||request.kind==FileOperationKind::RetargetFile||request.kind==FileOperationKind::ReplaceFile)==request.fileDestination.has_value(),ErrorCode::InvalidFormat,"Invalid per-file destination");
  require(recycling(request.kind)==request.trashRoot.has_value(),ErrorCode::InvalidFormat,"Recycle requests require an explicit owned trash root");
  if(oneFile){require(*request.fileIndex<layout->files.size()&&!layout->files[*request.fileIndex].padding,ErrorCode::InvalidFormat,"Selected original file index is missing or padding");}
  if(oneFile){const auto& row=inventoryRow(inventory,*saved);require(!row.find("BiglyBTC.verifiedSeeding")&&!row.find("BiglyBTC.verifiedExistingScope"),ErrorCode::Unsupported,"Per-file mapping cannot replace an authenticated verified-existing scope");}
  const bool fileSelection=selecting(request.kind);
  require(fileSelection||!request.selectionIntentEncoding,ErrorCode::InvalidFormat,"Storage-selection intent belongs only to file selection");
  require(fileSelection==request.selectionPolicyEncoding.has_value()&&fileSelection==request.expectedSelectionEntry.has_value(),ErrorCode::InvalidFormat,"Selection encoding/source belong only to file-selection operations");
  std::vector<compat::FilePolicy> previousPolicy,nextPolicy,movementBefore;
  std::optional<SelectionStorageIntent> explicitIntent;
  auto selectedWanted=wanted;
  if(fileSelection){
    require(!request.destinationParent&&request.selectionPolicyEncoding->size()<=MaximumDocument,ErrorCode::InvalidFormat,"Invalid file-selection request");
    require(encoded(inventoryRow(inventory,*saved))==*request.expectedSelectionEntry,ErrorCode::Conflict,"Selected original file policy changed during preparation");
    auto row=parse(encoded(inventoryRow(inventory,*saved)));previousPolicy=selectionPolicies(row.root(),*layout,false);
    put(row,"file_priorities_c",*request.selectionPolicyEncoding);nextPolicy=selectionPolicies(row.root(),*layout);std::optional<core::Document> movementMask;if(request.postSkipIntentEncoding){movementMask=parse(*request.postSkipIntentEncoding);}movementBefore=selectionMovementBefore(previousPolicy,nextPolicy,movementMask?&movementMask->root():nullptr);
    if(request.selectionIntentEncoding){
      const auto input=parse(*request.selectionIntentEncoding,MaximumDocument);
      explicitIntent=selectionStorageIntent(input.root(),*layout,attrs.root(),*wanted.find("storetypes"),inventoryRow(inventory,*saved),previousPolicy,nextPolicy,!std::ranges::equal(previousPolicy,nextPolicy));
      put(selectedWanted,"storetypes",selectionStorageTypes(*explicitIntent));
    }
    p.destinationParent=saveParent;auto parent=directory(saveParent);const auto target=heldFileStamp(parent.get());require(target.owned,ErrorCode::UnsafePath,"Selection save parent must be owned");
    p.targetDevice=target.device;p.targetInode=target.inode;p.preview.targetRoot=p.preview.sourceRoot;p.preview.requiresRecheck=true;
  }else if(oneFile){
    require(!request.destinationParent,ErrorCode::InvalidFormat,"Per-file operation does not accept a parent-root move");
    const auto index=*request.fileIndex;
    const auto canonical=saveParent/(layout->multifile?pathFromUtf8(*saved->saveFile)/pathFromUtf8(layout->files[index].path):pathFromUtf8(*saved->saveFile));
    const auto target=request.kind==FileOperationKind::RevertFile?canonical:*request.fileDestination;payloadPath(target,p.profile);
    require(target!=paths[index],ErrorCode::Conflict,"File is already at that mapping");
    for(std::size_t n=0;n<paths.size();++n){if(n!=index&&!layout->files[n].padding){require(!intersects(target,paths[n]),ErrorCode::Conflict,"File destination overlaps another original file");}}
    p.destinationParent=target.parent_path();auto parent=directory(p.destinationParent,false,true);while(parent.get()==detail::InvalidStateHandle){const auto ancestor=p.destinationParent.parent_path();require(ancestor!=p.destinationParent&&!ancestor.empty(),ErrorCode::UnsafePath,"Per-file destination has no existing ancestor");p.destinationParent=ancestor;parent=directory(p.destinationParent,false,true);}const auto id=heldFileStamp(parent.get());require(id.owned,ErrorCode::UnsafePath,"Per-file destination parent must be owned");
    p.targetDevice=id.device;p.targetInode=id.inode;p.preview.targetRoot=p.preview.sourceRoot;p.preview.requiresRecheck=true;
  }else if(recycling(request.kind)){
    require(!request.destinationParent,ErrorCode::InvalidFormat,"Recycle request cannot move the save root");prepareTrash(p,*request.trashRoot);
  }else if(moving(request.kind)){
    require(request.destinationParent.has_value(),ErrorCode::InvalidFormat,"Destination parent is required");p.destinationParent=*request.destinationParent;pathShape(p.destinationParent);auto destination=directory(p.destinationParent);const auto target=heldFileStamp(destination.get());require(target.owned,ErrorCode::UnsafePath,"Destination parent must be owned");p.targetDevice=target.device;p.targetInode=target.inode;
    p.preview.targetRoot=p.destinationParent/pathFromUtf8(*saved->saveFile);pathShape(p.preview.targetRoot);require(!intersects(p.preview.sourceRoot,p.preview.targetRoot)&&!intersects(p.preview.targetRoot,p.profile),ErrorCode::Conflict,"Source, destination and profile paths overlap");
    if(request.kind==FileOperationKind::Relocate){require(!expectedValue(detail::stateStampAt(destination.get(),pathFromUtf8(*saved->saveFile))),ErrorCode::Conflict,"Destination name already exists; relocation never merges or overwrites");}
    p.preview.requiresRecheck=true;
  }else{require(!request.destinationParent,ErrorCode::InvalidFormat,"Removal does not accept a destination");}
  if(storageSnapshot){require(layout->pieceLength>0&&layout->bytes>=0,ErrorCode::InvalidFormat,"Storage snapshot piece geometry is invalid");p.storageSource.pieceCount=static_cast<std::uint64_t>(layout->bytes/layout->pieceLength+(layout->bytes%layout->pieceLength!=0));require(p.storageSource.pieceCount<=1000000,ErrorCode::Unsupported,"Storage selection exceeds the managed piece limit");p.storageSource.profileDevice=p.profileDevice;p.storageSource.profileInode=p.profileInode;
    p.storageSource.row=encoded(inventoryRow(inventory,*saved));p.storageSource.requestedAttributes=wanted.serialize();
    p.storageSource.infoSha256=digest(expectedValue(originalTorrentInfoSubject(state.document())));
    p.storageSource.filenameConversions=*p.filenameConversions;p.storageSource.decoderShowAll=*p.decoderShowAll;}
  const auto suffix=attrs.find("incompfilesuffix")?text(attrs.find("incompfilesuffix")):std::string{};require(suffix.find('/')==suffix.npos&&suffix.find('\n')==suffix.npos&&suffix.size()<=200,ErrorCode::Unsupported,"Unsupported incomplete suffix");
  std::set<fs::path,detail::FilesystemPathLess> unique;std::set<std::pair<std::uint64_t,std::uint64_t>> inodes;
  for(std::size_t index=0;index<layout->files.size();++index){checkpoint(stop);if(layout->files[index].padding||(oneFile&&index!=*request.fileIndex)){continue;}Payload f;f.view.index=index;f.view.logicalBytes=layout->files[index].bytes;f.view.source=paths[index];f.source=inspect(f.view.source);
    const auto base=saveParent/(layout->multifile?pathFromUtf8(*saved->saveFile)/pathFromUtf8(layout->files[index].path):pathFromUtf8(*saved->saveFile));
    if(!f.source&&paths[index]==base&&!suffix.empty()){f.view.source+=suffix;f.source=inspect(f.view.source);}
    payloadPath(f.view.source,p.profile);
    if(f.source){auto maximum=physicalMaximum(*layout,attrs.root(),index);
      if(storageSnapshot){if(const auto* pending=attrs.find("BiglyBTC.compactConversion")){for(const auto& item:pending->children){if(static_cast<std::size_t>(integer(item.find("index")))==index){
        require(static_cast<std::uint64_t>(integer(item.find("device")))==static_cast<std::uint64_t>(f.source->device)&&static_cast<std::uint64_t>(integer(item.find("inode")))==static_cast<std::uint64_t>(f.source->inode),ErrorCode::Conflict,"Compact snapshot inode intent differs");
        require(storageKind(attrs.root(),index,layout->files.size())==OriginalStorageKind::Compact||storageKind(attrs.root(),index,layout->files.size())==OriginalStorageKind::Linear,ErrorCode::Conflict,"Compact intent physical kind differs");maximum=layout->files[index].bytes;
      }}}}
      ownedPayload(*f.source,maximum);require(inodes.emplace(f.source->device,f.source->inode).second,ErrorCode::UnsafePath,"Selected files share an inode");}
    if(storageSnapshot){p.storageSource.files.push_back({index,f.view.source,f.source,
      {static_cast<std::uint64_t>(layout->files[index].offset),static_cast<std::uint64_t>(layout->files[index].bytes),static_cast<std::uint32_t>(layout->pieceLength)},
      storageKind(wanted.root(),index,layout->files.size()),storageKind(attrs.root(),index,layout->files.size())});}
    if(fileSelection){
      f.view.destination=selectionDestination(*saved,*layout,attrs.root(),index,f.view.source,movementBefore[index],nextPolicy[index]);
      if(f.view.destination==f.view.source&&storageSnapshot){continue;}
      if(f.view.destination!=f.view.source){payloadPath(f.view.destination,p.profile);selectionTargetIndependent(f.view.destination,index,paths,*saved,*layout,suffix);
      {auto parent=directory(f.view.destination.parent_path(),false,true);if(parent.get()!=detail::InvalidStateHandle){const auto id=heldFileStamp(parent.get());require(id.owned,ErrorCode::UnsafePath,"DND destination parent must be owned");}}
      // Original skip leaves an occupied destination untouched. Do not turn
      // that optional relocation into overwrite or discard of either file.
      if(inspect(f.view.destination)){if(storageSnapshot){continue;}f.view.destination=f.view.source;}}
    }
    require(unique.insert(f.view.source).second,ErrorCode::UnsafePath,"Selected files share a path");
    if(oneFile){
      f.view.destination=request.kind==FileOperationKind::RevertFile?base:*request.fileDestination;
      if(retargeting(request.kind)){
        require(storageKind(attrs.root(),index,layout->files.size())==OriginalStorageKind::Linear,ErrorCode::Unsupported,"Retargeting existing data currently requires original linear storage");
        f.destination=inspect(f.view.destination);require(f.destination.has_value(),ErrorCode::Missing,"Retarget requires an existing original file");ownedPayload(*f.destination,physicalMaximum(*layout,attrs.root(),index));
        require(inodes.emplace(f.destination->device,f.destination->inode).second,ErrorCode::Conflict,"Retarget destination aliases the old source");
      }else if(request.kind==FileOperationKind::ReplaceFile){
        require(f.source.has_value(),ErrorCode::Missing,"Replacement requires actual source bytes");f.destination=inspect(f.view.destination);require(f.destination.has_value(),ErrorCode::Missing,"Explicit replacement requires an existing destination");ownedPayload(*f.destination,std::numeric_limits<std::int64_t>::max());
        require(inodes.emplace(f.destination->device,f.destination->inode).second,ErrorCode::Conflict,"Replacement destination aliases the source");
        const auto hash=p.payloadDigests.digest(f.view.destination,*f.destination,stop);const auto leaf=".BiglyBTC.replaced."+p.preview.hash.hex()+'.'+std::to_string(index)+'.'+digest(pathToUtf8(f.view.destination)+':'+std::to_string(f.destination->device)+':'+std::to_string(f.destination->inode)).substr(0,16);
        const auto replacementRoot=f.view.destination.parent_path()/leaf;require(!directoryExists(replacementRoot),ErrorCode::Conflict,"A prior destination preimage already occupies the reserved path");
        p.replacement=Replacement{replacementRoot,hash};p.preview.replacement=FileOperationPreimage{replacementRoot/"data",static_cast<std::uint64_t>(f.destination->size),hash};
      }else{require(!inspect(f.view.destination),ErrorCode::Conflict,"File destination exists; moving/reverting never overwrites");}
    }else if(recycling(request.kind)){f.view.destination=trashDestination(p,f);}
    else if(moving(request.kind)&&!fileSelection){f.view.destination=relocatedPath(*saved,*layout,index,f.view.source,p.destinationParent,suffix);payloadPath(f.view.destination,p.profile);if(request.kind==FileOperationKind::Relink){f.destination=inspect(f.view.destination);if(f.destination){ownedPayload(*f.destination,physicalMaximum(*layout,attrs.root(),index));require(inodes.emplace(f.destination->device,f.destination->inode).second,ErrorCode::UnsafePath,"Relink target shares a selected inode");}}
      else{require(!inspect(f.view.destination),ErrorCode::Conflict,"A relocation payload destination already exists");}
      if(request.kind==FileOperationKind::Relocate&&f.source&&f.source->device!=p.targetDevice){p.preview.crossDevice=true;}}
    if(relocating(request.kind)&&f.source&&f.source->device!=p.targetDevice){p.preview.crossDevice=true;}
    if(moving(request.kind)&&f.view.destination!=f.view.source){require(unique.insert(f.view.destination).second,ErrorCode::UnsafePath,"Selected destination shares a path");}
    const auto& visible=retargeting(request.kind)?f.destination:f.source;f.view.present=visible.has_value();if(visible){f.view.device=visible->device;f.view.inode=visible->inode;f.view.actualBytes=visible->size;++p.preview.presentFiles;require(static_cast<std::uint64_t>(visible->size)<=UINT64_MAX-p.preview.presentBytes,ErrorCode::InvalidFormat,"Payload byte sum overflow");p.preview.presentBytes+=static_cast<std::uint64_t>(visible->size);}else{++p.preview.missingFiles;}
    p.files.push_back(std::move(f));
  }
  if(fileSelection&&!storageSnapshot){std::optional<core::Document> mask;if(request.postSkipIntentEncoding){mask=parse(*request.postSkipIntentEncoding);}else if(request.selectionIntentEncoding){mask=parse(*request.selectionIntentEncoding);}prepareSelectionEffects(p,*layout,state.document().root(),selectedWanted,inventoryRow(inventory,*saved),previousPolicy,nextPolicy,mask?&mask->root():nullptr);}
  if (request.kind == FileOperationKind::Relocate) {
    // DiskManagerUtil.runMoveTask schedules when data exists or either
    // original counter is nonzero, before attempting filesystem changes.
    const auto selected = core::projectSavedDndProgress(layout->bytes, rawSaved.completedBytes,
        core::readSavedDndFacts(&originalAttributes.root())).percentThousandths;
    if (p.preview.presentFiles != 0 || (rawSaved.downloaded && *rawSaved.downloaded > 0)
        || (selected && *selected > 0)) {
      p.preview.originalMoveScheduled = true;
    } else if (rawSaved.downloaded == 0 && selected == 0) {
      p.preview.originalMoveScheduled = false;
    }
  }
  if(request.kind==FileOperationKind::Relink){require(p.preview.presentFiles!=0,ErrorCode::Missing,"Relink destination has no original payload files");}
  if(request.deleteTorrentFile){p.archive=Archive{pathFromUtf8(rawSaved.torrentFile),{},false,{}};auto& archive=*p.archive;
    archive.internalState=archive.path==statePath||archive.path==pathFromUtf8(pathToUtf8(statePath)+".bak");archive.before=read(archive.path,MaximumDocument);if(!archive.internalState){archive.backup=read(pathFromUtf8(pathToUtf8(archive.path)+".bak"),MaximumDocument);}archiveShape(p,archive);
    for(const auto& file:p.files){archivePayload(p,file.view.source);}
    p.preview.torrentArchive=FileOperationArchive{archive.path,archive.before.bytes?static_cast<std::uint64_t>(archive.before.bytes->size()):0,archive.before.bytes.has_value(),archive.internalState};if(!archive.internalState){p.preview.torrentArchiveBackup=FileOperationArchive{pathFromUtf8(pathToUtf8(archive.path)+".bak"),archive.backup.bytes?static_cast<std::uint64_t>(archive.backup.bytes->size()):0,archive.backup.bytes.has_value(),false};}}
  // No accidental shared data: inspect every other saved root and supported
  // explicit physical map, including identical inode aliases.
  if(request.kind!=FileOperationKind::RemoveKeepData||p.archive){for(const auto& other:library.downloads){
    if(other.hash&&*other.hash==request.hash){continue;}archiveReference(p,other);checkpoint(stop);require(other.hash.has_value(),ErrorCode::Unsupported,"Cannot prove independence from another unresolved library mapping");
    auto otherRaw=read(p.profile/"active"/(other.hash->hex()+".dat"),MaximumDocument);auto otherState=envelope(otherRaw);hydrate(otherState,cacheEntry(cache?&*cache:nullptr,*other.hash));hashMatches(otherState,*other.hash);auto otherLayout=readOriginalLayout(lease,otherState.document(),MaximumFiles);require(otherLayout.has_value(),ErrorCode::Unsupported,"Another library payload map is unsupported");auto otherAttrs=section(otherState.document().find("attributes"));auto otherRoot=resolveOriginalSaveRoot(other,*otherLayout);require(otherRoot.has_value(),ErrorCode::Unsupported,"Another original save root is unresolved");const fs::path absoluteRoot=pathFromUtf8(otherRoot->saveDirectory)/pathFromUtf8(*otherRoot->saveFile);require(!intersects(absoluteRoot,p.preview.sourceRoot)&&(!(moving(request.kind)||recycling(request.kind))||!intersects(absoluteRoot,p.preview.targetRoot)),ErrorCode::Conflict,"Payload roots overlap another library torrent");if(p.trash){require(!intersects(absoluteRoot,p.trash->root),ErrorCode::Conflict,"Trash overlaps another library root");}
    auto otherDefaults=resolveOriginalStorageTypes(lease,*other.hash,*otherLayout,otherAttrs.root());require(otherDefaults.has_value()&&applyOriginalStorageDefaults(otherAttrs,*otherDefaults,true).has_value(),ErrorCode::Unsupported,"Another original physical map is unresolved");auto otherPaths=mapping(*otherRoot,*otherLayout,otherAttrs.root());
    for(const auto& path:otherPaths){archivePayload(p,path);for(const auto& selected:p.files){require(!intersects(path,selected.view.source)&&(selected.view.destination.empty()||!intersects(path,selected.view.destination)),ErrorCode::Conflict,"Payload path is shared by another torrent");}if(auto info=inspect(path)){require(!inodes.contains({info->device,info->inode}),ErrorCode::Conflict,"Payload inode belongs to another torrent");}}
  }}
  if(request.kind!=FileOperationKind::RemoveKeepData){for(const auto& row:library.downloads){const fs::path archive=pathFromUtf8(row.torrentFile);pathShape(archive);for(const auto& f:p.files){require(f.view.source!=archive&&(f.view.destination.empty()||f.view.destination!=archive),ErrorCode::UnsafePath,"Payload operation aliases a retained torrent archive");}}}
  if(storageSnapshot){std::set<fs::path,detail::FilesystemPathLess> snapshotPaths,archives;std::set<std::pair<std::uint64_t,std::uint64_t>> archiveInodes;
    for(const auto& row:library.downloads){const fs::path archive=pathFromUtf8(row.torrentFile);for(const auto& candidate:{archive,pathFromUtf8(pathToUtf8(archive)+".bak")}){
      pathShape(candidate);if(archives.insert(candidate).second){if(const auto stamp=inspect(candidate)){archiveInodes.emplace(stamp->device,stamp->inode);}}
    }}
    for(const auto& file:p.storageSource.files){require(snapshotPaths.insert(file.path).second,ErrorCode::UnsafePath,"Storage snapshot paths alias");
      const auto descendant=archives.lower_bound(file.path);require(descendant==archives.end()||!within(*descendant,file.path),ErrorCode::UnsafePath,"Storage snapshot contains retained torrent metadata");
      for(auto ancestor=file.path;!ancestor.empty();ancestor=ancestor.parent_path()){require(!archives.contains(ancestor),ErrorCode::UnsafePath,"Storage snapshot aliases retained torrent metadata");if(ancestor==ancestor.parent_path()){break;}}
      require(!file.stamp||!archiveInodes.contains({file.stamp->device,file.stamp->inode}),ErrorCode::UnsafePath,"Storage snapshot inode aliases retained torrent metadata");
    }
    require(p.files.empty(),ErrorCode::Unsupported,"Storage snapshot cannot authorize a DND move");
  }
  const auto& selectedRow=inventoryRow(inventory,*saved);auto updatedRow=parse(encoded(selectedRow));std::string selection;
  for(const auto* key:{"file_priorities","file_priorities_c"}){if(const auto* priorities=selectedRow.find(key)){selection+=b(key)+encoded(*priorities);}}for(const auto* key:{"storetypes","filelinks2","incompfilesuffix","dnd_pfx","dnd_sf","dnd_al","flags"}){if(const auto* n=attrs.find(key)){selection+=b(key)+encoded(*n);}}
  // Include priority/skip and actual path maps in the stable scope, while
  // excluding force/startup/pause/counters changed by legitimate retirement.
  selection+=b("BiglyBTC.requested.storetypes")+encoded(*wanted.find("storetypes"));
  selection+=b("BiglyBTC.resolved.storetypes")+encoded(*attrs.find("storetypes"));
  if(fileSelection){selection+=b("BiglyBTC.selection.policy")+b(*request.selectionPolicyEncoding);}
  if(request.selectionIntentEncoding){selection+=b(std::ranges::equal(previousPolicy,nextPolicy)?SelectionIntentKey:SelectionChangedIntentKey)+*request.selectionIntentEncoding;}
  if(request.postSkipIntentEncoding){require(fileSelection,ErrorCode::InvalidFormat,"Post-skip intent outside selection");selection+=b("BiglyBTC.postSkip.intent")+*request.postSkipIntentEncoding;}
  p.scopeSelection=selection;
  if(cache){const auto* entries=cache->document().find("state");if(entries){require(cache->replaceValue("state",withoutSelected(entries,request.hash,false,std::nullopt,"hash")).has_value(),ErrorCode::InvalidFormat,"Cannot remove selected state cache entry");const auto after=serialize(*cache);if(after!=*cacheRaw.bytes){p.edits.push_back({cachePath,cacheRaw,after,FileOperationStage::CachePublished});}}}
  if(request.kind==FileOperationKind::RemoveKeepData){
    if(std::ranges::none_of(p.edits,[&](const Edit& e){return e.path==cachePath;})){p.removalGuards.push_back({cachePath,cacheRaw});}
    const auto path=p.profile/"active/cache.dat.bak";auto raw=read(path,MaximumDocument);auto backup=cacheDocument(raw);
    if(backup){static_cast<void>(cacheEntry(&*backup,request.hash,true));if(const auto* rows=backup->document().find("state")){require(backup->replaceValue("state",withoutSelected(rows,request.hash,false,std::nullopt,"hash")).has_value(),ErrorCode::InvalidFormat,"Cannot remove selected backup-cache entry");}const auto after=serialize(*backup);if(after!=*raw.bytes){p.edits.push_back({path,raw,after,FileOperationStage::CachePublished});}else{p.removalGuards.push_back({path,std::move(raw)});}}
    else{p.removalGuards.push_back({path,std::move(raw)});}
    const auto inventoryBackup=p.profile/"downloads.config.bak";auto before=read(inventoryBackup,MaximumDocument);
    if(before.bytes){auto backupInventory=parse(*before.bytes);static_cast<void>(parsedInventory(backupInventory));editInventory(backupInventory,request.hash);if(const auto* pauses=backupInventory.find("pause_data")){put(backupInventory,"pause_data",withoutSelected(pauses,request.hash,true));}const auto after=backupInventory.serialize();if(after!=*before.bytes){p.edits.push_back({inventoryBackup,before,after,FileOperationStage::InventoryPublished});}else{p.removalGuards.push_back({inventoryBackup,std::move(before)});}}
    else{p.removalGuards.push_back({inventoryBackup,std::move(before)});}
  }
  if(fileSelection){
    put(updatedRow,"file_priorities_c",*request.selectionPolicyEncoding);
    // Existing roots/links remain the inputs for the durable selection transaction.
    const auto links=selectionLinks(*saved,*layout,paths,p.files);
    put(attrs,"storetypes",encoded(*selectedWanted.find("storetypes")));if(const auto* receipts=selectedWanted.find(SelectionAbsentKey)){put(attrs,SelectionAbsentKey,encoded(*receipts));}if(defaults->reorderMinimumMiB){put(attrs,"reordermb",number(*defaults->reorderMinimumMiB));}
    put(attrs,"filelinks2",links);markRecheck(state,std::move(attrs));const auto newState=serialize(state);
    const auto backup=pathFromUtf8(pathToUtf8(statePath)+".bak");p.edits.push_back({backup,read(backup,MaximumDocument),newState,FileOperationStage::StatePublished});p.edits.push_back({statePath,stateRaw,newState,FileOperationStage::StatePublished});
    editInventory(inventory,request.hash,updatedRow.serialize());
  }else if(oneFile){
    const auto& changed=p.files.front();std::string links="l";
    for(std::size_t n=0;n<layout->files.size();++n){if(layout->files[n].padding){continue;}
      const auto base=saveParent/(layout->multifile?pathFromUtf8(*saved->saveFile)/pathFromUtf8(layout->files[n].path):pathFromUtf8(*saved->saveFile));
      const auto target=n==changed.view.index?changed.view.destination:paths[n];
      if(target!=base){links+=b(std::to_string(n)+'\n'+pathToUtf8(base)+'\n'+pathToUtf8(target));}
    }
    put(attrs,"storetypes",encoded(*wanted.find("storetypes")));if(defaults->reorderMinimumMiB){put(attrs,"reordermb",number(*defaults->reorderMinimumMiB));}
    put(attrs,"filelinks2",links+'e');markRecheck(state,std::move(attrs));const auto newState=serialize(state);
    const auto backup=pathFromUtf8(pathToUtf8(statePath)+".bak");p.edits.push_back({backup,read(backup,MaximumDocument),newState,FileOperationStage::StatePublished});p.edits.push_back({statePath,stateRaw,newState,FileOperationStage::StatePublished});
    // Save root, original row, pause/force/queue and all unrelated fields stay exact.
    editInventory(inventory,request.hash,updatedRow.serialize());
  }else if(moving(request.kind)){
    put(updatedRow,"save_dir",b(pathToUtf8(p.destinationParent)));put(updatedRow,"save_file",b(*saved->saveFile));std::string links="l";
    for(const auto& f:p.files){const auto& original=layout->files[f.view.index];const auto base=p.destinationParent/(layout->multifile?pathFromUtf8(*saved->saveFile)/pathFromUtf8(original.path):pathFromUtf8(*saved->saveFile));if(base!=f.view.destination){links+=b(std::to_string(f.view.index)+'\n'+pathToUtf8(base)+'\n'+pathToUtf8(f.view.destination));}}
    put(attrs,"storetypes",encoded(*wanted.find("storetypes")));if(defaults->reorderMinimumMiB){put(attrs,"reordermb",number(*defaults->reorderMinimumMiB));}
    put(attrs,"filelinks2",links+'e');put(attrs,"canosavedir",b(pathToUtf8(p.preview.targetRoot)+'\n'+pathToUtf8(p.preview.targetRoot)));markRecheck(state,std::move(attrs));const auto newState=serialize(state);
    const auto backup=pathFromUtf8(pathToUtf8(statePath)+".bak");p.edits.push_back({backup,read(backup,MaximumDocument),newState,FileOperationStage::StatePublished});p.edits.push_back({statePath,stateRaw,newState,FileOperationStage::StatePublished});
    editInventory(inventory,request.hash,updatedRow.serialize());
  }else{
    editInventory(inventory,request.hash);if(const auto* pauses=inventory.find("pause_data")){put(inventory,"pause_data",withoutSelected(pauses,request.hash,true));}
  }
  p.edits.push_back({p.profile/"downloads.config",inventoryRaw,inventory.serialize(),FileOperationStage::InventoryPublished});
  if(!moving(request.kind)){p.edits.push_back({statePath,stateRaw,std::nullopt,FileOperationStage::StateRemoved});const auto backup=pathFromUtf8(pathToUtf8(statePath)+".bak");auto raw=read(backup,MaximumDocument);if(raw.bytes){if(request.kind==FileOperationKind::RemoveKeepData){auto prior=envelope(raw);hashMatches(prior,request.hash);require(expectedValue(originalTorrentInfoSubject(prior.document()))==expectedValue(originalTorrentInfoSubject(state.document())),ErrorCode::Conflict,"State backup differs from selected full info");}p.edits.push_back({backup,std::move(raw),std::nullopt,FileOperationStage::StateRemoved});}else if(request.kind==FileOperationKind::RemoveKeepData){p.removalGuards.push_back({backup,std::move(raw)});}}
  for(const auto& path:controlPaths(p.profile,request.hash,layout->files.size())){require(!inspect(pathFromUtf8(pathToUtf8(path)+".saving")),ErrorCode::Conflict,"Original control recovery is pending");p.controls.push_back({path,read(path,MaximumDocument)});if(request.kind==FileOperationKind::RemoveKeepData){const auto saving=pathFromUtf8(pathToUtf8(path)+".saving");p.removalGuards.push_back({saving,read(saving,MaximumDocument)});}}
  if(explicitIntent){validateSelectionStorageBoundary(p,*explicitIntent,*saved,*layout,inventoryRow(inventory,*saved),paths,suffix);}
  captureControlGeometry(p,*layout);validateCompletionControls(p.controls,p.controlGeometry);
  auto checkedDefaults=resolveOriginalStorageTypes(lease,request.hash,*layout,originalAttributes.root());require(checkedDefaults&&*checkedDefaults==*defaults,ErrorCode::Conflict,"Storage controls/defaults changed during file-operation preparation");
  auto checkedRoot=resolveOriginalSaveRoot(rawSaved,*layout);require(checkedRoot&&checkedRoot->saveDirectory==saved->saveDirectory&&checkedRoot->saveFile==saved->saveFile,ErrorCode::Conflict,"Original root changed during file-operation preparation");
  require(expectedValue(readOriginalFilenameContext(lease))==filenameContext,ErrorCode::Conflict,"Original filename context changed during preparation");
  if(storageSnapshot){for(const auto& c:p.controls){p.storageSource.controls.push_back({c.path,c.before});}}
  const auto material=canonical(p,true);p.preview.approvalScopeDigest=digest(material);p.preview.executionCasDigest=digest(canonical(p,false));
  for(const auto& f:p.files){p.preview.files.push_back(f.view);}checkpoint(stop);return p;
}
}
namespace {
fs::path temporaryPath(const Plan& p,const Payload& f){return f.view.destination.parent_path()/(".BiglyBTC.fileop."+p.preview.executionCasDigest.substr(0,16)+'.'+std::to_string(f.view.index));}
void targetIdentity(const Plan& p){if(!moving(p.preview.kind)&&!recycling(p.preview.kind)){return;}auto fd=directory(p.destinationParent);const auto current=heldFileStamp(fd.get());require(current.owned&&current.device==p.targetDevice&&current.inode==p.targetInode,ErrorCode::Conflict,"Destination parent identity changed");}
void readyDestination(Plan& p,Payload& f){
  if(!f.source){return;}targetIdentity(p);auto present=inspect(f.view.destination);
  if(present){const bool ours=identity(*present,*f.source)||(f.temporary&&identity(*present,*f.temporary));require(ours&&present->size==f.source->size&&p.payloadDigests.digest(f.view.destination,*present)==f.sha256,ErrorCode::Conflict,"Destination appeared or changed during relocation");return;}
  auto source=inspect(f.view.source);require(source&&identity(*source,*f.source)&&p.payloadDigests.digest(f.view.source,*f.source)==f.sha256,ErrorCode::Conflict,"Relocation source changed");auto from=directory(f.view.source.parent_path());auto to=directory(f.view.destination.parent_path(),true);if(selecting(p.preview.kind)){const auto parent=heldFileStamp(to.get());require(parent.owned,ErrorCode::UnsafePath,"DND destination parent must remain owned");}
  if(tryLinkPayload(from.get(),f.view.source.filename(),*f.source,to.get(),f.view.destination.filename())){return;}
  const auto temporary=temporaryPath(p,f);const auto prior=inspect(temporary);
  require(!prior||(f.temporary&&identity(*prior,*f.temporary)&&prior->links==1),
      ErrorCode::Conflict,"An unrecorded relocation temporary requires explicit review");
  auto output=openCopyScratch(to.get(),temporary.filename(),prior);
  if(!prior){
    f.temporary=heldFileStamp(output.get());sync(to.get());saveJournal(p);
  }
  copyPayload(from.get(),f.view.source.filename(),*f.source,output.get());
  output={};
  const auto scratch=inspect(temporary);
  require(scratch&&f.temporary&&identity(*scratch,*f.temporary)&&p.payloadDigests.digest(temporary,*scratch)==f.sha256
      &&p.payloadDigests.digest(f.view.source,*f.source)==f.sha256,ErrorCode::Conflict,"Relocation copy/source validation failed");
  f.temporary=*scratch;saveJournal(p);
  renamePayload(to.get(),temporary.filename(),*scratch,to.get(),f.view.destination.filename());
}
#include "file_replacement.inc"

std::optional<std::string> mergedDocument(const Plan& p,const Edit& edit,const Raw& current){
  if(!current.bytes){require(!edit.before.bytes,ErrorCode::Conflict,"Original document disappeared");return edit.after;}
  if(current.bytes==edit.after){return current.bytes;}
  if(edit.path==p.profile/"downloads.config"){
    require(edit.before.bytes&&edit.after,ErrorCode::InvalidFormat,"Invalid inventory journal");auto before=parse(*edit.before.bytes),after=parse(*edit.after),now=parse(*current.bytes);
    const auto old=inventorySelection(before,p.preview.hash),next=inventorySelection(after,p.preview.hash),actual=inventorySelection(now,p.preview.hash);
    require(actual==old||actual==next,ErrorCode::Conflict,"Selected inventory row changed during recovery");
    if(actual!=next){std::optional<std::string> replacement;if(next!="le"){auto parsed=core::Document::parse(next,limits());require(parsed&&parsed->root().children.size()==1,ErrorCode::InvalidFormat,"Invalid updated target row");replacement=encoded(parsed->root().children.front());}editInventory(now,p.preview.hash,replacement?std::optional<std::string_view>(*replacement):std::nullopt);}
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
  acquisitionAvailable(lease);
  if(p.decoderShowAll){const auto current=expectedValue(readOriginalFilenameContext(lease));require(p.filenameConversions&&current.conversions==*p.filenameConversions&&current.decoderShowAll==*p.decoderShowAll,ErrorCode::Conflict,"Filename context changed before file-operation effects");}
  profileIdentity(lease,p);targetIdentity(p);checkControls(p,recovered);const auto notify=[&](FileOperationStage stage){if(observer){observer(stage);}acquisitionAvailable(lease);};
  if(p.preview.kind==FileOperationKind::RemoveKeepData){require(!recovered&&p.preparedLease&&p.ownedRemovalJournal,ErrorCode::Conflict,"Interrupted Keep Data removal requires explicit review");checkRemovalGuards(p);}
  if(p.archive&&!p.archive->internalState){checkArchive(*p.archive,recovered);}
  if(recycling(p.preview.kind)){acquisitionAvailable(lease);ensureTrash(p);}
  if(p.replacement){preserveDestination(lease,p,observer);}
  if(relocating(p.preview.kind)){for(auto& f:p.files){if(p.postSkip&&!effectMoves(f)){continue;}acquisitionAvailable(lease);readyDestination(p,f);if(f.source){notify(FileOperationStage::DestinationPublished);}}}
  if(retargeting(p.preview.kind)){for(const auto& f:p.files){const auto now=inspect(f.view.destination);require(bool(now)==bool(f.destination)&&(!now||(identity(*now,*f.destination)&&p.payloadDigests.digest(f.view.destination,*f.destination)==f.sha256)),ErrorCode::Conflict,"Relink target changed after confirmation");}}
  if(recycling(p.preview.kind)){acquisitionAvailable(lease);publishTrashInfo(p);notify(FileOperationStage::TrashInfoPublished);}
  for(const auto& e:p.edits){if(e.after){acquisitionAvailable(lease);if(p.preview.kind==FileOperationKind::RemoveKeepData){checkRemovalGuards(p);publish(e.path,e.before,*e.after,MaximumDocument);auto committed=read(e.path,MaximumDocument);require(committed.bytes==e.after,ErrorCode::Conflict,"Fresh removal publication changed");p.removalPublished.push_back({e.path,std::move(committed)});}else{publishEdit(p,e);}notify(e.stage);}}
  executeSelectionEffects(p,notify);
  FileOperationResult result{p.preview.kind,p.preview.hash,0,0,recovered};
  if(relocating(p.preview.kind)||p.preview.kind==FileOperationKind::RemoveDeleteData){for(const auto& f:p.files){if(!f.source||(p.postSkip&&!effectMoves(f))){continue;}if(fileMapping(p.preview.kind)){targetIdentity(p);const auto destination=inspect(f.view.destination);require(destination&&(identity(*destination,*f.source)||(f.temporary&&identity(*destination,*f.temporary)))&&p.payloadDigests.digest(f.view.destination,*destination)==f.sha256,ErrorCode::Conflict,"Per-file destination changed before source removal");if(p.replacement){validatePreimage(p);}}if(recycling(p.preview.kind)){ensureTrash(p);const auto destination=inspect(f.view.destination);require(destination&&(identity(*destination,*f.source)||(f.temporary&&identity(*destination,*f.temporary)))&&p.payloadDigests.digest(f.view.destination,*destination)==f.sha256,ErrorCode::Conflict,"Trash data changed before source removal");const auto info=read(p.trash->root/"info"/(pathToUtf8(f.view.destination.filename())+".trashinfo"),16384);require(info.bytes&&*info.bytes==trashInfo(p,f),ErrorCode::Conflict,"Trash information changed before source removal");}const auto now=inspect(f.view.source);if(now){require(identity(*now,*f.source)&&p.payloadDigests.digest(f.view.source,*f.source)==f.sha256,ErrorCode::Conflict,"Source changed before final removal");acquisitionAvailable(lease);erase(f.view.source,*now);}if(recycling(p.preview.kind)){++result.filesRecycled;}else if(relocating(p.preview.kind)){++result.filesMoved;}else{++result.filesDeleted;}notify(FileOperationStage::SourceRemoved);}}
  for(const auto& e:p.edits){if(!e.after){acquisitionAvailable(lease);if(p.preview.kind==FileOperationKind::RemoveKeepData){checkRemovalGuards(p);check(e.path,e.before,MaximumDocument);}const auto current=read(e.path,MaximumDocument);if(current.bytes){require(current.bytes==e.before.bytes&&current.stamp&&e.before.stamp&&unchanged(*current.stamp,*e.before.stamp),ErrorCode::Conflict,"Active cleanup source changed");erase(e.path,*current.stamp);}if(p.preview.kind==FileOperationKind::RemoveKeepData){auto removed=read(e.path,MaximumDocument);require(!removed.bytes,ErrorCode::Conflict,"Fresh removed state reappeared");p.removalPublished.push_back({e.path,std::move(removed)});}notify(FileOperationStage::StateRemoved);}}
  if(!moving(p.preview.kind)){for(const auto& c:p.controls){acquisitionAvailable(lease);if(p.preview.kind==FileOperationKind::RemoveKeepData){checkRemovalGuards(p);check(c.path,c.before,MaximumDocument);}const auto current=read(c.path,MaximumDocument);if(current.bytes){require(current.bytes==c.before.bytes&&current.stamp&&c.before.stamp&&unchanged(*current.stamp,*c.before.stamp),ErrorCode::Conflict,"Original control changed before removal");erase(c.path,*current.stamp);}notify(FileOperationStage::StateRemoved);}}
  if(p.archive){const auto& a=*p.archive;if(!a.internalState){const auto remove=[&](const fs::path& path,const Raw& before){const auto current=read(path,MaximumDocument);if(current.bytes){check(path,before,MaximumDocument);acquisitionAvailable(lease);erase(path,*current.stamp);result.torrentArchiveDeleted=true;notify(FileOperationStage::ArchiveFileRemoved);}};remove(a.path,a.before);remove(pathFromUtf8(pathToUtf8(a.path)+".bak"),a.backup);}notify(FileOperationStage::ArchiveRemoved);}
  if(p.replacement){validatePreimage(p);result.retainedPreimage=p.replacement->root/"data";}
  if(p.preview.kind==FileOperationKind::RemoveKeepData){checkRemovalGuards(p);for(const auto& committed:p.removalPublished){check(committed.path,committed.before,MaximumDocument);}}
  acquisitionAvailable(lease);const auto raw=read(p.profile/JournalName,MaximumJournal);require(raw.stamp.has_value(),ErrorCode::Conflict,"File-operation journal disappeared");erase(p.profile/JournalName,*raw.stamp);if(p.preview.kind==FileOperationKind::RemoveKeepData){p.removalEventMillis=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();p.removalEventEnd=std::chrono::steady_clock::now()+std::chrono::seconds(30);}
  notify(FileOperationStage::Completed);return result;
}
}
namespace {
core::TorrentHash decodeHash(std::string_view hex){require(hex.size()==40||hex.size()==64,ErrorCode::InvalidFormat,"Invalid journal torrent identity");std::string raw;raw.reserve(hex.size()/2);for(std::size_t i=0;i<hex.size();i+=2){unsigned value=0;const auto parsed=std::from_chars(hex.data()+i,hex.data()+i+2,value,16);require(parsed.ec==std::errc{}&&parsed.ptr==hex.data()+i+2,ErrorCode::InvalidFormat,"Invalid journal hash digits");raw+=static_cast<char>(value);}auto key=core::TorrentHash::parse(raw);require(key.has_value(),ErrorCode::InvalidFormat,"Invalid journal hash");return *key;}
bool digestShape(std::string_view value){return value.size()==64&&std::ranges::all_of(value,[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');});}
void validateCurrentIndependence(const ProfileLease& lease,const Plan& p){
  if(p.preview.kind==FileOperationKind::RemoveKeepData&&!p.archive){return;}
  const auto raw=read(p.profile/"downloads.config",MaximumDocument);require(raw.bytes.has_value(),ErrorCode::Conflict,"Recovery inventory disappeared");auto doc=parse(*raw.bytes);auto library=parsedInventory(doc);auto cache=cacheDocument(read(p.profile/"active/cache.dat",MaximumDocument));
  for(const auto& other:library.downloads){if(other.hash&&*other.hash==p.preview.hash){continue;}archiveReference(p,other);
    require(other.hash.has_value(),ErrorCode::Unsupported,"Another recovery payload mapping is unresolved");
    auto state=envelope(read(p.profile/"active"/(other.hash->hex()+".dat"),MaximumDocument));hydrate(state,cacheEntry(cache?&*cache:nullptr,*other.hash));hashMatches(state,*other.hash);auto layout=readOriginalLayout(lease,state.document(),MaximumFiles);require(layout.has_value(),ErrorCode::Unsupported,"Another recovery file layout is unsupported");auto attrs=section(state.document().find("attributes"));auto root=resolveOriginalSaveRoot(other,*layout);require(root.has_value(),ErrorCode::Unsupported,"Another recovery save root is unresolved");const auto absolute=pathFromUtf8(root->saveDirectory)/pathFromUtf8(*root->saveFile);require(!intersects(absolute,p.preview.sourceRoot)&&(p.preview.targetRoot.empty()||!intersects(absolute,p.preview.targetRoot)),ErrorCode::Conflict,"Another torrent now overlaps operation scope");if(p.trash){require(!intersects(absolute,p.trash->root),ErrorCode::Conflict,"Recovery trash overlaps another library root");}auto defaults=resolveOriginalStorageTypes(lease,*other.hash,*layout,attrs.root());require(defaults.has_value()&&applyOriginalStorageDefaults(attrs,*defaults,true).has_value(),ErrorCode::Unsupported,"Another recovery physical map is unresolved");auto paths=mapping(*root,*layout,attrs.root());
    for(const auto& path:paths){archivePayload(p,path);const auto actual=inspect(path);for(const auto& f:p.files){require(!intersects(path,f.view.source)&&(f.view.destination.empty()||!intersects(path,f.view.destination)),ErrorCode::Conflict,"Another torrent now shares a selected path");if(actual){require((!f.source||!identity(*actual,*f.source))&&(!f.destination||!identity(*actual,*f.destination))&&(!f.temporary||!identity(*actual,*f.temporary)),ErrorCode::Conflict,"Another torrent now shares selected inode");}}}
  }
}
Plan decode(const Raw& journalRaw,const fs::path& profile){
  {
    const auto parent=directory(profile);const auto file=expectedValue(detail::openStateFile(parent.get(),JournalName));
    require(journalRaw.bytes&&journalRaw.stamp&&journalRaw.stamp->links==1&&privateEntry(file.get())
        &&unchanged(heldFileStamp(file.get()),*journalRaw.stamp),ErrorCode::UnsafePath,"Recovery journal must be owned private single-link data");
  }
  auto outer=parse(*journalRaw.bytes,MaximumJournal);require(integer(outer.find("version"))==1,ErrorCode::Unsupported,"Unsupported file-operation journal version");const auto bytes=text(outer.find("payload"));require(digest(bytes)==text(outer.find("sha256")),ErrorCode::InvalidFormat,"Recovery journal checksum mismatch");auto d=parse(bytes,MaximumJournal);Plan p;
  const auto kind=integer(d.find("kind"));require(kind>=0&&kind<=9,ErrorCode::InvalidFormat,"Invalid journal operation");p.preview.kind=static_cast<FileOperationKind>(kind);p.preview.hash=decodeHash(text(d.find("hash")));p.profile=pathFromUtf8(text(d.find("profile")));pathShape(p.profile);p.preview.sourceRoot=pathFromUtf8(text(d.find("source")));pathShape(p.preview.sourceRoot);p.preview.targetRoot=pathFromUtf8(text(d.find("target")));p.destinationParent=pathFromUtf8(text(d.find("parent")));p.targetDevice=unsignedText(d.find("td"));p.targetInode=unsignedText(d.find("ti"));p.scopeSelection=text(d.find("selection"));p.profileDevice=unsignedText(d.find("pd"));p.profileInode=unsignedText(d.find("pi"));p.preview.approvalScopeDigest=text(d.find("scope"));p.preview.executionCasDigest=text(d.find("cas"));require(digestShape(p.preview.approvalScopeDigest)&&digestShape(p.preview.executionCasDigest),ErrorCode::InvalidFormat,"Invalid journal digests");
  require(!intersects(p.profile,p.preview.sourceRoot),ErrorCode::UnsafePath,"Recovery payload overlaps profile");if(moving(p.preview.kind)){pathShape(p.preview.targetRoot);pathShape(p.destinationParent);require((selecting(p.preview.kind)?p.preview.targetRoot==p.preview.sourceRoot&&p.destinationParent==p.preview.sourceRoot.parent_path():fileMapping(p.preview.kind)?p.preview.targetRoot==p.preview.sourceRoot:p.preview.targetRoot.parent_path()==p.destinationParent&&!intersects(p.preview.sourceRoot,p.preview.targetRoot))&&!intersects(p.profile,p.preview.targetRoot),ErrorCode::UnsafePath,"Recovery destination overlaps protected paths");}
  if(const auto* flag=d.find("postSkip")){require(selecting(p.preview.kind)&&integer(flag)==1,ErrorCode::InvalidFormat,"Invalid post-skip owner version");p.postSkip=true;}
  if(const auto* policy=d.find("filenameConversions")){require(policy->type==Node::Type::Bytes&&policy->bytes.size()<=16384,ErrorCode::InvalidFormat,"Invalid captured filename conversions");p.filenameConversions=std::string(policy->bytes);}
  std::size_t decoderFields=0;for(std::size_t n=0;n<d.root().children.size();n+=2){decoderFields+=d.root().children[n].bytes=="decoderShowAll";}require(decoderFields<=1,ErrorCode::InvalidFormat,"Duplicate decoder journal context");
  if(const auto* flag=d.find("decoderShowAll")){const auto value=integer(flag);require((value==0||value==1)&&p.filenameConversions.has_value(),ErrorCode::InvalidFormat,"Invalid captured decoder context");p.decoderShowAll=value==1;}
  if(recycling(p.preview.kind)){
    const auto& t=field(d.root(),"trash",Node::Type::Dictionary);require(t.children.size()==4,ErrorCode::InvalidFormat,"Unknown trash intent fields");p.trash=Trash{pathFromUtf8(text(t.find("root"))),text(t.find("date"))};validateTrashDate(p.trash->deletionDate);payloadPath(p.trash->root,p.profile);
    require(p.trash->root.filename()=="Trash"&&p.preview.targetRoot==p.trash->root/"files"&&within(p.trash->root,p.destinationParent)&&!intersects(p.trash->root,p.profile)&&!intersects(p.trash->root,p.preview.sourceRoot),ErrorCode::UnsafePath,"Recovery trash scope differs from approved paths");
    if(const auto* dirs=d.find("trashDirectories")){require(dirs->type==Node::Type::List&&dirs->children.size()==3,ErrorCode::InvalidFormat,"Invalid trash directory authority");for(std::size_t n=0;n<3;++n){require(dirs->children[n].type==Node::Type::Dictionary&&dirs->children[n].children.size()==4,ErrorCode::InvalidFormat,"Unknown trash directory authority fields");p.trash->directories[n]={unsignedText(dirs->children[n].find("dev")),unsignedText(dirs->children[n].find("ino"))};require(p.trash->directories[n].second,ErrorCode::InvalidFormat,"Missing trash directory identity");}}
  }else{require(!d.find("trash")&&!d.find("trashDirectories"),ErrorCode::InvalidFormat,"Non-recycle journal contains trash authority");}
  const auto& files=field(d.root(),"files",Node::Type::List);const auto& runtime=field(d.root(),"runtime",Node::Type::List);require(files.children.size()<=MaximumFiles&&runtime.children.size()==files.children.size(),ErrorCode::InvalidFormat,"Invalid recovery file count");std::set<std::size_t> indices;std::set<fs::path,detail::FilesystemPathLess> paths;
  for(std::size_t i=0;i<files.children.size();++i){const auto& n=files.children[i];Payload f;const auto index=integer(n.find("i"));require(index>=0&&static_cast<std::uint64_t>(index)<MaximumFiles&&indices.insert(static_cast<std::size_t>(index)).second,ErrorCode::InvalidFormat,"Duplicate recovery file index");f.view.index=static_cast<std::size_t>(index);f.view.source=pathFromUtf8(text(n.find("s")));pathShape(f.view.source);require(paths.insert(f.view.source).second,ErrorCode::InvalidFormat,"Duplicate recovery source");f.view.destination=pathFromUtf8(text(n.find("d")));f.view.logicalBytes=integer(n.find("bytes"));require(f.view.logicalBytes>=0,ErrorCode::InvalidFormat,"Invalid original file length");if(const auto* s=n.find("stamp")){f.source=readStamp(*s);require(f.source->links==1,ErrorCode::InvalidFormat,"Invalid recovery source stamp");}
    if(const auto* s=n.find("destination")){f.destination=readStamp(*s);require(f.destination->links==1,ErrorCode::InvalidFormat,"Invalid relink stamp");}
    if(moving(p.preview.kind)||recycling(p.preview.kind)){pathShape(f.view.destination);require((selecting(p.preview.kind)||within(f.view.destination,p.destinationParent))&&!intersects(f.view.destination,p.profile),ErrorCode::UnsafePath,"Recovery destination escapes selected parent");payloadPath(f.view.destination,p.profile);}
    if(p.postSkip){const auto remove=integer(n.find("remove")),allocate=integer(n.find("allocate"));require((remove==0||remove==1)&&(allocate==0||allocate==1)&&!(remove&&allocate),ErrorCode::InvalidFormat,"Invalid selection effects flags");f.selectionRemove=remove;f.selectionAllocate=allocate;if(remove||allocate){f.selectionAnchor=pathFromUtf8(text(n.find("anchor")));pathShape(f.selectionAnchor);require(within(f.view.destination,f.selectionAnchor),ErrorCode::InvalidFormat,"Selection effect ancestor escapes target");f.selectionDevice=unsignedText(n.find("anchorDevice"));f.selectionInode=unsignedText(n.find("anchorInode"));require(f.selectionInode,ErrorCode::InvalidFormat,"Missing selection ancestor identity");}}
    f.sha256=text(runtime.children[i].find("sha"));if((f.source&&p.preview.kind!=FileOperationKind::RemoveKeepData&&(!p.postSkip||effectMoves(f)||f.selectionRemove))||(f.destination&&retargeting(p.preview.kind))){require(digestShape(f.sha256),ErrorCode::InvalidFormat,"Missing recovery content digest");}if(const auto* t=runtime.children[i].find("temp")){f.temporary=readStamp(*t);require(f.temporary->links==1,ErrorCode::InvalidFormat,"Recovery scratch must have one link");}if(const auto* token=runtime.children[i].find("allocationToken")){f.allocationToken=text(token);require(p.postSkip&&f.selectionAllocate&&f.temporary&&f.allocationToken.size()==64,ErrorCode::InvalidFormat,"Invalid empty-file allocation receipt");}p.files.push_back(std::move(f));
  }
  if(p.preview.kind==FileOperationKind::ReplaceFile){
    const auto& replacement=field(d.root(),"replacement",Node::Type::Dictionary);require((replacement.children.size()==6||replacement.children.size()==8)&&p.files.size()==1&&p.files.front().destination&&p.files.front().source,ErrorCode::InvalidFormat,"Invalid replacement scope");
    p.replacement=Replacement{pathFromUtf8(text(replacement.find("root"))),text(replacement.find("sha"))};const auto& f=p.files.front();const auto leaf=".BiglyBTC.replaced."+p.preview.hash.hex()+'.'+std::to_string(f.view.index)+'.'+digest(pathToUtf8(f.view.destination)+':'+std::to_string(f.destination->device)+':'+std::to_string(f.destination->inode)).substr(0,16);
    require(replacementRootFor(p.replacement->root,f.view.destination.parent_path(),leaf)&&digestShape(p.replacement->sha256)&&integer(replacement.find("bytes"))==f.destination->size,ErrorCode::InvalidFormat,"Replacement preimage binding changed");
    if(const auto* abandoned=replacement.find("unsealed")){p.preview.unsealedPreimageDirectory=pathFromUtf8(text(abandoned));require(replacementRootFor(*p.preview.unsealedPreimageDirectory,f.view.destination.parent_path(),leaf)&&*p.preview.unsealedPreimageDirectory!=p.replacement->root,ErrorCode::InvalidFormat,"Unsealed preimage path differs from original reservation family");}
    require(replacement.children.size()==(p.preview.unsealedPreimageDirectory?8:6),ErrorCode::InvalidFormat,"Unknown replacement intent fields");
    if(const auto* id=d.find("preimageDirectory")){require(id->type==Node::Type::Dictionary&&id->children.size()==4,ErrorCode::InvalidFormat,"Invalid preimage directory authority");p.replacement->directoryDevice=unsignedText(id->find("dev"));p.replacement->directoryInode=unsignedText(id->find("ino"));require(p.replacement->directoryInode,ErrorCode::InvalidFormat,"Missing preimage directory identity");}
    p.preview.replacement=FileOperationPreimage{p.replacement->root/"data",static_cast<std::uint64_t>(f.destination->size),p.replacement->sha256};
  }else{require(!d.find("replacement")&&!d.find("preimageDirectory"),ErrorCode::InvalidFormat,"Unexpected replacement authority");}
  if(const auto* controls=d.find("controls")){require(controls->type==Node::Type::List&&controls->children.size()<=MaximumFiles*4,ErrorCode::InvalidFormat,"Invalid recovery control count");std::set<fs::path,detail::FilesystemPathLess> seen;for(const auto& n:controls->children){Control c;c.path=pathFromUtf8(text(n.find("path")));pathShape(c.path);require(seen.insert(c.path).second,ErrorCode::InvalidFormat,"Duplicate recovery control");if(const auto* before=n.find("before")){c.before.bytes=text(before);c.before.stamp=readStamp(field(n,"stamp",Node::Type::Dictionary));require(c.before.stamp->links==1&&c.before.stamp->size==static_cast<std::int64_t>(c.before.bytes->size())&&c.before.bytes->size()<=MaximumDocument,ErrorCode::InvalidFormat,"Invalid recovery control stamp");}p.controls.push_back(std::move(c));}}
  if(const auto* node=d.find("archive")){require(node->type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid archive journal");Archive a;a.path=pathFromUtf8(text(node->find("path")));const auto internal=integer(node->find("internal"));require(internal==0||internal==1,ErrorCode::InvalidFormat,"Invalid archive kind");a.internalState=internal!=0;
    if(const auto* preimage=node->find("before")){a.before.bytes=text(preimage);a.before.stamp=readStamp(field(*node,"stamp",Node::Type::Dictionary));}if(!a.internalState){const auto& backup=field(*node,"backup",Node::Type::Dictionary);if(const auto* preimage=backup.find("before")){a.backup.bytes=text(preimage);a.backup.stamp=readStamp(field(backup,"stamp",Node::Type::Dictionary));}}p.archive=std::move(a);archiveShape(p,*p.archive);}
  const auto& edits=field(d.root(),"edits",Node::Type::List);require(edits.children.size()>=2&&edits.children.size()<=5,ErrorCode::InvalidFormat,"Invalid recovery document count");std::set<fs::path,detail::FilesystemPathLess> names;const auto active=p.profile/"active"/(p.preview.hash.hex()+".dat");
  for(const auto& n:edits.children){Edit e;e.path=pathFromUtf8(text(n.find("path")));pathShape(e.path);const bool cache=e.path==p.profile/"active/cache.dat",inventory=e.path==p.profile/"downloads.config",state=e.path==active||e.path==pathFromUtf8(pathToUtf8(active)+".bak");require((cache||inventory||state)&&names.insert(e.path).second,ErrorCode::UnsafePath,"Recovery document is not an exact fixed owned path");if(const auto* before=n.find("before")){e.before.bytes=text(before);e.before.stamp=readStamp(field(n,"stamp",Node::Type::Dictionary));require(e.before.stamp->links==1,ErrorCode::InvalidFormat,"Recovery document is linked");}if(const auto* after=n.find("after")){e.after=text(after);}const auto stage=integer(n.find("stage"));e.stage=static_cast<FileOperationStage>(stage);require((cache&&e.after&&e.stage==FileOperationStage::CachePublished)||(inventory&&e.after&&e.stage==FileOperationStage::InventoryPublished)||(state&&((moving(p.preview.kind)&&e.after&&e.stage==FileOperationStage::StatePublished)||(!moving(p.preview.kind)&&!e.after&&e.stage==FileOperationStage::StateRemoved))),ErrorCode::InvalidFormat,"Invalid recovery document stage");p.edits.push_back(std::move(e));}
  require(names.contains(active)&&names.contains(p.profile/"downloads.config"),ErrorCode::InvalidFormat,"Recovery lacks target state/inventory");require(digest(canonical(p,false))==p.preview.executionCasDigest&&digest(canonical(p,true))==p.preview.approvalScopeDigest,ErrorCode::InvalidFormat,"Recovery scope/execution binding mismatch");
  const auto stateEdit=std::ranges::find(p.edits,active,&Edit::path);auto state=envelope(stateEdit->before);const auto cache=std::ranges::find(p.edits,p.profile/"active/cache.dat",&Edit::path);auto cacheDoc=cache==p.edits.end()?std::optional<EncodedDocument>{}:cacheDocument(cache->before);hydrate(state,cacheEntry(cacheDoc?&*cacheDoc:nullptr,p.preview.hash));hashMatches(state,p.preview.hash);
  const auto inventory=std::ranges::find(p.edits,p.profile/"downloads.config",&Edit::path);require(inventory->before.bytes.has_value(),ErrorCode::InvalidFormat,"Recovery original inventory missing");auto inv=parse(*inventory->before.bytes);auto library=parsedInventory(inv);std::optional<core::SavedDownload> selected;for(const auto& row:library.downloads){if(row.hash&&*row.hash==p.preview.hash){require(!selected,ErrorCode::InvalidFormat,"Duplicate recovery target");selected=row;}}
  require(selected.has_value(),ErrorCode::InvalidFormat,"Recovery original target is missing");if(p.archive){require(p.archive->path==pathFromUtf8(selected->torrentFile),ErrorCode::UnsafePath,"Recovery archive differs from the selected original record");}auto layout=p.filenameConversions?compat::originalLayout(state.document(),MaximumFiles,*p.filenameConversions,p.decoderShowAll.value_or(false)):compat::originalLayout(state.document(),MaximumFiles);require(layout.has_value(),ErrorCode::Unsupported,"Unsupported recovery original layout");auto restored=restoreOriginalSaveRoot(*selected,*layout,p.preview.sourceRoot);require(restored.has_value(),ErrorCode::InvalidFormat,"Recovery root differs from original path choices");selected=std::move(*restored);require(!moving(p.preview.kind)||(fileMapping(p.preview.kind)?p.preview.targetRoot==p.preview.sourceRoot:p.preview.targetRoot==p.destinationParent/ *selected->saveFile),ErrorCode::InvalidFormat,"Recovery destination root changed");auto attrs=section(state.document().find("attributes"));restoreStorageProjection(attrs,p,*layout);const auto map=mapping(*selected,*layout,attrs.root());const auto suffix=attrs.find("incompfilesuffix")?text(attrs.find("incompfilesuffix")):std::string{};
  std::vector<compat::FilePolicy> previousPolicy,nextPolicy,movementBefore;
  std::optional<SelectionStorageIntent> explicitIntent;
  const auto selectionScope=parse('d'+p.scopeSelection+'e',MaximumJournal);
  const auto* unchangedRequest=selectionScope.find(SelectionIntentKey);
  const auto* changedRequest=selectionScope.find(SelectionChangedIntentKey);
  require(!unchangedRequest||!changedRequest,ErrorCode::InvalidFormat,"Multiple storage intent versions are ambiguous");
  const auto* selectionRequest=unchangedRequest?unchangedRequest:changedRequest;
  require(!selectionRequest||selecting(p.preview.kind),ErrorCode::InvalidFormat,"Storage intent is outside its selection operation");
  if(selecting(p.preview.kind)){
    previousPolicy=selectionPolicies(inventoryRow(inv,*selected),*layout,false);
    const auto scope=parse('d'+p.scopeSelection+'e',MaximumJournal);const auto policy=text(scope.find("BiglyBTC.selection.policy"));
    auto changed=parse(encoded(inventoryRow(inv,*selected)));put(changed,"file_priorities_c",policy);nextPolicy=selectionPolicies(changed.root(),*layout);movementBefore=selectionMovementBefore(previousPolicy,nextPolicy,selectionScope.find("BiglyBTC.postSkip.intent"));
    if(p.postSkip){
      require(p.files.size()==static_cast<std::size_t>(std::ranges::count_if(layout->files,[](const auto& file){return !file.padding;})),ErrorCode::InvalidFormat,"Post-skip journal lacks the complete real-file census");
      auto afterAttrs=attrs;if(selectionRequest){auto read=compat::readOriginalFileSelectionIntent(*selectionRequest,layout->files.size());require(read.has_value(),ErrorCode::InvalidFormat,"Invalid selection storage mask");const auto* requested=selectionScope.find("BiglyBTC.requested.storetypes");require(requested,ErrorCode::InvalidFormat,"Missing requested storage kinds");auto intent=selectionStorageIntent(*selectionRequest,*layout,attrs.root(),*requested,inventoryRow(inv,*selected),previousPolicy,nextPolicy,changedRequest!=nullptr);put(afterAttrs,"storetypes",selectionStorageTypes(intent));}
      const auto decisions=selectionDecisions(p,*layout,state.document().root(),afterAttrs.root(),previousPolicy,nextPolicy,selectionScope.find("BiglyBTC.postSkip.intent")?selectionScope.find("BiglyBTC.postSkip.intent"):selectionRequest);
      for(const auto& f:p.files){require(f.view.index<decisions.size()&&decisions[f.view.index]==std::pair{f.selectionRemove,f.selectionAllocate},ErrorCode::InvalidFormat,"Post-skip effects differ from derived Source decisions");if(f.selectionAllocate){const auto* allocated=inventoryRow(inv,*selected).find("allocated");require(!allocated||static_cast<std::uint32_t>(integer(allocated))!=1||absentSelectionProof(attrs.root(),f.view.index,f.view.source),ErrorCode::Missing,"Missing previously allocated data lacks an owned absence receipt");}}
    }
    if(p.postSkip){auto expected=attrs;applySelectionReceipts(p,expected);require(stateEdit->after.has_value(),ErrorCode::InvalidFormat,"Missing post-skip metadata");auto published=envelope(Raw{stateEdit->after,{}});auto actual=section(published.document().find("attributes"));const auto* a=expected.find(SelectionAbsentKey),*z=actual.find(SelectionAbsentKey);require(bool(a)==bool(z)&&(!a||encoded(*a)==encoded(*z)),ErrorCode::InvalidFormat,"Selection absence postimage differs from derived actions");}
    if(selectionRequest){
      const auto* requested=selectionScope.find("BiglyBTC.requested.storetypes");require(requested,ErrorCode::InvalidFormat,"Storage intent lacks its baseline requested kinds");
      explicitIntent=selectionStorageIntent(*selectionRequest,*layout,attrs.root(),*requested,inventoryRow(inv,*selected),previousPolicy,nextPolicy,changedRequest!=nullptr);
      auto expectedAttrs=attrs;put(expectedAttrs,"storetypes",selectionStorageTypes(*explicitIntent));
      put(expectedAttrs,"filelinks2",selectionLinks(*selected,*layout,map,p.files));
      if(p.postSkip){applySelectionReceipts(p,expectedAttrs);}
      auto expectedState=state;markRecheck(expectedState,std::move(expectedAttrs));
      require(stateEdit->after.has_value(),ErrorCode::InvalidFormat,"Storage intent lacks state postimage");
      const auto published=EncodedDocument::parse(*stateEdit->after,limits());
      require(published&&published->compressed()==expectedState.compressed()
          &&published->document().serialize()==expectedState.document().serialize(),ErrorCode::InvalidFormat,"Storage-selection state postimage differs from its derived intent");
      const auto backup=std::ranges::find(p.edits,pathFromUtf8(pathToUtf8(active)+".bak"),&Edit::path);
      require(backup!=p.edits.end()&&backup->after==stateEdit->after,ErrorCode::InvalidFormat,"Storage-selection backup postimage differs");
    }
    const auto after=parse(*inventory->after);const auto afterLibrary=parsedInventory(after);const core::SavedDownload* chosen=nullptr;
    for(const auto& row:afterLibrary.downloads){if(row.hash&&*row.hash==p.preview.hash){require(!chosen,ErrorCode::InvalidFormat,"Duplicate selection recovery row");chosen=&row;}}
    require(chosen&&encoded(inventoryRow(after,*chosen))==changed.serialize(),ErrorCode::InvalidFormat,"Selection recovery changed unrelated selected-row fields");
  }
  if(deleting(p.preview.kind)){if(const auto* flags=attrs.find("flags")){require(flags->type==Node::Type::Integer&&(flags->integer&0x40)==0,ErrorCode::Unsupported,"Original flags prohibit payload deletion");}}
  for(const auto& f:p.files){archivePayload(p,f.view.source);require(f.view.index<layout->files.size()&&!layout->files[f.view.index].padding&&f.view.logicalBytes==layout->files[f.view.index].bytes,ErrorCode::InvalidFormat,"Recovery file geometry changed");payloadPath(f.view.source,p.profile);const auto expected=map[f.view.index];const auto base=pathFromUtf8(selected->saveDirectory)/(layout->multifile?pathFromUtf8(*selected->saveFile)/pathFromUtf8(layout->files[f.view.index].path):pathFromUtf8(*selected->saveFile));require(f.view.source==expected||(expected==base&&!suffix.empty()&&f.view.source==pathFromUtf8(pathToUtf8(base)+suffix)),ErrorCode::UnsafePath,"Recovery source differs from original file mapping");if(recycling(p.preview.kind)){require(f.view.destination.parent_path()==p.trash->root/"files"&&trashLeafFor(p,f),ErrorCode::UnsafePath,"Recovery trash leaf differs from approved original file");}
    if(fileMapping(p.preview.kind)){require(p.files.size()==1&&within(f.view.destination.parent_path(),p.destinationParent)&&f.view.destination!=f.view.source,ErrorCode::InvalidFormat,"Recovery per-file scope is invalid");
      if(p.preview.kind==FileOperationKind::RevertFile){require(f.view.destination==base,ErrorCode::UnsafePath,"Revert recovery must restore original canonical path");}
      for(std::size_t n=0;n<map.size();++n){if(n!=f.view.index&&!layout->files[n].padding){require(!intersects(f.view.destination,map[n]),ErrorCode::Conflict,"Recovery per-file target overlaps another file");}}
      if(retargeting(p.preview.kind)){require(f.destination&&storageKind(attrs.root(),f.view.index,layout->files.size())==OriginalStorageKind::Linear,ErrorCode::InvalidFormat,"Recovery retarget lost linear target authority");}
    }else if(moving(p.preview.kind)){require(f.view.destination==(selecting(p.preview.kind)?selectionRecoveryDestination(p,f,*selected,*layout,attrs.root(),movementBefore,nextPolicy):relocatedPath(*selected,*layout,f.view.index,f.view.source,p.destinationParent,suffix)),ErrorCode::UnsafePath,"Recovery destination differs from approved mapping");if(selecting(p.preview.kind)){selectionTargetIndependent(f.view.destination,f.view.index,map,*selected,*layout,suffix);}}}
  if(!p.controls.empty()){const auto expected=controlPaths(p.profile,p.preview.hash,layout->files.size());require(expected.size()==p.controls.size(),ErrorCode::InvalidFormat,"Recovery control inventory changed");for(std::size_t i=0;i<expected.size();++i){require(expected[i]==p.controls[i].path,ErrorCode::UnsafePath,"Recovery control is not an exact original indexed state leaf");}}else{for(std::size_t i=0;i<layout->files.size();++i){require(storageKind(attrs.root(),i,layout->files.size())==OriginalStorageKind::Linear,ErrorCode::InvalidFormat,"Legacy journal cannot omit nonlinear control inventory");}}
  if(explicitIntent){validateSelectionStorageBoundary(p,*explicitIntent,*selected,*layout,inventoryRow(inv,*selected),map,suffix);}
  captureControlGeometry(p,*layout);validateCompletionControls(p.controls,p.controlGeometry);
  for(const auto& f:p.files){const auto maximum=physicalMaximum(*layout,attrs.root(),f.view.index);if(f.source){ownedPayload(*f.source,maximum);}if(f.destination){ownedPayload(*f.destination,p.preview.kind==FileOperationKind::ReplaceFile?std::numeric_limits<std::int64_t>::max():maximum);}}
  p.journalCreated=true;return p;
}
}
namespace {
void validateLegacyNaming(const ProfileLease& lease,const Plan& p){
  if(p.decoderShowAll){const auto current=expectedValue(readOriginalFilenameContext(lease));require(p.filenameConversions&&current.conversions==*p.filenameConversions&&current.decoderShowAll==*p.decoderShowAll,ErrorCode::Conflict,"Filename context changed since file-operation acceptance");}
  if(p.filenameConversions){return;}
  const auto active=std::ranges::find(p.edits,p.profile/"active"/(p.preview.hash.hex()+".dat"),&Edit::path);require(active!=p.edits.end(),ErrorCode::InvalidFormat,"Legacy journal lacks original state");
  auto state=envelope(active->before);const auto cached=std::ranges::find(p.edits,p.profile/"active/cache.dat",&Edit::path);auto cache=cached==p.edits.end()?std::optional<EncodedDocument>{}:cacheDocument(cached->before);hydrate(state,cacheEntry(cache?&*cache:nullptr,p.preview.hash));
  const auto raw=compat::originalLayout(state.document(),MaximumFiles);const auto current=readOriginalLayout(lease,state.document(),MaximumFiles);
  require(raw&&current&&raw->name==current->name&&std::ranges::equal(raw->files,current->files,[](const auto& a,const auto& b){return a.path==b.path&&a.offset==b.offset&&a.bytes==b.bytes&&a.padding==b.padding;}),ErrorCode::Conflict,"Legacy journal predates a changed filename conversion; restore the original policy before recovery");
}
}
struct PreparedFileOperation::Impl {Plan plan;explicit Impl(Plan p):plan(std::move(p)){}};
PreparedFileOperation::PreparedFileOperation(std::unique_ptr<Impl> p):impl_(std::move(p)){}
PreparedFileOperation::PreparedFileOperation(PreparedFileOperation&&) noexcept=default;
PreparedFileOperation& PreparedFileOperation::operator=(PreparedFileOperation&&) noexcept=default;
PreparedFileOperation::~PreparedFileOperation()=default;
const FileOperationPreview& PreparedFileOperation::preview()const noexcept{return impl_->plan.preview;}
std::expected<OriginalTorrentIdentity,Error> PreparedFileOperation::historyRemovalIdentity(const ProfileLease& lease)const{
  return guarded([&]()->std::expected<OriginalTorrentIdentity,Error>{
    require(impl_&&impl_->plan.preview.kind==FileOperationKind::RemoveKeepData,ErrorCode::Conflict,"Tag capture needs an actual prepared Keep Data removal");const auto& p=impl_->plan;
    require(p.preparedLease&&p.preparedLease->sharesOwnership(lease)&&p.preparedWorker==std::this_thread::get_id(),ErrorCode::Conflict,"Tag identity needs the prepared worker/SAME OFD");
    auto access=expectedValue(lease.sourceAccess());checkRemovalGuards(p);for(const auto& edit:p.edits){check(edit.path,edit.before,MaximumDocument);}
    auto state=EncodedDocument::parse(p.removalBeforeState,limits());require(state.has_value(),ErrorCode::InvalidFormat,"Prepared removal identity source is unavailable");
    require(digest(expectedValue(originalTorrentInfoSubject(state->document())))==p.removalInfoSha256,ErrorCode::Conflict,"Prepared full info changed");
    return fullIdentity(*state,p.preview.hash);
  });
}
std::expected<detail::StorageSelectionSource,Error> detail::readStorageSelectionSource(const ProfileLease& lease,const FileOperationRequest& request){
  return guarded([&]()->std::expected<detail::StorageSelectionSource,Error>{
    require(request.kind==FileOperationKind::SetFilePolicies&&!request.selectionIntentEncoding,ErrorCode::InvalidFormat,"Storage snapshot requires a plain selection source request");
    auto p=prepare(lease,request,{},true);return std::move(p.storageSource);
  });
}
std::expected<PreparedFileOperation,Error> prepareFileOperation(const ProfileLease& lease,const FileOperationRequest& request,std::stop_token stop){return guarded([&]()->std::expected<PreparedFileOperation,Error>{require(!hasPendingOriginalCompaction(lease),ErrorCode::Conflict,"Recover pending explicit compaction before file operations");return PreparedFileOperation(std::make_unique<PreparedFileOperation::Impl>(prepare(lease,request,stop)));});}
std::expected<PreparedFileOperation,Error> prepareInterruptedReplacementReview(const ProfileLease& lease,std::stop_token stop){
  return guarded([&]()->std::expected<PreparedFileOperation,Error>{
    checkpoint(stop);require(!hasPendingOriginalCompaction(lease)&&!hasPendingTorrentIntake(lease),ErrorCode::Conflict,"Another profile recovery must finish first");
    const auto profile=pathFromUtf8(profilePath(lease));require(!inspect(profile/(std::string(JournalName)+".BiglyBTC.fileop-saving")),ErrorCode::Conflict,"Unpublished journal needs separate review");
    auto old=read(profile/JournalName,MaximumJournal);auto p=decode(old,profile);profileIdentity(lease,p);targetIdentity(p);validateLegacyNaming(lease,p);validateCurrentIndependence(lease,p);
    require(p.preview.kind==FileOperationKind::ReplaceFile&&p.replacement&&!p.replacement->directoryInode&&p.files.size()==1&&!p.archive,ErrorCode::Unsupported,"Only an unsealed untouched replacement can be replanned");
    const auto unsealed=namedEntry(p.replacement->root);require(unsealed&&unsealed->directory,ErrorCode::Conflict,"No uncertain preimage directory needs review");
    if(p.filenameConversions){require(expectedValue(readOriginalFilenameConversions(lease))==*p.filenameConversions,ErrorCode::Conflict,"Original filename policy changed");}
    // Exact before-only checks. Once any payload, control, or document effect
    // has occurred, this path is unavailable; ordinary recovery stays required.
    checkControls(p);for(const auto& e:p.edits){check(e.path,e.before,MaximumDocument);}auto& f=p.files.front();
    require(f.source&&f.destination&&!f.temporary,ErrorCode::Conflict,"Replacement source authority is incomplete");
    for(const auto& pair:{std::pair{f.view.source,*f.source},std::pair{f.view.destination,*f.destination}}){const auto now=inspect(pair.first);require(now&&unchanged(*now,pair.second),ErrorCode::Conflict,"Original replacement preimage changed");}
    require(p.payloadDigests.digest(f.view.source,*f.source,stop)==f.sha256&&p.payloadDigests.digest(f.view.destination,*f.destination,stop)==p.replacement->sha256,ErrorCode::Conflict,"Original replacement bytes changed");
    const auto abandoned=p.replacement->root;
    const auto leaf=".BiglyBTC.replaced."+p.preview.hash.hex()+'.'+std::to_string(f.view.index)+'.'+digest(pathToUtf8(f.view.destination)+':'+std::to_string(f.destination->device)+':'+std::to_string(f.destination->inode)).substr(0,16);
    fs::path fresh;for(unsigned n=1;n<=1024;++n){const auto candidate=f.view.destination.parent_path()/(leaf+".retry."+std::to_string(n));if(!directoryExists(candidate)){fresh=candidate;break;}}
    require(!fresh.empty(),ErrorCode::Conflict,"Fresh preimage reservation bound exhausted");p.replacement->root=fresh;p.preview.unsealedPreimageDirectory=abandoned;p.preview.replacement->path=fresh/"data";
    f.view.present=true;f.view.actualBytes=f.source->size;f.view.device=f.source->device;f.view.inode=f.source->inode;p.preview.files={f.view};p.preview.presentFiles=1;p.preview.presentBytes=f.source->size;p.preview.requiresRecheck=true;
    p.preview.approvalScopeDigest=digest(canonical(p,true));p.preview.executionCasDigest=digest(canonical(p,false));p.reviewedJournal=std::move(old);p.journalCreated=false;
    checkpoint(stop);return PreparedFileOperation(std::make_unique<PreparedFileOperation::Impl>(std::move(p)));
  });
}
std::expected<FileOperationConfirmation,Error> confirmFileOperation(const PreparedFileOperation& p,std::string_view accepted,bool deleteConfirmed,bool archiveConfirmed){return guarded([&]()->std::expected<FileOperationConfirmation,Error>{require(accepted==p.preview().approvalScopeDigest,ErrorCode::Conflict,"File-operation approved scope changed");require(!(deleting(p.preview().kind)||p.preview().kind==FileOperationKind::ReplaceFile)||deleteConfirmed,ErrorCode::Conflict,"Explicit payload deletion confirmation is required");require(!p.preview().torrentArchive||archiveConfirmed,ErrorCode::Conflict,"Explicit reviewed torrent archive confirmation is required");return FileOperationConfirmation{std::string(accepted),deleteConfirmed,archiveConfirmed};});}
std::expected<FileOperationResult,Error> applyFileOperation(const ProfileLease& lease,PreparedFileOperation&& prepared,const FileOperationConfirmation& confirmation,void(*observer)(FileOperationStage),std::stop_token stop,RemovalHistoryOutcome* history,CapturedRemovalTags* tags){
  if(history){*history={};}
  return guarded([&]()->std::expected<FileOperationResult,Error>{acquisitionAvailable(lease);require(!hasPendingOriginalCompaction(lease),ErrorCode::Conflict,"Recover pending explicit compaction before applying file operations");require(bool(prepared.impl_),ErrorCode::Conflict,"Prepared file operation was moved/consumed");auto& p=prepared.impl_->plan;
    // The rvalue reference does not destroy the caller's prepared object. Drop
    // every transient pinned fd on success and exception, leaving its attempted
    // state/journal/retry semantics unchanged.
    struct ClearDigests {PayloadDigestCache& cache;~ClearDigests(){cache.clear();}} clearDigests{p.payloadDigests};
    checkpoint(stop);require(!p.attempted&&confirmation.approvalScopeDigest==p.preview.approvalScopeDigest&&(!(deleting(p.preview.kind)||p.preview.kind==FileOperationKind::ReplaceFile)||confirmation.deleteConfirmed)&&(!p.archive||confirmation.archiveConfirmed),ErrorCode::Conflict,"File-operation confirmation is stale or missing");profileIdentity(lease,p);targetIdentity(p);if(p.reviewedJournal){check(p.profile/JournalName,*p.reviewedJournal,MaximumJournal);require(!inspect(p.profile/(std::string(JournalName)+".BiglyBTC.fileop-saving")),ErrorCode::Conflict,"Journal publication is uncertain");validateCurrentIndependence(lease,p);}else{require(!hasPendingFileOperation(lease),ErrorCode::Conflict,"Existing file operation needs recovery");}
    const auto currentContext=expectedValue(readOriginalFilenameContext(lease));
    if(p.filenameConversions){require(currentContext.conversions==*p.filenameConversions,ErrorCode::Conflict,"Original filename policy changed after preparation");}
    if(p.decoderShowAll){require(currentContext.decoderShowAll==*p.decoderShowAll,ErrorCode::Conflict,"Original decoder context changed after preparation");}
    if(p.preview.kind==FileOperationKind::RemoveKeepData){require(!hasPendingCategoryDeletion(lease),ErrorCode::Conflict,"Category recovery prevents Keep Data removal");}
    checkRemovalGuards(p);if(p.archive){checkArchive(*p.archive,false);}checkControls(p);for(const auto& e:p.edits){check(e.path,e.before,MaximumDocument);}for(auto it=p.files.rbegin();it!=p.files.rend();++it){auto& f=*it;checkpoint(stop);const auto& expected=retargeting(p.preview.kind)?f.destination:f.source;const auto& path=retargeting(p.preview.kind)?f.view.destination:f.view.source;const auto now=inspect(path);require(bool(now)==bool(expected)&&(!now||unchanged(*now,*expected)),ErrorCode::Conflict,"Payload changed after preparation");if(expected&&p.preview.kind!=FileOperationKind::RemoveKeepData&&(!p.postSkip||effectMoves(f)||f.selectionRemove)){f.sha256=p.payloadDigests.digest(path,*expected,stop);}}
    if(p.postSkip){for(const auto& f:p.files){if(f.selectionRemove||f.selectionAllocate){checkSelectionAnchor(f);}}}
    if(p.replacement){checkReplacementBeforeJournal(p,stop);}
    checkpoint(stop);acquisitionAvailable(lease);p.attempted=true;if(p.reviewedJournal){
      // Replace ONLY the exact confirmed old journal, before any new payload or
      // profile effect. The old uncertain directory is never adopted or removed.
      publish(p.profile/JournalName,*p.reviewedJournal,journal(p),MaximumJournal);p.reviewedJournal.reset();p.journalCreated=true;
    }else{saveJournal(p);}if(observer){observer(FileOperationStage::JournalSynced);}auto result=execute(lease,p,false,observer);
    // No receipt exists until this exact fresh apply has completed and removed
    // its own journal. Errors here are auxiliary to the already durable removal.
    if(history&&p.preview.kind==FileOperationKind::RemoveKeepData){
      try{
        require(p.removalBeforeSaved.has_value(),ErrorCode::Conflict,"Prepared removal history source absent");
        std::vector<DurableRemovalReceipt::Postimage> postimages;
        const auto add=[&](const Control& source){DurableRemovalReceipt::Postimage out;out.name=source.path.lexically_relative(p.profile).generic_string();if(source.before.bytes){out.bytes=*source.before.bytes;}if(source.before.stamp){const auto& v=*source.before.stamp;out.metadata=std::array<std::uint64_t,11>{static_cast<std::uint64_t>(v.device),static_cast<std::uint64_t>(v.inode),static_cast<std::uint64_t>(v.mode),static_cast<std::uint64_t>(v.owner),static_cast<std::uint64_t>(v.group),static_cast<std::uint64_t>(v.links),static_cast<std::uint64_t>(v.size),static_cast<std::uint64_t>(v.modified.tv_sec),static_cast<std::uint64_t>(v.modified.tv_nsec),static_cast<std::uint64_t>(v.changed.tv_sec),static_cast<std::uint64_t>(v.changed.tv_nsec)};}postimages.push_back(std::move(out));};
        for(const auto& source:p.removalPublished){add(source);}
        auto issued=DurableRemovalReceipt::issue(lease,*p.removalBeforeSaved,p.removalBeforeState,postimages,p.removalActiveDirectory,p.removalEventMillis,p.removalEventEnd,tags);
        if(issued){history->tagWarning=issued->tagWarning();history->receipt.emplace(std::move(*issued));}else{history->warning=issued.error();}
      }catch(const Failure& failure){try{history->warning=failure.error;}catch(...){}}catch(...){try{history->warning=Error{ErrorCode::Io,"Removal committed; history witness unavailable"};}catch(...){}}
    }
    return result;
  });
}
std::expected<bool,Error> torrentArchiveDeletionPreference(const ProfileLease& lease,std::stop_token stop){
  const auto settings=readApplicationSettings(lease,stop);if(!settings){return std::unexpected(settings.error());}
  const auto* value=settings->source?settings->source->find("def.deletetorrent"):nullptr;
  if(!value){return true;}if(value->type!=Node::Type::Integer){return std::unexpected(Error{ErrorCode::InvalidFormat,"Invalid setting: def.deletetorrent"});}return static_cast<std::uint32_t>(value->integer)!=0;
}
std::expected<bool,Error> payloadRecyclePreference(const ProfileLease& lease,std::stop_token stop){
  const auto settings=readApplicationSettings(lease,stop);if(!settings){return std::unexpected(settings.error());}
  const auto* value=settings->source?settings->source->find("Move Deleted Data To Recycle Bin"):nullptr;
  if(!value){return true;}if(value->type!=Node::Type::Integer){return std::unexpected(Error{ErrorCode::InvalidFormat,"Invalid setting: Move Deleted Data To Recycle Bin"});}return static_cast<std::uint32_t>(value->integer)!=0;
}
std::expected<fs::path,Error> linuxHomeTrashRoot(){
#ifdef _WIN32
  return std::unexpected(Error{ErrorCode::Unsupported,"The Linux home trash is unavailable on Windows"});
#else
  return guarded([]()->std::expected<fs::path,Error>{
  const char* data=std::getenv("XDG_DATA_HOME");fs::path root;
  if(data&&*data){root=fs::path(data);require(root.is_absolute(),ErrorCode::UnsafePath,"XDG_DATA_HOME must be absolute for recycling");}
  else{const char* home=std::getenv("HOME");require(home&&*home,ErrorCode::Missing,"Home trash location is unavailable");root=fs::path(home)/".local/share";}
  pathShape(root);return root/"Trash";
  });
#endif
}
bool hasPendingFileOperation(const ProfileLease& lease) noexcept {
  try{const auto profile=pathFromUtf8(profilePath(lease));return inspect(profile/JournalName).has_value()||inspect(profile/(std::string(JournalName)+".BiglyBTC.fileop-saving")).has_value();}catch(...){return true;}
}
bool keepDataRemovalNeedsReview(const ProfileLease& lease) noexcept {
  try{
    const auto profile=pathFromUtf8(profilePath(lease));
    if(inspect(profile/(std::string(JournalName)+".BiglyBTC.fileop-saving"))){return true;}
    const auto raw=read(profile/JournalName,MaximumJournal);return raw.bytes&&journalIsKeepData(raw);
  }catch(...){return true;}
}
std::expected<std::optional<FileOperationResult>,Error> recoverFileOperation(const ProfileLease& lease,void(*observer)(FileOperationStage)){
  return guarded([&]()->std::expected<std::optional<FileOperationResult>,Error>{acquisitionAvailable(lease);require(!hasPendingOriginalCompaction(lease),ErrorCode::Conflict,"Recover pending explicit compaction before file-operation recovery");const auto profile=pathFromUtf8(profilePath(lease));auto raw=read(profile/JournalName,MaximumJournal);if(!raw.bytes){require(!hasPendingFileOperation(lease),ErrorCode::Conflict,"Unpublished file-operation journal requires explicit review");return std::nullopt;}require(!journalIsKeepData(raw),ErrorCode::Conflict,"Interrupted Keep Data removal requires explicit review; no files were replayed");auto p=decode(raw,profile);profileIdentity(lease,p);validateLegacyNaming(lease,p);validateCurrentIndependence(lease,p);
    // Check every affected document before any further data mutation. Sparse
    // target row/pauses/cache merges tolerate unrelated owner checkpoints.
    for(const auto& e:p.edits){const auto now=read(e.path,MaximumDocument);if(e.after){static_cast<void>(mergedDocument(p,e,now));}else if(now.bytes){require(now.bytes==e.before.bytes&&now.stamp&&e.before.stamp&&unchanged(*now.stamp,*e.before.stamp),ErrorCode::Conflict,"Removal state changed during recovery");}}
    return std::optional<FileOperationResult>{execute(lease,p,true,observer)};
  });
}
#include "selection_group_handoff.inc"
} // namespace biglybtc::storage
