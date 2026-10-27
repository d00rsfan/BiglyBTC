#include "original_control.hpp"
#include <filesystem>
#include <cstdlib>
#include <iterator>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
namespace fs=std::filesystem;
namespace st=biglybtc::storage;
namespace en=biglybtc::engine::detail;
namespace {
void check(bool value,const char* message){if(!value){throw std::runtime_error(message);}}
template<class T>T take(std::expected<T,st::Error> value){if(!value){throw std::runtime_error(value.error().message);}return std::move(*value);}
void take(std::expected<void,st::Error> value){if(!value){throw std::runtime_error(value.error().message);}}
void write(const fs::path& path,std::string_view value){fs::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary);file.write(value.data(),static_cast<std::streamsize>(value.size()));check(bool(file),"write generated control");}
std::string read(const fs::path& path){std::ifstream file(path,std::ios::binary);check(bool(file),"read generated control");return {std::istreambuf_iterator<char>(file),{}};}
std::string compact(unsigned value){return "d6:lengthi"+std::to_string(value)+"e7:versioni0ee";}
struct Fixture {
  fs::path root,profile,dir,leaf;st::ProfileLease lease;
  static fs::path create(){char name[]="/tmp/biglybtc-control-XXXXXX";const auto* path=::mkdtemp(name);check(path,"create generated control area");return path;}
  static st::ProfileLease heldProfile(const fs::path& profile){fs::create_directories(profile/"active");return take(st::ProfileLease::acquire(profile));}
  Fixture():root(create()),profile(root/"profile"),dir(profile/"active"/std::string(40,'A')),leaf(dir/"fmfile0.dat"),lease(heldProfile(profile)){}
  ~Fixture(){std::error_code error;fs::remove_all(root,error);}
  std::shared_ptr<en::OriginalControl> open(bool create=true){return take(en::OriginalControl::open(lease,std::string(40,'A'),0,st::OriginalStorageKind::Compact,create));}
};
void originalSelectionAndPublication(){
  {Fixture f;check(!f.open(false),"read-only missing control directory remains absent");check(!fs::exists(f.dir),"no creation on optional read");auto owner=f.open();check(!take(owner->load()),"missing control distinct from recorded zero");auto publish=owner->publisher();take(publish({},compact(0)));check(take(owner->load())==compact(0),"actual zero control preserved");take(publish(compact(0),compact(8)));check(read(f.leaf)==compact(8)&&read(fs::path(f.leaf.string()+".bak"))==compact(0),"valid primary rotated with exact original backup");write(f.dir/"foreign","untouched");take(owner->retire());check(!fs::exists(f.leaf)&&!fs::exists(fs::path(f.leaf.string()+".bak"))&&read(f.dir/"foreign")=="untouched","retirement removes exact primary/backup only");}
  for(const auto broken:{"not-bencode","d6:length3:bad7:versioni0ee"}){Fixture f;write(f.leaf,broken);write(fs::path(f.leaf.string()+".bak"),compact(4));auto owner=f.open(false);check(take(owner->load())==compact(4),"malformed primary selects structurally valid backup");take(owner->publisher()(compact(4),compact(9)));check(read(f.leaf)==compact(9)&&read(fs::path(f.leaf.string()+".bak"))==compact(4),"damaged primary never replaces the valid fallback backup");}
  {Fixture f;const auto leaf=f.dir/"fmfile1.dat.2";const std::string reordered="d3:leni4e4:nexti1e6:pieces4:"+std::string(4,'\0')+"2:sti3ee";write(leaf,"de");write(fs::path(leaf.string()+".bak"),reordered);auto owner=take(en::OriginalControl::open(f.lease,std::string(40,'A'),1,st::OriginalStorageKind::Reordered,false));check(take(owner->load())==reordered,"reordered required fields select valid raw backup without geometry invention");}
}
void readOnlyGeometrySelectionNeverRepairsCandidates(){
  Fixture f;const auto bad=compact(9999999),good=compact(64);write(f.leaf,bad);const auto backup=fs::path(f.leaf.string()+".bak");write(backup,good);auto owner=f.open(false);
  check(take(owner->load())==bad,"Generic original envelope selection stays unchanged");
  check(take(owner->loadValidated({17,65536,16384}))==good,"Read-only codec geometry selects valid original backup without replacing primary");
  check(read(f.leaf)==bad&&read(backup)==good&&!fs::exists(fs::path(f.leaf.string()+".saving")),"Read-only candidate selection performs no repair/publication");
  fs::rename(backup,f.dir/"retained-backup");write(backup,good);check(!owner->loadValidated({17,65536,16384}),"Same-byte selected backup replacement never becomes a fresh grant");
}
void identityCasAcrossCalls(){
  {Fixture f;write(f.leaf,compact(4));auto owner=f.open(false);auto publish=owner->publisher();fs::rename(f.leaf,f.dir/"old-primary");write(f.leaf,compact(4));check(!publish(compact(4),compact(8))&&!owner->retire()&&!fs::exists(fs::path(f.leaf.string()+".saving")),"same-byte replacement inode refuses publication and retirement before effects");check(read(f.leaf)==compact(4)&&read(f.dir/"old-primary")==compact(4),"foreign replacement and original retained");}
  {Fixture f;write(f.leaf,compact(4));write(fs::path(f.leaf.string()+".bak"),compact(2));auto owner=f.open(false);fs::rename(fs::path(f.leaf.string()+".bak"),f.dir/"old-backup");write(fs::path(f.leaf.string()+".bak"),compact(2));check(!owner->publisher()(compact(4),compact(8))&&read(f.leaf)==compact(4),"same-byte backup replacement also invalidates accepted candidate identity");}
  {Fixture f;write(f.leaf,compact(4));auto owner=f.open(false);check(!owner->publisher()(compact(7),compact(8))&&!owner->publisher()(compact(4),"de")&&read(f.leaf)==compact(4)&&!fs::exists(fs::path(f.leaf.string()+".saving")),"wrong expected bytes and invalid replacement refuse before temporary creation");}
}
void unsafeCandidatesAndDetachedDirectories(){
  {Fixture f;const auto outside=f.root/"outside";write(outside,compact(4));fs::create_directories(f.dir);fs::create_symlink(outside,f.leaf);check(!en::OriginalControl::open(f.lease,std::string(40,'A'),0,st::OriginalStorageKind::Compact,false)&&read(outside)==compact(4),"symbolic control never followed");}
  {Fixture f;const auto outside=f.root/"outside";write(outside,compact(4));fs::create_directories(f.dir);fs::create_hard_link(outside,f.leaf);check(!en::OriginalControl::open(f.lease,std::string(40,'A'),0,st::OriginalStorageKind::Compact,false)&&read(outside)==compact(4),"hard-linked control refuses");}
  {Fixture f;write(f.leaf,compact(4));write(fs::path(f.leaf.string()+".bak.saving"),compact(9));check(!en::OriginalControl::open(f.lease,std::string(40,'A'),0,st::OriginalStorageKind::Compact,false)&&read(f.leaf)==compact(4),"pending backup saving refuses recovery selection");}
  {Fixture f;write(f.leaf,compact(4));auto owner=f.open(false);const auto detached=f.profile/"active/detached";fs::rename(f.dir,detached);write(f.leaf,compact(4));check(!owner->load()&&!owner->publisher()(compact(4),compact(8))&&!owner->retire(),"replaced hash directory cannot receive detached writes or retirement");check(read(f.leaf)==compact(4)&&read(detached/"fmfile0.dat")==compact(4),"both original and replacement directory leaves retained");}
  {Fixture f;check(!en::OriginalControl::open(f.lease,"A",0,st::OriginalStorageKind::Compact,true)&&!fs::exists(f.profile/"active/A"),"short pseudo-hash cannot authorize a directory");}
}
void publisherOwnsOnlyItsSafeControlLifetime(){
  Fixture f;auto owner=f.open();auto publish=owner->publisher();owner.reset();take(publish({},compact(3)));check(read(f.leaf)==compact(3),"publisher safely retains authenticated directory descriptors");check(!st::ProfileLease::acquire(f.profile),"publisher lifetime does not bypass actual held profile lease");
}
}
int main(){try{originalSelectionAndPublication();readOnlyGeometrySelectionNeverRepairsCandidates();identityCasAcrossCalls();unsafeCandidatesAndDetachedDirectories();publisherOwnsOnlyItsSafeControlLifetime();std::cout<<"five generated original-control families passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
