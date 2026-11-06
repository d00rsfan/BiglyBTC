// SPDX-License-Identifier: GPL-2.0-or-later
// Manual offline interop target. Generated profiles only; no engine or payload owner.
#include "category_store.hpp"
#include "encoded_document.hpp"
#include <openssl/evp.h>
#include <array>
#include <cerrno>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
namespace s=biglybtc::storage;namespace c=biglybtc::core;namespace fs=std::filesystem;
namespace {
constexpr std::string_view Owned="BIGLYBTC_CATEGORY_JAVA_HANDOFF_PRIVATE_V1\n";
constexpr std::string_view Journal="BiglyBTC.category-delete.config";
void check(bool ok,const char* message){if(!ok){throw std::runtime_error(message);}}
template<class T>T take(std::expected<T,s::Error> value){if(!value){throw std::runtime_error(value.error().message);}return std::move(*value);}
void take(std::expected<void,s::Error> value){if(!value){throw std::runtime_error(value.error().message);}}
std::string b(std::string_view value){return std::to_string(value.size())+':'+std::string(value);}
std::string dict(std::initializer_list<std::pair<std::string,std::string>> fields){std::string out="d";for(const auto& [key,value]:fields){out+=b(key)+value;}return out+'e';}
std::string digest(std::string_view bytes){std::string out(20,'\0');unsigned size=0;check(EVP_Digest(bytes.data(),bytes.size(),reinterpret_cast<unsigned char*>(out.data()),&size,EVP_sha1(),nullptr)==1&&size==20,"Fixture digest");return out;}
std::string info(){return dict({{"length","i3e"},{"name",b("category.bin")},{"piece length","i16384e"},{"pieces",b(digest("abc"))}});}
c::TorrentHash hash(){auto value=c::TorrentHash::parse(digest(info()));check(bool(value),"Fixture raw hash");return *value;}
std::string read(const fs::path& path){std::ifstream in(path,std::ios::binary);check(bool(in),"Fixture read");return {std::istreambuf_iterator<char>(in),{}};}
void create(const fs::path& path,std::string_view bytes){
  const int fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);check(fd>=0,"Exclusive fixture output");
  std::size_t done=0;while(done<bytes.size()){const auto n=::write(fd,bytes.data()+done,bytes.size()-done);if(n<0&&errno==EINTR){continue;}if(n<=0){::close(fd);throw std::runtime_error("Fixture write");}done+=static_cast<std::size_t>(n);}
  const bool synced=::fsync(fd)==0;const bool closed=::close(fd)==0;check(synced&&closed,"Fixture sync");
}
void directory(const fs::path& path){check(fs::create_directory(path)&&::chmod(path.c_str(),0700)==0,"New private directory required");}
fs::path state(const fs::path& root){return root/"active"/(hash().hex()+".dat");}
fs::path payload(const fs::path& root){return fs::path(root.string()+"-payload")/"category.bin";}
std::string inventory(const fs::path& root){const auto key=hash();const auto bytes=key.bytes();const auto row=dict({{"torrent_hash",b(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()))},{"torrent",b((root/"input.torrent").string())},{"save_dir",b(payload(root).parent_path().string())},{"save_file",b("category.bin")},{"state","i70e"},{"persistent","i1e"}});return dict({{"downloads","l"+row+'e'},{"handoff.inventory",b("preserve")}});}
void owned(const fs::path& root){struct stat value{};check(root.is_absolute()&&::lstat(root.c_str(),&value)==0&&S_ISDIR(value.st_mode)&&value.st_uid==::getuid()&&(value.st_mode&0777)==0700,"Private owned profile required");check(read(root/"handoff-owned")==Owned,"Generated profile marker required");}
c::Document decoded(const fs::path& path){auto value=s::EncodedDocument::parse(read(path));check(bool(value),"Original saved document");return value->document();}
const c::Node& field(const c::Document& document,std::string_view key){const auto* value=document.find(key);check(value,"Missing original field");return *value;}
void unchangedData(const fs::path& root){
  check(read(payload(root))=="abc"&&read(root/"downloads.config")==inventory(root),"Payload/inventory changed");
  const auto document=decoded(state(root));const auto& originalInfo=field(document,"info");
  check(document.originalBytes().substr(originalInfo.begin,originalInfo.end-originalInfo.begin)==info(),"Original info changed");
  check(field(document,"handoff.future.root").bytes==std::string("root\0keep",9),"Unknown state root changed");
  const auto* future=field(document,"attributes").find("handoff.future");check(future&&future->bytes=="preserve","Unknown state attribute changed");
}
void emit(const fs::path& root){
  check(root.is_absolute()&&!fs::exists(root)&&!fs::exists(payload(root).parent_path()),"Fresh generated profile required");
  directory(root);directory(root/"active");directory(payload(root).parent_path());create(root/"handoff-owned",Owned);create(payload(root),"abc");
  create(root/"input.torrent",dict({{"announce",b("dht://"+hash().hex()+".dht/announce")},{"info",info()}}));
  create(state(root),dict({{"info",info()},{"attributes",dict({{"handoff.future",b("preserve")}})},{"handoff.future.root",b(std::string("root\0keep",9))}}));
  create(root/"downloads.config",inventory(root));
  create(root/"biglybt.config","de");
  create(root/"categories.config",dict({{"categories","l"+dict({{"name",b("FutureCategory")},{"maxup","i31e"},{"maxdown","i17e"},{"attr",dict({{"handoff.future",b("preserve")}})}})+'e'}}));
  auto lease=take(s::ProfileLease::acquire(root));auto store=take(s::CategoryStore::open(lease));
  auto definitions=take(store->create(take(store->snapshot()),"NativeCategory")).snapshot;
  const auto target=take(store->readTarget(definitions,hash()));const auto assigned=take(store->assign(definitions,target,std::string("NativeCategory")));
  check(assigned.changed&&assigned.snapshot.assignment()->resolved,"Native category not committed");unchangedData(root);take(store->close());
  std::cout<<"PASS_NATIVE_CATEGORY_CREATE_ASSIGN_HANDOFF\n";
}
void verify(const fs::path& root){
  owned(root);unchangedData(root);auto lease=take(s::ProfileLease::acquire(root));auto store=take(s::CategoryStore::open(lease));const auto defs=take(store->snapshot());
  check(defs.writable(),"Healthy original handoff is not writable");const auto target=take(store->readTarget(defs,hash()));
  check(target.assignment()->resolved&&target.assignment()->declaredName=="JavaCategory","Original assignment not read by native Store");
  const auto* rows=defs.categories();check(rows,"Original categories not read");bool native=false,java=false,future=false;
  for(const auto& row:rows->categories){native|=row.name=="NativeCategory";if(row.name=="JavaCategory"){java=row.rawMaxUpload==456;}if(row.name=="FutureCategory"){future=row.rawMaxUpload==31&&row.rawMaxDownload==17;}}
  check(native&&java&&future,"Original definition changes/limits not read");
  const auto document=decoded(state(root));const auto* javaValue=field(document,"attributes").find("handoff.java");check(javaValue&&javaValue->bytes=="written","Original state writer missing");
  const auto categories=decoded(root/"categories.config");const auto& list=field(categories,"categories");bool futureAttribute=false,javaAttribute=false;
  for(const auto& row:list.children){const auto* name=row.find("name");const auto* attrs=row.find("attr");if(!name||!attrs){continue;}if(name->bytes=="FutureCategory"){const auto* value=attrs->find("handoff.future");futureAttribute=value&&value->bytes=="preserve";}if(name->bytes=="JavaCategory"){const auto* value=attrs->find("handoff.java");javaAttribute=value&&value->bytes=="written";}}
  check(futureAttribute&&javaAttribute,"Unknown original category attributes changed");take(store->close());std::cout<<"PASS_NATIVE_READS_ORIGINAL_CATEGORY_HANDOFF\n";
}
bool acknowledge=false;
void prefixObserver(s::CategoryDeletionStage stage,std::size_t){if(stage==(acknowledge?s::CategoryDeletionStage::MemberAcknowledged:s::CategoryDeletionStage::MemberWritten)){::_exit(acknowledge?61:62);}}
void emitPrefix(const fs::path& root,bool ack){
  emit(root);const auto parent=::getpid();const auto child=::fork();check(child>=0,"Prefix fork");
  if(child==0){if(::prctl(PR_SET_PDEATHSIG,SIGKILL)!=0||::getppid()!=parent){::_exit(123);}acknowledge=ack;try{auto lease=take(s::ProfileLease::acquire(root));auto store=take(s::CategoryStore::open(lease));(void)store->remove(take(store->snapshot()),"NativeCategory",{},std::chrono::steady_clock::now()+std::chrono::seconds(10),prefixObserver);}catch(...){::_exit(121);}::_exit(122);}
  int status=0;check(::waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==(ack?61:62),"Exact durable prefix not reached");
  check(fs::exists(root/Journal),"Deletion prefix journal missing");std::cout<<"PASS_NATIVE_DEFINITION_LAST_PREFIX_CAPTURED\n";
}
void verifyPrefix(const fs::path& root){
  owned(root);unchangedData(root);const auto journal=read(root/Journal);const auto definitions=read(root/"categories.config");const auto before=read(state(root));
  auto lease=take(s::ProfileLease::acquire(root));auto store=take(s::CategoryStore::open(lease));const auto snapshot=take(store->snapshot());check(!snapshot.writable(),"Pending deletion became writable");
  const auto target=take(store->readTarget(snapshot,hash()));check(!target.assignment()->declaredName,"Cleared durable prefix differs");
  bool retained=false;for(const auto& row:snapshot.categories()->categories){retained|=row.name=="NativeCategory";}check(retained,"Definition deleted before all prefixes acknowledged");
  check(!store->create(snapshot,"MustNotReplay"),"Pending journal authorized mutation");take(store->close());
  check(read(root/Journal)==journal&&read(root/"categories.config")==definitions&&read(state(root))==before,"Read-only prefix was replayed/changed");std::cout<<"PASS_NATIVE_PREFIX_PRESERVED_REVIEW_ONLY\n";
}
}
int main(int argc,char** argv){try{
  if(argc==3){const fs::path root=argv[2];const std::string command=argv[1];if(command=="--emit"){emit(root);}else if(command=="--verify"){verify(root);}else if(command=="--verify-prefix"){verifyPrefix(root);}else{throw std::runtime_error("Unknown private handoff command");}}
  else if(argc==4&&std::string_view(argv[1])=="--emit-prefix"){const std::string_view kind=argv[2];check(kind=="acknowledged"||kind=="ambiguous","Unknown prefix kind");emitPrefix(argv[3],kind=="acknowledged");}
  else{throw std::runtime_error("Private emit/verify profile arguments required");}return 0;
}catch(const std::exception& failure){std::cerr<<failure.what()<<'\n';return 1;}}
