#include "../platform/utf8_path.hpp"
#include "original_payload_io.hpp"
#include "original_filename_policy.hpp"
#include "linear_to_compact.hpp"
#include "original_selection_group.hpp"
#include "encoded_document.hpp"
#include "file_operation_io.hpp"
#include "file_policy_commands.hpp"
#include "original_storage_defaults.hpp"
#include "torrent_state.hpp"
#include "biglybtc/file_policy.hpp"
#include "biglybtc/original_file_links.hpp"
#include "biglybtc/torrent_layout.hpp"

#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <climits>
#include <span>
#include <map>
#include <memory>
#include <set>

namespace biglybtc::storage {
namespace {
namespace fs = std::filesystem;
using namespace fileop;
using platform::pathFromUtf8;
using platform::pathToUtf8;
using detail::InvalidStateHandle;
using Node = core::Node;
constexpr std::size_t MaximumMetadata = 16 * 1024 * 1024;
constexpr std::size_t MaximumDocument = 4 * 1024 * 1024;
constexpr std::uint64_t MaximumTail = 256ULL * 1024 * 1024;
constexpr std::string_view Journal = ".BiglyBTC.linear-to-compact";
constexpr std::string_view Saving = ".BiglyBTC.linear-to-compact.saving";
constexpr std::string_view Magic = "BIGLYBTC-LINEAR-COMPACT-1\n";
using Observer = void (*)(OriginalCompactionStage);

template<class T> T take(std::expected<T, Error> value) {
  if (!value) { throw Failure{value.error()}; }
  return std::move(*value);
}
void done(std::expected<void, Error> value) { if (!value) { throw Failure{value.error()}; } }
template<class F> auto guarded(F&& f) -> decltype(f()) {
  try { return f(); }
  catch (const Failure& e) { return std::unexpected(e.error); }
  catch (...) { return std::unexpected(Error{ErrorCode::Io, "Compaction failed; check pending recovery before admission"}); }
}
void observe(Observer callback, OriginalCompactionStage stage) { if (callback) { callback(stage); } }
void cancelled(std::stop_token stop) { require(!stop.stop_requested(), ErrorCode::Cancelled, "Compaction cancelled before journal publication"); }
std::string b(std::string_view s) { return std::to_string(s.size()) + ':' + std::string(s); }
std::string i(std::uint64_t n) {
  require(n <= static_cast<std::uint64_t>(INT64_MAX), ErrorCode::Unsupported, "Compaction number exceeds original signed range");
  return 'i' + std::to_string(n) + 'e';
}
std::string dict(const std::map<std::string, std::string>& fields) {
  std::string out = "d";
  for (const auto& [key, value] : fields) { out += b(key) + value; }
  return out + 'e';
}
core::Document parse(std::string bytes, std::size_t maximum = MaximumMetadata) {
  auto result = core::Document::parse(std::move(bytes), {.inputBytes=maximum,.stringBytes=maximum,.nodes=400000,.depth=128});
  require(result && result->root().type == Node::Type::Dictionary, ErrorCode::InvalidFormat, "Invalid compaction dictionary");
  return std::move(*result);
}
std::string encoded(const Node& n) {
  auto result = core::encodeValue(n, compatibleConfigLimits());
  require(result.has_value(), ErrorCode::InvalidFormat, "Cannot encode compaction field");
  return std::move(*result);
}
core::Document section(const Node* n) { return parse(n ? encoded(*n) : "de"); }
void put(core::Document& d, std::string_view key, std::string value) {
  require(d.replaceValue(key, std::move(value)).has_value(), ErrorCode::InvalidFormat, "Compaction edit exceeds document limits");
}
const Node& field(const Node& n, std::string_view key, Node::Type type) {
  const auto* result = n.find(key);
  require(result && result->type == type, ErrorCode::InvalidFormat, "Missing or malformed compaction field");
  return *result;
}
std::uint64_t number(const Node& n, std::string_view key) {
  const auto value = field(n, key, Node::Type::Integer).integer;
  require(value >= 0, ErrorCode::InvalidFormat, "Negative compaction value");
  return static_cast<std::uint64_t>(value);
}
std::string text(const Node& n, std::string_view key) { return std::string(field(n, key, Node::Type::Bytes).bytes); }
std::optional<std::string> optionalBytes(const Node& n, std::string_view key) {
  const auto* value = n.find(key);
  require(value && (value->type == Node::Type::Bytes || (value->type == Node::Type::List && value->children.empty())), ErrorCode::InvalidFormat, "Invalid optional compaction snapshot");
  if (value->type == Node::Type::List) { return {}; }
  return std::string(value->bytes);
}
std::string optional(const std::optional<std::string>& value) { return value ? b(*value) : "le"; }
using Md = Sha256Context;
void add(Md& md, std::string_view data) {
  require(EVP_DigestUpdate(md.get(), data.data(), data.size()) == 1, ErrorCode::Io, "Cannot hash compaction bytes");
}
void readAll(FileHandle fd, std::uint64_t offset, std::span<char> data) {
  const auto maximum = detail::originalPayloadMaximumOffset();
  require(data.size() <= maximum && offset <= maximum - data.size(), ErrorCode::Unsupported, "Compaction offset overflow");
  const auto bytes = std::as_writable_bytes(data);
  for (std::size_t used = 0; used < bytes.size();) {
    const auto count = detail::readOriginalPayload(fd, offset + used, bytes.subspan(used));
    require(count > 0, ErrorCode::Io, "Compaction source ended before its frozen bound");
    used += static_cast<std::size_t>(count);
  }
}
void writeAll(FileHandle fd, std::uint64_t offset, std::span<const char> data) {
  const auto maximum = detail::originalPayloadMaximumOffset();
  require(data.size() <= maximum && offset <= maximum - data.size(), ErrorCode::Unsupported, "Compaction write offset overflow");
  const auto bytes = std::as_bytes(data);
  for (std::size_t used = 0; used < bytes.size();) {
    const auto count = detail::writeOriginalPayload(fd, offset + used, bytes.subspan(used));
    require(count > 0, ErrorCode::Io, "Cannot write compaction transaction");
    used += static_cast<std::size_t>(count);
  }
}
// Fixed scratch regardless of the original piece/tail bound. No missing byte
// is synthesized: any unexpected short read aborts with the journal intact.
void stream(FileHandle input, std::uint64_t start, std::uint64_t count, FileHandle output,
    std::uint64_t destination, Md& md, std::stop_token stop = {}) {
  std::array<char, 65536> buffer{};
  for (std::uint64_t used = 0; used < count;) {
    cancelled(stop);
    const auto size = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), count-used));
    readAll(input, start+used, std::span<char>(buffer.data(), size));
    add(md, std::string_view(buffer.data(), size));
    if (output != InvalidStateHandle) { writeAll(output, destination+used, std::span<const char>(buffer.data(), size)); }
    used += size;
  }
}
struct Geometry {
  std::uint64_t first = 0, tailStart = 0, tail = 0;
};
Geometry edges(OriginalFileGeometry g) {
  require(g.pieceBytes && g.pieceBytes <= MaximumTail && g.fileBytes <= static_cast<std::uint64_t>(INT64_MAX), ErrorCode::Unsupported, "Unsupported original compact geometry");
  const auto first = std::min(g.fileBytes, (g.pieceBytes-g.torrentOffset%g.pieceBytes)%g.pieceBytes);
  const auto tail = (g.fileBytes-first)%g.pieceBytes;
  return {first, g.fileBytes-tail, tail};
}
Fd openPayload(const fs::path& path, const FileStamp& expected, bool writing) {
  const auto parent = directory(path.parent_path());
  using Access = detail::OriginalPayloadAccess;
  auto file = take(detail::openOriginalPayloadAt(parent.get(), path.filename(),
      writing ? Access::ReadWrite : Access::ReadOnly, 1, true));
  const auto held = heldFileStamp(file.get());
  require(unchanged(held, expected), ErrorCode::Conflict,
      "Compaction payload changed or is not exclusively owned regular data");
  return Fd(file.release());
}
std::string rangeHash(FileHandle fd, std::uint64_t start, std::uint64_t count) {
  auto md = beginSha256(); stream(fd, start, count, InvalidStateHandle, 0, md); return finishSha256(md);
}
struct Edit {
  fs::path path;
  std::optional<std::string> before;
  std::string after;
};
struct Guard { fs::path path; std::optional<std::string> sha; };
struct Plan {
  fs::path profile, source;
  core::TorrentHash hash;
  std::size_t index = 0;
  std::uint64_t profileDevice = 0, profileInode = 0, sourceDevice = 0, sourceInode = 0;
  std::uint64_t oldLength = 0, prefix = 0, tailStart = 0, tail = 0;
  std::string prefixHash;
  bool present = false;
  std::vector<Edit> controls, state, inventory;
  std::vector<Guard> guards;
  std::optional<std::string> filenameConversions;
  std::optional<bool> decoderShowAll;
  std::uint64_t packed() const { return prefix+tail; }
};
std::string encodeEdits(const std::vector<Edit>& edits) {
  std::string out = "l";
  for (const auto& e : edits) { out += dict({{"path",b(pathToUtf8(e.path))},{"before",optional(e.before)},{"after",b(e.after)}}); }
  return out+'e';
}
std::string metadata(const Plan& p) {
  std::string guards = "l";
  for (const auto& g : p.guards) { guards += dict({{"path",b(pathToUtf8(g.path))},{"sha",optional(g.sha)}}); }
  auto result=dict({{"version",i(1)},{"profile",b(pathToUtf8(p.profile))},{"profileDevice",i(p.profileDevice)},{"profileInode",i(p.profileInode)},
    {"source",b(pathToUtf8(p.source))},{"present",i(p.present)},{"sourceDevice",i(p.sourceDevice)},{"sourceInode",i(p.sourceInode)},
    {"hash",b(p.hash.hex())},{"index",i(p.index)},{"oldLength",i(p.oldLength)},
    {"prefix",i(p.prefix)},{"tailStart",i(p.tailStart)},{"tail",i(p.tail)},{"prefixHash",b(p.prefixHash)},
    {"controls",encodeEdits(p.controls)},{"state",encodeEdits(p.state)},{"inventory",encodeEdits(p.inventory)},{"guards",guards+'e'}});
  if(p.filenameConversions){auto document=parse(result);put(document,"filenameConversions",b(*p.filenameConversions));result=document.serialize();}
  if(p.decoderShowAll){auto document=parse(result);put(document,"decoderShowAll",i(*p.decoderShowAll?1:0));result=document.serialize();}return result;
}
std::vector<Edit> decodeEdits(const Node& root, std::string_view key, const fs::path& profile) {
  const auto& list = field(root, key, Node::Type::List); require(list.children.size() <= 2, ErrorCode::InvalidFormat, "Too many compaction edits");
  std::vector<Edit> out;
  for (const auto& item : list.children) {
    auto path = pathFromUtf8(text(item, "path")); pathShape(path);
    require(path.parent_path() == profile || path.parent_path() == profile/"active" || path.parent_path().parent_path() == profile/"active", ErrorCode::UnsafePath, "Compaction edit leaves profile");
    auto before = optionalBytes(item, "before"); auto after = text(item, "after");
    require((!before || before->size() <= MaximumDocument) && after.size() <= MaximumDocument, ErrorCode::InvalidFormat, "Compaction edit too large");
    out.push_back({std::move(path),std::move(before),std::move(after)});
  }
  return out;
}
Plan decode(const core::Document& d) {
  const auto& root = d.root(); require(number(root, "version") == 1, ErrorCode::InvalidFormat, "Unknown compaction journal version");
  const auto hex = text(root, "hash");
  require(hex.size() == 40 || hex.size() == 64, ErrorCode::InvalidFormat, "Invalid compaction hash length");
  std::string raw(hex.size()/2, '\0');
  for (std::size_t pos=0; pos<raw.size(); ++pos) {
    const auto digit = [](char c) { return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; };
    const auto a=digit(hex[pos*2]), z=digit(hex[pos*2+1]); require(a>=0&&z>=0, ErrorCode::InvalidFormat, "Invalid compaction hash"); raw[pos]=static_cast<char>((a<<4)|z);
  }
  auto id=core::TorrentHash::parse(raw); require(id&&id->hex()==hex, ErrorCode::InvalidFormat, "Invalid or noncanonical compaction identity");
  std::size_t conversionFields=0;for(std::size_t n=0;n<root.children.size();n+=2){conversionFields+=root.children[n].bytes=="filenameConversions";}require(conversionFields<=1,ErrorCode::InvalidFormat,"Duplicate compaction filename policy");
  Plan p{};if(root.find("filenameConversions")){p.filenameConversions=text(root,"filenameConversions");require(p.filenameConversions->size()<=16384,ErrorCode::InvalidFormat,"Oversized compaction filename policy");}
  std::size_t decoderFields=0;for(std::size_t n=0;n<root.children.size();n+=2){decoderFields+=root.children[n].bytes=="decoderShowAll";}require(decoderFields<=1,ErrorCode::InvalidFormat,"Duplicate compaction decoder context");
  if(root.find("decoderShowAll")){const auto value=number(root,"decoderShowAll");require(value<=1&&p.filenameConversions.has_value(),ErrorCode::InvalidFormat,"Invalid compaction decoder context");p.decoderShowAll=value==1;}
  p.profile=pathFromUtf8(text(root,"profile"));p.source=pathFromUtf8(text(root,"source"));p.hash=*id;
  pathShape(p.profile); pathShape(p.source); p.index=number(root,"index");
  p.profileDevice=number(root,"profileDevice"); p.profileInode=number(root,"profileInode");
  p.sourceDevice=number(root,"sourceDevice"); p.sourceInode=number(root,"sourceInode");
  p.oldLength=number(root,"oldLength"); p.prefix=number(root,"prefix"); p.tailStart=number(root,"tailStart"); p.tail=number(root,"tail");
  const auto present=number(root,"present"); require(present<=1, ErrorCode::InvalidFormat, "Invalid compaction presence"); p.present=present;
  require(p.index<10000 && p.tail<=MaximumTail && p.prefix<=p.oldLength && p.tailStart<=p.oldLength
      && p.tail<=p.oldLength-p.tailStart && p.prefix<=p.tailStart && p.packed()<=p.oldLength
      && (p.present || (!p.oldLength&&!p.sourceDevice&&!p.sourceInode)), ErrorCode::InvalidFormat, "Invalid compaction byte bounds");
  p.prefixHash=text(root,"prefixHash"); require(p.prefixHash.size()==64,ErrorCode::InvalidFormat,"Invalid compaction prefix digest");
  p.controls=decodeEdits(root,"controls",p.profile); p.state=decodeEdits(root,"state",p.profile); p.inventory=decodeEdits(root,"inventory",p.profile);
  const auto statePath=p.profile/"active"/(hex+".dat");
  require(p.state.size()==2 && p.state[0].path==pathFromUtf8(pathToUtf8(statePath)+".bak") && p.state[1].path==statePath
      && p.inventory.size()==2 && p.inventory[0].path==p.profile/"downloads.config.bak" && p.inventory[1].path==p.profile/"downloads.config",
      ErrorCode::UnsafePath,"Compaction journal has unexpected document targets");
  const auto control=p.profile/"active"/hex/originalControlLeaf(p.index,OriginalStorageKind::Compact);
  require(p.controls.size()==(p.present?1U:0U) && (!p.present || (p.controls[0].path==control&&!p.controls[0].before)), ErrorCode::UnsafePath,"Unexpected compaction control target");
  const auto& list=field(root,"guards",Node::Type::List); require(list.children.size()<=60010,ErrorCode::InvalidFormat,"Compaction guard bound");
  std::set<fs::path,detail::FilesystemPathLess> unique;
  for(const auto& item:list.children){auto path=pathFromUtf8(text(item,"path"));pathShape(path);
    require((path.parent_path()==p.profile||path.parent_path()==p.profile/"active"||path.parent_path()==p.profile/"active"/hex)&&unique.insert(path).second,ErrorCode::UnsafePath,"Invalid compaction guard path");
    auto sha=optionalBytes(item,"sha");require(!sha||sha->size()==64,ErrorCode::InvalidFormat,"Invalid compaction guard digest");p.guards.push_back({std::move(path),std::move(sha)});}
  return p;
}
void identity(const ProfileLease& lease, const Plan& p) {
  require(pathFromUtf8(profilePath(lease))==p.profile,ErrorCode::Conflict,"Compaction belongs to another held profile");
  const auto s=heldFileStamp(lease.directoryHandle());require(s.device==p.profileDevice&&s.inode==p.profileInode,ErrorCode::Conflict,"Compaction profile was replaced");
}
void verifyEdits(const std::vector<Edit>& edits) {
  for(const auto& e:edits){auto now=read(e.path,MaximumDocument);require(now.bytes==e.before||now.bytes==std::optional<std::string>(e.after),ErrorCode::Conflict,"Compaction document changed outside transaction");}
}
void verifyGuards(const Plan& p) {
  for(const auto& g:p.guards){const auto now=read(g.path,MaximumDocument);require((now.bytes?std::optional<std::string>(digest(*now.bytes)):std::nullopt)==g.sha,ErrorCode::Conflict,"Another original state changed during pending compaction");}
}
// Temp bytes are authorized only by a complete verified journal and exact
// destination CAS. A crash-short prefix can be rewritten, never reinterpreted.
bool privateCompactionFile(FileHandle file,const FileStamp& held) {
#ifndef _WIN32
  if((held.mode&0777)!=0600){return false;}
#endif
  return held.regular&&held.owned&&held.links==1&&privateEntry(file);
}
void publishEdit(const Edit& edit) {
  const auto before=read(edit.path,MaximumDocument);
  const auto parent=directory(edit.path.parent_path());
  const auto leaf=edit.path.filename();
  auto temporary=leaf;
  temporary+=".BiglyBTC.compact-saving";
  if(before.bytes==std::optional<std::string>(edit.after)){
    require(!inspect(edit.path.parent_path()/temporary),ErrorCode::Conflict,"Unexpected temporary beside published compaction document");
    check(edit.path,before,MaximumDocument);
#ifdef _WIN32
    const auto existing=take(detail::openWritableStateFile(parent.get(),leaf,false));
#else
    const auto existing=take(detail::openStateFile(parent.get(),leaf));
#endif
    require(before.stamp&&unchanged(heldFileStamp(existing.get()),*before.stamp),ErrorCode::Conflict,
        "Published compaction document changed before resync");
    sync(existing.get());sync(parent.get());return;
  }
  require(before.bytes==edit.before,ErrorCode::Conflict,"Compaction publication lost its raw snapshot");
  auto staged=detail::openWritableStateFile(parent.get(),temporary,true);
  if(!staged&&staged.error().code==ErrorCode::Conflict){
    const auto old=read(edit.path.parent_path()/temporary,MaximumDocument);
    require(old.bytes&&old.stamp&&edit.after.starts_with(*old.bytes),ErrorCode::Conflict,
        "Compaction temporary is not an authorized prefix");
    staged=detail::openWritableStateFile(parent.get(),temporary,false);
    require(staged&&unchanged(heldFileStamp(staged->get()),*old.stamp),ErrorCode::Conflict,
        "Compaction temporary changed before recovery");
  }
  const auto file=take(std::move(staged));
  require(privateCompactionFile(file.get(),heldFileStamp(file.get())),ErrorCode::UnsafePath,"Unsafe compaction temporary");
  writeAll(file.get(),0,edit.after);
  require(detail::resizeOriginalPayload(file.get(),edit.after.size()),ErrorCode::Io,"Cannot bound compaction document");
  sync(file.get());
  const auto held=heldFileStamp(file.get());
  check(edit.path,before,MaximumDocument);
  done(detail::publishStateFile(parent.get(),temporary,leaf,file.get(),before.bytes.has_value(),&held));
  sync(parent.get());
}
void publish(const std::vector<Edit>& edits) { for(const auto& e:edits){publishEdit(e);} }
struct JournalFile {Fd fd;FileStamp stamp{};Plan plan;std::uint64_t bodyOffset=0;};
JournalFile openJournal(const ProfileLease& lease) {
  const auto path=pathFromUtf8(profilePath(lease))/Journal;
  const auto before=inspect(path);require(before.has_value(),ErrorCode::Missing,"Compaction journal disappeared");
  auto file=openPayload(path,*before,false);
  const auto actual=heldFileStamp(file.get());
  require(privateCompactionFile(file.get(),actual),ErrorCode::UnsafePath,"Invalid compaction journal ownership");
  std::array<char,8> size{};std::string magic(Magic.size(),'\0');readAll(file.get(),0,magic);readAll(file.get(),Magic.size(),size);
  require(magic==Magic,ErrorCode::InvalidFormat,"Invalid compaction journal header");
  std::uint64_t length=0;for(const unsigned char c:size){length=(length<<8)|c;}
  require(length<=MaximumMetadata,ErrorCode::InvalidFormat,"Compaction metadata bound exceeded");
  std::string bytes(static_cast<std::size_t>(length),'\0');readAll(file.get(),Magic.size()+8,bytes);
  auto plan=decode(parse(bytes));identity(lease,plan);
  const auto body=Magic.size()+8+length;
  require(static_cast<std::uint64_t>(actual.size)==body+plan.tail+64,ErrorCode::InvalidFormat,"Compaction journal body is truncated or has trailing bytes");
  auto md=beginSha256();stream(file.get(),0,body+plan.tail,InvalidStateHandle,0,md);std::array<char,64> footer{};readAll(file.get(),body+plan.tail,footer);
  require(finishSha256(md)==std::string_view(footer.data(),footer.size()),ErrorCode::InvalidFormat,"Compaction journal digest mismatch");
  const auto after=heldFileStamp(file.get());const auto named=inspect(path);
  require(named&&unchanged(actual,after)&&unchanged(actual,*named),ErrorCode::Conflict,"Compaction journal changed while verifying");
  return {std::move(file),actual,std::move(plan),body};
}
void saveJournal(const ProfileLease& lease,const Plan& p,FileHandle source,std::stop_token stop){
  auto body=metadata(p);require(body.size()<=MaximumMetadata,ErrorCode::Unsupported,"Compaction metadata exceeds bounded journal");
  cancelled(stop);
  const auto directory=lease.directoryHandle();
  const auto file=take(detail::openWritableStateFile(directory,Saving,true));
  try {
    require(privateCompactionFile(file.get(),heldFileStamp(file.get())),ErrorCode::UnsafePath,"Unsafe compaction journal");
    std::array<char,8> length{};auto n=static_cast<std::uint64_t>(body.size());
    for(std::size_t j=0;j<8;++j){length[7-j]=static_cast<char>(n&255);n>>=8;}
    std::string header(Magic);header.append(length.data(),length.size());header+=body;
    auto md=beginSha256();add(md,header);writeAll(file.get(),0,header);
    FileStamp sourceBefore;
    if(p.present){
      sourceBefore=heldFileStamp(source);
      require(sourceBefore.device==p.sourceDevice&&sourceBefore.inode==p.sourceInode
          &&sourceBefore.size==static_cast<std::int64_t>(p.oldLength),ErrorCode::Conflict,"Tail snapshot source changed");
    }
    stream(source,p.tailStart,p.tail,file.get(),header.size(),md,stop);
    if(p.present){
      const auto named=inspect(p.source);
      require(named&&unchanged(sourceBefore,heldFileStamp(source))&&unchanged(sourceBefore,*named)
          &&rangeHash(source,0,p.prefix)==p.prefixHash,ErrorCode::Conflict,"Payload changed while freezing compact tail");
    }
    const auto footer=finishSha256(md);writeAll(file.get(),header.size()+p.tail,footer);sync(file.get());
    const auto held=heldFileStamp(file.get());
    cancelled(stop);
    done(detail::publishStateFile(directory,Saving,Journal,file.get(),false,&held));
    sync(directory);
  } catch (...) {
    const auto held=detail::stateStamp(file.get());
    if(held&&held->regular&&held->owned&&held->links==1
        &&detail::removePayloadName(directory,Saving,*held)){
      static_cast<void>(detail::syncStateDirectory(directory));
    }
    throw;
  }
}
void checkTemporaryAbsence(const fs::path& path){
  require(!inspect(pathFromUtf8(pathToUtf8(path)+".saving"))&&!inspect(pathFromUtf8(pathToUtf8(path)+".BiglyBTC.fileop-saving"))&&!inspect(pathFromUtf8(pathToUtf8(path)+".BiglyBTC.compact-saving")),ErrorCode::Conflict,"An older document transaction requires recovery");
}
std::string policiesEncoding(std::span<const compat::FilePolicy> policies){
  std::map<std::int64_t,std::string> groups;
  for(std::size_t index=0;index<policies.size();++index){const auto& policy=policies[index];const auto key=policy.skipped?0:policy.priority<=0?static_cast<std::int64_t>(policy.priority)-1:policy.priority;auto& value=groups[key];if(!value.empty()){value+=',';}value+=std::to_string(index);}
  std::map<std::string,std::string> out;for(const auto& [key,value]:groups){out.emplace(std::to_string(key),b(value));}return dict(out);
}
const core::SavedDownload& selected(const core::LibrarySnapshot& library,const core::TorrentHash& hash){
  const core::SavedDownload* found=nullptr;for(const auto& s:library.downloads){if(s.hash&&*s.hash==hash){require(!found,ErrorCode::Conflict,"Duplicate compaction torrent identity");found=&s;}}
  require(found&&found->persistent,ErrorCode::Missing,"Persistent compaction torrent not found");return *found;
}
const Node& row(const core::Document& document,const core::SavedDownload& saved,bool legacy){
  return legacy?document.root().children.at(saved.sourceIndex*2+1):field(document.root(),"downloads",Node::Type::List).children.at(saved.sourceIndex);
}
void verifyNoPendingControls(const fs::path& control){
  for(const auto* suffix:{"",".bak",".saving",".BiglyBTC.fileop-saving",".BiglyBTC.compact-saving"}){require(!inspect(pathFromUtf8(pathToUtf8(control)+suffix)),ErrorCode::Conflict,"Linear compaction requires absent original controls");}
}
std::pair<std::string,std::uint64_t> projectState(const ProfileLease& lease,const Plan& p,
    const compat::OriginalLayout& layout,std::string before){
  auto desired=EncodedDocument::parse(std::move(before),compatibleConfigLimits());
  require(desired.has_value(),ErrorCode::InvalidFormat,"Cannot decode frozen compaction state");
  auto attrs=section(desired->document().find("attributes"));
  const auto defaults=take(resolveOriginalStorageTypes(lease,p.hash,layout,attrs.root()));
  require(defaults.requested[p.index]==OriginalStorageKind::Linear,ErrorCode::Conflict,"Frozen compaction policy is not linear");
  done(applyOriginalStorageDefaults(attrs,defaults,true));
  const auto& stores=field(attrs.root(),"storetypes",Node::Type::List);std::string kinds="l";
  for(std::size_t j=0;j<stores.children.size();++j){kinds+=j==p.index?"1:C":encoded(stores.children[j]);}put(attrs,"storetypes",kinds+'e');
  auto downloaded=section(attrs.find("filedownloaded"));const auto* counts=downloaded.find("downloaded");
  require(!counts||counts->type==Node::Type::List,ErrorCode::InvalidFormat,"Malformed original per-file completion");
  require(!counts||counts->children.size()<=layout.files.size(),ErrorCode::InvalidFormat,"Too many original completion entries");
  std::uint64_t completed=0;std::string values="l";
  for(std::size_t j=0;j<layout.files.size();++j){std::uint64_t count=0;
    if(counts&&j<counts->children.size()){const auto& n=counts->children[j];require(n.type==Node::Type::Integer&&n.integer>=0&&n.integer<=layout.files[j].bytes,ErrorCode::InvalidFormat,"Invalid original file completion value");count=static_cast<std::uint64_t>(n.integer);}
    if(j==p.index&&(!p.present||p.packed()<static_cast<std::uint64_t>(layout.files[j].bytes))){count=0;}
    if(!layout.files[j].padding){require(completed<=static_cast<std::uint64_t>(INT64_MAX)-count,ErrorCode::InvalidFormat,"Original completed bytes overflow");completed+=count;}values+=i(count);
  }
  put(downloaded,"downloaded",values+'e');put(attrs,"filedownloaded",downloaded.serialize());put(attrs,"resumecomplete","i1e");
  require(desired->replaceValue("attributes",attrs.serialize()).has_value(),ErrorCode::InvalidFormat,"Cannot encode compacted original attributes");auto after=desired->serialize();
  require(after.has_value()&&after->size()<=MaximumDocument,ErrorCode::Unsupported,"Compaction state exceeds snapshot bound");
  return {std::move(*after),completed};
}
std::string projectInventory(std::string before,const core::TorrentHash& hash,std::uint64_t completed){
  auto config=parse(std::move(before),MaximumDocument);const auto library=core::readLibrary(config);
  require(library&&library->issues.empty(),ErrorCode::InvalidFormat,"Invalid frozen compaction inventory");
  const auto& saved=selected(*library,hash);auto edited=parse(encoded(row(config,saved,library->legacyMap)));
  put(edited,"completedbytes",i(completed));
  if(library->legacyMap){put(config,*saved.legacyKey,edited.serialize());}
  else{std::string list="l";const auto& rows=field(config.root(),"downloads",Node::Type::List);for(std::size_t j=0;j<rows.children.size();++j){list+=j==saved.sourceIndex?edited.serialize():encoded(rows.children[j]);}put(config,"downloads",list+'e');}
  return config.serialize();
}
bool within(const fs::path& child,const fs::path& parent){
  auto part=child.begin();
  for(const auto& ancestor:parent){
    if(part==child.end()||!detail::sameFilesystemComponent(*part,ancestor)){return false;}
    ++part;
  }
  return true;
}
bool samePath(const fs::path& left,const fs::path& right){return within(left,right)&&within(right,left);}
bool reservedComponent(const fs::path& path){
  static const fs::path git(".git"),biglybtc(".BiglyBTC.");
  const auto prefix=fs::path(path.native().substr(0,biglybtc.native().size()));
  return detail::sameFilesystemComponent(path,git)||detail::sameFilesystemComponent(prefix,biglybtc);
}
Plan prepare(const ProfileLease& lease,const core::TorrentHash& hash,std::size_t index,std::stop_token stop){
  cancelled(stop);require(!hasPendingOriginalCompaction(lease)&&!hasPendingFileOperation(lease),ErrorCode::Conflict,"Resolve pending storage transaction before compaction");
  const auto filenameContext=take(readOriginalFilenameContext(lease));
  const auto qualified=take(prepareFileOperation(lease,{hash,FileOperationKind::RemoveKeepData,{}},stop));static_cast<void>(qualified);
  const auto profile=pathFromUtf8(profilePath(lease));auto store=take(TorrentStateStore::open(lease));auto state=take(store.load(lease,hash));auto layout=compat::originalLayout(state.document(),100000,filenameContext.conversions,filenameContext.decoderShowAll);
  require(layout&&layout->files.size()<=10000&&index<layout->files.size()&&!layout->files[index].padding,ErrorCode::InvalidFormat,"Compaction file index is not an ordinary metadata file");
  auto attrs=section(state.document().find("attributes"));auto defaults=take(resolveOriginalStorageTypes(lease,hash,*layout,attrs.root()));
  require(!defaults.unsupportedConversion&&defaults.requested[index]==OriginalStorageKind::Linear&&defaults.actual[index]==OriginalStorageKind::Linear,ErrorCode::Unsupported,"Explicit compaction accepts only resolved linear storage");
  auto config=take(ConfigFile::load(lease,"downloads.config"));require(config.recoverySource()==RecoverySource::Primary,ErrorCode::Conflict,"Resolve original inventory recovery before compaction");
  auto library=core::readLibrary(config.document());require(library&&library->issues.empty(),ErrorCode::InvalidFormat,"Invalid original download inventory");const auto saved=selected(*library,hash);
  auto requested=compat::originalFilePolicy(row(config.document(),saved,library->legacyMap),layout->files.size());require(requested.has_value(),ErrorCode::InvalidFormat,"Invalid original compaction selection");
  (*requested)[index]={true,0};for(std::size_t j=0;j<requested->size();++j){if(layout->files[j].padding){(*requested)[j]={true,0};}}
  cancelled(stop);require(take(readOriginalFilenameContext(lease))==filenameContext,ErrorCode::Conflict,"Compaction filename context changed before accepted Skip");done(setFilePolicies(lease,hash,*requested)); // An accepted Skip can remain on any subsequent failure.
  state=take(store.beginUpdate(lease,hash)); // Hydrates/removes cache; durably invalidates original resume BEFORE journal/data.
  config=take(ConfigFile::load(lease,"downloads.config"));library=core::readLibrary(config.document());require(library&&library->issues.empty(),ErrorCode::InvalidFormat,"Inventory changed during compaction preparation");const auto current=selected(*library,hash);
  const auto currentRow=encoded(row(config.document(),current,library->legacyMap));
  // Identical selection preflight proves all-library path/inode independence,
  // without executing any file operation or manufacturing a destination.
  auto proof=take(prepareFileOperation(lease,{hash,FileOperationKind::SetFilePolicies,{},policiesEncoding(*requested),currentRow},stop));
  static_cast<void>(proof);
  auto paths=take(prepareFileOperation(lease,{hash,FileOperationKind::RemoveKeepData,{}},stop));
  const FileOperationEntry* payload=nullptr;for(const auto& file:paths.preview().files){if(file.index==index){payload=&file;}}
  require(payload,ErrorCode::InvalidFormat,"Compaction has no authenticated source path");
  // Identical-selection preparation records every existing selected inode but
  // has no moved-file rows. Explicitly inspect read-only maps as well, covering
  // absent external links and retained torrent archive names (not only inodes).
  const auto intersects=[](const fs::path& a,const fs::path& z){
    return within(a,z)||within(z,a);
  };
  std::size_t inspected=paths.preview().files.size();
  for(const auto& item:library->downloads){
    const auto archive=pathFromUtf8(item.torrentFile);pathShape(archive);
    require(!samePath(payload->source,archive),ErrorCode::UnsafePath,"Compaction would overwrite a retained torrent archive");
    if(item.hash&&*item.hash==hash){continue;}
    require(item.hash.has_value(),ErrorCode::Unsupported,"Another library mapping lacks an identity");
    const auto other=take(prepareFileOperation(lease,{*item.hash,FileOperationKind::RemoveKeepData,{}},stop));
    inspected+=other.preview().files.size();require(inspected<=100000,ErrorCode::Unsupported,"Compaction all-library path proof exceeds bound");
    for(const auto& mapped:other.preview().files){require(!intersects(payload->source,mapped.source),ErrorCode::Conflict,"Compaction source aliases another original file map");}
  }
  Plan p{};p.filenameConversions=filenameContext.conversions;p.decoderShowAll=filenameContext.decoderShowAll;p.profile=profile;p.source=payload->source;p.hash=hash;p.index=index;
  const auto profileStat=heldFileStamp(lease.directoryHandle());p.profileDevice=profileStat.device;p.profileInode=profileStat.inode;
  p.present=payload->present;p.sourceDevice=payload->device;p.sourceInode=payload->inode;p.oldLength=static_cast<std::uint64_t>(payload->actualBytes);
  const auto& file=layout->files[index];const auto shape=edges({static_cast<std::uint64_t>(file.offset),static_cast<std::uint64_t>(file.bytes),static_cast<std::uint32_t>(layout->pieceLength)});
  p.prefix=std::min(p.oldLength,shape.first);p.tailStart=std::min(p.oldLength,shape.tailStart);p.tail=p.oldLength>shape.tailStart?std::min(shape.tail,p.oldLength-shape.tailStart):0;
  p.prefixHash=digest("");if(p.present){const auto actual=inspect(p.source);require(actual&&actual->device==p.sourceDevice&&actual->inode==p.sourceInode&&actual->size==static_cast<std::int64_t>(p.oldLength)&&actual->links==1,ErrorCode::Conflict,"Compaction payload changed");auto fd=openPayload(p.source,*actual,false);p.prefixHash=rangeHash(fd.get(),0,p.prefix);}
  const auto primary=profile/"active"/(hash.hex()+".dat");const auto before=read(primary,MaximumDocument);require(before.bytes.has_value(),ErrorCode::Missing,"Prepared original state missing");
  const auto [after,completed]=projectState(lease,p,*layout,*before.bytes);
  p.state={{pathFromUtf8(pathToUtf8(primary)+".bak"),read(pathFromUtf8(pathToUtf8(primary)+".bak"),MaximumDocument).bytes,after},{primary,before.bytes,after}};
  const auto configPath=profile/"downloads.config";const auto configBefore=read(configPath,MaximumDocument);
  require(configBefore.bytes.has_value(),ErrorCode::Missing,"Original inventory disappeared");
  p.inventory={{profile/"downloads.config.bak",read(profile/"downloads.config.bak",MaximumDocument).bytes,*configBefore.bytes},
    {configPath,configBefore.bytes,projectInventory(*configBefore.bytes,hash,completed)}};
  const auto control=profile/"active"/hash.hex()/originalControlLeaf(index,OriginalStorageKind::Compact);verifyNoPendingControls(control);
  if(p.present){p.controls.push_back({control,{},dict({{"version","i0e"},{"length",i(p.oldLength)}})});}
  for(const auto& item:library->downloads){require(item.hash.has_value(),ErrorCode::Unsupported,"Cannot prove all-library compaction ownership without identity");if(*item.hash==hash){continue;}
    for(const auto* suffix:{".dat",".dat.bak"}){const auto path=profile/"active"/(item.hash->hex()+suffix);const auto bytes=read(path,MaximumDocument);p.guards.push_back({path,bytes.bytes?std::optional<std::string>(digest(*bytes.bytes)):std::nullopt});checkTemporaryAbsence(path);}}
  for(const auto* name:{"cache.dat","cache.dat.bak"}){const auto path=profile/"active"/name;const auto bytes=read(path,MaximumDocument);p.guards.push_back({path,bytes.bytes?std::optional<std::string>(digest(*bytes.bytes)):std::nullopt});checkTemporaryAbsence(path);}
  for(std::size_t j=0;j<layout->files.size();++j){for(const auto kind:{OriginalStorageKind::Compact,OriginalStorageKind::Reordered}){
    const auto leaf=profile/"active"/hash.hex()/originalControlLeaf(j,kind);
    for(const auto* suffix:{"",".bak"}){const auto path=pathFromUtf8(pathToUtf8(leaf)+suffix);if(path==control){continue;}const auto bytes=read(path,MaximumDocument);p.guards.push_back({path,bytes.bytes?std::optional<std::string>(digest(*bytes.bytes)):std::nullopt});checkTemporaryAbsence(path);}
  }}
  for(const auto* name:{"biglybt.config","biglybt.config.bak","azureus.config","azureus.config.bak"}){const auto path=profile/name;const auto bytes=read(path,MaximumDocument);p.guards.push_back({path,bytes.bytes?std::optional<std::string>(digest(*bytes.bytes)):std::nullopt});checkTemporaryAbsence(path);}
  checkTemporaryAbsence(primary);checkTemporaryAbsence(configPath);
  require(take(readOriginalFilenameContext(lease))==filenameContext,ErrorCode::Conflict,"Compaction filename context changed during preparation");
  require(metadata(p).size()<=MaximumMetadata,ErrorCode::Unsupported,"Compaction snapshots exceed bounded transaction metadata");cancelled(stop);return p;
}
void validateIntent(const ProfileLease& lease,const Plan& p){
  require(p.state[1].before&&p.inventory[1].before,ErrorCode::InvalidFormat,"Compaction source documents absent");
  auto source=EncodedDocument::parse(*p.state[1].before,compatibleConfigLimits());
  require(source.has_value(),ErrorCode::InvalidFormat,"Invalid frozen original state");
  const auto* info=source->document().find("info");require(info&&info->type==Node::Type::Dictionary,ErrorCode::InvalidFormat,"Frozen metadata missing info");
  const auto infoBytes=source->document().originalBytes().substr(info->begin,info->end-info->begin);
  std::array<unsigned char,EVP_MAX_MD_SIZE> digestBytes{};unsigned size=0;const auto key=p.hash.bytes();
  require(EVP_Digest(infoBytes.data(),infoBytes.size(),digestBytes.data(),&size,key.size()==20&&info->find("pieces")?EVP_sha1():EVP_sha256(),nullptr)==1
    && size>=key.size()&&std::equal(key.begin(),key.end(),reinterpret_cast<const std::byte*>(digestBytes.data())),ErrorCode::InvalidFormat,"Frozen metadata does not match compaction hash");
  const auto currentContext=take(readOriginalFilenameContext(lease));const auto& currentConversions=currentContext.conversions;
  if(p.decoderShowAll){require(*p.decoderShowAll==currentContext.decoderShowAll,ErrorCode::Conflict,"Compaction decoder context changed since approval");}
  if(p.filenameConversions){require(*p.filenameConversions==currentConversions,ErrorCode::Conflict,"Compaction filename policy changed since approval");}
  const auto layout=p.filenameConversions?compat::originalLayout(source->document(),10000,*p.filenameConversions,p.decoderShowAll.value_or(false)):compat::originalLayout(source->document(),10000);
  require(layout&&p.index<layout->files.size()&&!layout->files[p.index].padding,ErrorCode::InvalidFormat,"Frozen compaction layout/index invalid");
  if(!p.filenameConversions){const auto converted=take(readOriginalLayout(lease,source->document(),10000));require(layout->name==converted.name&&std::ranges::equal(layout->files,converted.files,[](const auto& a,const auto& b){return a.path==b.path;}),ErrorCode::Unsupported,"Legacy compaction names need explicit review before conversion");}
  const auto& file=layout->files[p.index];const auto geometry=edges({static_cast<std::uint64_t>(file.offset),static_cast<std::uint64_t>(file.bytes),static_cast<std::uint32_t>(layout->pieceLength)});
  require(p.oldLength<=static_cast<std::uint64_t>(file.bytes)&&p.prefix==std::min(p.oldLength,geometry.first)&&p.tailStart==std::min(p.oldLength,geometry.tailStart)
      &&p.tail==(p.oldLength>geometry.tailStart?std::min(geometry.tail,p.oldLength-geometry.tailStart):0),ErrorCode::InvalidFormat,"Frozen compact geometry differs from original metadata");
  auto inventory=parse(*p.inventory[1].before,MaximumDocument);const auto library=core::readLibrary(inventory);
  require(library&&library->issues.empty(),ErrorCode::InvalidFormat,"Frozen library invalid");const auto& saved=selected(*library,p.hash);
  const auto policy=compat::originalFilePolicy(row(inventory,saved,library->legacyMap),layout->files.size());require(policy&&(*policy)[p.index].skipped,ErrorCode::InvalidFormat,"Compaction was not explicitly skipped");
  const auto resolved=take(resolveOriginalSaveRoot(saved,*layout));std::vector<std::string> bases;
  for(const auto& f:layout->files){bases.push_back(pathToUtf8(pathFromUtf8(resolved.saveDirectory)/(layout->multifile?pathFromUtf8(*resolved.saveFile)/pathFromUtf8(f.path):pathFromUtf8(*resolved.saveFile))));}
  const auto& preparedResume=field(source->document().root(),"resume",Node::Type::Dictionary);
  const auto& preparedData=field(preparedResume,"data",Node::Type::Dictionary);
  require(field(preparedData,"valid",Node::Type::Integer).integer==0&&field(preparedData,"blocks",Node::Type::Dictionary).children.empty(),ErrorCode::InvalidFormat,"Compaction source resume was not durably invalidated");
  if(const auto* flags=preparedData.find("resume data")){require(flags->type==Node::Type::Bytes&&std::ranges::all_of(flags->bytes,[](char c){return c=='\2';}),ErrorCode::InvalidFormat,"Compaction source contains trusted piece flags");}
  auto attrs=section(source->document().find("attributes"));require(field(attrs.root(),"resumecomplete",Node::Type::Integer).integer==1,ErrorCode::InvalidFormat,"Compaction source completion was not invalidated");const auto links=compat::resolveOriginalFileLinks(attrs.find("filelinks2"),bases);
  require(links.has_value(),ErrorCode::InvalidFormat,"Frozen original links invalid");
  const auto& link=links->byIndex[p.index];const auto base=pathFromUtf8(bases[p.index]);
  const auto expected=link?pathFromUtf8(link->target):base;pathShape(expected);
  const auto* suffix=attrs.find("incompfilesuffix");const auto suffixBytes=suffix&&suffix->type==Node::Type::Bytes?std::string(suffix->bytes):std::string{};
  require(!suffix||suffix->type==Node::Type::Bytes,ErrorCode::InvalidFormat,"Invalid frozen incomplete suffix");
  require(p.source==expected||(!link&&!suffixBytes.empty()&&suffixBytes.size()<=200&&suffixBytes.find('/')==suffixBytes.npos&&suffixBytes.find('\n')==suffixBytes.npos&&p.source==pathFromUtf8(pathToUtf8(base)+suffixBytes)),ErrorCode::UnsafePath,"Frozen compaction source differs from original indexed path");
  require(!within(p.source,p.profile)&&!within(p.profile,p.source),ErrorCode::UnsafePath,"Compaction payload intersects profile");
  for(const auto& part:p.source){require(!reservedComponent(part),ErrorCode::UnsafePath,"Compaction payload intersects reserved control paths");}
  // Re-check live library ownership on every replay. The guarded state/cache
  // bytes fix interpretation; real path/inode inspection catches moved aliases.
  const auto cacheRaw=read(p.profile/"active/cache.dat",MaximumDocument);
  std::optional<EncodedDocument> cache;
  if(cacheRaw.bytes){auto parsed=EncodedDocument::parse(*cacheRaw.bytes,compatibleConfigLimits());require(parsed.has_value(),ErrorCode::InvalidFormat,"Invalid guarded original cache");cache.emplace(std::move(*parsed));}
  const auto* cached=cache?cache->document().find("state"):nullptr;
  require(!cached||cached->type==Node::Type::List,ErrorCode::InvalidFormat,"Invalid guarded cache entries");
  const auto selectedRoot=pathFromUtf8(resolved.saveDirectory)/pathFromUtf8(*resolved.saveFile);
  const auto payloadStat=inspect(p.source);std::size_t inspected=layout->files.size();
  for(const auto& other:library->downloads){
    const fs::path archive(other.torrentFile);pathShape(archive);require(p.source!=archive,ErrorCode::UnsafePath,"Compaction source is a retained archive");
    if(other.hash&&*other.hash==p.hash){continue;}require(other.hash.has_value(),ErrorCode::Unsupported,"Unresolved original library identity");
    const auto raw=read(p.profile/"active"/(other.hash->hex()+".dat"),MaximumDocument);require(raw.bytes.has_value(),ErrorCode::Missing,"Other original state missing during replay");
    auto otherState=EncodedDocument::parse(*raw.bytes,compatibleConfigLimits());require(otherState.has_value(),ErrorCode::InvalidFormat,"Other original state malformed during replay");
    auto otherAttrs=section(otherState->document().find("attributes"));
    const Node* cachedEntry=nullptr;
    if(cached){for(const auto& entry:cached->children){const auto* keyNode=entry.find("hash");const auto bytes=other.hash->bytes();
      if(keyNode&&keyNode->type==Node::Type::Bytes&&keyNode->bytes==std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size())){cachedEntry=&entry;}}}
    if(cachedEntry){if(const auto* properties=cachedEntry->find("attributes")){otherAttrs=section(properties);}}
    const auto otherLayout=compat::originalLayout(otherState->document(),10000,currentConversions,currentContext.decoderShowAll);require(otherLayout.has_value(),ErrorCode::Unsupported,"Other original layout unsupported during replay");
    inspected+=otherLayout->files.size();require(inspected<=100000,ErrorCode::Unsupported,"Compaction replay ownership bound");
    const auto otherSaved=take(resolveOriginalSaveRoot(other,*otherLayout));const auto otherRoot=pathFromUtf8(otherSaved.saveDirectory)/pathFromUtf8(*otherSaved.saveFile);
    require(!within(otherRoot,selectedRoot)&&!within(selectedRoot,otherRoot),ErrorCode::Conflict,"Another torrent overlaps compaction root");
    std::vector<std::string> otherBases;for(const auto& fileInfo:otherLayout->files){otherBases.push_back(pathToUtf8(pathFromUtf8(otherSaved.saveDirectory)/(otherLayout->multifile?pathFromUtf8(*otherSaved.saveFile)/pathFromUtf8(fileInfo.path):pathFromUtf8(*otherSaved.saveFile))));}
    const auto otherLinks=compat::resolveOriginalFileLinks(otherAttrs.find("filelinks2"),otherBases);require(otherLinks.has_value(),ErrorCode::Unsupported,"Other original links unsupported during replay");
    const auto* suffixNode=otherAttrs.find("incompfilesuffix");require(!suffixNode||suffixNode->type==Node::Type::Bytes,ErrorCode::InvalidFormat,"Other incomplete suffix malformed");
    const auto suffixValue=suffixNode?std::string(suffixNode->bytes):std::string{};require(suffixValue.size()<=200&&suffixValue.find('/')==suffixValue.npos&&suffixValue.find('\n')==suffixValue.npos,ErrorCode::Unsupported,"Other incomplete suffix unsafe");
    for(std::size_t j=0;j<otherBases.size();++j){const auto& actualLink=otherLinks->byIndex[j];auto path=actualLink?pathFromUtf8(actualLink->target):pathFromUtf8(otherBases[j]);pathShape(path);auto actual=inspect(path);
      if(!actual&&!actualLink&&!suffixValue.empty()){path+=pathFromUtf8(suffixValue);pathShape(path);actual=inspect(path);}
      require(!within(path,p.source)&&!within(p.source,path),ErrorCode::Conflict,"Another original file aliases compaction source");
      require(!actual||!payloadStat||actual->device!=payloadStat->device||actual->inode!=payloadStat->inode,ErrorCode::Conflict,"Another torrent shares compaction inode");
    }
  }
  const auto [stateAfter,completed]=projectState(lease,p,*layout,*p.state[1].before);
  require(p.state[0].after==stateAfter&&p.state[1].after==stateAfter
      &&p.inventory[0].after==*p.inventory[1].before&&p.inventory[1].after==projectInventory(*p.inventory[1].before,p.hash,completed),ErrorCode::InvalidFormat,"Compaction journal contains unrelated metadata edits");
  if(p.present){require(p.controls[0].after==dict({{"version","i0e"},{"length",i(p.oldLength)}}),ErrorCode::InvalidFormat,"Compaction control logical length differs");}
  std::set<fs::path,detail::FilesystemPathLess> required;
  for(const auto& item:library->downloads){require(item.hash.has_value(),ErrorCode::InvalidFormat,"Frozen library identity unavailable");if(*item.hash!=p.hash){for(const auto* tail:{".dat",".dat.bak"}){required.insert(p.profile/"active"/(item.hash->hex()+tail));}}}
  for(const auto* name:{"cache.dat","cache.dat.bak"}){required.insert(p.profile/"active"/name);}
  const auto selectedControl=p.profile/"active"/p.hash.hex()/originalControlLeaf(p.index,OriginalStorageKind::Compact);
  for(std::size_t j=0;j<layout->files.size();++j){for(const auto kind:{OriginalStorageKind::Compact,OriginalStorageKind::Reordered}){const auto leaf=p.profile/"active"/p.hash.hex()/originalControlLeaf(j,kind);for(const auto* tail:{"",".bak"}){const auto path=pathFromUtf8(pathToUtf8(leaf)+tail);if(path!=selectedControl){required.insert(path);}}}}
  for(const auto* name:{"biglybt.config","biglybt.config.bak","azureus.config","azureus.config.bak"}){required.insert(p.profile/name);}
  std::set<fs::path,detail::FilesystemPathLess> actual;for(const auto& guard:p.guards){require(actual.insert(guard.path).second,ErrorCode::InvalidFormat,"Duplicate compaction guard");}
  require(actual==required,ErrorCode::InvalidFormat,"Compaction journal omitted an ownership/source guard");
}
OriginalCompactionResult execute(const ProfileLease& lease,JournalFile& journal,bool recovered,Observer observer){
  done(requireNoPendingSubscriptionAcquisition(lease));
  const auto notify=[&](OriginalCompactionStage stage){observe(observer,stage);done(requireNoPendingSubscriptionAcquisition(lease));};
  const auto& p=journal.plan;require(!hasPendingFileOperation(lease),ErrorCode::Conflict,"File-operation recovery must finish before compaction replay");identity(lease,p);verifyGuards(p);verifyEdits(p.controls);verifyEdits(p.state);verifyEdits(p.inventory);validateIntent(lease,p);
  const auto source=inspect(p.source);require(source.has_value()==p.present,ErrorCode::Conflict,"Compaction payload presence changed");
  if(p.present){require(source->device==p.sourceDevice&&source->inode==p.sourceInode&&source->links==1
      && (source->size==static_cast<std::int64_t>(p.oldLength)||source->size==static_cast<std::int64_t>(p.packed())),ErrorCode::Conflict,"Compaction payload identity/length changed");
    auto fd=openPayload(p.source,*source,true);require(rangeHash(fd.get(),0,p.prefix)==p.prefixHash,ErrorCode::Conflict,"Compaction preserved prefix changed");
    done(requireNoPendingSubscriptionAcquisition(lease));auto md=beginSha256();stream(journal.fd.get(),journal.bodyOffset,p.tail,fd.get(),p.prefix,md);static_cast<void>(finishSha256(md));
    require(detail::resizeOriginalPayload(fd.get(),p.packed()),ErrorCode::Io,"Cannot truncate compacted payload");sync(fd.get());
    const auto now=inspect(p.source);const auto held=heldFileStamp(fd.get());require(now&&unchanged(held,*now)&&now->device==p.sourceDevice&&now->inode==p.sourceInode&&now->size==static_cast<std::int64_t>(p.packed()),ErrorCode::Conflict,"Compaction payload name changed during pack");
    require(rangeHash(fd.get(),0,p.prefix)==p.prefixHash&&rangeHash(fd.get(),p.prefix,p.tail)==rangeHash(journal.fd.get(),journal.bodyOffset,p.tail),ErrorCode::Io,"Compacted payload does not match durable preserved bytes");
  }
  const auto journalNow=heldFileStamp(journal.fd.get());const auto journalName=inspect(p.profile/Journal);
  require(journalName&&unchanged(journal.stamp,journalNow)&&unchanged(journal.stamp,*journalName),ErrorCode::Conflict,"Compaction journal changed during pack");
  notify(OriginalCompactionStage::PayloadSynced);
  // Revalidate all frozen authorities again before any original-document write.
  verifyGuards(p);verifyEdits(p.controls);verifyEdits(p.state);verifyEdits(p.inventory);
  if(p.present){static_cast<void>(directory(p.controls[0].path.parent_path(),true));}
  publish(p.controls);notify(OriginalCompactionStage::ControlPublished);
  publish(p.state);notify(OriginalCompactionStage::StatePublished);
  publish(p.inventory);notify(OriginalCompactionStage::InventoryPublished);
  const auto named=inspect(p.profile/Journal);const auto held=heldFileStamp(journal.fd.get());require(named&&unchanged(held,journal.stamp)&&unchanged(*named,journal.stamp),ErrorCode::Conflict,"Compaction journal changed before retirement");
  erase(p.profile/Journal,journal.stamp);notify(OriginalCompactionStage::Completed);
  return {p.hash,p.index,p.oldLength,p.packed(),recovered};
}
} // namespace

