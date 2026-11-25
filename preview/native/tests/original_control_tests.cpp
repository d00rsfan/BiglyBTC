#include "original_control.hpp"
#include <chrono>
#include <filesystem>
#include <cstdlib>
#include <iterator>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <array>
#include <barrier>
#include <thread>
namespace fs=std::filesystem;
namespace st=biglybtc::storage;
namespace {
void check(bool value,const char* message){if(!value){throw std::runtime_error(message);}}
template<class T>T take(std::expected<T,st::Error> value){if(!value){throw std::runtime_error(value.error().message);}return std::move(*value);}
void take(std::expected<void,st::Error> value){if(!value){throw std::runtime_error(value.error().message);}}
void write(const fs::path& path,std::string_view value){fs::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary);file.write(value.data(),static_cast<std::streamsize>(value.size()));check(bool(file),"write generated control");}
std::string read(const fs::path& path){std::ifstream file(path,std::ios::binary);check(bool(file),"read generated control");return {std::istreambuf_iterator<char>(file),{}};}
fs::path suffixed(fs::path path,std::string_view suffix){path += suffix;return path;}
std::string compact(unsigned value){return "d6:lengthi"+std::to_string(value)+"e7:versioni0ee";}
struct Fixture {
  fs::path root,profile,dir,leaf;st::ProfileLease lease;
  static fs::path create(){
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    auto name = fs::path(u8"biglybtc-control-\u03bb-");
    name += std::to_string(id);
    const auto path = fs::temp_directory_path() / name;
    check(fs::create_directory(path), "create generated control area");
    return fs::canonical(path);
  }
  static st::ProfileLease heldProfile(const fs::path& profile){fs::create_directories(profile/"active");return take(st::ProfileLease::acquire(profile));}
  Fixture():root(create()),profile(root/"profile"),dir(profile/"active"/std::string(40,'A')),leaf(dir/"fmfile0.dat"),lease(heldProfile(profile)){}
  ~Fixture(){std::error_code error;fs::remove_all(root,error);}
  std::shared_ptr<st::OriginalControl> open(bool create=true){return take(st::OriginalControl::open(lease,std::string(40,'A'),0,st::OriginalStorageKind::Compact,create));}
};
void originalSelectionAndPublication(){
  {Fixture f;check(!f.open(false),"read-only missing control directory remains absent");check(!fs::exists(f.dir),"no creation on optional read");auto owner=f.open();check(!take(owner->load()),"missing control distinct from recorded zero");auto publish=owner->publisher();take(publish({},compact(0)));check(take(owner->load())==compact(0),"actual zero control preserved");take(publish(compact(0),compact(8)));check(read(f.leaf)==compact(8)&&read(suffixed(f.leaf,".bak"))==compact(0),"valid primary rotated with exact original backup");write(f.dir/"foreign","untouched");take(owner->retire());check(!fs::exists(f.leaf)&&!fs::exists(suffixed(f.leaf,".bak"))&&read(f.dir/"foreign")=="untouched","retirement removes exact primary/backup only");}
  for(const auto broken:{"not-bencode","d6:length3:bad7:versioni0ee"}){Fixture f;write(f.leaf,broken);write(suffixed(f.leaf,".bak"),compact(4));auto owner=f.open(false);check(take(owner->load())==compact(4),"malformed primary selects structurally valid backup");take(owner->publisher()(compact(4),compact(9)));check(read(f.leaf)==compact(9)&&read(suffixed(f.leaf,".bak"))==compact(4),"damaged primary never replaces the valid fallback backup");}
  {Fixture f;const auto leaf=f.dir/"fmfile1.dat.2";const std::string reordered="d3:leni4e4:nexti1e6:pieces4:"+std::string(4,'\0')+"2:sti3ee";write(leaf,"de");write(suffixed(leaf,".bak"),reordered);auto owner=take(st::OriginalControl::open(f.lease,std::string(40,'A'),1,st::OriginalStorageKind::Reordered,false));check(take(owner->load())==reordered,"reordered required fields select valid raw backup without geometry invention");}
}
void readOnlyGeometrySelectionNeverRepairsCandidates(){
  Fixture f;const auto bad=compact(9999999),good=compact(64);write(f.leaf,bad);const auto backup=suffixed(f.leaf,".bak");write(backup,good);auto owner=f.open(false);
  check(take(owner->load())==bad,"Generic original envelope selection stays unchanged");
  check(take(owner->loadValidated({17,65536,16384}))==good,"Read-only codec geometry selects valid original backup without replacing primary");
  check(read(f.leaf)==bad&&read(backup)==good&&!fs::exists(suffixed(f.leaf,".saving")),"Read-only candidate selection performs no repair/publication");
  fs::rename(backup,f.dir/"retained-backup");write(backup,good);check(!owner->loadValidated({17,65536,16384}),"Same-byte selected backup replacement never becomes a fresh grant");
}
void identityCasAcrossCalls(){
  {Fixture f;write(f.leaf,compact(4));auto owner=f.open(false);auto publish=owner->publisher();fs::rename(f.leaf,f.dir/"old-primary");write(f.leaf,compact(4));check(!publish(compact(4),compact(8))&&!owner->retire()&&!fs::exists(suffixed(f.leaf,".saving")),"same-byte replacement inode refuses publication and retirement before effects");check(read(f.leaf)==compact(4)&&read(f.dir/"old-primary")==compact(4),"foreign replacement and original retained");}
  {Fixture f;write(f.leaf,compact(4));write(suffixed(f.leaf,".bak"),compact(2));auto owner=f.open(false);fs::rename(suffixed(f.leaf,".bak"),f.dir/"old-backup");write(suffixed(f.leaf,".bak"),compact(2));check(!owner->publisher()(compact(4),compact(8))&&read(f.leaf)==compact(4),"same-byte backup replacement also invalidates accepted candidate identity");}
  {Fixture f;write(f.leaf,compact(4));auto owner=f.open(false);check(!owner->publisher()(compact(7),compact(8))&&!owner->publisher()(compact(4),"de")&&read(f.leaf)==compact(4)&&!fs::exists(suffixed(f.leaf,".saving")),"wrong expected bytes and invalid replacement refuse before temporary creation");}
}
void unsafeCandidatesAndDetachedDirectories(){
  {
    Fixture f;
    const auto outside=f.root/"outside";
    write(outside,compact(4));
    fs::create_directories(f.dir);
    std::error_code linkError;
    fs::create_symlink(outside,f.leaf,linkError);
#ifndef _WIN32
    check(!linkError,"create generated symbolic control");
#endif
    if (!linkError) {
      check(!st::OriginalControl::open(f.lease,std::string(40,'A'),0,st::OriginalStorageKind::Compact,false)
          &&read(outside)==compact(4),"symbolic control never followed");
    }
  }
  {Fixture f;const auto outside=f.root/"outside";write(outside,compact(4));fs::create_directories(f.dir);fs::create_hard_link(outside,f.leaf);check(!st::OriginalControl::open(f.lease,std::string(40,'A'),0,st::OriginalStorageKind::Compact,false)&&read(outside)==compact(4),"hard-linked control refuses");}
  {Fixture f;write(f.leaf,compact(4));write(suffixed(f.leaf,".bak.saving"),compact(9));check(!st::OriginalControl::open(f.lease,std::string(40,'A'),0,st::OriginalStorageKind::Compact,false)&&read(f.leaf)==compact(4),"pending backup saving refuses recovery selection");}
  {
    Fixture f;
    write(f.leaf,compact(4));
    auto owner=f.open(false);
    const auto detached=f.profile/"active/detached";
    std::error_code moveError;
    fs::rename(f.dir,detached,moveError);
#ifndef _WIN32
    check(!moveError,"move generated control directory");
#endif
    if (moveError) {
      check(take(owner->load())==compact(4)&&!fs::exists(detached),"held Win32 directory refused replacement");
    } else {
      write(f.leaf,compact(4));
      check(!owner->load()&&!owner->publisher()(compact(4),compact(8))&&!owner->retire(),"replaced hash directory cannot receive detached writes or retirement");
      check(read(f.leaf)==compact(4)&&read(detached/"fmfile0.dat")==compact(4),"both original and replacement directory leaves retained");
    }
  }
  {Fixture f;check(!st::OriginalControl::open(f.lease,"A",0,st::OriginalStorageKind::Compact,true)&&!fs::exists(f.profile/"active/A"),"short pseudo-hash cannot authorize a directory");}
}
void concurrentReadOnlySnapshotsShareOneControlWithoutBusyOrRecapture(){
  Fixture f;const std::string opaque(1024*1024,'q');
  const auto raw="d6:lengthi64e6:opaque"+std::to_string(opaque.size())+':'+opaque+"7:versioni0ee";
  const auto backup=suffixed(f.leaf,".bak");write(f.leaf,raw);write(backup,compact(32));auto owner=f.open(false);
  const auto active = take(st::detail::openStateDirectory(f.lease.directoryHandle(), "active"));
  const auto directory = take(st::detail::openStateDirectory(active.get(), std::string(40, 'A')));
  const auto before = take(st::detail::stateStampAt(directory.get(), "fmfile0.dat"));
  const auto backupBefore = take(st::detail::stateStampAt(directory.get(), "fmfile0.dat.bak"));
  std::barrier turn(2);std::array<unsigned,2> errors{},busy{};std::array<std::string,2> first;
  {std::array<std::jthread,2> readers;
    for(std::size_t n=0;n<readers.size();++n){readers[n]=std::jthread([&,n]{
      // Real regular files and real decoder only: no injected mutex/fault hook.
      // Both rounds exercise validated/validated and raw/validated overlap.
      for(unsigned round=0;round<32;++round){turn.arrive_and_wait();try{
        const auto loaded=(n==1||round%2==0)?owner->loadValidated({17,65536,16384}):owner->load();
        if(!loaded){++errors[n];if(loaded.error().code==st::ErrorCode::Busy){++busy[n];}if(first[n].empty()){first[n]=loaded.error().message;}}
        else if(!loaded->has_value()||**loaded!=raw){++errors[n];if(first[n].empty()){first[n]="Actual candidate bytes differ";}}
      }catch(const std::exception& error){++errors[n];if(first[n].empty()){first[n]=error.what();}}}
    });}
  } // Both actual readers are joined before any failure or candidate edit.
  if(errors[0]||errors[1]){throw std::runtime_error("Concurrent original-control readers failed: busy="+std::to_string(busy[0]+busy[1])+"; "+first[0]+"; "+first[1]);}
  const auto after = take(st::detail::stateStampAt(directory.get(), "fmfile0.dat"));
  const auto backupAfter = take(st::detail::stateStampAt(directory.get(), "fmfile0.dat.bak"));
  check(before && after && backupBefore && backupAfter
      &&st::detail::sameStateSnapshot(*before,*after)&&st::detail::sameStateSnapshot(*backupBefore,*backupAfter)
      &&read(f.leaf)==raw&&read(backup)==compact(32)
      &&!fs::exists(suffixed(f.leaf,".saving"))&&!fs::exists(suffixed(backup,".saving")),"Concurrent read-only calls preserve actual candidate identities and bytes");
  fs::rename(f.leaf,f.dir/"retained-primary");write(f.leaf,raw);
  const auto generic=owner->load(),validated=owner->loadValidated({17,65536,16384});
  check(!generic&&!validated&&generic.error().code==st::ErrorCode::Conflict&&validated.error().code==st::ErrorCode::Conflict,
      "Serialization never recaptures a same-byte external replacement");
  check(read(f.leaf)==raw&&read(f.dir/"retained-primary")==raw,"Refused replacement preserves both actual files");
}
void publisherOwnsOnlyItsSafeControlLifetime(){
  Fixture f;auto owner=f.open();auto publish=owner->publisher();owner.reset();take(publish({},compact(3)));check(read(f.leaf)==compact(3),"publisher safely retains authenticated directory descriptors");check(!st::ProfileLease::acquire(f.profile),"publisher lifetime does not bypass actual held profile lease");
}
}
namespace {
void ownershipAuthenticationUsesExactProfileFileAndFamily(){
  Fixture f,other;auto owner=f.open();const auto hash=std::string(40,'A');
  take(owner->authenticate(f.lease,hash,0,st::OriginalStorageKind::Compact));
  check(!owner->authenticate(other.lease,hash,0,st::OriginalStorageKind::Compact),"different held profile refuses");
  check(!owner->authenticate(f.lease,std::string(40,'B'),0,st::OriginalStorageKind::Compact),"different original hash refuses");
  check(!owner->authenticate(f.lease,hash,1,st::OriginalStorageKind::Compact),"different original file index refuses");
  check(!owner->authenticate(f.lease,hash,0,st::OriginalStorageKind::Reordered),"different original control family refuses");
  auto reordered=take(st::OriginalControl::open(f.lease,hash,0,st::OriginalStorageKind::ReorderedCompact,true));
  take(reordered->authenticate(f.lease,hash,0,st::OriginalStorageKind::Reordered));
  check(!fs::exists(f.leaf)&&!fs::exists(f.dir/"fmfile0.dat.2"),"owner authentication creates no control data");
}
}
int main(){try{ownershipAuthenticationUsesExactProfileFileAndFamily();originalSelectionAndPublication();readOnlyGeometrySelectionNeverRepairsCandidates();concurrentReadOnlySnapshotsShareOneControlWithoutBusyOrRecapture();identityCasAcrossCalls();unsafeCandidatesAndDetachedDirectories();publisherOwnsOnlyItsSafeControlLifetime();std::cout<<"seven generated original-control families passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
