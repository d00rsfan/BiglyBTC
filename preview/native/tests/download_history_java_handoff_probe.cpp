// SPDX-License-Identifier: GPL-2.0-or-later
// Manual offline interop only. No engine, payload owner or live profile.
#include "download_history_store.hpp"
#include <array>
#include <cerrno>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
namespace s=biglybtc::storage;namespace c=biglybtc::core;namespace fs=std::filesystem;
namespace {
constexpr std::string_view Owned="BIGLYBTC_HISTORY_JAVA_HANDOFF_PRIVATE_V1\n",Intent="BiglyBTC.download-history.config";
constexpr std::int64_t Keep=0x1122334455667788LL,Remove=-9,Dead=37,DeadRemove=-41;
void check(bool ok,const char* why){if(!ok){throw std::runtime_error(why);}}
template<class T>T take(std::expected<T,s::Error> value){if(!value){throw std::runtime_error(value.error().message);}return std::move(*value);}
void take(std::expected<void,s::Error> value){if(!value){throw std::runtime_error(value.error().message);}}
std::string read(const fs::path& path){std::ifstream in(path,std::ios::binary);check(bool(in),"Generated fixture read");return {std::istreambuf_iterator<char>(in),{}};}
void create(const fs::path& path,std::string_view bytes){const int fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);check(fd>=0,"Exclusive fixture create");std::size_t done=0;while(done<bytes.size()){const auto n=::write(fd,bytes.data()+done,bytes.size()-done);if(n<0&&errno==EINTR){continue;}if(n<=0){::close(fd);throw std::runtime_error("Fixture write");}done+=static_cast<std::size_t>(n);}const bool synced=::fsync(fd)==0;const bool closed=::close(fd)==0;check(synced&&closed,"Fixture sync");}
void directory(const fs::path& path){check(fs::create_directory(path)&&::chmod(path.c_str(),0700)==0,"Fresh private directory required");}
fs::path payload(const fs::path& root){return fs::path(root.string()+"-payload")/"history.bin";}
void owned(const fs::path& root){struct stat st{};check(root.is_absolute()&&::lstat(root.c_str(),&st)==0&&S_ISDIR(st.st_mode)&&st.st_uid==::geteuid()&&(st.st_mode&0777)==0700&&read(root/"handoff-owned")==Owned,"Marked generated private profile required");check(read(payload(root))=="history payload unchanged","Payload sentinel changed");}
std::string hash(unsigned length,unsigned seed){std::string out(length,'\0');for(unsigned i=0;i<length;++i){out[i]=static_cast<char>(seed+i);}return out;}
std::string bytes(std::string_view value){return std::to_string(value.size())+':'+std::string(value);}
std::string fixture(const fs::path& root,bool dead){
  // Actual production typed append, plus deliberately opaque fixture extensions
  // whose loss after the ORIGINAL Java writer is explicitly expected.
  auto doc=c::Document::parse("d14:handoff.future13:root-unknowns7:recordslee");check(bool(doc),"Fixture root document");
  std::string raw=doc->originalBytes();
  for(const auto id:dead?std::array<std::int64_t,2>{Dead,DeadRemove}:std::array<std::int64_t,2>{Keep,Remove}){
    c::DownloadHistoryNewRecord row;row.uid=id;row.hash=hash(20,id==Keep?1:3);row.hashV2=hash(32,id==Keep?5:7);
    row.name=id==Keep?"Native active keep":id==Dead?"Native dead keep":"Native selected removal";row.saveLocation=payload(root).string();
    row.size=1234567890123LL;row.addTime=1700000000123LL;row.completeTime=2000;row.removeTime=dead?3000:0;row.tags={"A","β","A"};
    auto next=c::editSavedDownloadHistory(raw,c::DownloadHistoryAppend{std::move(row)});check(bool(next)&&next->changed,"Production history append");raw=next->value.source->originalBytes();
  }
  auto value=c::Document::parse(raw);check(bool(value),"Fixture appended source");const auto* records=value->find("records");check(records,"Fixture records");std::string list="l";
  for(const auto& record:records->children){auto row=c::Document::parse(value->originalBytes().substr(record.begin,record.end-record.begin));check(row&&row->replaceValue("handoff.row",bytes("row-unknown")),"Fixture opaque row extension");list+=row->serialize();}list+='e';check(value->replaceValue("records",list).has_value(),"Fixture records extension");return value->serialize();
}
void emit(const fs::path& root){
  check(root.is_absolute()&&!fs::exists(root)&&!fs::exists(payload(root).parent_path()),"Fresh generated paths only");directory(root);directory(payload(root).parent_path());create(root/"handoff-owned",Owned);create(payload(root),"history payload unchanged");
  auto lease=take(s::ProfileLease::acquire(root));(void)take(s::ConfigFile::createMissing(lease,"biglybt.config","d24:Download History Enabledi1ee"));
  (void)take(s::ConfigFile::createMissing(lease,"dlhistorya.config",fixture(root,false)));(void)take(s::ConfigFile::createMissing(lease,"dlhistoryd.config",fixture(root,true)));
  auto store=take(s::DownloadHistoryStore::open(lease));auto changed=take(store->apply(take(store->snapshot()),s::DownloadHistorySide::Active,c::DownloadHistorySetName{Keep,"Native renamed μ"}));check(changed.changed&&changed.snapshot.history(s::DownloadHistorySide::Active)->source->find("handoff.future"),"Actual Store write/raw preservation");take(store->close());
  // Original manager construction is disabled before it can register search.
  auto config=take(s::ConfigFile::load(lease,"biglybt.config"));check(config.document().replaceValue("Download History Enabled","i0e").has_value(),"Disable fixture manager startup");take(config.save(lease));
  owned(root);std::cout<<"PASS_NATIVE_HISTORY_STORE_EMIT\n";
}
const c::SavedDownloadHistoryRecord& one(const c::SavedDownloadHistory* rows,std::int64_t uid){check(rows&&rows->records.size()==1&&rows->records.front().uid==uid,"Exact original remaining record");return rows->records.front();}
void verify(const fs::path& root,bool final){
  owned(root);auto lease=take(s::ProfileLease::acquire(root));auto store=take(s::DownloadHistoryStore::open(lease));const auto snapshot=take(store->snapshot());check(snapshot.writable()&&!snapshot.enabled(),"Actual original disabled primary handoff");
  const auto& active=one(snapshot.history(s::DownloadHistorySide::Active),Keep);const auto& dead=one(snapshot.history(s::DownloadHistorySide::Dead),Dead);
  check(active.name==(final?"Native return μ":"Native renamed μ")&&dead.name=="Native dead keep"&&active.hash==hash(20,1)&&active.hashV2==hash(32,5)&&dead.hash==hash(20,3)&&dead.hashV2==hash(32,7),"Actual original fields/hash readback");
  for(const auto* row:{&active,&dead}){check(row->size==1234567890123LL&&row->addTime==1700000000123LL&&row->completeTime==2000&&row->saveLocation==payload(root).string()&&row->tags==std::vector<std::string>{"A","β","A"},"Original compatible record fields");}
  check(active.removeTime==0&&dead.removeTime==3000,"Original remove times");
  for(const auto side:{s::DownloadHistorySide::Active,s::DownloadHistorySide::Dead}){const auto& value=*snapshot.history(side);check(!value.source->find("handoff.future")&&!value.source->find("records")->children.front().find("handoff.row"),"Original rebuilt writer unexpectedly retained fixture extensions");}
  if(!final){
    // Re-enable only the Native metadata editor, then disable again before the
    // next original read. No setting lifecycle/reset or fresh event is claimed.
    take(store->close());auto config=take(s::ConfigFile::load(lease,"biglybt.config"));check(config.document().replaceValue("Download History Enabled","i1e").has_value(),"Native return setting");take(config.save(lease));
    store=take(s::DownloadHistoryStore::open(lease));const auto changed=take(store->apply(take(store->snapshot()),s::DownloadHistorySide::Active,c::DownloadHistorySetName{Keep,"Native return μ"}));check(changed.changed,"Actual Native return Store write");take(store->close());
    config=take(s::ConfigFile::load(lease,"biglybt.config"));check(config.document().replaceValue("Download History Enabled","i0e").has_value(),"Return disabled original reader");take(config.save(lease));
  }else{take(store->close());}
  std::cout<<(final?"PASS_NATIVE_FINAL_HISTORY_HANDOFF":"PASS_NATIVE_READS_ORIGINAL_HISTORY_AND_WRITES_RETURN")<<'\n';
}
void prefix(s::DownloadHistoryWriteStage stage,std::size_t count){if(stage==s::DownloadHistoryWriteStage::FilePublished&&count==1){::_exit(61);}}
void emitPrefix(const fs::path& root){emit(root);const auto parent=::getpid();const auto child=::fork();check(child>=0,"Actual prefix fork");if(child==0){if(::prctl(PR_SET_PDEATHSIG,SIGKILL)!=0||::getppid()!=parent){::_exit(123);}try{auto lease=take(s::ProfileLease::acquire(root));auto store=take(s::DownloadHistoryStore::open(lease));(void)store->clear(take(store->snapshot()),{},std::chrono::steady_clock::now()+std::chrono::seconds(10),prefix);}catch(...){::_exit(121);}::_exit(122);}int status=0;check(::waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==61,"Actual dead-first prefix required");std::cout<<"PASS_NATIVE_HISTORY_DEAD_FIRST_PREFIX\n";}
void verifyPrefix(const fs::path& root){owned(root);const auto a=read(root/"dlhistorya.config"),d=read(root/"dlhistoryd.config"),intent=read(root/Intent);auto lease=take(s::ProfileLease::acquire(root));auto store=take(s::DownloadHistoryStore::open(lease));const auto snapshot=take(store->snapshot());check(!snapshot.writable()&&snapshot.review()==s::DownloadHistoryReadOnly::PendingIntent&&snapshot.history(s::DownloadHistorySide::Active)->records.size()==2&&snapshot.history(s::DownloadHistorySide::Dead)->records.empty(),"Actual prefix review only");check(!store->apply(snapshot,s::DownloadHistorySide::Active,c::DownloadHistoryErase{Keep}),"Intent permitted replay");take(store->close());check(read(root/"dlhistorya.config")==a&&read(root/"dlhistoryd.config")==d&&read(root/Intent)==intent,"Prefix changed/replayed");std::cout<<"PASS_NATIVE_HISTORY_PREFIX_PRESERVED_REVIEW_ONLY\n";}
}
int main(int argc,char** argv){try{check(argc==3,"Private mode/profile required");const fs::path root=argv[2];const std::string_view mode=argv[1];if(mode=="--emit"){emit(root);}else if(mode=="--verify-return"){verify(root,false);}else if(mode=="--verify-final"){verify(root,true);}else if(mode=="--emit-prefix"){emitPrefix(root);}else if(mode=="--verify-prefix"){verifyPrefix(root);}else{throw std::runtime_error("Unknown fixture mode");}return 0;}catch(const std::exception& failure){std::cerr<<failure.what()<<'\n';return 1;}}
