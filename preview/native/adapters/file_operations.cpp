#include "biglybtc/original_file_links.hpp"
#include "biglybtc/file_policy.hpp"
#include "file_operations.hpp"
#include "torrent_intake.hpp"
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
#include <algorithm>
#include <array>
#include <charconv>
#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <limits>
#include <linux/fs.h>
#include <map>
#include <set>
#include <span>
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
template<class T> T expectedValue(std::expected<T,Error>&& value){if(!value){throw Failure{value.error()};}return std::move(*value);}
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
struct Archive {fs::path path;Raw before;bool internalState=false;Raw backup;};
struct Trash {fs::path root;std::string deletionDate;std::array<std::pair<std::uint64_t,std::uint64_t>,3> directories{};};
struct Replacement {fs::path root;std::string sha256;std::uint64_t directoryDevice=0,directoryInode=0;};
struct Payload {FileOperationEntry view;std::optional<struct stat> source,destination;std::string sha256;std::optional<struct stat> temporary;};
struct Plan {
  FileOperationPreview preview;
  fs::path profile,destinationParent;
  std::uint64_t profileDevice=0,profileInode=0,targetDevice=0,targetInode=0;
  std::vector<Payload> files;
  std::vector<Control> controls;
  std::vector<OriginalFileGeometry> controlGeometry;
  std::vector<Edit> edits;
  std::optional<Archive> archive;
  std::optional<Trash> trash;
  std::optional<Replacement> replacement;
  std::optional<std::string> filenameConversions;
  std::string scopeSelection;
  std::optional<Raw> reviewedJournal; // Memory-only exact old journal CAS; never serialized as authority.
  bool attempted=false,journalCreated=false;
  PayloadDigestCache payloadDigests; // Empty on decode; at most eight pinned fds.
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
  if(p.replacement){auto r=parse("de");put(r,"root",b(p.replacement->root.string()));put(r,"sha",b(p.replacement->sha256));put(r,"bytes",number(p.files.front().destination->st_size));if(p.preview.unsealedPreimageDirectory){put(r,"unsealed",b(p.preview.unsealedPreimageDirectory->string()));}put(d,"replacement",r.serialize());}
  if(p.filenameConversions){put(d,"filenameConversions",b(*p.filenameConversions));}
  if(p.trash){auto t=parse("de");put(t,"root",b(p.trash->root.string()));if(!stable){put(t,"date",b(p.trash->deletionDate));}put(d,"trash",t.serialize());}
  if(!p.controls.empty()){std::string controls="l";for(const auto& c:p.controls){auto item=parse("de",MaximumJournal);put(item,"path",b(c.path.string()));if(!stable&&c.before.bytes){put(item,"before",b(*c.before.bytes));put(item,"stamp",stampText(*c.before.stamp));}controls+=item.serialize();}put(d,"controls",controls+'e');}
  if(!stable){std::string edits="l";for(const auto& e:p.edits){auto item=parse("de",MaximumJournal);put(item,"path",b(e.path.string()));put(item,"stage",number(static_cast<int>(e.stage)));if(e.before.bytes){put(item,"before",b(*e.before.bytes));put(item,"stamp",stampText(*e.before.stamp));}if(e.after){put(item,"after",b(*e.after));}edits+=item.serialize();}put(d,"edits",edits+'e');}
  if(p.archive){const auto& a=*p.archive;auto row=parse("de",MaximumJournal);put(row,"path",b(a.path.string()));put(row,"internal",number(a.internalState));
    // Selected internal state already has exact execution CAS below; legitimate
    // retirement checkpoints must not change the visible archive choice.
    if(stable&&!a.internalState&&a.before.stamp){put(row,"dev",b(std::to_string(a.before.stamp->st_dev)));put(row,"ino",b(std::to_string(a.before.stamp->st_ino)));put(row,"sha256",b(digest(*a.before.bytes)));}
    if(!stable&&a.before.bytes){put(row,"before",b(*a.before.bytes));put(row,"stamp",stampText(*a.before.stamp));}
    if(!a.internalState){auto backup=parse("de",MaximumJournal);if(a.backup.bytes){if(stable){put(backup,"dev",b(std::to_string(a.backup.stamp->st_dev)));put(backup,"ino",b(std::to_string(a.backup.stamp->st_ino)));put(backup,"sha256",b(digest(*a.backup.bytes)));}else{put(backup,"before",b(*a.backup.bytes));put(backup,"stamp",stampText(*a.backup.stamp));}}put(row,"backup",backup.serialize());}put(d,"archive",row.serialize());}
  return d.serialize();
}
std::string journal(const Plan& p){
  auto d=parse(canonical(p,false),MaximumJournal);put(d,"version","i1e");put(d,"pd",b(std::to_string(p.profileDevice)));put(d,"pi",b(std::to_string(p.profileInode)));put(d,"scope",b(p.preview.approvalScopeDigest));put(d,"cas",b(p.preview.executionCasDigest));
  if(p.trash&&p.trash->directories[0].second){std::string dirs="l";for(const auto& [dev,ino]:p.trash->directories){auto row=parse("de");put(row,"dev",b(std::to_string(dev)));put(row,"ino",b(std::to_string(ino)));dirs+=row.serialize();}put(d,"trashDirectories",dirs+'e');}
  if(p.replacement&&p.replacement->directoryInode){auto r=parse("de");put(r,"dev",b(std::to_string(p.replacement->directoryDevice)));put(r,"ino",b(std::to_string(p.replacement->directoryInode)));put(d,"preimageDirectory",r.serialize());}
  std::string runtime="l";for(const auto& f:p.files){auto n=parse("de");put(n,"sha",b(f.sha256));if(f.temporary){put(n,"temp",stampText(*f.temporary));}runtime+=n.serialize();}put(d,"runtime",runtime+'e');const auto body=d.serialize();return "d7:payload"+b(body)+"6:sha256"+b(digest(body))+"7:versioni1ee";
}
void saveJournal(Plan& p){const auto path=p.profile/JournalName;auto old=read(path,MaximumJournal);
  require(bool(old.bytes)==p.journalCreated,ErrorCode::Conflict,"File-operation journal appeared/disappeared unexpectedly");
  if(old.bytes){auto outer=parse(*old.bytes,MaximumJournal);const auto body=text(outer.find("payload"));require(digest(body)==text(outer.find("sha256")),ErrorCode::Conflict,"Existing operation journal changed");auto prior=parse(body,MaximumJournal);require(text(prior.find("scope"))==p.preview.approvalScopeDigest&&text(prior.find("cas"))==p.preview.executionCasDigest,ErrorCode::Conflict,"Existing journal belongs to another operation");}
  publish(path,old,journal(p),MaximumJournal);p.journalCreated=true;
}
void hashMatches(const EncodedDocument& doc,const core::TorrentHash& hash){
  const auto subject=originalTorrentInfoSubject(doc.document());require(subject.has_value(),subject?ErrorCode::Io:subject.error().code,subject?"Original info subject":subject.error().message.c_str());
  const auto* info=doc.document().find("info");OriginalFullInfoHashes full;
  const auto compute=[&](const EVP_MD* algorithm,std::span<std::byte> output){std::array<unsigned char,EVP_MAX_MD_SIZE> digest{};unsigned size=0;require(EVP_Digest(subject->data(),subject->size(),digest.data(),&size,algorithm,nullptr)==1&&size==output.size(),ErrorCode::InvalidFormat,"Cannot compute complete original info digest");std::copy_n(reinterpret_cast<const std::byte*>(digest.data()),output.size(),output.begin());};
  if(info->find("pieces")){full.v1.emplace();compute(EVP_sha1(),*full.v1);}if(info->find("file tree")){full.v2.emplace();compute(EVP_sha256(),*full.v2);}
  const auto identity=selectOriginalTorrentIdentity(doc.document(),{*subject,std::move(full)});require(identity.has_value(),identity?ErrorCode::Io:identity.error().code,identity?"Original selected identity":identity.error().message.c_str());
  const auto matched=validateOriginalTorrentKey(*identity,hash,true);require(matched.has_value(),matched?ErrorCode::Io:matched.error().code,matched?"Original saved identity":matched.error().message.c_str());
}
EncodedDocument envelope(const Raw& raw){require(raw.bytes.has_value(),ErrorCode::Missing,"Selected active metadata is absent");auto doc=EncodedDocument::parse(*raw.bytes,limits());require(doc&&doc->document().root().type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid original active metadata");return std::move(*doc);}
void archiveShape(const Plan& p,const Archive& a){
  pathShape(a.path);const auto active=p.profile/"active"/(p.preview.hash.hex()+".dat");
  require(a.internalState==(a.path==active||a.path==fs::path(active.string()+".bak")),ErrorCode::UnsafePath,"Archive internal-state alias changed");
  require(p.preview.kind==FileOperationKind::RemoveKeepData||p.preview.kind==FileOperationKind::RemoveDeleteData||p.preview.kind==FileOperationKind::RemoveRecycleData,ErrorCode::Unsupported,"Archive deletion is available only when removing a torrent");
  if(!a.internalState){
    require(!within(a.path,p.profile)||(!within(a.path,p.profile/"active")&&a.path.extension()==".torrent"),ErrorCode::UnsafePath,"Archive aliases protected profile data");
    require(!intersects(a.path,p.preview.sourceRoot),ErrorCode::UnsafePath,"Archive aliases the selected payload root");
  }
  const auto metadata=[&](const Raw& raw){if(raw.bytes){require(raw.stamp&&raw.stamp->st_nlink==1&&raw.stamp->st_size==static_cast<std::int64_t>(raw.bytes->size()),ErrorCode::UnsafePath,"Archive must be private owned single-link metadata");hashMatches(envelope(raw),p.preview.hash);}};
  metadata(a.before);metadata(a.backup);
  require(!a.internalState||!a.backup.bytes,ErrorCode::InvalidFormat,"Internal archive cannot add an unreviewed backup");
}
void checkArchive(const Archive& a,bool recovered){
  const auto one=[&](const fs::path& path,const Raw& before){const auto current=read(path,MaximumDocument);if(current.bytes){check(path,before,MaximumDocument);}else{require(recovered||!before.bytes,ErrorCode::Conflict,"Reviewed torrent archive disappeared");}};
  one(a.path,a.before);if(!a.internalState){one(fs::path(a.path.string()+".bak"),a.backup);}
}
void archiveReference(const Plan& p,const core::SavedDownload& other){
  if(!p.archive){return;}const auto& a=*p.archive;const fs::path primary(other.torrentFile);
  // Original archive deletion owns both the named metadata and its exact .bak.
  // Neither may become another torrent's reviewed deletion scope, even if the
  // backup was absent when that other library record was loaded.
  for(const auto& path:{primary,fs::path(primary.string()+".bak")}){
    pathShape(path);
    require(path!=a.path&&(a.internalState||path!=fs::path(a.path.string()+".bak")),ErrorCode::Conflict,"Torrent archive is referenced by another library item");
    const auto stamp=inspect(path);require(!stamp||((!a.before.stamp||!identity(*stamp,*a.before.stamp))&&(!a.backup.stamp||!identity(*stamp,*a.backup.stamp))),ErrorCode::Conflict,"Torrent archive inode is shared by another library item");
  }
}
void archivePayload(const Plan& p,const fs::path& path){
  if(!p.archive||p.archive->internalState){return;}const auto& a=*p.archive;
  require(!intersects(a.path,path)&&!intersects(fs::path(a.path.string()+".bak"),path),ErrorCode::UnsafePath,"Torrent archive aliases payload data");
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
  for(const auto& c:p.controls){require(!inspect(fs::path(c.path.string()+".saving")),ErrorCode::Conflict,"Original control recovery is pending");auto now=read(c.path,MaximumDocument);
    // An authenticated removal journal can already have removed its primary.
    // Its accepted cleared beforeimage remains the authority for stale backup
    // cleanup; move/relink never receive this exception.
    if(recovering&&p.preview.kind!=FileOperationKind::Relocate&&p.preview.kind!=FileOperationKind::Relink&&p.preview.kind!=FileOperationKind::SetFilePolicies&&!now.bytes){current.push_back(c);continue;}
    current.push_back({c.path,std::move(now)});
  }
  validateCompletionControls(current,p.controlGeometry);
  for(std::size_t i=0;i<current.size();++i){const auto& now=current[i].before;const auto& expected=p.controls[i].before;require(now.bytes==expected.bytes&&(!expected.stamp||(now.stamp&&unchanged(*now.stamp,*expected.stamp))),ErrorCode::Conflict,"Original control file changed after preparation");}
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
bool selecting(FileOperationKind kind){return kind==FileOperationKind::SetFilePolicies;}
bool fileMapping(FileOperationKind kind){return kind==FileOperationKind::MoveFile||kind==FileOperationKind::RetargetFile||kind==FileOperationKind::RevertFile||kind==FileOperationKind::ReplaceFile;}
bool replacementRootFor(const fs::path& root,const fs::path& parent,std::string_view leaf){
  if(root.parent_path()!=parent){return false;}const auto name=root.filename().string();if(name==leaf){return true;}
  const auto prefix=std::string(leaf)+".retry.";if(!name.starts_with(prefix)){return false;}const auto suffix=std::string_view(name).substr(prefix.size());unsigned n=0;
  const auto parsed=std::from_chars(suffix.data(),suffix.data()+suffix.size(),n);return parsed.ec==std::errc{}&&parsed.ptr==suffix.data()+suffix.size()&&n>=1&&n<=1024&&std::to_string(n)==suffix;
}
bool directoryExists(const fs::path& path){struct stat s{};if(::lstat(path.c_str(),&s)==0){return true;}require(errno==ENOENT,ErrorCode::Io,"Cannot inspect preimage directory");return false;}
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
  const auto base=fs::path(saved.saveDirectory)/(layout.multifile?fs::path(*saved.saveFile)/layout.files[index].path:fs::path(*saved.saveFile));
  const auto* sub=attrs.find("dnd_sf");const auto* alt=attrs.find("dnd_al");
  if(!sub&&!alt){return source;}
  auto name=source.filename().string();
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
    require(selectionUtf8(directoryName)&&directoryName!="."&&directoryName!=".."&&fs::path(directoryName).filename()==fs::path(directoryName)
        &&directoryName.find('\0')==directoryName.npos&&directoryName.find('\n')==directoryName.npos,
        ErrorCode::UnsafePath,"Original DND subfolder must be a safe leaf");
    if(after.skipped){return base.parent_path()/directoryName/name;}
    // Original unskip restores the parent and keeps the current name. Prefix
    // removal belongs to actual completion, not to the selection command.
    return source.parent_path().filename()==directoryName?source.parent_path().parent_path()/name:source;
  }
  const auto alternateText=text(alt);require(selectionUtf8(alternateText),ErrorCode::UnsafePath,"Original DND path is not UTF-8");
  const fs::path alternate(alternateText);pathShape(alternate);
  const auto root=fs::path(saved.saveDirectory)/ *saved.saveFile;
  if(after.skipped){
    // Original uses String.startsWith. Component containment is deliberately
    // stricter: a sibling sharing the textual prefix never grants authority.
    require(layout.multifile,ErrorCode::Unsupported,"Alternate DND relocation needs a multifile save root");
    require(within(base.parent_path(),root),ErrorCode::UnsafePath,"Original DND base escapes save root");
    auto relative=base.parent_path().lexically_relative(root);if(relative=="."){relative.clear();}
    return alternate/relative/name;
  }
  return within(source,alternate)?base.parent_path()/name:source;
}

void selectionTargetIndependent(const fs::path& target,std::size_t index,
    const std::vector<fs::path>& paths,const core::SavedDownload& saved,
    const compat::OriginalLayout& layout,std::string_view suffix){
  for(std::size_t other=0;other<paths.size();++other){if(other==index||layout.files[other].padding){continue;}
    require(!intersects(target,paths[other]),ErrorCode::Conflict,"DND destination overlaps another selected payload path");
    const auto base=fs::path(saved.saveDirectory)/(layout.multifile?fs::path(*saved.saveFile)/layout.files[other].path:fs::path(*saved.saveFile));
    if(paths[other]==base&&!suffix.empty()){
      require(!intersects(target,fs::path(base.string()+std::string(suffix))),ErrorCode::Conflict,"DND destination overlaps another incomplete filename");
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
Plan prepare(const ProfileLease& lease,const FileOperationRequest& request,std::stop_token stop){
  checkpoint(stop);Plan p;p.profile=profilePath(lease);p.preview.kind=request.kind;p.preview.hash=request.hash;struct stat profile{};require(::fstat(lease.directoryHandle(),&profile)==0,ErrorCode::Io,"Cannot inspect held profile");p.profileDevice=profile.st_dev;p.profileInode=profile.st_ino;
  require(request.kind>=FileOperationKind::RemoveKeepData&&request.kind<=FileOperationKind::ReplaceFile&&(request.hash.bytes().size()==20||request.hash.bytes().size()==32),ErrorCode::InvalidFormat,"Invalid file-operation request");
  require(!inspect(p.profile/JournalName)&&!inspect(p.profile/(std::string(JournalName)+".BiglyBTC.fileop-saving")),ErrorCode::Conflict,"File operation recovery is required before preparing another operation");
  require(!inspect(p.profile/"BiglyBTC.torrent-import"),ErrorCode::Conflict,"Torrent import recovery is pending");
  const auto inventoryRaw=read(p.profile/"downloads.config",MaximumDocument);require(inventoryRaw.bytes.has_value(),ErrorCode::Missing,"Saved inventory is absent");auto inventory=parse(*inventoryRaw.bytes);auto library=parsedInventory(inventory);
  std::optional<core::SavedDownload> saved;for(const auto& row:library.downloads){if(row.hash&&*row.hash==request.hash){require(!saved,ErrorCode::Conflict,"Duplicate selected torrent identity");saved=row;}}
  require(saved&&saved->persistent,ErrorCode::Unsupported,"Selected original save mapping is unavailable");const auto rawSaved=*saved;
  const auto statePath=p.profile/"active"/(request.hash.hex()+".dat");const auto stateRaw=read(statePath,MaximumDocument);auto state=envelope(stateRaw);
  require(!inspect(fs::path(statePath.string()+".saving"))&&!inspect(fs::path(statePath.string()+".bak.saving")),ErrorCode::Conflict,"Active state has unresolved temporary data");
  const auto cachePath=p.profile/"active/cache.dat";const auto cacheRaw=read(cachePath,MaximumDocument);auto cache=cacheDocument(cacheRaw);hydrate(state,cacheEntry(cache?&*cache:nullptr,request.hash));hashMatches(state,request.hash);
  p.filenameConversions=expectedValue(readOriginalFilenameConversions(lease));auto layout=compat::originalLayout(state.document(),MaximumFiles,*p.filenameConversions);require(layout.has_value(),ErrorCode::Unsupported,"Unsupported original payload layout");auto attrs=section(state.document().find("attributes"));const auto originalAttributes=attrs;
  auto root=resolveOriginalSaveRoot(*saved,*layout);require(root.has_value(),root?ErrorCode::Io:root.error().code,root?"Original root resolution":root.error().message.c_str());saved=std::move(*root);
  auto saveParent=fs::path(saved->saveDirectory);pathShape(saveParent);p.preview.sourceRoot=saveParent/ *saved->saveFile;pathShape(p.preview.sourceRoot);require(!intersects(p.preview.sourceRoot,p.profile),ErrorCode::UnsafePath,"Payload may not contain or reside inside the held profile");
  auto defaults=resolveOriginalStorageTypes(lease,request.hash,*layout,attrs.root());require(defaults.has_value(),defaults?ErrorCode::Io:defaults.error().code,defaults?"Storage default resolution":defaults.error().message.c_str());
  auto wanted=attrs;require(applyOriginalStorageDefaults(wanted,*defaults).has_value(),ErrorCode::InvalidFormat,"Cannot capture requested storage defaults");
  require(applyOriginalStorageDefaults(attrs,*defaults,true).has_value(),ErrorCode::InvalidFormat,"Cannot capture physical storage defaults");auto paths=mapping(*saved,*layout,attrs.root());
  const auto* completion=attrs.find("BiglyBTC.completion");if(completion){require(completion->type==Node::Type::Dictionary&&completion->children.empty(),ErrorCode::Conflict,"Completion rename recovery must finish before file operation");}
  if(const auto* conversion=attrs.find("BiglyBTC.compactConversion")){require(conversion->type==Node::Type::List&&conversion->children.empty(),ErrorCode::Conflict,"Compact conversion recovery must finish before file operation");}
  if(deleting(request.kind)){if(const auto* flags=attrs.find("flags")){require(flags->type==Node::Type::Integer,ErrorCode::InvalidFormat,"Malformed original deletion-protection flags");require((flags->integer&0x40)==0,ErrorCode::Unsupported,"Original torrent flags prohibit deleting payload on removal");}}
  const bool oneFile=fileMapping(request.kind);
  require(oneFile==request.fileIndex.has_value(),ErrorCode::InvalidFormat,"Exactly one original file index is required for a file mapping");
  require((request.kind==FileOperationKind::MoveFile||request.kind==FileOperationKind::RetargetFile||request.kind==FileOperationKind::ReplaceFile)==request.fileDestination.has_value(),ErrorCode::InvalidFormat,"Invalid per-file destination");
  require(recycling(request.kind)==request.trashRoot.has_value(),ErrorCode::InvalidFormat,"Recycle requests require an explicit owned trash root");
  if(oneFile){require(*request.fileIndex<layout->files.size()&&!layout->files[*request.fileIndex].padding,ErrorCode::InvalidFormat,"Selected original file index is missing or padding");}
  if(oneFile){const auto& row=inventoryRow(inventory,*saved);require(!row.find("BiglyBTC.verifiedSeeding")&&!row.find("BiglyBTC.verifiedExistingScope"),ErrorCode::Unsupported,"Per-file mapping cannot replace an authenticated verified-existing scope");}
  const bool fileSelection=selecting(request.kind);
  require(fileSelection==request.selectionPolicyEncoding.has_value()&&fileSelection==request.expectedSelectionEntry.has_value(),ErrorCode::InvalidFormat,"Selection encoding/source belong only to file-selection operations");
  std::vector<compat::FilePolicy> previousPolicy,nextPolicy;
  if(fileSelection){
    require(!request.destinationParent&&request.selectionPolicyEncoding->size()<=MaximumDocument,ErrorCode::InvalidFormat,"Invalid file-selection request");
    require(encoded(inventoryRow(inventory,*saved))==*request.expectedSelectionEntry,ErrorCode::Conflict,"Selected original file policy changed during preparation");
    auto row=parse(encoded(inventoryRow(inventory,*saved)));previousPolicy=selectionPolicies(row.root(),*layout,false);
    put(row,"file_priorities_c",*request.selectionPolicyEncoding);nextPolicy=selectionPolicies(row.root(),*layout);
    p.destinationParent=saveParent;auto parent=directory(saveParent);struct stat target{};
    require(::fstat(parent.get(),&target)==0&&target.st_uid==::geteuid(),ErrorCode::UnsafePath,"Selection save parent must be owned");
    p.targetDevice=target.st_dev;p.targetInode=target.st_ino;p.preview.targetRoot=p.preview.sourceRoot;p.preview.requiresRecheck=true;
  }else if(oneFile){
    require(!request.destinationParent,ErrorCode::InvalidFormat,"Per-file operation does not accept a parent-root move");
    const auto index=*request.fileIndex;
    const auto canonical=saveParent/(layout->multifile?fs::path(*saved->saveFile)/layout->files[index].path:fs::path(*saved->saveFile));
    const auto target=request.kind==FileOperationKind::RevertFile?canonical:*request.fileDestination;payloadPath(target,p.profile);
    require(target!=paths[index],ErrorCode::Conflict,"File is already at that mapping");
    for(std::size_t n=0;n<paths.size();++n){if(n!=index&&!layout->files[n].padding){require(!intersects(target,paths[n]),ErrorCode::Conflict,"File destination overlaps another original file");}}
    p.destinationParent=target.parent_path();auto parent=directory(p.destinationParent,false,true);while(parent.get()<0){const auto ancestor=p.destinationParent.parent_path();require(ancestor!=p.destinationParent&&!ancestor.empty(),ErrorCode::UnsafePath,"Per-file destination has no existing ancestor");p.destinationParent=ancestor;parent=directory(p.destinationParent,false,true);}struct stat id{};
    require(::fstat(parent.get(),&id)==0&&id.st_uid==::geteuid(),ErrorCode::UnsafePath,"Per-file destination parent must be owned");
    p.targetDevice=id.st_dev;p.targetInode=id.st_ino;p.preview.targetRoot=p.preview.sourceRoot;p.preview.requiresRecheck=true;
  }else if(recycling(request.kind)){
    require(!request.destinationParent,ErrorCode::InvalidFormat,"Recycle request cannot move the save root");prepareTrash(p,*request.trashRoot);
  }else if(moving(request.kind)){
    require(request.destinationParent.has_value(),ErrorCode::InvalidFormat,"Destination parent is required");p.destinationParent=*request.destinationParent;pathShape(p.destinationParent);auto destination=directory(p.destinationParent);struct stat target{};
    require(::fstat(destination.get(),&target)==0&&target.st_uid==::geteuid(),ErrorCode::UnsafePath,"Destination parent must be owned");p.targetDevice=target.st_dev;p.targetInode=target.st_ino;
    p.preview.targetRoot=p.destinationParent/ *saved->saveFile;pathShape(p.preview.targetRoot);require(!intersects(p.preview.sourceRoot,p.preview.targetRoot)&&!intersects(p.preview.targetRoot,p.profile),ErrorCode::Conflict,"Source, destination and profile paths overlap");
    if(request.kind==FileOperationKind::Relocate){struct stat exists{};require(::fstatat(destination.get(),saved->saveFile->c_str(),&exists,AT_SYMLINK_NOFOLLOW)!=0&&errno==ENOENT,ErrorCode::Conflict,"Destination name already exists; relocation never merges or overwrites");}
    p.preview.requiresRecheck=true;
  }else{require(!request.destinationParent,ErrorCode::InvalidFormat,"Removal does not accept a destination");}
  const auto suffix=attrs.find("incompfilesuffix")?text(attrs.find("incompfilesuffix")):std::string{};require(suffix.find('/')==suffix.npos&&suffix.find('\n')==suffix.npos&&suffix.size()<=200,ErrorCode::Unsupported,"Unsupported incomplete suffix");
  std::set<fs::path> unique;std::set<std::pair<dev_t,ino_t>> inodes;
  for(std::size_t index=0;index<layout->files.size();++index){checkpoint(stop);if(layout->files[index].padding||(oneFile&&index!=*request.fileIndex)){continue;}Payload f;f.view.index=index;f.view.logicalBytes=layout->files[index].bytes;f.view.source=paths[index];f.source=inspect(f.view.source);
    const auto base=saveParent/(layout->multifile?fs::path(*saved->saveFile)/layout->files[index].path:fs::path(*saved->saveFile));
    if(!f.source&&paths[index]==base&&!suffix.empty()){f.view.source+=suffix;f.source=inspect(f.view.source);}
    payloadPath(f.view.source,p.profile);
    if(f.source){ownedPayload(*f.source,physicalMaximum(*layout,attrs.root(),index));require(inodes.emplace(f.source->st_dev,f.source->st_ino).second,ErrorCode::UnsafePath,"Selected files share an inode");}
    if(fileSelection){
      f.view.destination=selectionDestination(*saved,*layout,attrs.root(),index,f.view.source,previousPolicy[index],nextPolicy[index]);
      if(f.view.destination==f.view.source){continue;}
      payloadPath(f.view.destination,p.profile);selectionTargetIndependent(f.view.destination,index,paths,*saved,*layout,suffix);
      {auto parent=directory(f.view.destination.parent_path(),false,true);if(parent.get()>=0){struct stat id{};require(::fstat(parent.get(),&id)==0&&id.st_uid==::geteuid(),ErrorCode::UnsafePath,"DND destination parent must be owned");}}
      // Original skip leaves an occupied destination untouched. Do not turn
      // that optional relocation into overwrite or discard of either file.
      if(inspect(f.view.destination)){continue;}
    }
    require(unique.insert(f.view.source).second,ErrorCode::UnsafePath,"Selected files share a path");
    if(oneFile){
      f.view.destination=request.kind==FileOperationKind::RevertFile?base:*request.fileDestination;
      if(retargeting(request.kind)){
        require(storageKind(attrs.root(),index,layout->files.size())==OriginalStorageKind::Linear,ErrorCode::Unsupported,"Retargeting existing data currently requires original linear storage");
        f.destination=inspect(f.view.destination);require(f.destination.has_value(),ErrorCode::Missing,"Retarget requires an existing original file");ownedPayload(*f.destination,physicalMaximum(*layout,attrs.root(),index));
        require(inodes.emplace(f.destination->st_dev,f.destination->st_ino).second,ErrorCode::Conflict,"Retarget destination aliases the old source");
      }else if(request.kind==FileOperationKind::ReplaceFile){
        require(f.source.has_value(),ErrorCode::Missing,"Replacement requires actual source bytes");f.destination=inspect(f.view.destination);require(f.destination.has_value(),ErrorCode::Missing,"Explicit replacement requires an existing destination");ownedPayload(*f.destination,std::numeric_limits<std::int64_t>::max());
        require(inodes.emplace(f.destination->st_dev,f.destination->st_ino).second,ErrorCode::Conflict,"Replacement destination aliases the source");
        const auto hash=p.payloadDigests.digest(f.view.destination,*f.destination,stop);const auto leaf=".BiglyBTC.replaced."+p.preview.hash.hex()+'.'+std::to_string(index)+'.'+digest(f.view.destination.string()+':'+std::to_string(f.destination->st_dev)+':'+std::to_string(f.destination->st_ino)).substr(0,16);
        const auto root=f.view.destination.parent_path()/leaf;require(!directoryExists(root),ErrorCode::Conflict,"A prior destination preimage already occupies the reserved path");
        p.replacement=Replacement{root,hash};p.preview.replacement=FileOperationPreimage{root/"data",static_cast<std::uint64_t>(f.destination->st_size),hash};
      }else{require(!inspect(f.view.destination),ErrorCode::Conflict,"File destination exists; moving/reverting never overwrites");}
    }else if(recycling(request.kind)){f.view.destination=trashDestination(p,f);}
    else if(moving(request.kind)&&!fileSelection){f.view.destination=relocatedPath(*saved,*layout,index,f.view.source,p.destinationParent,suffix);payloadPath(f.view.destination,p.profile);if(request.kind==FileOperationKind::Relink){f.destination=inspect(f.view.destination);if(f.destination){ownedPayload(*f.destination,physicalMaximum(*layout,attrs.root(),index));require(inodes.emplace(f.destination->st_dev,f.destination->st_ino).second,ErrorCode::UnsafePath,"Relink target shares a selected inode");}}
      else{require(!inspect(f.view.destination),ErrorCode::Conflict,"A relocation payload destination already exists");}
      if(request.kind==FileOperationKind::Relocate&&f.source&&f.source->st_dev!=p.targetDevice){p.preview.crossDevice=true;}}
    if(relocating(request.kind)&&f.source&&f.source->st_dev!=p.targetDevice){p.preview.crossDevice=true;}
    if(moving(request.kind)){require(unique.insert(f.view.destination).second,ErrorCode::UnsafePath,"Selected destination shares a path");}
    const auto& visible=retargeting(request.kind)?f.destination:f.source;f.view.present=visible.has_value();if(visible){f.view.device=visible->st_dev;f.view.inode=visible->st_ino;f.view.actualBytes=visible->st_size;++p.preview.presentFiles;require(static_cast<std::uint64_t>(visible->st_size)<=UINT64_MAX-p.preview.presentBytes,ErrorCode::InvalidFormat,"Payload byte sum overflow");p.preview.presentBytes+=static_cast<std::uint64_t>(visible->st_size);}else{++p.preview.missingFiles;}
    p.files.push_back(std::move(f));
  }
  if(request.kind==FileOperationKind::Relink){require(p.preview.presentFiles!=0,ErrorCode::Missing,"Relink destination has no original payload files");}
  if(request.deleteTorrentFile){p.archive=Archive{fs::path(rawSaved.torrentFile),{},false,{}};auto& archive=*p.archive;
    archive.internalState=archive.path==statePath||archive.path==fs::path(statePath.string()+".bak");archive.before=read(archive.path,MaximumDocument);if(!archive.internalState){archive.backup=read(fs::path(archive.path.string()+".bak"),MaximumDocument);}archiveShape(p,archive);
    for(const auto& file:p.files){archivePayload(p,file.view.source);}
    p.preview.torrentArchive=FileOperationArchive{archive.path,archive.before.bytes?static_cast<std::uint64_t>(archive.before.bytes->size()):0,archive.before.bytes.has_value(),archive.internalState};if(!archive.internalState){p.preview.torrentArchiveBackup=FileOperationArchive{fs::path(archive.path.string()+".bak"),archive.backup.bytes?static_cast<std::uint64_t>(archive.backup.bytes->size()):0,archive.backup.bytes.has_value(),false};}}
  // No accidental shared data: inspect every other saved root and supported
  // explicit physical map, including identical inode aliases.
  if(request.kind!=FileOperationKind::RemoveKeepData||p.archive){for(const auto& other:library.downloads){
    if(other.hash&&*other.hash==request.hash){continue;}archiveReference(p,other);checkpoint(stop);require(other.hash.has_value(),ErrorCode::Unsupported,"Cannot prove independence from another unresolved library mapping");
    auto otherRaw=read(p.profile/"active"/(other.hash->hex()+".dat"),MaximumDocument);auto otherState=envelope(otherRaw);hydrate(otherState,cacheEntry(cache?&*cache:nullptr,*other.hash));hashMatches(otherState,*other.hash);auto otherLayout=readOriginalLayout(lease,otherState.document(),MaximumFiles);require(otherLayout.has_value(),ErrorCode::Unsupported,"Another library payload map is unsupported");auto otherAttrs=section(otherState.document().find("attributes"));auto otherRoot=resolveOriginalSaveRoot(other,*otherLayout);require(otherRoot.has_value(),ErrorCode::Unsupported,"Another original save root is unresolved");const fs::path absoluteRoot=fs::path(otherRoot->saveDirectory)/ *otherRoot->saveFile;require(!intersects(absoluteRoot,p.preview.sourceRoot)&&(!(moving(request.kind)||recycling(request.kind))||!intersects(absoluteRoot,p.preview.targetRoot)),ErrorCode::Conflict,"Payload roots overlap another library torrent");if(p.trash){require(!intersects(absoluteRoot,p.trash->root),ErrorCode::Conflict,"Trash overlaps another library root");}
    auto otherDefaults=resolveOriginalStorageTypes(lease,*other.hash,*otherLayout,otherAttrs.root());require(otherDefaults.has_value()&&applyOriginalStorageDefaults(otherAttrs,*otherDefaults,true).has_value(),ErrorCode::Unsupported,"Another original physical map is unresolved");auto otherPaths=mapping(*otherRoot,*otherLayout,otherAttrs.root());
    for(const auto& path:otherPaths){archivePayload(p,path);for(const auto& selected:p.files){require(!intersects(path,selected.view.source)&&(selected.view.destination.empty()||!intersects(path,selected.view.destination)),ErrorCode::Conflict,"Payload path is shared by another torrent");}if(auto info=inspect(path)){require(!inodes.contains({info->st_dev,info->st_ino}),ErrorCode::Conflict,"Payload inode belongs to another torrent");}}
  }}
  if(request.kind!=FileOperationKind::RemoveKeepData){for(const auto& row:library.downloads){const fs::path archive(row.torrentFile);pathShape(archive);for(const auto& f:p.files){require(f.view.source!=archive&&(f.view.destination.empty()||f.view.destination!=archive),ErrorCode::UnsafePath,"Payload operation aliases a retained torrent archive");}}}
  const auto& selectedRow=inventoryRow(inventory,*saved);auto updatedRow=parse(encoded(selectedRow));std::string selection;
  for(const auto* key:{"file_priorities","file_priorities_c"}){if(const auto* priorities=selectedRow.find(key)){selection+=b(key)+encoded(*priorities);}}for(const auto* key:{"storetypes","filelinks2","incompfilesuffix","dnd_pfx","dnd_sf","dnd_al","flags"}){if(const auto* n=attrs.find(key)){selection+=b(key)+encoded(*n);}}
  // Include priority/skip and actual path maps in the stable scope, while
  // excluding force/startup/pause/counters changed by legitimate retirement.
  selection+=b("BiglyBTC.requested.storetypes")+encoded(*wanted.find("storetypes"));
  selection+=b("BiglyBTC.resolved.storetypes")+encoded(*attrs.find("storetypes"));
  if(fileSelection){selection+=b("BiglyBTC.selection.policy")+b(*request.selectionPolicyEncoding);}
  p.scopeSelection=selection;
  if(cache){const auto* entries=cache->document().find("state");if(entries){require(cache->replaceValue("state",withoutSelected(entries,request.hash,false,std::nullopt,"hash")).has_value(),ErrorCode::InvalidFormat,"Cannot remove selected state cache entry");const auto after=serialize(*cache);if(after!=*cacheRaw.bytes){p.edits.push_back({cachePath,cacheRaw,after,FileOperationStage::CachePublished});}}}
  if(fileSelection){
    put(updatedRow,"file_priorities_c",*request.selectionPolicyEncoding);
    // Preserve all existing resolved links, including actual incomplete names.
    // Only files in the admitted relocation list acquire a new destination.
    std::string links="l";
    for(std::size_t index=0;index<layout->files.size();++index){if(layout->files[index].padding){continue;}
      const auto base=saveParent/(layout->multifile?fs::path(*saved->saveFile)/layout->files[index].path:fs::path(*saved->saveFile));
      auto target=paths[index];const auto moved=std::ranges::find(p.files,index,[](const Payload& f){return f.view.index;});
      if(moved!=p.files.end()){target=moved->view.destination;}
      if(target!=base){links+=b(std::to_string(index)+'\n'+base.string()+'\n'+target.string());}
    }
    put(attrs,"storetypes",encoded(*wanted.find("storetypes")));if(defaults->reorderMinimumMiB){put(attrs,"reordermb",number(*defaults->reorderMinimumMiB));}
    put(attrs,"filelinks2",links+'e');markRecheck(state,std::move(attrs));const auto newState=serialize(state);
    const auto backup=fs::path(statePath.string()+".bak");p.edits.push_back({backup,read(backup,MaximumDocument),newState,FileOperationStage::StatePublished});p.edits.push_back({statePath,stateRaw,newState,FileOperationStage::StatePublished});
    editInventory(inventory,request.hash,updatedRow.serialize());
  }else if(oneFile){
    const auto& changed=p.files.front();std::string links="l";
    for(std::size_t n=0;n<layout->files.size();++n){if(layout->files[n].padding){continue;}
      const auto base=saveParent/(layout->multifile?fs::path(*saved->saveFile)/layout->files[n].path:fs::path(*saved->saveFile));
      const auto target=n==changed.view.index?changed.view.destination:paths[n];
      if(target!=base){links+=b(std::to_string(n)+'\n'+base.string()+'\n'+target.string());}
    }
    put(attrs,"storetypes",encoded(*wanted.find("storetypes")));if(defaults->reorderMinimumMiB){put(attrs,"reordermb",number(*defaults->reorderMinimumMiB));}
    put(attrs,"filelinks2",links+'e');markRecheck(state,std::move(attrs));const auto newState=serialize(state);
    const auto backup=fs::path(statePath.string()+".bak");p.edits.push_back({backup,read(backup,MaximumDocument),newState,FileOperationStage::StatePublished});p.edits.push_back({statePath,stateRaw,newState,FileOperationStage::StatePublished});
    // Save root, original row, pause/force/queue and all unrelated fields stay exact.
    editInventory(inventory,request.hash,updatedRow.serialize());
  }else if(moving(request.kind)){
    put(updatedRow,"save_dir",b(p.destinationParent.string()));put(updatedRow,"save_file",b(*saved->saveFile));std::string links="l";
    for(const auto& f:p.files){const auto& original=layout->files[f.view.index];const auto base=p.destinationParent/(layout->multifile?fs::path(*saved->saveFile)/original.path:fs::path(*saved->saveFile));if(base!=f.view.destination){links+=b(std::to_string(f.view.index)+'\n'+base.string()+'\n'+f.view.destination.string());}}
    put(attrs,"storetypes",encoded(*wanted.find("storetypes")));if(defaults->reorderMinimumMiB){put(attrs,"reordermb",number(*defaults->reorderMinimumMiB));}
    put(attrs,"filelinks2",links+'e');put(attrs,"canosavedir",b(p.preview.targetRoot.string()+'\n'+p.preview.targetRoot.string()));markRecheck(state,std::move(attrs));const auto newState=serialize(state);
    const auto backup=fs::path(statePath.string()+".bak");p.edits.push_back({backup,read(backup,MaximumDocument),newState,FileOperationStage::StatePublished});p.edits.push_back({statePath,stateRaw,newState,FileOperationStage::StatePublished});
    editInventory(inventory,request.hash,updatedRow.serialize());
  }else{
    editInventory(inventory,request.hash);if(const auto* pauses=inventory.find("pause_data")){put(inventory,"pause_data",withoutSelected(pauses,request.hash,true));}
  }
  p.edits.push_back({p.profile/"downloads.config",inventoryRaw,inventory.serialize(),FileOperationStage::InventoryPublished});
  if(!moving(request.kind)){p.edits.push_back({statePath,stateRaw,std::nullopt,FileOperationStage::StateRemoved});const auto backup=fs::path(statePath.string()+".bak");auto raw=read(backup,MaximumDocument);if(raw.bytes){p.edits.push_back({backup,std::move(raw),std::nullopt,FileOperationStage::StateRemoved});}}
  for(const auto& path:controlPaths(p.profile,request.hash,layout->files.size())){require(!inspect(fs::path(path.string()+".saving")),ErrorCode::Conflict,"Original control recovery is pending");p.controls.push_back({path,read(path,MaximumDocument)});}
  captureControlGeometry(p,*layout);validateCompletionControls(p.controls,p.controlGeometry);
  auto checkedDefaults=resolveOriginalStorageTypes(lease,request.hash,*layout,originalAttributes.root());require(checkedDefaults&&*checkedDefaults==*defaults,ErrorCode::Conflict,"Storage controls/defaults changed during file-operation preparation");
  auto checkedRoot=resolveOriginalSaveRoot(rawSaved,*layout);require(checkedRoot&&checkedRoot->saveDirectory==saved->saveDirectory&&checkedRoot->saveFile==saved->saveFile,ErrorCode::Conflict,"Original root changed during file-operation preparation");
  require(expectedValue(readOriginalFilenameConversions(lease))==*p.filenameConversions,ErrorCode::Conflict,"Original filename conversions changed during preparation");
  const auto material=canonical(p,true);p.preview.approvalScopeDigest=digest(material);p.preview.executionCasDigest=digest(canonical(p,false));
  for(const auto& f:p.files){p.preview.files.push_back(f.view);}checkpoint(stop);return p;
}
}
namespace {
fs::path temporaryPath(const Plan& p,const Payload& f){return f.view.destination.parent_path()/(".BiglyBTC.fileop."+p.preview.executionCasDigest.substr(0,16)+'.'+std::to_string(f.view.index));}
void targetIdentity(const Plan& p){if(!moving(p.preview.kind)&&!recycling(p.preview.kind)){return;}auto fd=directory(p.destinationParent);struct stat current{};require(::fstat(fd.get(),&current)==0&&current.st_uid==::geteuid()&&current.st_dev==p.targetDevice&&current.st_ino==p.targetInode,ErrorCode::Conflict,"Destination parent identity changed");}
void readyDestination(Plan& p,Payload& f){
  if(!f.source){return;}targetIdentity(p);auto present=inspect(f.view.destination);
  if(present){const bool ours=identity(*present,*f.source)||(f.temporary&&identity(*present,*f.temporary));require(ours&&present->st_size==f.source->st_size&&p.payloadDigests.digest(f.view.destination,*present)==f.sha256,ErrorCode::Conflict,"Destination appeared or changed during relocation");return;}
  auto source=inspect(f.view.source);require(source&&identity(*source,*f.source)&&p.payloadDigests.digest(f.view.source,*f.source)==f.sha256,ErrorCode::Conflict,"Relocation source changed");auto from=directory(f.view.source.parent_path());auto to=directory(f.view.destination.parent_path(),true);if(selecting(p.preview.kind)){struct stat parent{};require(::fstat(to.get(),&parent)==0&&parent.st_uid==::geteuid(),ErrorCode::UnsafePath,"DND destination parent must remain owned");}
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
  require(copied==static_cast<std::uint64_t>(f.source->st_size),ErrorCode::Conflict,"Relocation source shortened");require(::fchmod(output.get(),f.source->st_mode&0777)==0,ErrorCode::Io,"Cannot preserve payload permissions");sync(output.get());auto scratch=inspect(temporary);require(scratch&&f.temporary&&identity(*scratch,*f.temporary)&&p.payloadDigests.digest(temporary,*scratch)==f.sha256&&p.payloadDigests.digest(f.view.source,*f.source)==f.sha256,ErrorCode::Conflict,"Relocation copy/source validation failed");f.temporary=*scratch;saveJournal(p);
  require(::syscall(SYS_renameat2,to.get(),temporary.filename().c_str(),to.get(),f.view.destination.filename().c_str(),RENAME_NOREPLACE)==0,ErrorCode::Conflict,"Relocation destination appeared before publication");sync(to.get());
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
  profileIdentity(lease,p);targetIdentity(p);checkControls(p,recovered);const auto notify=[&](FileOperationStage stage){if(observer){observer(stage);}};
  if(p.archive&&!p.archive->internalState){checkArchive(*p.archive,recovered);}
  if(recycling(p.preview.kind)){ensureTrash(p);}
  if(p.replacement){preserveDestination(p,observer);}
  if(relocating(p.preview.kind)){for(auto& f:p.files){readyDestination(p,f);if(f.source){notify(FileOperationStage::DestinationPublished);}}}
  if(retargeting(p.preview.kind)){for(const auto& f:p.files){const auto now=inspect(f.view.destination);require(bool(now)==bool(f.destination)&&(!now||(identity(*now,*f.destination)&&p.payloadDigests.digest(f.view.destination,*f.destination)==f.sha256)),ErrorCode::Conflict,"Relink target changed after confirmation");}}
  if(recycling(p.preview.kind)){publishTrashInfo(p);notify(FileOperationStage::TrashInfoPublished);}
  for(const auto& e:p.edits){if(e.after){publishEdit(p,e);notify(e.stage);}}
  FileOperationResult result{p.preview.kind,p.preview.hash,0,0,recovered};
  if(relocating(p.preview.kind)||p.preview.kind==FileOperationKind::RemoveDeleteData){for(const auto& f:p.files){if(!f.source){continue;}if(fileMapping(p.preview.kind)){targetIdentity(p);const auto destination=inspect(f.view.destination);require(destination&&(identity(*destination,*f.source)||(f.temporary&&identity(*destination,*f.temporary)))&&p.payloadDigests.digest(f.view.destination,*destination)==f.sha256,ErrorCode::Conflict,"Per-file destination changed before source removal");if(p.replacement){validatePreimage(p);}}if(recycling(p.preview.kind)){ensureTrash(p);const auto destination=inspect(f.view.destination);require(destination&&(identity(*destination,*f.source)||(f.temporary&&identity(*destination,*f.temporary)))&&p.payloadDigests.digest(f.view.destination,*destination)==f.sha256,ErrorCode::Conflict,"Trash data changed before source removal");const auto info=read(p.trash->root/"info"/(f.view.destination.filename().string()+".trashinfo"),16384);require(info.bytes&&*info.bytes==trashInfo(p,f),ErrorCode::Conflict,"Trash information changed before source removal");}const auto now=inspect(f.view.source);if(now){require(identity(*now,*f.source)&&p.payloadDigests.digest(f.view.source,*f.source)==f.sha256,ErrorCode::Conflict,"Source changed before final removal");erase(f.view.source,*now);}if(recycling(p.preview.kind)){++result.filesRecycled;}else if(relocating(p.preview.kind)){++result.filesMoved;}else{++result.filesDeleted;}notify(FileOperationStage::SourceRemoved);}}
  for(const auto& e:p.edits){if(!e.after){const auto current=read(e.path,MaximumDocument);if(current.bytes){require(current.bytes==e.before.bytes&&current.stamp&&e.before.stamp&&unchanged(*current.stamp,*e.before.stamp),ErrorCode::Conflict,"Active cleanup source changed");erase(e.path,*current.stamp);}notify(FileOperationStage::StateRemoved);}}
  if(!moving(p.preview.kind)){for(const auto& c:p.controls){const auto current=read(c.path,MaximumDocument);if(current.bytes){require(current.bytes==c.before.bytes&&current.stamp&&c.before.stamp&&unchanged(*current.stamp,*c.before.stamp),ErrorCode::Conflict,"Original control changed before removal");erase(c.path,*current.stamp);}notify(FileOperationStage::StateRemoved);}}
  if(p.archive){const auto& a=*p.archive;if(!a.internalState){const auto remove=[&](const fs::path& path,const Raw& before){const auto current=read(path,MaximumDocument);if(current.bytes){check(path,before,MaximumDocument);erase(path,*current.stamp);result.torrentArchiveDeleted=true;notify(FileOperationStage::ArchiveFileRemoved);}};remove(a.path,a.before);remove(fs::path(a.path.string()+".bak"),a.backup);}notify(FileOperationStage::ArchiveRemoved);}
  if(p.replacement){validatePreimage(p);result.retainedPreimage=p.replacement->root/"data";}
  const auto raw=read(p.profile/JournalName,MaximumJournal);require(raw.stamp.has_value(),ErrorCode::Conflict,"File-operation journal disappeared");erase(p.profile/JournalName,*raw.stamp);notify(FileOperationStage::Completed);return result;
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
    auto state=envelope(read(p.profile/"active"/(other.hash->hex()+".dat"),MaximumDocument));hydrate(state,cacheEntry(cache?&*cache:nullptr,*other.hash));hashMatches(state,*other.hash);auto layout=readOriginalLayout(lease,state.document(),MaximumFiles);require(layout.has_value(),ErrorCode::Unsupported,"Another recovery file layout is unsupported");auto attrs=section(state.document().find("attributes"));auto root=resolveOriginalSaveRoot(other,*layout);require(root.has_value(),ErrorCode::Unsupported,"Another recovery save root is unresolved");const auto absolute=fs::path(root->saveDirectory)/ *root->saveFile;require(!intersects(absolute,p.preview.sourceRoot)&&(p.preview.targetRoot.empty()||!intersects(absolute,p.preview.targetRoot)),ErrorCode::Conflict,"Another torrent now overlaps operation scope");if(p.trash){require(!intersects(absolute,p.trash->root),ErrorCode::Conflict,"Recovery trash overlaps another library root");}auto defaults=resolveOriginalStorageTypes(lease,*other.hash,*layout,attrs.root());require(defaults.has_value()&&applyOriginalStorageDefaults(attrs,*defaults,true).has_value(),ErrorCode::Unsupported,"Another recovery physical map is unresolved");auto paths=mapping(*root,*layout,attrs.root());
    for(const auto& path:paths){archivePayload(p,path);const auto actual=inspect(path);for(const auto& f:p.files){require(!intersects(path,f.view.source)&&(f.view.destination.empty()||!intersects(path,f.view.destination)),ErrorCode::Conflict,"Another torrent now shares a selected path");if(actual){require((!f.source||!identity(*actual,*f.source))&&(!f.destination||!identity(*actual,*f.destination))&&(!f.temporary||!identity(*actual,*f.temporary)),ErrorCode::Conflict,"Another torrent now shares selected inode");}}}
  }
}
Plan decode(const Raw& journalRaw){
  require(journalRaw.bytes&&journalRaw.stamp&&journalRaw.stamp->st_nlink==1&&(journalRaw.stamp->st_mode&0077)==0,ErrorCode::UnsafePath,"Recovery journal must be owned private single-link data");auto outer=parse(*journalRaw.bytes,MaximumJournal);require(integer(outer.find("version"))==1,ErrorCode::Unsupported,"Unsupported file-operation journal version");const auto bytes=text(outer.find("payload"));require(digest(bytes)==text(outer.find("sha256")),ErrorCode::InvalidFormat,"Recovery journal checksum mismatch");auto d=parse(bytes,MaximumJournal);Plan p;
  const auto kind=integer(d.find("kind"));require(kind>=0&&kind<=9,ErrorCode::InvalidFormat,"Invalid journal operation");p.preview.kind=static_cast<FileOperationKind>(kind);p.preview.hash=decodeHash(text(d.find("hash")));p.profile=text(d.find("profile"));pathShape(p.profile);p.preview.sourceRoot=text(d.find("source"));pathShape(p.preview.sourceRoot);p.preview.targetRoot=text(d.find("target"));p.destinationParent=text(d.find("parent"));p.targetDevice=unsignedText(d.find("td"));p.targetInode=unsignedText(d.find("ti"));p.scopeSelection=text(d.find("selection"));p.profileDevice=unsignedText(d.find("pd"));p.profileInode=unsignedText(d.find("pi"));p.preview.approvalScopeDigest=text(d.find("scope"));p.preview.executionCasDigest=text(d.find("cas"));require(digestShape(p.preview.approvalScopeDigest)&&digestShape(p.preview.executionCasDigest),ErrorCode::InvalidFormat,"Invalid journal digests");
  require(!intersects(p.profile,p.preview.sourceRoot),ErrorCode::UnsafePath,"Recovery payload overlaps profile");if(moving(p.preview.kind)){pathShape(p.preview.targetRoot);pathShape(p.destinationParent);require((selecting(p.preview.kind)?p.preview.targetRoot==p.preview.sourceRoot&&p.destinationParent==p.preview.sourceRoot.parent_path():p.preview.targetRoot.parent_path()==p.destinationParent&&!intersects(p.preview.sourceRoot,p.preview.targetRoot))&&!intersects(p.profile,p.preview.targetRoot),ErrorCode::UnsafePath,"Recovery destination overlaps protected paths");}
  if(const auto* policy=d.find("filenameConversions")){require(policy->type==Node::Type::Bytes&&policy->bytes.size()<=16384,ErrorCode::InvalidFormat,"Invalid captured filename conversions");p.filenameConversions=std::string(policy->bytes);}
  if(recycling(p.preview.kind)){
    const auto& t=field(d.root(),"trash",Node::Type::Dictionary);require(t.children.size()==4,ErrorCode::InvalidFormat,"Unknown trash intent fields");p.trash=Trash{text(t.find("root")),text(t.find("date"))};validateTrashDate(p.trash->deletionDate);payloadPath(p.trash->root,p.profile);
    require(p.trash->root.filename()=="Trash"&&p.preview.targetRoot==p.trash->root/"files"&&within(p.trash->root,p.destinationParent)&&!intersects(p.trash->root,p.profile)&&!intersects(p.trash->root,p.preview.sourceRoot),ErrorCode::UnsafePath,"Recovery trash scope differs from approved paths");
    if(const auto* dirs=d.find("trashDirectories")){require(dirs->type==Node::Type::List&&dirs->children.size()==3,ErrorCode::InvalidFormat,"Invalid trash directory authority");for(std::size_t n=0;n<3;++n){require(dirs->children[n].type==Node::Type::Dictionary&&dirs->children[n].children.size()==4,ErrorCode::InvalidFormat,"Unknown trash directory authority fields");p.trash->directories[n]={unsignedText(dirs->children[n].find("dev")),unsignedText(dirs->children[n].find("ino"))};require(p.trash->directories[n].second,ErrorCode::InvalidFormat,"Missing trash directory identity");}}
  }else{require(!d.find("trash")&&!d.find("trashDirectories"),ErrorCode::InvalidFormat,"Non-recycle journal contains trash authority");}
  const auto& files=field(d.root(),"files",Node::Type::List);const auto& runtime=field(d.root(),"runtime",Node::Type::List);require(files.children.size()<=MaximumFiles&&runtime.children.size()==files.children.size(),ErrorCode::InvalidFormat,"Invalid recovery file count");std::set<std::size_t> indices;std::set<fs::path> paths;
  for(std::size_t i=0;i<files.children.size();++i){const auto& n=files.children[i];Payload f;const auto index=integer(n.find("i"));require(index>=0&&static_cast<std::uint64_t>(index)<MaximumFiles&&indices.insert(static_cast<std::size_t>(index)).second,ErrorCode::InvalidFormat,"Duplicate recovery file index");f.view.index=static_cast<std::size_t>(index);f.view.source=text(n.find("s"));pathShape(f.view.source);require(paths.insert(f.view.source).second,ErrorCode::InvalidFormat,"Duplicate recovery source");f.view.destination=text(n.find("d"));f.view.logicalBytes=integer(n.find("bytes"));require(f.view.logicalBytes>=0,ErrorCode::InvalidFormat,"Invalid original file length");if(const auto* s=n.find("stamp")){f.source=readStamp(*s);require(f.source->st_nlink==1,ErrorCode::InvalidFormat,"Invalid recovery source stamp");}
    if(const auto* s=n.find("destination")){f.destination=readStamp(*s);require(f.destination->st_nlink==1,ErrorCode::InvalidFormat,"Invalid relink stamp");}
    if(moving(p.preview.kind)||recycling(p.preview.kind)){pathShape(f.view.destination);require((selecting(p.preview.kind)||within(f.view.destination,p.destinationParent))&&!intersects(f.view.destination,p.profile),ErrorCode::UnsafePath,"Recovery destination escapes selected parent");payloadPath(f.view.destination,p.profile);}
    f.sha256=text(runtime.children[i].find("sha"));if((f.source&&p.preview.kind!=FileOperationKind::RemoveKeepData)||(f.destination&&retargeting(p.preview.kind))){require(digestShape(f.sha256),ErrorCode::InvalidFormat,"Missing recovery content digest");}if(const auto* t=runtime.children[i].find("temp")){f.temporary=readStamp(*t);require(f.temporary->st_nlink==1,ErrorCode::InvalidFormat,"Recovery scratch must have one link");}p.files.push_back(std::move(f));
  }
  if(p.preview.kind==FileOperationKind::ReplaceFile){
    const auto& replacement=field(d.root(),"replacement",Node::Type::Dictionary);require((replacement.children.size()==6||replacement.children.size()==8)&&p.files.size()==1&&p.files.front().destination&&p.files.front().source,ErrorCode::InvalidFormat,"Invalid replacement scope");
    p.replacement=Replacement{text(replacement.find("root")),text(replacement.find("sha"))};const auto& f=p.files.front();const auto leaf=".BiglyBTC.replaced."+p.preview.hash.hex()+'.'+std::to_string(f.view.index)+'.'+digest(f.view.destination.string()+':'+std::to_string(f.destination->st_dev)+':'+std::to_string(f.destination->st_ino)).substr(0,16);
    require(replacementRootFor(p.replacement->root,f.view.destination.parent_path(),leaf)&&digestShape(p.replacement->sha256)&&integer(replacement.find("bytes"))==f.destination->st_size,ErrorCode::InvalidFormat,"Replacement preimage binding changed");
    if(const auto* abandoned=replacement.find("unsealed")){p.preview.unsealedPreimageDirectory=text(abandoned);require(replacementRootFor(*p.preview.unsealedPreimageDirectory,f.view.destination.parent_path(),leaf)&&*p.preview.unsealedPreimageDirectory!=p.replacement->root,ErrorCode::InvalidFormat,"Unsealed preimage path differs from original reservation family");}
    require(replacement.children.size()==(p.preview.unsealedPreimageDirectory?8:6),ErrorCode::InvalidFormat,"Unknown replacement intent fields");
    if(const auto* id=d.find("preimageDirectory")){require(id->type==Node::Type::Dictionary&&id->children.size()==4,ErrorCode::InvalidFormat,"Invalid preimage directory authority");p.replacement->directoryDevice=unsignedText(id->find("dev"));p.replacement->directoryInode=unsignedText(id->find("ino"));require(p.replacement->directoryInode,ErrorCode::InvalidFormat,"Missing preimage directory identity");}
    p.preview.replacement=FileOperationPreimage{p.replacement->root/"data",static_cast<std::uint64_t>(f.destination->st_size),p.replacement->sha256};
  }else{require(!d.find("replacement")&&!d.find("preimageDirectory"),ErrorCode::InvalidFormat,"Unexpected replacement authority");}
  if(const auto* controls=d.find("controls")){require(controls->type==Node::Type::List&&controls->children.size()<=MaximumFiles*4,ErrorCode::InvalidFormat,"Invalid recovery control count");std::set<fs::path> seen;for(const auto& n:controls->children){Control c;c.path=text(n.find("path"));pathShape(c.path);require(seen.insert(c.path).second,ErrorCode::InvalidFormat,"Duplicate recovery control");if(const auto* before=n.find("before")){c.before.bytes=text(before);c.before.stamp=readStamp(field(n,"stamp",Node::Type::Dictionary));require(c.before.stamp->st_nlink==1&&c.before.stamp->st_size==static_cast<std::int64_t>(c.before.bytes->size())&&c.before.bytes->size()<=MaximumDocument,ErrorCode::InvalidFormat,"Invalid recovery control stamp");}p.controls.push_back(std::move(c));}}
  if(const auto* node=d.find("archive")){require(node->type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid archive journal");Archive a;a.path=text(node->find("path"));const auto internal=integer(node->find("internal"));require(internal==0||internal==1,ErrorCode::InvalidFormat,"Invalid archive kind");a.internalState=internal!=0;
    if(const auto* bytes=node->find("before")){a.before.bytes=text(bytes);a.before.stamp=readStamp(field(*node,"stamp",Node::Type::Dictionary));}if(!a.internalState){const auto& backup=field(*node,"backup",Node::Type::Dictionary);if(const auto* bytes=backup.find("before")){a.backup.bytes=text(bytes);a.backup.stamp=readStamp(field(backup,"stamp",Node::Type::Dictionary));}}p.archive=std::move(a);archiveShape(p,*p.archive);}
  const auto& edits=field(d.root(),"edits",Node::Type::List);require(edits.children.size()>=2&&edits.children.size()<=5,ErrorCode::InvalidFormat,"Invalid recovery document count");std::set<fs::path> names;const auto active=p.profile/"active"/(p.preview.hash.hex()+".dat");
  for(const auto& n:edits.children){Edit e;e.path=text(n.find("path"));pathShape(e.path);const bool cache=e.path==p.profile/"active/cache.dat",inventory=e.path==p.profile/"downloads.config",state=e.path==active||e.path==fs::path(active.string()+".bak");require((cache||inventory||state)&&names.insert(e.path).second,ErrorCode::UnsafePath,"Recovery document is not an exact fixed owned path");if(const auto* before=n.find("before")){e.before.bytes=text(before);e.before.stamp=readStamp(field(n,"stamp",Node::Type::Dictionary));require(e.before.stamp->st_nlink==1,ErrorCode::InvalidFormat,"Recovery document is linked");}if(const auto* after=n.find("after")){e.after=text(after);}const auto stage=integer(n.find("stage"));e.stage=static_cast<FileOperationStage>(stage);require((cache&&e.after&&e.stage==FileOperationStage::CachePublished)||(inventory&&e.after&&e.stage==FileOperationStage::InventoryPublished)||(state&&((moving(p.preview.kind)&&e.after&&e.stage==FileOperationStage::StatePublished)||(!moving(p.preview.kind)&&!e.after&&e.stage==FileOperationStage::StateRemoved))),ErrorCode::InvalidFormat,"Invalid recovery document stage");p.edits.push_back(std::move(e));}
  require(names.contains(active)&&names.contains(p.profile/"downloads.config"),ErrorCode::InvalidFormat,"Recovery lacks target state/inventory");require(digest(canonical(p,false))==p.preview.executionCasDigest&&digest(canonical(p,true))==p.preview.approvalScopeDigest,ErrorCode::InvalidFormat,"Recovery scope/execution binding mismatch");
  const auto stateEdit=std::ranges::find(p.edits,active,&Edit::path);auto state=envelope(stateEdit->before);const auto cache=std::ranges::find(p.edits,p.profile/"active/cache.dat",&Edit::path);auto cacheDoc=cache==p.edits.end()?std::optional<EncodedDocument>{}:cacheDocument(cache->before);hydrate(state,cacheEntry(cacheDoc?&*cacheDoc:nullptr,p.preview.hash));hashMatches(state,p.preview.hash);
  const auto inventory=std::ranges::find(p.edits,p.profile/"downloads.config",&Edit::path);require(inventory->before.bytes.has_value(),ErrorCode::InvalidFormat,"Recovery original inventory missing");auto inv=parse(*inventory->before.bytes);auto library=parsedInventory(inv);std::optional<core::SavedDownload> selected;for(const auto& row:library.downloads){if(row.hash&&*row.hash==p.preview.hash){require(!selected,ErrorCode::InvalidFormat,"Duplicate recovery target");selected=row;}}
  require(selected.has_value(),ErrorCode::InvalidFormat,"Recovery original target is missing");if(p.archive){require(p.archive->path==fs::path(selected->torrentFile),ErrorCode::UnsafePath,"Recovery archive differs from the selected original record");}auto layout=p.filenameConversions?compat::originalLayout(state.document(),MaximumFiles,*p.filenameConversions):compat::originalLayout(state.document(),MaximumFiles);require(layout.has_value(),ErrorCode::Unsupported,"Unsupported recovery original layout");auto restored=restoreOriginalSaveRoot(*selected,*layout,p.preview.sourceRoot);require(restored.has_value(),ErrorCode::InvalidFormat,"Recovery root differs from original path choices");selected=std::move(*restored);require(!moving(p.preview.kind)||(fileMapping(p.preview.kind)?p.preview.targetRoot==p.preview.sourceRoot:p.preview.targetRoot==p.destinationParent/ *selected->saveFile),ErrorCode::InvalidFormat,"Recovery destination root changed");auto attrs=section(state.document().find("attributes"));restoreStorageProjection(attrs,p,*layout);const auto map=mapping(*selected,*layout,attrs.root());const auto suffix=attrs.find("incompfilesuffix")?text(attrs.find("incompfilesuffix")):std::string{};
  std::vector<compat::FilePolicy> previousPolicy,nextPolicy;
  if(selecting(p.preview.kind)){
    previousPolicy=selectionPolicies(inventoryRow(inv,*selected),*layout,false);
    const auto scope=parse('d'+p.scopeSelection+'e',MaximumJournal);const auto policy=text(scope.find("BiglyBTC.selection.policy"));
    auto changed=parse(encoded(inventoryRow(inv,*selected)));put(changed,"file_priorities_c",policy);nextPolicy=selectionPolicies(changed.root(),*layout);
    const auto after=parse(*inventory->after);const auto afterLibrary=parsedInventory(after);const core::SavedDownload* chosen=nullptr;
    for(const auto& row:afterLibrary.downloads){if(row.hash&&*row.hash==p.preview.hash){require(!chosen,ErrorCode::InvalidFormat,"Duplicate selection recovery row");chosen=&row;}}
    require(chosen&&encoded(inventoryRow(after,*chosen))==changed.serialize(),ErrorCode::InvalidFormat,"Selection recovery changed unrelated selected-row fields");
  }
  if(deleting(p.preview.kind)){if(const auto* flags=attrs.find("flags")){require(flags->type==Node::Type::Integer&&(flags->integer&0x40)==0,ErrorCode::Unsupported,"Original flags prohibit payload deletion");}}
  for(const auto& f:p.files){archivePayload(p,f.view.source);require(f.view.index<layout->files.size()&&!layout->files[f.view.index].padding&&f.view.logicalBytes==layout->files[f.view.index].bytes,ErrorCode::InvalidFormat,"Recovery file geometry changed");payloadPath(f.view.source,p.profile);const auto expected=map[f.view.index];const auto base=fs::path(selected->saveDirectory)/(layout->multifile?fs::path(*selected->saveFile)/layout->files[f.view.index].path:fs::path(*selected->saveFile));require(f.view.source==expected||(expected==base&&!suffix.empty()&&f.view.source==fs::path(base.string()+suffix)),ErrorCode::UnsafePath,"Recovery source differs from original file mapping");if(recycling(p.preview.kind)){require(f.view.destination.parent_path()==p.trash->root/"files"&&trashLeafFor(p,f),ErrorCode::UnsafePath,"Recovery trash leaf differs from approved original file");}
    if(fileMapping(p.preview.kind)){require(p.files.size()==1&&within(f.view.destination.parent_path(),p.destinationParent)&&f.view.destination!=f.view.source,ErrorCode::InvalidFormat,"Recovery per-file scope is invalid");
      if(p.preview.kind==FileOperationKind::RevertFile){require(f.view.destination==base,ErrorCode::UnsafePath,"Revert recovery must restore original canonical path");}
      for(std::size_t n=0;n<map.size();++n){if(n!=f.view.index&&!layout->files[n].padding){require(!intersects(f.view.destination,map[n]),ErrorCode::Conflict,"Recovery per-file target overlaps another file");}}
      if(retargeting(p.preview.kind)){require(f.destination&&storageKind(attrs.root(),f.view.index,layout->files.size())==OriginalStorageKind::Linear,ErrorCode::InvalidFormat,"Recovery retarget lost linear target authority");}
    }else if(moving(p.preview.kind)){require(f.view.destination==(selecting(p.preview.kind)?selectionDestination(*selected,*layout,attrs.root(),f.view.index,f.view.source,previousPolicy[f.view.index],nextPolicy[f.view.index]):relocatedPath(*selected,*layout,f.view.index,f.view.source,p.destinationParent,suffix)),ErrorCode::UnsafePath,"Recovery destination differs from approved mapping");if(selecting(p.preview.kind)){selectionTargetIndependent(f.view.destination,f.view.index,map,*selected,*layout,suffix);}}}
  if(!p.controls.empty()){const auto expected=controlPaths(p.profile,p.preview.hash,layout->files.size());require(expected.size()==p.controls.size(),ErrorCode::InvalidFormat,"Recovery control inventory changed");for(std::size_t i=0;i<expected.size();++i){require(expected[i]==p.controls[i].path,ErrorCode::UnsafePath,"Recovery control is not an exact original indexed state leaf");}}else{for(std::size_t i=0;i<layout->files.size();++i){require(storageKind(attrs.root(),i,layout->files.size())==OriginalStorageKind::Linear,ErrorCode::InvalidFormat,"Legacy journal cannot omit nonlinear control inventory");}}
  captureControlGeometry(p,*layout);validateCompletionControls(p.controls,p.controlGeometry);
  for(const auto& f:p.files){const auto maximum=physicalMaximum(*layout,attrs.root(),f.view.index);if(f.source){ownedPayload(*f.source,maximum);}if(f.destination){ownedPayload(*f.destination,p.preview.kind==FileOperationKind::ReplaceFile?std::numeric_limits<std::int64_t>::max():maximum);}}
  p.journalCreated=true;return p;
}
}
namespace {
void validateLegacyNaming(const ProfileLease& lease,const Plan& p){
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
std::expected<PreparedFileOperation,Error> prepareFileOperation(const ProfileLease& lease,const FileOperationRequest& request,std::stop_token stop){return guarded([&]()->std::expected<PreparedFileOperation,Error>{require(!hasPendingOriginalCompaction(lease),ErrorCode::Conflict,"Recover pending explicit compaction before file operations");return PreparedFileOperation(std::make_unique<PreparedFileOperation::Impl>(prepare(lease,request,stop)));});}
std::expected<PreparedFileOperation,Error> prepareInterruptedReplacementReview(const ProfileLease& lease,std::stop_token stop){
  return guarded([&]()->std::expected<PreparedFileOperation,Error>{
    checkpoint(stop);require(!hasPendingOriginalCompaction(lease)&&!hasPendingTorrentIntake(lease),ErrorCode::Conflict,"Another profile recovery must finish first");
    const auto profile=fs::path(profilePath(lease));require(!inspect(profile/(std::string(JournalName)+".BiglyBTC.fileop-saving")),ErrorCode::Conflict,"Unpublished journal needs separate review");
    auto old=read(profile/JournalName,MaximumJournal);auto p=decode(old);profileIdentity(lease,p);targetIdentity(p);validateLegacyNaming(lease,p);validateCurrentIndependence(lease,p);
    require(p.preview.kind==FileOperationKind::ReplaceFile&&p.replacement&&!p.replacement->directoryInode&&p.files.size()==1&&!p.archive,ErrorCode::Unsupported,"Only an unsealed untouched replacement can be replanned");
    struct stat unsealed{};require(::lstat(p.replacement->root.c_str(),&unsealed)==0&&S_ISDIR(unsealed.st_mode),ErrorCode::Conflict,"No uncertain preimage directory needs review");
    if(p.filenameConversions){require(expectedValue(readOriginalFilenameConversions(lease))==*p.filenameConversions,ErrorCode::Conflict,"Original filename policy changed");}
    // Exact before-only checks. Once any payload, control, or document effect
    // has occurred, this path is unavailable; ordinary recovery stays required.
    checkControls(p);for(const auto& e:p.edits){check(e.path,e.before,MaximumDocument);}auto& f=p.files.front();
    require(f.source&&f.destination&&!f.temporary,ErrorCode::Conflict,"Replacement source authority is incomplete");
    for(const auto& pair:{std::pair{f.view.source,*f.source},std::pair{f.view.destination,*f.destination}}){const auto now=inspect(pair.first);require(now&&unchanged(*now,pair.second),ErrorCode::Conflict,"Original replacement preimage changed");}
    require(p.payloadDigests.digest(f.view.source,*f.source,stop)==f.sha256&&p.payloadDigests.digest(f.view.destination,*f.destination,stop)==p.replacement->sha256,ErrorCode::Conflict,"Original replacement bytes changed");
    const auto abandoned=p.replacement->root;
    const auto leaf=".BiglyBTC.replaced."+p.preview.hash.hex()+'.'+std::to_string(f.view.index)+'.'+digest(f.view.destination.string()+':'+std::to_string(f.destination->st_dev)+':'+std::to_string(f.destination->st_ino)).substr(0,16);
    fs::path fresh;for(unsigned n=1;n<=1024;++n){const auto candidate=f.view.destination.parent_path()/(leaf+".retry."+std::to_string(n));if(!directoryExists(candidate)){fresh=candidate;break;}}
    require(!fresh.empty(),ErrorCode::Conflict,"Fresh preimage reservation bound exhausted");p.replacement->root=fresh;p.preview.unsealedPreimageDirectory=abandoned;p.preview.replacement->path=fresh/"data";
    f.view.present=true;f.view.actualBytes=f.source->st_size;f.view.device=f.source->st_dev;f.view.inode=f.source->st_ino;p.preview.files={f.view};p.preview.presentFiles=1;p.preview.presentBytes=f.source->st_size;p.preview.requiresRecheck=true;
    p.preview.approvalScopeDigest=digest(canonical(p,true));p.preview.executionCasDigest=digest(canonical(p,false));p.reviewedJournal=std::move(old);p.journalCreated=false;
    checkpoint(stop);return PreparedFileOperation(std::make_unique<PreparedFileOperation::Impl>(std::move(p)));
  });
}
std::expected<FileOperationConfirmation,Error> confirmFileOperation(const PreparedFileOperation& p,std::string_view accepted,bool deleteConfirmed,bool archiveConfirmed){return guarded([&]()->std::expected<FileOperationConfirmation,Error>{require(accepted==p.preview().approvalScopeDigest,ErrorCode::Conflict,"File-operation approved scope changed");require(!(deleting(p.preview().kind)||p.preview().kind==FileOperationKind::ReplaceFile)||deleteConfirmed,ErrorCode::Conflict,"Explicit payload deletion confirmation is required");require(!p.preview().torrentArchive||archiveConfirmed,ErrorCode::Conflict,"Explicit reviewed torrent archive confirmation is required");return FileOperationConfirmation{std::string(accepted),deleteConfirmed,archiveConfirmed};});}
std::expected<FileOperationResult,Error> applyFileOperation(const ProfileLease& lease,PreparedFileOperation&& prepared,const FileOperationConfirmation& confirmation,void(*observer)(FileOperationStage),std::stop_token stop){
  return guarded([&]()->std::expected<FileOperationResult,Error>{require(!hasPendingOriginalCompaction(lease),ErrorCode::Conflict,"Recover pending explicit compaction before applying file operations");require(bool(prepared.impl_),ErrorCode::Conflict,"Prepared file operation was moved/consumed");auto& p=prepared.impl_->plan;
    // The rvalue reference does not destroy the caller's prepared object. Drop
    // every transient pinned fd on success and exception, leaving its attempted
    // state/journal/retry semantics unchanged.
    struct ClearDigests {PayloadDigestCache& cache;~ClearDigests(){cache.clear();}} clearDigests{p.payloadDigests};
    checkpoint(stop);require(!p.attempted&&confirmation.approvalScopeDigest==p.preview.approvalScopeDigest&&(!(deleting(p.preview.kind)||p.preview.kind==FileOperationKind::ReplaceFile)||confirmation.deleteConfirmed)&&(!p.archive||confirmation.archiveConfirmed),ErrorCode::Conflict,"File-operation confirmation is stale or missing");profileIdentity(lease,p);targetIdentity(p);if(p.reviewedJournal){check(p.profile/JournalName,*p.reviewedJournal,MaximumJournal);require(!inspect(p.profile/(std::string(JournalName)+".BiglyBTC.fileop-saving")),ErrorCode::Conflict,"Journal publication is uncertain");validateCurrentIndependence(lease,p);}else{require(!hasPendingFileOperation(lease),ErrorCode::Conflict,"Existing file operation needs recovery");}
    if(p.filenameConversions){require(expectedValue(readOriginalFilenameConversions(lease))==*p.filenameConversions,ErrorCode::Conflict,"Original filename policy changed after preparation");}
    if(p.archive){checkArchive(*p.archive,false);}checkControls(p);for(const auto& e:p.edits){check(e.path,e.before,MaximumDocument);}for(auto it=p.files.rbegin();it!=p.files.rend();++it){auto& f=*it;checkpoint(stop);const auto& expected=retargeting(p.preview.kind)?f.destination:f.source;const auto& path=retargeting(p.preview.kind)?f.view.destination:f.view.source;const auto now=inspect(path);require(bool(now)==bool(expected)&&(!now||unchanged(*now,*expected)),ErrorCode::Conflict,"Payload changed after preparation");if(expected&&p.preview.kind!=FileOperationKind::RemoveKeepData){f.sha256=p.payloadDigests.digest(path,*expected,stop);}}
    if(p.replacement){checkReplacementBeforeJournal(p,stop);}
    checkpoint(stop);p.attempted=true;if(p.reviewedJournal){
      // Replace ONLY the exact confirmed old journal, before any new payload or
      // profile effect. The old uncertain directory is never adopted or removed.
      publish(p.profile/JournalName,*p.reviewedJournal,journal(p),MaximumJournal);p.reviewedJournal.reset();p.journalCreated=true;
    }else{saveJournal(p);}if(observer){observer(FileOperationStage::JournalSynced);}return execute(lease,p,false,observer);
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
std::expected<fs::path,Error> linuxHomeTrashRoot(){return guarded([]()->std::expected<fs::path,Error>{
  const char* data=std::getenv("XDG_DATA_HOME");fs::path root;
  if(data&&*data){root=fs::path(data);require(root.is_absolute(),ErrorCode::UnsafePath,"XDG_DATA_HOME must be absolute for recycling");}
  else{const char* home=std::getenv("HOME");require(home&&*home,ErrorCode::Missing,"Home trash location is unavailable");root=fs::path(home)/".local/share";}
  pathShape(root);return root/"Trash";
});}
bool hasPendingFileOperation(const ProfileLease& lease) noexcept {
  try{const auto profile=fs::path(profilePath(lease));return inspect(profile/JournalName).has_value()||inspect(profile/(std::string(JournalName)+".BiglyBTC.fileop-saving")).has_value();}catch(...){return true;}
}
std::expected<std::optional<FileOperationResult>,Error> recoverFileOperation(const ProfileLease& lease,void(*observer)(FileOperationStage)){
  return guarded([&]()->std::expected<std::optional<FileOperationResult>,Error>{require(!hasPendingOriginalCompaction(lease),ErrorCode::Conflict,"Recover pending explicit compaction before file-operation recovery");const auto profile=fs::path(profilePath(lease));auto raw=read(profile/JournalName,MaximumJournal);if(!raw.bytes){require(!hasPendingFileOperation(lease),ErrorCode::Conflict,"Unpublished file-operation journal requires explicit review");return std::nullopt;}auto p=decode(raw);profileIdentity(lease,p);validateLegacyNaming(lease,p);validateCurrentIndependence(lease,p);
    // Check every affected document before any further data mutation. Sparse
    // target row/pauses/cache merges tolerate unrelated owner checkpoints.
    for(const auto& e:p.edits){const auto now=read(e.path,MaximumDocument);if(e.after){static_cast<void>(mergedDocument(p,e,now));}else if(now.bytes){require(now.bytes==e.before.bytes&&now.stamp&&e.before.stamp&&unchanged(*now.stamp,*e.before.stamp),ErrorCode::Conflict,"Removal state changed during recovery");}}
    return std::optional<FileOperationResult>{execute(lease,p,true,observer)};
  });
}
} // namespace biglybtc::storage
