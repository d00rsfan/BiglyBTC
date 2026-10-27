#include "original_filename_policy.hpp"
#include "linear_to_compact.hpp"
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
#include <cerrno>
#include <climits>
#include <span>
#include <fcntl.h>
#include <limits>
#include <linux/fs.h>
#include <map>
#include <memory>
#include <set>
#include <sys/syscall.h>
#include <unistd.h>

namespace biglybtc::storage {
namespace {
namespace fs = std::filesystem;
using namespace fileop;
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
using Md = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
Md hash() {
  Md result(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  require(result && EVP_DigestInit_ex(result.get(), EVP_sha256(), nullptr) == 1, ErrorCode::Io, "Cannot initialize compaction digest");
  return result;
}
void add(Md& md, std::string_view data) { require(EVP_DigestUpdate(md.get(), data.data(), data.size()) == 1, ErrorCode::Io, "Cannot hash compaction bytes"); }
std::string finish(Md& md) {
  std::array<unsigned char, 32> raw{}; unsigned size = 0;
  require(EVP_DigestFinal_ex(md.get(), raw.data(), &size) == 1 && size == raw.size(), ErrorCode::Io, "Cannot finalize compaction digest");
  constexpr char hex[] = "0123456789abcdef";
  std::string out(64, '0');
  for (std::size_t p = 0; p < raw.size(); ++p) { out[p*2] = hex[raw[p] >> 4]; out[p*2+1] = hex[raw[p] & 15]; }
  return out;
}
void readAll(int fd, std::uint64_t offset, std::span<char> data) {
  require(offset <= static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()) - data.size(), ErrorCode::Unsupported, "Compaction offset overflow");
  std::size_t used = 0;
  while (used < data.size()) {
    auto count = ::pread(fd, data.data()+used, data.size()-used, static_cast<off_t>(offset+used));
    if (count < 0 && errno == EINTR) { continue; }
    require(count > 0, ErrorCode::Io, "Compaction source ended before its frozen bound"); used += static_cast<std::size_t>(count);
  }
}
void writeAll(int fd, std::uint64_t offset, std::span<const char> data) {
  require(offset <= static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()) - data.size(), ErrorCode::Unsupported, "Compaction write offset overflow");
  std::size_t used = 0;
  while (used < data.size()) {
    auto count = ::pwrite(fd, data.data()+used, data.size()-used, static_cast<off_t>(offset+used));
    if (count < 0 && errno == EINTR) { continue; }
    require(count > 0, ErrorCode::Io, "Cannot write compaction transaction"); used += static_cast<std::size_t>(count);
  }
}
// Fixed scratch regardless of the original piece/tail bound. No missing byte
// is synthesized: any unexpected short read aborts with the journal intact.
void stream(int input, std::uint64_t start, std::uint64_t count, int output,
    std::uint64_t destination, Md& md, std::stop_token stop = {}) {
  std::array<char, 65536> buffer{};
  for (std::uint64_t used = 0; used < count;) {
    cancelled(stop);
    const auto size = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), count-used));
    readAll(input, start+used, std::span<char>(buffer.data(), size));
    add(md, std::string_view(buffer.data(), size));
    if (output >= 0) { writeAll(output, destination+used, std::span<const char>(buffer.data(), size)); }
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
Fd openPayload(const fs::path& path, const struct stat& expected, bool writing) {
  auto parent = directory(path.parent_path());
  Fd fd(::openat(parent.get(), path.filename().c_str(), (writing?O_RDWR:O_RDONLY)|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));
  struct stat actual{};
  require(fd.get() >= 0 && ::fstat(fd.get(), &actual) == 0 && unchanged(actual, expected)
      && actual.st_uid == ::geteuid() && S_ISREG(actual.st_mode) && actual.st_nlink == 1,
      ErrorCode::Conflict, "Compaction payload changed or is not exclusively owned regular data");
  return fd;
}
std::string rangeHash(int fd, std::uint64_t start, std::uint64_t count) {
  auto md = hash(); stream(fd, start, count, -1, 0, md); return finish(md);
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
  std::uint64_t packed() const { return prefix+tail; }
};
std::string encodeEdits(const std::vector<Edit>& edits) {
  std::string out = "l";
  for (const auto& e : edits) { out += dict({{"path",b(e.path.native())},{"before",optional(e.before)},{"after",b(e.after)}}); }
  return out+'e';
}
std::string metadata(const Plan& p) {
  std::string guards = "l";
  for (const auto& g : p.guards) { guards += dict({{"path",b(g.path.native())},{"sha",optional(g.sha)}}); }
  auto result=dict({{"version",i(1)},{"profile",b(p.profile.native())},{"profileDevice",i(p.profileDevice)},{"profileInode",i(p.profileInode)},
    {"source",b(p.source.native())},{"present",i(p.present)},{"sourceDevice",i(p.sourceDevice)},{"sourceInode",i(p.sourceInode)},
    {"hash",b(p.hash.hex())},{"index",i(p.index)},{"oldLength",i(p.oldLength)},
    {"prefix",i(p.prefix)},{"tailStart",i(p.tailStart)},{"tail",i(p.tail)},{"prefixHash",b(p.prefixHash)},
    {"controls",encodeEdits(p.controls)},{"state",encodeEdits(p.state)},{"inventory",encodeEdits(p.inventory)},{"guards",guards+'e'}});
  if(p.filenameConversions){auto document=parse(result);put(document,"filenameConversions",b(*p.filenameConversions));result=document.serialize();}return result;
}
std::vector<Edit> decodeEdits(const Node& root, std::string_view key, const fs::path& profile) {
  const auto& list = field(root, key, Node::Type::List); require(list.children.size() <= 2, ErrorCode::InvalidFormat, "Too many compaction edits");
  std::vector<Edit> out;
  for (const auto& item : list.children) {
    auto path = fs::path(text(item, "path")); pathShape(path);
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
  p.profile=text(root,"profile");p.source=text(root,"source");p.hash=*id;
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
  require(p.state.size()==2 && p.state[0].path==fs::path(statePath.string()+".bak") && p.state[1].path==statePath
      && p.inventory.size()==2 && p.inventory[0].path==p.profile/"downloads.config.bak" && p.inventory[1].path==p.profile/"downloads.config",
      ErrorCode::UnsafePath,"Compaction journal has unexpected document targets");
  const auto control=p.profile/"active"/hex/originalControlLeaf(p.index,OriginalStorageKind::Compact);
  require(p.controls.size()==(p.present?1U:0U) && (!p.present || (p.controls[0].path==control&&!p.controls[0].before)), ErrorCode::UnsafePath,"Unexpected compaction control target");
  const auto& list=field(root,"guards",Node::Type::List); require(list.children.size()<=60010,ErrorCode::InvalidFormat,"Compaction guard bound");
  std::set<fs::path> unique;
  for(const auto& item:list.children){auto path=fs::path(text(item,"path"));pathShape(path);
    require((path.parent_path()==p.profile||path.parent_path()==p.profile/"active"||path.parent_path()==p.profile/"active"/hex)&&unique.insert(path).second,ErrorCode::UnsafePath,"Invalid compaction guard path");
    auto sha=optionalBytes(item,"sha");require(!sha||sha->size()==64,ErrorCode::InvalidFormat,"Invalid compaction guard digest");p.guards.push_back({std::move(path),std::move(sha)});}
  return p;
}
void identity(const ProfileLease& lease, const Plan& p) {
  require(fs::path(profilePath(lease))==p.profile,ErrorCode::Conflict,"Compaction belongs to another held profile");
  struct stat s{};require(::fstat(lease.directoryHandle(),&s)==0&&s.st_dev==p.profileDevice&&s.st_ino==p.profileInode,ErrorCode::Conflict,"Compaction profile was replaced");
}
void verifyEdits(const std::vector<Edit>& edits) {
  for(const auto& e:edits){auto now=read(e.path,MaximumDocument);require(now.bytes==e.before||now.bytes==std::optional<std::string>(e.after),ErrorCode::Conflict,"Compaction document changed outside transaction");}
}
void verifyGuards(const Plan& p) {
  for(const auto& g:p.guards){const auto now=read(g.path,MaximumDocument);require((now.bytes?std::optional<std::string>(digest(*now.bytes)):std::nullopt)==g.sha,ErrorCode::Conflict,"Another original state changed during pending compaction");}
}
// Temp bytes are authorized only by a complete verified journal and exact
// destination CAS. A crash-short prefix can be rewritten, never reinterpreted.
void publishEdit(const Edit& edit) {
  auto before=read(edit.path,MaximumDocument);
  auto parent=directory(edit.path.parent_path());const auto leaf=edit.path.filename().string();
  const auto temporary=leaf+".BiglyBTC.compact-saving";
  if(before.bytes==std::optional<std::string>(edit.after)){
    require(!inspect(edit.path.parent_path()/temporary),ErrorCode::Conflict,"Unexpected temporary beside published compaction document");
    Fd existing(::openat(parent.get(),leaf.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));struct stat actual{};
    require(existing.get()>=0&&::fstat(existing.get(),&actual)==0&&before.stamp&&unchanged(actual,*before.stamp),ErrorCode::Conflict,"Published compaction document changed before resync");
    check(edit.path,before,MaximumDocument);sync(existing.get());sync(parent.get());return;
  }
  require(before.bytes==edit.before,ErrorCode::Conflict,"Compaction publication lost its raw snapshot");
  int raw=::openat(parent.get(),temporary.c_str(),O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC,0600);
  if(raw<0&&errno==EEXIST){
    const auto old=read(edit.path.parent_path()/temporary,MaximumDocument);
    require(old.bytes&&edit.after.starts_with(*old.bytes),ErrorCode::Conflict,"Compaction temporary is not an authorized prefix");
    raw=::openat(parent.get(),temporary.c_str(),O_RDWR|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    struct stat opened{};require(raw>=0&&::fstat(raw,&opened)==0&&old.stamp&&unchanged(opened,*old.stamp),ErrorCode::Conflict,"Compaction temporary changed before recovery");
  }
  Fd file(raw);struct stat held{},named{};
  require(raw>=0&&::fstat(raw,&held)==0&&S_ISREG(held.st_mode)&&held.st_uid==::geteuid()&&held.st_nlink==1&&(held.st_mode&0777)==0600,ErrorCode::UnsafePath,"Unsafe compaction temporary");
  writeAll(raw,0,std::span<const char>(edit.after.data(),edit.after.size()));require(::ftruncate(raw,static_cast<off_t>(edit.after.size()))==0,ErrorCode::Io,"Cannot bound compaction document");sync(raw);
  require(::fstat(raw,&held)==0&&::fstatat(parent.get(),temporary.c_str(),&named,AT_SYMLINK_NOFOLLOW)==0&&unchanged(held,named),ErrorCode::Conflict,"Compaction temporary name changed");
  check(edit.path,before,MaximumDocument);
  const auto rc=before.bytes?::renameat(parent.get(),temporary.c_str(),parent.get(),leaf.c_str()):static_cast<int>(::syscall(SYS_renameat2,parent.get(),temporary.c_str(),parent.get(),leaf.c_str(),RENAME_NOREPLACE));
  require(rc==0,ErrorCode::Conflict,"Cannot publish compacted original document");sync(parent.get());
}
void publish(const std::vector<Edit>& edits) { for(const auto& e:edits){publishEdit(e);} }
struct JournalFile {Fd fd;struct stat stamp{};Plan plan;std::uint64_t bodyOffset=0;};
JournalFile openJournal(const ProfileLease& lease) {
  const auto path=fs::path(profilePath(lease))/Journal;
  const auto before=inspect(path);require(before&&before->st_nlink==1&&(before->st_mode&0777)==0600,ErrorCode::UnsafePath,"Invalid compaction journal ownership");
  auto parent=directory(path.parent_path());Fd file(::openat(parent.get(),path.filename().c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));struct stat actual{};
  require(file.get()>=0&&::fstat(file.get(),&actual)==0&&unchanged(*before,actual),ErrorCode::Conflict,"Compaction journal changed before opening");
  std::array<char,8> size{};std::string magic(Magic.size(),'\0');readAll(file.get(),0,magic);readAll(file.get(),Magic.size(),size);
  require(magic==Magic,ErrorCode::InvalidFormat,"Invalid compaction journal header");
  std::uint64_t length=0;for(const unsigned char c:size){length=(length<<8)|c;}
  require(length<=MaximumMetadata,ErrorCode::InvalidFormat,"Compaction metadata bound exceeded");
  std::string bytes(static_cast<std::size_t>(length),'\0');readAll(file.get(),Magic.size()+8,bytes);
  auto plan=decode(parse(bytes));identity(lease,plan);
  const auto body=Magic.size()+8+length;
  require(static_cast<std::uint64_t>(actual.st_size)==body+plan.tail+64,ErrorCode::InvalidFormat,"Compaction journal body is truncated or has trailing bytes");
  auto md=hash();stream(file.get(),0,body+plan.tail,-1,0,md);std::array<char,64> footer{};readAll(file.get(),body+plan.tail,footer);
  require(finish(md)==std::string_view(footer.data(),footer.size()),ErrorCode::InvalidFormat,"Compaction journal digest mismatch");
  struct stat after{};const auto named=inspect(path);require(::fstat(file.get(),&after)==0&&named&&unchanged(actual,after)&&unchanged(actual,*named),ErrorCode::Conflict,"Compaction journal changed while verifying");
  return {std::move(file),actual,std::move(plan),body};
}
void saveJournal(const ProfileLease& lease,const Plan& p,int source,std::stop_token stop){
  auto body=metadata(p);require(body.size()<=MaximumMetadata,ErrorCode::Unsupported,"Compaction metadata exceeds bounded journal");
  cancelled(stop);Fd file(::openat(lease.directoryHandle(),std::string(Saving).c_str(),O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC,0600));
  require(file.get()>=0,ErrorCode::Conflict,"Unpublished compaction journal requires review");
  try {
  std::array<char,8> length{};auto n=static_cast<std::uint64_t>(body.size());for(std::size_t j=0;j<8;++j){length[7-j]=static_cast<char>(n&255);n>>=8;}
  std::string header(Magic);header.append(length.data(),length.size());header+=body;
  auto md=hash();add(md,header);writeAll(file.get(),0,header);
  struct stat sourceBefore{},sourceAfter{};
  if(p.present){require(::fstat(source,&sourceBefore)==0&&sourceBefore.st_dev==p.sourceDevice&&sourceBefore.st_ino==p.sourceInode&&sourceBefore.st_size==static_cast<off_t>(p.oldLength),ErrorCode::Conflict,"Tail snapshot source changed");}
  stream(source,p.tailStart,p.tail,file.get(),header.size(),md,stop);
  if(p.present){const auto namedSource=inspect(p.source);require(::fstat(source,&sourceAfter)==0&&unchanged(sourceBefore,sourceAfter)&&namedSource&&unchanged(sourceBefore,*namedSource)&&rangeHash(source,0,p.prefix)==p.prefixHash,ErrorCode::Conflict,"Payload changed while freezing compact tail");}
  const auto footer=finish(md);writeAll(file.get(),header.size()+p.tail,footer);sync(file.get());
  struct stat held{},named{};
  require(::fstat(file.get(),&held)==0&&::fstatat(lease.directoryHandle(),std::string(Saving).c_str(),&named,AT_SYMLINK_NOFOLLOW)==0&&unchanged(held,named),ErrorCode::Conflict,"Compaction journal name changed before publication");
  cancelled(stop);
  require(::syscall(SYS_renameat2,lease.directoryHandle(),std::string(Saving).c_str(),lease.directoryHandle(),std::string(Journal).c_str(),RENAME_NOREPLACE)==0,ErrorCode::Conflict,"Cannot publish compaction journal without replacement");sync(lease.directoryHandle());
  } catch (...) {
    // Only this still-open O_EXCL inode may be discarded before publication.
    // A SIGKILL can leave .saving: recovery refuses it, since no complete intent
    // was accepted for payload mutation. Never erase a replacement pathname.
    struct stat held{},named{};
    if(::fstat(file.get(),&held)==0&&::fstatat(lease.directoryHandle(),std::string(Saving).c_str(),&named,AT_SYMLINK_NOFOLLOW)==0
        &&unchanged(held,named)&&S_ISREG(named.st_mode)&&named.st_uid==::geteuid()&&named.st_nlink==1){
      if(::unlinkat(lease.directoryHandle(),std::string(Saving).c_str(),0)==0){static_cast<void>(::fsync(lease.directoryHandle()));}
    }
    throw;
  }
}
void checkTemporaryAbsence(const fs::path& path){
  require(!inspect(fs::path(path.string()+".saving"))&&!inspect(fs::path(path.string()+".BiglyBTC.fileop-saving"))&&!inspect(fs::path(path.string()+".BiglyBTC.compact-saving")),ErrorCode::Conflict,"An older document transaction requires recovery");
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
  for(const auto* suffix:{"",".bak",".saving",".BiglyBTC.fileop-saving",".BiglyBTC.compact-saving"}){require(!inspect(fs::path(control.string()+suffix)),ErrorCode::Conflict,"Linear compaction requires absent original controls");}
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
Plan prepare(const ProfileLease& lease,const core::TorrentHash& hash,std::size_t index,std::stop_token stop){
  cancelled(stop);require(!hasPendingOriginalCompaction(lease)&&!hasPendingFileOperation(lease),ErrorCode::Conflict,"Resolve pending storage transaction before compaction");
  const auto qualified=take(prepareFileOperation(lease,{hash,FileOperationKind::RemoveKeepData,{}},stop));static_cast<void>(qualified);
  const auto profile=fs::path(profilePath(lease));auto store=take(TorrentStateStore::open(lease));auto state=take(store.load(lease,hash));auto layout=readOriginalLayout(lease,state.document());
  require(layout&&layout->files.size()<=10000&&index<layout->files.size()&&!layout->files[index].padding,ErrorCode::InvalidFormat,"Compaction file index is not an ordinary metadata file");
  auto attrs=section(state.document().find("attributes"));auto defaults=take(resolveOriginalStorageTypes(lease,hash,*layout,attrs.root()));
  require(!defaults.unsupportedConversion&&defaults.requested[index]==OriginalStorageKind::Linear&&defaults.actual[index]==OriginalStorageKind::Linear,ErrorCode::Unsupported,"Explicit compaction accepts only resolved linear storage");
  auto config=take(ConfigFile::load(lease,"downloads.config"));require(config.recoverySource()==RecoverySource::Primary,ErrorCode::Conflict,"Resolve original inventory recovery before compaction");
  auto library=core::readLibrary(config.document());require(library&&library->issues.empty(),ErrorCode::InvalidFormat,"Invalid original download inventory");const auto saved=selected(*library,hash);
  auto requested=compat::originalFilePolicy(row(config.document(),saved,library->legacyMap),layout->files.size());require(requested.has_value(),ErrorCode::InvalidFormat,"Invalid original compaction selection");
  (*requested)[index]={true,0};for(std::size_t j=0;j<requested->size();++j){if(layout->files[j].padding){(*requested)[j]={true,0};}}
  cancelled(stop);done(setFilePolicies(lease,hash,*requested)); // An accepted Skip can remain on any subsequent failure.
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
    const auto within=[](const fs::path& child,const fs::path& parent){auto c=child.begin();for(auto b=parent.begin();b!=parent.end();++b,++c){if(c==child.end()||*c!=*b){return false;}}return true;};
    return within(a,z)||within(z,a);
  };
  std::size_t inspected=paths.preview().files.size();
  for(const auto& item:library->downloads){
    const fs::path archive(item.torrentFile);pathShape(archive);
    require(payload->source!=archive,ErrorCode::UnsafePath,"Compaction would overwrite a retained torrent archive");
    if(item.hash&&*item.hash==hash){continue;}
    require(item.hash.has_value(),ErrorCode::Unsupported,"Another library mapping lacks an identity");
    const auto other=take(prepareFileOperation(lease,{*item.hash,FileOperationKind::RemoveKeepData,{}},stop));
    inspected+=other.preview().files.size();require(inspected<=100000,ErrorCode::Unsupported,"Compaction all-library path proof exceeds bound");
    for(const auto& mapped:other.preview().files){require(!intersects(payload->source,mapped.source),ErrorCode::Conflict,"Compaction source aliases another original file map");}
  }
  Plan p{};p.filenameConversions=take(readOriginalFilenameConversions(lease));p.profile=profile;p.source=payload->source;p.hash=hash;p.index=index;
  struct stat profileStat{};require(::fstat(lease.directoryHandle(),&profileStat)==0,ErrorCode::Io,"Cannot bind compaction profile");p.profileDevice=profileStat.st_dev;p.profileInode=profileStat.st_ino;
  p.present=payload->present;p.sourceDevice=payload->device;p.sourceInode=payload->inode;p.oldLength=static_cast<std::uint64_t>(payload->actualBytes);
  const auto& file=layout->files[index];const auto shape=edges({static_cast<std::uint64_t>(file.offset),static_cast<std::uint64_t>(file.bytes),static_cast<std::uint32_t>(layout->pieceLength)});
  p.prefix=std::min(p.oldLength,shape.first);p.tailStart=std::min(p.oldLength,shape.tailStart);p.tail=p.oldLength>shape.tailStart?std::min(shape.tail,p.oldLength-shape.tailStart):0;
  p.prefixHash=digest("");if(p.present){const auto actual=inspect(p.source);require(actual&&actual->st_dev==p.sourceDevice&&actual->st_ino==p.sourceInode&&actual->st_size==static_cast<off_t>(p.oldLength)&&actual->st_nlink==1,ErrorCode::Conflict,"Compaction payload changed");auto fd=openPayload(p.source,*actual,false);p.prefixHash=rangeHash(fd.get(),0,p.prefix);}
  const auto primary=profile/"active"/(hash.hex()+".dat");const auto before=read(primary,MaximumDocument);require(before.bytes.has_value(),ErrorCode::Missing,"Prepared original state missing");
  const auto [after,completed]=projectState(lease,p,*layout,*before.bytes);
  p.state={{fs::path(primary.string()+".bak"),read(fs::path(primary.string()+".bak"),MaximumDocument).bytes,after},{primary,before.bytes,after}};
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
    for(const auto* suffix:{"",".bak"}){const auto path=fs::path(leaf.string()+suffix);if(path==control){continue;}const auto bytes=read(path,MaximumDocument);p.guards.push_back({path,bytes.bytes?std::optional<std::string>(digest(*bytes.bytes)):std::nullopt});checkTemporaryAbsence(path);}
  }}
  for(const auto* name:{"biglybt.config","biglybt.config.bak","azureus.config","azureus.config.bak"}){const auto path=profile/name;const auto bytes=read(path,MaximumDocument);p.guards.push_back({path,bytes.bytes?std::optional<std::string>(digest(*bytes.bytes)):std::nullopt});checkTemporaryAbsence(path);}
  checkTemporaryAbsence(primary);checkTemporaryAbsence(configPath);
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
  const auto currentConversions=take(readOriginalFilenameConversions(lease));
  if(p.filenameConversions){require(*p.filenameConversions==currentConversions,ErrorCode::Conflict,"Compaction filename policy changed since approval");}
  const auto layout=p.filenameConversions?compat::originalLayout(source->document(),10000,*p.filenameConversions):compat::originalLayout(source->document(),10000);
  require(layout&&p.index<layout->files.size()&&!layout->files[p.index].padding,ErrorCode::InvalidFormat,"Frozen compaction layout/index invalid");
  if(!p.filenameConversions){const auto converted=take(readOriginalLayout(lease,source->document(),10000));require(layout->name==converted.name&&std::ranges::equal(layout->files,converted.files,[](const auto& a,const auto& b){return a.path==b.path;}),ErrorCode::Unsupported,"Legacy compaction names need explicit review before conversion");}
  const auto& file=layout->files[p.index];const auto geometry=edges({static_cast<std::uint64_t>(file.offset),static_cast<std::uint64_t>(file.bytes),static_cast<std::uint32_t>(layout->pieceLength)});
  require(p.oldLength<=static_cast<std::uint64_t>(file.bytes)&&p.prefix==std::min(p.oldLength,geometry.first)&&p.tailStart==std::min(p.oldLength,geometry.tailStart)
      &&p.tail==(p.oldLength>geometry.tailStart?std::min(geometry.tail,p.oldLength-geometry.tailStart):0),ErrorCode::InvalidFormat,"Frozen compact geometry differs from original metadata");
  auto inventory=parse(*p.inventory[1].before,MaximumDocument);const auto library=core::readLibrary(inventory);
  require(library&&library->issues.empty(),ErrorCode::InvalidFormat,"Frozen library invalid");const auto& saved=selected(*library,p.hash);
  const auto policy=compat::originalFilePolicy(row(inventory,saved,library->legacyMap),layout->files.size());require(policy&&(*policy)[p.index].skipped,ErrorCode::InvalidFormat,"Compaction was not explicitly skipped");
  const auto resolved=take(resolveOriginalSaveRoot(saved,*layout));std::vector<std::string> bases;
  for(const auto& f:layout->files){bases.push_back((fs::path(resolved.saveDirectory)/(layout->multifile?fs::path(*resolved.saveFile)/f.path:fs::path(*resolved.saveFile))).string());}
  const auto& preparedResume=field(source->document().root(),"resume",Node::Type::Dictionary);
  const auto& preparedData=field(preparedResume,"data",Node::Type::Dictionary);
  require(field(preparedData,"valid",Node::Type::Integer).integer==0&&field(preparedData,"blocks",Node::Type::Dictionary).children.empty(),ErrorCode::InvalidFormat,"Compaction source resume was not durably invalidated");
  if(const auto* flags=preparedData.find("resume data")){require(flags->type==Node::Type::Bytes&&std::ranges::all_of(flags->bytes,[](char c){return c=='\2';}),ErrorCode::InvalidFormat,"Compaction source contains trusted piece flags");}
  auto attrs=section(source->document().find("attributes"));require(field(attrs.root(),"resumecomplete",Node::Type::Integer).integer==1,ErrorCode::InvalidFormat,"Compaction source completion was not invalidated");const auto links=compat::resolveOriginalFileLinks(attrs.find("filelinks2"),bases);
  require(links.has_value(),ErrorCode::InvalidFormat,"Frozen original links invalid");
  const auto& link=links->byIndex[p.index];const auto base=fs::path(bases[p.index]);
  const auto expected=link?fs::path(link->target):base;pathShape(expected);
  const auto* suffix=attrs.find("incompfilesuffix");const auto suffixBytes=suffix&&suffix->type==Node::Type::Bytes?std::string(suffix->bytes):std::string{};
  require(!suffix||suffix->type==Node::Type::Bytes,ErrorCode::InvalidFormat,"Invalid frozen incomplete suffix");
  require(p.source==expected||(!link&&!suffixBytes.empty()&&suffixBytes.size()<=200&&suffixBytes.find('/')==suffixBytes.npos&&suffixBytes.find('\n')==suffixBytes.npos&&p.source==fs::path(base.string()+suffixBytes)),ErrorCode::UnsafePath,"Frozen compaction source differs from original indexed path");
  const auto within=[](const fs::path& child,const fs::path& parent){auto a=child.begin();for(auto z=parent.begin();z!=parent.end();++z,++a){if(a==child.end()||*a!=*z){return false;}}return true;};
  require(!within(p.source,p.profile)&&!within(p.profile,p.source),ErrorCode::UnsafePath,"Compaction payload intersects profile");
  for(const auto& part:p.source){require(part!=".git"&&!part.native().starts_with(".BiglyBTC."),ErrorCode::UnsafePath,"Compaction payload intersects reserved control paths");}
  // Re-check live library ownership on every replay. The guarded state/cache
  // bytes fix interpretation; real path/inode inspection catches moved aliases.
  const auto cacheRaw=read(p.profile/"active/cache.dat",MaximumDocument);
  std::optional<EncodedDocument> cache;
  if(cacheRaw.bytes){auto parsed=EncodedDocument::parse(*cacheRaw.bytes,compatibleConfigLimits());require(parsed.has_value(),ErrorCode::InvalidFormat,"Invalid guarded original cache");cache.emplace(std::move(*parsed));}
  const auto* cached=cache?cache->document().find("state"):nullptr;
  require(!cached||cached->type==Node::Type::List,ErrorCode::InvalidFormat,"Invalid guarded cache entries");
  const auto selectedRoot=fs::path(resolved.saveDirectory)/ *resolved.saveFile;
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
    const auto otherLayout=compat::originalLayout(otherState->document(),10000,currentConversions);require(otherLayout.has_value(),ErrorCode::Unsupported,"Other original layout unsupported during replay");
    inspected+=otherLayout->files.size();require(inspected<=100000,ErrorCode::Unsupported,"Compaction replay ownership bound");
    const auto otherSaved=take(resolveOriginalSaveRoot(other,*otherLayout));const auto otherRoot=fs::path(otherSaved.saveDirectory)/ *otherSaved.saveFile;
    require(!within(otherRoot,selectedRoot)&&!within(selectedRoot,otherRoot),ErrorCode::Conflict,"Another torrent overlaps compaction root");
    std::vector<std::string> otherBases;for(const auto& fileInfo:otherLayout->files){otherBases.push_back((fs::path(otherSaved.saveDirectory)/(otherLayout->multifile?fs::path(*otherSaved.saveFile)/fileInfo.path:fs::path(*otherSaved.saveFile))).string());}
    const auto otherLinks=compat::resolveOriginalFileLinks(otherAttrs.find("filelinks2"),otherBases);require(otherLinks.has_value(),ErrorCode::Unsupported,"Other original links unsupported during replay");
    const auto* suffixNode=otherAttrs.find("incompfilesuffix");require(!suffixNode||suffixNode->type==Node::Type::Bytes,ErrorCode::InvalidFormat,"Other incomplete suffix malformed");
    const auto suffixValue=suffixNode?std::string(suffixNode->bytes):std::string{};require(suffixValue.size()<=200&&suffixValue.find('/')==suffixValue.npos&&suffixValue.find('\n')==suffixValue.npos,ErrorCode::Unsupported,"Other incomplete suffix unsafe");
    for(std::size_t j=0;j<otherBases.size();++j){const auto& actualLink=otherLinks->byIndex[j];auto path=actualLink?fs::path(actualLink->target):fs::path(otherBases[j]);pathShape(path);auto actual=inspect(path);
      if(!actual&&!actualLink&&!suffixValue.empty()){path+=suffixValue;pathShape(path);actual=inspect(path);}
      require(!within(path,p.source)&&!within(p.source,path),ErrorCode::Conflict,"Another original file aliases compaction source");
      require(!actual||!payloadStat||actual->st_dev!=payloadStat->st_dev||actual->st_ino!=payloadStat->st_ino,ErrorCode::Conflict,"Another torrent shares compaction inode");
    }
  }
  const auto [stateAfter,completed]=projectState(lease,p,*layout,*p.state[1].before);
  require(p.state[0].after==stateAfter&&p.state[1].after==stateAfter
      &&p.inventory[0].after==*p.inventory[1].before&&p.inventory[1].after==projectInventory(*p.inventory[1].before,p.hash,completed),ErrorCode::InvalidFormat,"Compaction journal contains unrelated metadata edits");
  if(p.present){require(p.controls[0].after==dict({{"version","i0e"},{"length",i(p.oldLength)}}),ErrorCode::InvalidFormat,"Compaction control logical length differs");}
  std::set<fs::path> required;
  for(const auto& item:library->downloads){require(item.hash.has_value(),ErrorCode::InvalidFormat,"Frozen library identity unavailable");if(*item.hash!=p.hash){for(const auto* tail:{".dat",".dat.bak"}){required.insert(p.profile/"active"/(item.hash->hex()+tail));}}}
  for(const auto* name:{"cache.dat","cache.dat.bak"}){required.insert(p.profile/"active"/name);}
  const auto selectedControl=p.profile/"active"/p.hash.hex()/originalControlLeaf(p.index,OriginalStorageKind::Compact);
  for(std::size_t j=0;j<layout->files.size();++j){for(const auto kind:{OriginalStorageKind::Compact,OriginalStorageKind::Reordered}){const auto leaf=p.profile/"active"/p.hash.hex()/originalControlLeaf(j,kind);for(const auto* tail:{"",".bak"}){const auto path=fs::path(leaf.string()+tail);if(path!=selectedControl){required.insert(path);}}}}
  for(const auto* name:{"biglybt.config","biglybt.config.bak","azureus.config","azureus.config.bak"}){required.insert(p.profile/name);}
  std::set<fs::path> actual;for(const auto& guard:p.guards){require(actual.insert(guard.path).second,ErrorCode::InvalidFormat,"Duplicate compaction guard");}
  require(actual==required,ErrorCode::InvalidFormat,"Compaction journal omitted an ownership/source guard");
}
OriginalCompactionResult execute(const ProfileLease& lease,JournalFile& journal,bool recovered,Observer observer){
  const auto& p=journal.plan;require(!hasPendingFileOperation(lease),ErrorCode::Conflict,"File-operation recovery must finish before compaction replay");identity(lease,p);verifyGuards(p);verifyEdits(p.controls);verifyEdits(p.state);verifyEdits(p.inventory);validateIntent(lease,p);
  const auto source=inspect(p.source);require(source.has_value()==p.present,ErrorCode::Conflict,"Compaction payload presence changed");
  if(p.present){require(source->st_dev==p.sourceDevice&&source->st_ino==p.sourceInode&&source->st_nlink==1
      && (source->st_size==static_cast<off_t>(p.oldLength)||source->st_size==static_cast<off_t>(p.packed())),ErrorCode::Conflict,"Compaction payload identity/length changed");
    auto fd=openPayload(p.source,*source,true);require(rangeHash(fd.get(),0,p.prefix)==p.prefixHash,ErrorCode::Conflict,"Compaction preserved prefix changed");
    auto md=hash();stream(journal.fd.get(),journal.bodyOffset,p.tail,fd.get(),p.prefix,md);static_cast<void>(finish(md));
    require(::ftruncate(fd.get(),static_cast<off_t>(p.packed()))==0,ErrorCode::Io,"Cannot truncate compacted payload");sync(fd.get());
    const auto now=inspect(p.source);struct stat held{};require(now&&::fstat(fd.get(),&held)==0&&unchanged(held,*now)&&now->st_dev==p.sourceDevice&&now->st_ino==p.sourceInode&&now->st_size==static_cast<off_t>(p.packed()),ErrorCode::Conflict,"Compaction payload name changed during pack");
    require(rangeHash(fd.get(),0,p.prefix)==p.prefixHash&&rangeHash(fd.get(),p.prefix,p.tail)==rangeHash(journal.fd.get(),journal.bodyOffset,p.tail),ErrorCode::Io,"Compacted payload does not match durable preserved bytes");
  }
  struct stat journalNow{};const auto journalName=inspect(p.profile/Journal);
  require(::fstat(journal.fd.get(),&journalNow)==0&&journalName&&unchanged(journal.stamp,journalNow)&&unchanged(journal.stamp,*journalName),ErrorCode::Conflict,"Compaction journal changed during pack");
  observe(observer,OriginalCompactionStage::PayloadSynced);
  // Revalidate all frozen authorities again before any original-document write.
  verifyGuards(p);verifyEdits(p.controls);verifyEdits(p.state);verifyEdits(p.inventory);
  if(p.present){static_cast<void>(directory(p.controls[0].path.parent_path(),true));}
  publish(p.controls);observe(observer,OriginalCompactionStage::ControlPublished);
  publish(p.state);observe(observer,OriginalCompactionStage::StatePublished);
  publish(p.inventory);observe(observer,OriginalCompactionStage::InventoryPublished);
  const auto named=inspect(p.profile/Journal);struct stat held{};require(named&&::fstat(journal.fd.get(),&held)==0&&unchanged(held,journal.stamp)&&unchanged(*named,journal.stamp),ErrorCode::Conflict,"Compaction journal changed before retirement");
  erase(p.profile/Journal,journal.stamp);observe(observer,OriginalCompactionStage::Completed);
  return {p.hash,p.index,p.oldLength,p.packed(),recovered};
}
} // namespace

