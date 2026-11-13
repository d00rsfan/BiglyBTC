// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
using namespace biglybtc;
namespace fs=std::filesystem;
namespace {
int families=0;std::size_t assertions=0;
void check(bool condition,const char* message){++assertions;if(!condition){throw std::runtime_error(message);}}
std::string b(std::string_view s){return std::to_string(s.size())+":"+std::string(s);}
template<class T>T take(std::expected<T,storage::Error> result){check(bool(result),result?"":result.error().message.c_str());return std::move(*result);}
struct Area {
 fs::path root;storage::ProfileLease lease;
 static fs::path make(){char path[]="/tmp/biglybtc-config-snapshots-XXXXXX";const auto* value=::mkdtemp(path);check(value!=nullptr,"Create generated root");return value;}
 Area():root(make()),lease(take(storage::ProfileLease::acquire(root))){}
 ~Area(){std::error_code error;fs::remove_all(root,error);}
 void write(std::string_view name,std::string_view bytes){std::ofstream f(root/std::string(name),std::ios::binary|std::ios::trunc);f.write(bytes.data(),std::streamsize(bytes.size()));f.flush();check(bool(f),"Write generated metadata");}
 std::string read(std::string_view name){std::ifstream f(root/std::string(name),std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
 storage::ConfigFile load(std::string name="sample.config",core::Limits limits=storage::compatibleConfigLimits()){return take(storage::ConfigFile::load(lease,std::move(name),limits));}
};
core::Document document(std::string raw){auto parsed=core::Document::parse(std::move(raw),storage::compatibleConfigLimits());check(bool(parsed),"Parse generated document");return std::move(*parsed);}
std::string raw(int value=1){return "d1:ni"+std::to_string(value)+"e6:opaqued3:raw"+b(std::string("\0\xffQ",3))+"ee";}
std::int64_t integer(const core::Document& d){const auto* n=d.find("n");check(n&&n->type==core::Node::Type::Integer,"Expected generated integer");return n->integer;}
template<class F>void family(const char* name,F&& body){body();++families;std::cout<<"PASS "<<name<<'\n';}
}
int main(){try{
 family("copy isolation, no-op and failed edit preserve borrowed trees",[]{
  auto original=document(raw());auto copy=original;const auto* root=&original.root();const auto* borrowed=copy.find("opaque");check(&copy.root()==root,"Cheap immutable snapshot");check(copy.setInteger("n",1)&&&copy.root()==root,"No-op does not detach");check(!copy.replaceValue("opaque","broken")&&&copy.root()==root&&copy.find("opaque")==borrowed,"Rejected edit preserves borrowed pointer");check(copy.setInteger("n",2),"Edit detached copy");check(integer(original)==1&&integer(copy)==2&&&copy.root()!=root,"Copy does not change original");auto branch=copy;check(bool(branch.replaceValue("opaque","d1:xi9ee")),"Replace nested value");check(copy.find("opaque")->find("raw")&&branch.find("opaque")->find("x"),"Nested replacement stays isolated");check(original.serialize()==raw(),"Original bytes remain exact");check(integer(document(copy.serialize()))==2,"Modified snapshot round trip");
 });
 family("separate snapshots may edit concurrently",[]{auto original=document(raw());auto a=original,c=original;bool firstOK=false,secondOK=false;std::jthread first([&]{firstOK=a.setInteger("n",7);});std::jthread second([&]{secondOK=c.setInteger("n",8);});first.join();second.join();check(firstOK&&secondOK,"Both snapshot edits succeeded");check(integer(original)==1&&integer(a)==7&&integer(c)==8,"Concurrent snapshots remain independent");});
 family("retained config loads share one immutable tree",[]{Area a;a.write("sample.config",raw());std::vector<storage::ConfigFile> held;std::set<const core::Node*> roots;for(int n=0;n<128;++n){held.push_back(a.load());roots.insert(&held.back().document().root());}const auto stats=a.lease.configReadCacheStatistics();check(roots.size()==1,"Retained readers do not clone trees");check(stats.loads==128&&stats.parses==1&&stats.hits==127,"One parse across repeated readers");check(stats.entries==1&&stats.retainedBytes<=64*1024*1024,"Bounded cache");});
 family("mutations detach, successful save refreshes, stale CAS refuses",[]{Area a;a.write("sample.config",raw());auto changed=a.load(),stale=a.load();check(changed.document().setInteger("n",9),"Local edit");check(integer(a.load().document())==1,"Unsaved edit cannot poison cache");check(bool(changed.save(a.lease)),"Compatible save");check(integer(a.load().document())==9,"Fresh bytes invalidate cached parse");check(stale.document().setInteger("n",8),"Stale edit");const auto saved=stale.save(a.lease);check(!saved&&saved.error().code==storage::ErrorCode::Conflict,"Stale snapshot cannot overwrite actual save");check(a.read("sample.config")==raw(9),"Opaque and byte ordering preserved");});
 family("external same-size edits with restored mtime are still observed",[]{Area a;a.write("sample.config",raw());auto stale=a.load();struct stat before{},after{};check(::stat((a.root/"sample.config").c_str(),&before)==0,"Initial file identity");a.write("sample.config",raw(2));const timespec times[]{before.st_atim,before.st_mtim};check(::utimensat(AT_FDCWD,(a.root/"sample.config").c_str(),times,0)==0,"Restore generated mtime");check(::stat((a.root/"sample.config").c_str(),&after)==0&&before.st_ino==after.st_ino&&before.st_size==after.st_size&&before.st_mtim.tv_sec==after.st_mtim.tv_sec&&before.st_mtim.tv_nsec==after.st_mtim.tv_nsec,"Same size/inode/mtime fixture");check(integer(a.load().document())==2,"Full-byte check sees changed content");check(a.lease.configReadCacheStatistics().parses==2,"Changed bytes reparse");check(stale.document().setInteger("n",3)&&!stale.save(a.lease),"Original CAS still rejects stale writes");});
 family("actual lease aliases share cache, independent profiles do not",[]{Area a,other;a.write("sample.config",raw(1));other.write("sample.config",raw(2));auto first=a.load();auto alias=take(a.lease.retain());auto same=take(storage::ConfigFile::load(*alias,"sample.config"));check(&first.document().root()==&same.document().root(),"Same OFD aliases share cache");check(integer(other.load().document())==2,"Other actual profile is isolated");check(a.lease.configReadCacheStatistics().hits==1&&other.lease.configReadCacheStatistics().hits==0,"Separate cache lineages");});
 family("strict limits cannot borrow a permissive cached parse",[]{Area a;a.write("sample.config",raw());auto first=a.load();auto strict=storage::compatibleConfigLimits();strict.nodes=2;const auto limited=storage::ConfigFile::load(a.lease,"sample.config",strict);check(!limited&&limited.error().code==storage::ErrorCode::InvalidFormat,"Node limit still enforced");strict=storage::compatibleConfigLimits();strict.inputBytes=4;check(!storage::ConfigFile::load(a.lease,"sample.config",strict),"Fresh input bound enforced");check(integer(a.load().document())==1,"Strict refusal does not poison valid cache");});
 family("recovery priority stays fresh after corrupt or absent primary",[]{Area a;a.write("sample.config",raw());auto first=a.load();a.write("sample.config.bak",raw(7));a.write("sample.config","broken");auto recovered=a.load();check(recovered.recoverySource()==storage::RecoverySource::Backup&&integer(recovered.document())==7,"Backup selected after actual corrupt primary");a.write("sample.config",raw());auto restored=a.load();check(restored.recoverySource()==storage::RecoverySource::Primary&&integer(restored.document())==1,"Restored primary wins cached recovery");fs::remove(a.root/"sample.config");a.write("sample.config.saving",raw(8));auto saving=a.load();check(saving.recoverySource()==storage::RecoverySource::Saving&&integer(saving.document())==8,"Missing primary retains original saving priority");});
 family("entry eviction does not invalidate retained snapshots",[]{Area a;std::optional<core::Document> retained;for(int n=0;n<12;++n){const auto name="generated"+std::to_string(n)+".config";a.write(name,raw(n));auto loaded=a.load(name);if(n==0){retained=loaded.document();}}const auto stats=a.lease.configReadCacheStatistics();check(stats.entries==8&&stats.evictions==4&&stats.retainedBytes<=64*1024*1024,"Bounded LRU entry count");check(retained&&integer(*retained)==0&&retained->serialize()==raw(0),"Evicted backing tree stays alive for retained reader");});
 family("capacity budget evicts large parsed trees",[]{Area a;const auto large="d1:ni1e4:blob"+b(std::string(9*1024*1024,'p'))+'e';for(int n=0;n<8;++n){const auto name="large"+std::to_string(n)+".config";a.write(name,large);auto loaded=a.load(name);check(integer(loaded.document())==1,"Large generated document intact");}const auto stats=a.lease.configReadCacheStatistics();check(stats.evictions>0&&stats.entries<8&&stats.retainedBytes<=64*1024*1024,"Byte capacity independently bounded");});
 family("symlink and nonregular primary cannot use cached bytes",[]{Area a;a.write("sample.config",raw());auto first=a.load();a.write("other.config",raw());fs::remove(a.root/"sample.config");fs::create_symlink("other.config",a.root/"sample.config");check(!storage::ConfigFile::load(a.lease,"sample.config"),"Cache does not bypass no-follow");});
 std::cout<<"PASS_IMMUTABLE_CONFIG "<<families<<" families "<<assertions<<" assertions\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL_IMMUTABLE_CONFIG "<<e.what()<<'\n';return 1;}}