bool hasPendingOriginalCompaction(const ProfileLease& lease) noexcept {
  try{const auto root=pathFromUtf8(fileop::profilePath(lease));return inspect(root/Journal).has_value()||inspect(root/Saving).has_value();}catch(...){return true;}
}
std::expected<OriginalCompactionResult,Error> compactOriginalFile(const ProfileLease& lease,const core::TorrentHash& hash,std::size_t index,std::stop_token stop,Observer observer){
  return guarded([&]()->std::expected<OriginalCompactionResult,Error>{done(requireNoPendingSubscriptionAcquisition(lease));auto p=prepare(lease,hash,index,stop);fileop::Fd fd;
    if(p.present){const auto actual=inspect(p.source);require(actual.has_value(),ErrorCode::Missing,"Compaction payload disappeared");fd=openPayload(p.source,*actual,false);}
    verifyGuards(p);verifyEdits(p.state);verifyEdits(p.inventory);done(requireNoPendingSubscriptionAcquisition(lease));saveJournal(lease,p,fd.get(),stop);fd={};
    observe(observer,OriginalCompactionStage::JournalSynced);auto journal=openJournal(lease);return execute(lease,journal,false,observer);});
}
std::expected<std::optional<OriginalCompactionResult>,Error> recoverOriginalCompaction(const ProfileLease& lease,Observer observer){
  return guarded([&]()->std::expected<std::optional<OriginalCompactionResult>,Error>{done(requireNoPendingSubscriptionAcquisition(lease));const auto root=pathFromUtf8(fileop::profilePath(lease));
    if(take(isOriginalSelectionGroupPending(lease))){return std::optional<OriginalCompactionResult>(take(recoverOriginalSelectionGroup(lease)));}
    if(!inspect(root/Journal)){require(!hasPendingOriginalCompaction(lease),ErrorCode::Conflict,"Interrupted unpublished compaction journal requires explicit review; no payload packing was authorized");return std::nullopt;}
    require(!inspect(root/Saving),ErrorCode::Conflict,"Unexpected second compaction journal");auto journal=openJournal(lease);return std::optional<OriginalCompactionResult>(execute(lease,journal,true,observer));});
}
} // namespace biglybtc::storage