bool hasPendingOriginalCompaction(const ProfileLease& lease) noexcept {
  try{const auto root=fs::path(fileop::profilePath(lease));return fileop::inspect(root/Journal).has_value()||fileop::inspect(root/Saving).has_value();}catch(...){return true;}
}
std::expected<OriginalCompactionResult,Error> compactOriginalFile(const ProfileLease& lease,const core::TorrentHash& hash,std::size_t index,std::stop_token stop,Observer observer){
  return guarded([&]()->std::expected<OriginalCompactionResult,Error>{auto p=prepare(lease,hash,index,stop);fileop::Fd fd(-1);
    if(p.present){const auto actual=fileop::inspect(p.source);require(actual.has_value(),ErrorCode::Missing,"Compaction payload disappeared");fd=openPayload(p.source,*actual,false);}
    verifyGuards(p);verifyEdits(p.state);verifyEdits(p.inventory);saveJournal(lease,p,fd.get(),stop);
    observe(observer,OriginalCompactionStage::JournalSynced);auto journal=openJournal(lease);return execute(lease,journal,false,observer);});
}
std::expected<std::optional<OriginalCompactionResult>,Error> recoverOriginalCompaction(const ProfileLease& lease,Observer observer){
  return guarded([&]()->std::expected<std::optional<OriginalCompactionResult>,Error>{const auto root=fs::path(fileop::profilePath(lease));
    if(!fileop::inspect(root/Journal)){require(!hasPendingOriginalCompaction(lease),ErrorCode::Conflict,"Interrupted unpublished compaction journal requires explicit review; no payload packing was authorized");return std::nullopt;}
    require(!fileop::inspect(root/Saving),ErrorCode::Conflict,"Unexpected second compaction journal");auto journal=openJournal(lease);return std::optional<OriginalCompactionResult>(execute(lease,journal,true,observer));});
}
} // namespace biglybtc::storage
