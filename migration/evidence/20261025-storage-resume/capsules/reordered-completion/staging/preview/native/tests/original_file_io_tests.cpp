#include "original_file_io.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace fs=std::filesystem;
namespace st=biglybtc::storage;
namespace {
void check(bool v,const char* why){if(!v){throw std::runtime_error(why);}}
template<class T>T take(std::expected<T,st::Error>&& v){if(!v){throw std::runtime_error(v.error().message);}return std::move(*v);}
void done(std::expected<void,st::Error>&& v){if(!v){throw std::runtime_error(v.error().message);}}
struct Area{fs::path root;int fd=-1;Area(){char name[]="/tmp/biglybtc-original-io-XXXXXX";const auto* p=::mkdtemp(name);check(p,"mkdtemp");root=p;fd=::open((root/"payload").c_str(),O_CREAT|O_EXCL|O_RDWR|O_CLOEXEC,0600);check(fd>=0,"fixture open");}~Area(){if(fd>=0){::close(fd);}std::error_code e;fs::remove_all(root,e);}};
std::span<const std::byte> asBytes(std::string_view s){return std::as_bytes(std::span(s.data(),s.size()));}
std::string physical(int fd){struct stat s{};check(::fstat(fd,&s)==0,"stat");std::string out(static_cast<std::size_t>(s.st_size),'\0');std::size_t n=0;while(n<out.size()){const auto k=::pread(fd,out.data()+n,out.size()-n,static_cast<off_t>(n));check(k>0,"physical read");n+=static_cast<std::size_t>(k);}return out;}
struct Store{std::optional<std::string> current;unsigned calls=0;bool failBefore=false,failAfter=false;unsigned failureCall=0;bool failureAfter=false;std::vector<std::uint64_t> sizes;int fd;
  explicit Store(int f):fd(f){}
  st::OriginalControlPublisher publisher(){return [this](const auto& expected,std::string_view next)->std::expected<void,st::Error>{check(expected==current,"control CAS expected exact original");++calls;struct stat s{};check(::fstat(fd,&s)==0,"publisher actual stat");sizes.push_back(static_cast<std::uint64_t>(s.st_size));if((failBefore&&calls>1)||(failureCall==calls&&!failureAfter)){return std::unexpected(st::Error{st::ErrorCode::Io,"injected before publication"});}current=std::string(next);if((failAfter&&calls>1)||(failureCall==calls&&failureAfter)){return std::unexpected(st::Error{st::ErrorCode::Io,"injected after publication"});}return {};};}
};
std::string read(st::OriginalFileIo& io,std::uint64_t offset,std::size_t count,st::OriginalReadResult* facts=nullptr){std::string out(count,'x');auto result=take(io.read(offset,std::as_writable_bytes(std::span(out.data(),out.size()))));if(facts){*facts=std::move(result);}else{check(result.complete(),"expected real complete read");}return out;}
st::OriginalFileGeometry geometry{3,60,16};
std::string mapBytes(const std::vector<int>& map){std::string bytes;for(int n:map){const auto v=static_cast<std::uint32_t>(n);for(int shift:{24,16,8,0}){bytes+=static_cast<char>((v>>shift)&255);}}return bytes;}
void boundsAndLinear(){
  check(take(st::originalMaximumPhysicalSize({3,50,16},st::OriginalStorageKind::Compact))==18,"compact physical edges bound");check(take(st::originalMaximumPhysicalSize(geometry,st::OriginalStorageKind::Reordered))==61,"reorder padded bound");check(st::originalControlLeaf(1,st::OriginalStorageKind::Compact)=="fmfile1.dat"&&st::originalControlLeaf(1,st::OriginalStorageKind::ReorderedCompact)=="fmfile1.dat.2","exact original control names");check(!st::originalMaximumPhysicalSize({0,100,0},st::OriginalStorageKind::Compact)&&!st::originalMaximumPhysicalSize({0,100,256*1024*1024+1},st::OriginalStorageKind::Reordered),"nonlinear invalid/bounded piece rejects");check(st::originalMaximumPhysicalSize({0,100,128*1024*1024},st::OriginalStorageKind::Linear).has_value(),"linear ordinary piece size remains supported");
  Area a;auto io=take(st::OriginalFileIo::open(a.fd,{0,20,16384},st::OriginalStorageKind::Linear,{}));done(io.write(2,asBytes("hello")));check(io.length()==7&&read(io,2,5)=="hello","linear real positional writes");st::OriginalReadResult facts;read(io,7,4,&facts);check(facts.missing==std::vector<st::OriginalMissingRange>{{7,4}},"physical EOF is missing not verified zeros");done(io.setLength(3));done(io.flush());check(physical(a.fd).size()==3,"linear setLength changes actual length");fs::create_hard_link(a.root/"payload",a.root/"alias");check(!st::OriginalFileIo::open(a.fd,{0,20,16384},st::OriginalStorageKind::Linear,{}),"default descriptor admission rejects aliases");auto authenticated=take(st::OriginalFileIo::open(a.fd,{0,20,16384},st::OriginalStorageKind::Linear,{}, {},false,false,2));check(authenticated.length()==3,"explicit exact two-link admission retains caller-proven aliases");
}
void compactEdgesAndUnknown(){
  Area a;Store store(a.fd);store.current="d6:lengthi0e7:versioni0e6:opaqueli7e3:rawee";auto io=take(st::OriginalFileIo::open(a.fd,{3,50,16},st::OriginalStorageKind::Compact,store.current,store.publisher()));check(store.calls==0&&physical(a.fd).empty(),"open is read only");std::string data;for(int i=0;i<50;++i){data+=static_cast<char>('A'+i%26);}done(io.write(0,asBytes(data)));check(physical(a.fd)==data.substr(0,13)+data.substr(45),"compact stores first and last fragments only");st::OriginalReadResult facts;const auto got=read(io,0,50,&facts);check(got.substr(0,13)==data.substr(0,13)&&got.substr(45)==data.substr(45)&&facts.missing==std::vector<st::OriginalMissingRange>{{13,32}},"compact missing middle explicit");done(io.flush());auto ctl=biglybtc::core::Document::parse(*store.current);check(ctl&&ctl->find("length")->integer==50&&ctl->find("opaque"),"compact controls retain unknown bytes");done(io.setLength(9));done(io.flush());check(physical(a.fd).size()==18&&io.length()==9,"compact virtual length does not truncate edges");
}
void arbitraryReorderedMaps(){
  for(const auto kind:{st::OriginalStorageKind::Reordered,st::OriginalStorageKind::ReorderedCompact}){Area a;Store store(a.fd);auto io=take(st::OriginalFileIo::open(a.fd,geometry,kind,{},store.publisher()));check(store.calls==0,"new reorder open publishes nothing");done(io.write(45,asBytes(std::string(15,'D'))));check(store.calls==2&&store.sizes[0]==0&&store.sizes[1]==28,"original marker precedes data and map follows real data");st::OriginalReadResult gap;read(io,13,16,&gap);check(gap.missing==std::vector<st::OriginalMissingRange>{{13,16}},"unallocated original piece is missing");done(io.write(13,asBytes(std::string(16,'B'))));done(io.write(29,asBytes(std::string(16,'C'))));done(io.write(0,asBytes(std::string(13,'A'))));done(io.flush());auto ctl=biglybtc::core::Document::parse(*store.current);check(ctl&&ctl->find("st")->integer==static_cast<int>(kind)&&ctl->find("pieces")->bytes==mapBytes({0,2,3,1}),"append-only noncanonical original permutation");const auto publications=store.calls;const auto unchangedControl=*store.current;done(io.write(8,asBytes(std::string(20,'Z'))));check(read(io,8,20)==std::string(20,'Z')&&store.calls==publications&&*store.current==unchangedControl,"allocated cross-piece overwrite retains map/publication policy");done(io.write(8,asBytes(std::string(5,'A')+std::string(15,'B'))));check(store.calls==publications&&*store.current==unchangedControl,"restored allocated writes publish no new slots");const auto before=physical(a.fd),control=*store.current;check(!take(io.pieceComplete(3,asBytes(std::string(15,'D'))))&&before==physical(a.fd)&&control==*store.current,"completion does not perform crash-unsafe slot swap");auto reopened=take(st::OriginalFileIo::open(a.fd,geometry,kind,store.current,store.publisher()));check(read(reopened,0,60)==std::string(13,'A')+std::string(16,'B')+std::string(16,'C')+std::string(15,'D'),"reopened original map resolves all pieces");}
}
void virtualLengthsAndShortMarker(){
  Area a;Store store(a.fd);auto io=take(st::OriginalFileIo::open(a.fd,geometry,st::OriginalStorageKind::Reordered,{},store.publisher()));done(io.setLength(60));check(physical(a.fd).empty(),"reorder allocation virtual only");done(io.flush());check(store.current&&io.length()==60,"virtual length durable in original control");auto reopened=take(st::OriginalFileIo::open(a.fd,geometry,st::OriginalStorageKind::Reordered,store.current,store.publisher()));st::OriginalReadResult facts;read(reopened,0,60,&facts);check(facts.missing==std::vector<st::OriginalMissingRange>{{0,60}},"virtual length does not fabricate pieces");
  Area b;Store shortStore(b.fd);auto shortIo=take(st::OriginalFileIo::open(b.fd,{3,10,16},st::OriginalStorageKind::Reordered,{},shortStore.publisher()));done(shortIo.write(0,asBytes("0123456789")));done(shortIo.flush());check(read(shortIo,0,10)=="0123456789"&&shortStore.current,"short reordered file stays linear with original marker");
}
void publicationFailuresKeepJavaMap(){
  for(bool after:{false,true}){Area a;Store store(a.fd);store.failBefore=!after;store.failAfter=after;auto io=take(st::OriginalFileIo::open(a.fd,geometry,st::OriginalStorageKind::Reordered,{},store.publisher()));const auto written=io.write(45,asBytes(std::string(15,'D')));check(!written&&io.recoveryPending()&&!io.flush(),"uncertain map publication fences further writes");store.failBefore=store.failAfter=false;auto reopened=take(st::OriginalFileIo::open(a.fd,geometry,st::OriginalStorageKind::Reordered,store.current,store.publisher()));st::OriginalReadResult facts;const auto value=read(reopened,45,15,&facts);check(after?(facts.complete()&&value==std::string(15,'D')):facts.missing==std::vector<st::OriginalMissingRange>{{45,15}},"both publication outcomes retain valid original old/new map");}
}
void recoveryAndStrictControls(){
  Area a;check(::pwrite(a.fd,"partial",7,0)==7,"recovery real bytes");Store store(a.fd);check(!st::OriginalFileIo::open(a.fd,geometry,st::OriginalStorageKind::Reordered,{},store.publisher()),"missing existing control never guessed implicitly");auto io=take(st::OriginalFileIo::open(a.fd,geometry,st::OriginalStorageKind::Reordered,{},store.publisher(),true));check(io.requiresRecheck()&&io.length()==7&&store.calls==0,"explicit original linear-prefix recovery is unverified and read only");done(io.flush());check(store.current.has_value(),"recovered map durable only on flush");auto bad=biglybtc::core::Document::parse(*store.current);check(bad.has_value(),"parse control");check(bad->replaceValue("pieces","16:"+mapBytes({0,0,-1,-1})).has_value(),"hostile map fixture");check(!st::OriginalFileIo::open(a.fd,geometry,st::OriginalStorageKind::Reordered,bad->serialize(),store.publisher()),"duplicate physical slots reject");check(!io.write(59,asBytes("xx"))&&physical(a.fd)=="partial","range failure no mutation");
}
// Test-only real ftruncate failure after actual compact tail copy. No fake data.
bool failResize=false,failResizeAfter=false;int resizeFd=-1;unsigned resizeHits=0;
void conversionKeepsCompactPrefix(){
  Area a;Store store(a.fd);auto io=take(st::OriginalFileIo::open(a.fd,{3,50,16},st::OriginalStorageKind::Compact,{},store.publisher()));const std::string data="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWX";done(io.write(0,asBytes(data)));done(io.flush());const auto packed=physical(a.fd);resizeFd=a.fd;failResize=true;const auto failed=io.convertToLinear();failResize=false;check(!failed&&resizeHits>0&&physical(a.fd).substr(0,18)==packed,"conversion interruption retains all original packed data");check(!st::OriginalFileIo::open(a.fd,{3,50,16},st::OriginalStorageKind::Compact,store.current,store.publisher()),"ordinary compact guard rejects expanded payload");auto resumed=take(st::OriginalFileIo::open(a.fd,{3,50,16},st::OriginalStorageKind::Compact,store.current,store.publisher(),false,true));st::OriginalReadResult facts;const auto old=read(resumed,0,50,&facts);check(old.substr(0,13)==data.substr(0,13)&&old.substr(45)==data.substr(45),"authenticated pending conversion still reads old compact edges");done(resumed.convertToLinear());check(resumed.requiresRecheck()&&read(resumed,0,13)==data.substr(0,13)&&read(resumed,45,5)==data.substr(45)&&physical(a.fd).size()==50,"idempotent expansion establishes linear edges");
}
// Sparse large-piece fixtures write only small known fragments, never a full
// 128/256 MiB piece. Existing 64 MiB request bounds and all old cases remain.
std::string physicalRange(int fd,std::uint64_t offset,std::size_t length){
  std::string value(length,'\0');std::size_t used=0;
  while(used<length){const auto n=::pread(fd,value.data()+used,length-used,static_cast<off_t>(offset+used));if(n<0&&errno==EINTR){continue;}check(n>0,"bounded physical fixture read");used+=static_cast<std::size_t>(n);}return value;
}
void seedPacked(int fd,const std::string& value){check(::pwrite(fd,value.data(),value.size(),0)==static_cast<ssize_t>(value.size()),"seed actual packed bytes");}
bool copyFailAfter=false;int copyFd=-1;unsigned copyReadCalls=0,copyWriteCalls=0,copyFailWrite=0;std::size_t copyReadMaximum=0,copyWriteMaximum=0;
struct CopyProbe{
  explicit CopyProbe(int fd,unsigned failWrite=0,bool after=false){check(copyFd<0,"one owned conversion probe");copyFd=fd;copyReadCalls=copyWriteCalls=0;copyReadMaximum=copyWriteMaximum=0;copyFailWrite=failWrite;copyFailAfter=after;}
  void stop(){copyFd=-1;copyFailWrite=0;copyFailAfter=false;}
  ~CopyProbe(){stop();}
};
void largePieceBoundaries(){
  for(const std::uint32_t piece:{128U*1024*1024,256U*1024*1024}){
    const st::OriginalFileGeometry g{piece-13,2ULL*piece+20,piece};
    for(const auto kind:{st::OriginalStorageKind::Compact,st::OriginalStorageKind::Reordered,st::OriginalStorageKind::ReorderedCompact}){
      Area a;Store store(a.fd);const bool compact=kind==st::OriginalStorageKind::Compact;
      check(take(st::originalMaximumPhysicalSize(g,kind))==(compact?20:13+3ULL*piece),"large piece physical bound uses original edge/map geometry");
      auto io=take(st::OriginalFileIo::open(a.fd,g,kind,{},store.publisher()));
      done(io.write(2ULL*piece+13,asBytes("TAIL123")));
      st::OriginalReadResult absent;read(io,13,8,&absent);check(absent.missing==std::vector<st::OriginalMissingRange>{{13,8}},"large unallocated middle is unknown rather than present zeros");
      if(!compact){done(io.write(piece+13,asBytes("MIDDLE7")));}
      done(io.write(11,asBytes("ABCD")));done(io.flush());
      auto reopened=take(st::OriginalFileIo::open(a.fd,g,kind,store.current,store.publisher()));
      check(read(reopened,2ULL*piece+13,7)=="TAIL123","large final fragment survives exact original reopen");
      if(compact){st::OriginalReadResult gap;const auto value=read(reopened,11,4,&gap);check(value.substr(0,2)=="AB"&&gap.missing==std::vector<st::OriginalMissingRange>{{13,2}},"compact large boundary retains edge and reports discarded interior");}
      else{check(read(reopened,11,4)=="ABCD"&&read(reopened,piece+13,7)=="MIDDLE7","large original slots resolve cross-piece and sparse mapped fragments");}
    }
  }
}
void largeSparseCompactConversion(){
  constexpr std::size_t chunk=64*1024;
  for(const std::uint32_t piece:{128U*1024*1024,256U*1024*1024}){
    Area a;Store store(a.fd);const st::OriginalFileGeometry g{piece-13,2ULL*piece+6,piece};
    const std::string packed=std::string(13,'F')+std::string(2*chunk+7,'T');seedPacked(a.fd,packed);
    store.current="d6:lengthi"+std::to_string(g.fileBytes)+"e7:versioni0e6:opaqueli7e3:rawee";const auto control=*store.current;
    auto io=take(st::OriginalFileIo::open(a.fd,g,st::OriginalStorageKind::Compact,store.current,store.publisher()));
    CopyProbe probe(a.fd);done(io.convertToLinear());probe.stop();
    check(copyReadCalls==3&&copyWriteCalls==3&&copyReadMaximum<=chunk&&copyWriteMaximum<=chunk,"large tail streams only original EOF bytes in fixed scratch chunks");
    check(physicalRange(a.fd,0,packed.size())==packed,"all old packed bytes survive sparse large conversion");
    check(read(io,piece+13,2*chunk+7)==packed.substr(13)&&io.requiresRecheck(),"large converted linear destination contains actual source tail bytes and requires recheck");
    struct stat actual{};check(::fstat(a.fd,&actual)==0&&static_cast<std::uint64_t>(actual.st_size)==g.fileBytes,"large linear length published only after chunk copy");
    check(store.current==control&&store.calls==0,"conversion never retires or rewrites original compact control itself");
  }
}
void streamedConversionFailureReplay(){
  constexpr std::size_t chunk=64*1024;
  for(const bool partial:{false,true}){
    const std::uint32_t piece=partial?1024U*1024:256U*1024*1024;
    const std::uint64_t tail=partial?3*chunk+5:static_cast<std::uint64_t>(piece)-7;
    const st::OriginalFileGeometry g{piece-13,13ULL+piece+tail,piece};
    const std::string packed=std::string(13,'F')+std::string(3*chunk+5,'T');
    Area a;Store store(a.fd);seedPacked(a.fd,packed);store.current="d6:lengthi"+std::to_string(g.fileBytes)+"e7:versioni0ee";const auto control=*store.current;
    auto io=take(st::OriginalFileIo::open(a.fd,g,st::OriginalStorageKind::Compact,store.current,store.publisher()));
    CopyProbe first(a.fd,partial?2:1);const auto failed=io.convertToLinear();first.stop();
    check(!failed&&failed.error().code==st::ErrorCode::Io&&io.recoveryPending()&&!io.flush(),"actual targeted chunk-write failure poisons further IO until reopen");
    check(copyWriteCalls==(partial?2U:1U)&&copyWriteMaximum<=chunk,"failure occurred on the selected real bounded write");
    check(physicalRange(a.fd,0,packed.size())==packed&&store.current==control,"failure never destroys old Java-readable packed edges/control");
    if(partial){check(physicalRange(a.fd,piece+13,chunk)==packed.substr(13,chunk),"first actual destination chunk completed before second write failed");check(!st::OriginalFileIo::open(a.fd,g,st::OriginalStorageKind::Compact,store.current,store.publisher()),"ordinary compact open refuses incomplete expanded payload");}
    auto resumed=take(st::OriginalFileIo::open(a.fd,g,st::OriginalStorageKind::Compact,store.current,store.publisher(),false,true));
    check(read(resumed,piece+13,packed.size()-13)==packed.substr(13),"authenticated pending replay still resolves the original packed tail");
    CopyProbe retry(a.fd);done(resumed.convertToLinear());retry.stop();
    check(copyReadCalls==4&&copyWriteCalls==4&&copyReadMaximum<=chunk&&copyWriteMaximum<=chunk,"replay is bounded and idempotent after actual chunk failure");
    check(read(resumed,piece+13,packed.size()-13)==packed.substr(13)&&physicalRange(a.fd,0,packed.size())==packed,"replay establishes linear data while retaining old source bytes");
    check(store.current==control,"caller retains final storetype/control publication authority");
  }
}
std::string completeBytes(std::size_t count){std::string result(count,'\0');for(std::size_t i=0;i<count;++i){result[i]=static_cast<char>((i*17+i/211)%251);}return result;}
st::OriginalFileIo completeOutOfOrder(Area& a,Store& store,st::OriginalStorageKind kind,st::OriginalFileGeometry g,const std::string& expected){
  const auto first=std::min<std::uint64_t>(g.fileBytes,g.pieceBytes-g.torrentOffset%g.pieceBytes);
  const auto count=1+(g.torrentOffset%g.pieceBytes+g.fileBytes-1)/g.pieceBytes;
  std::vector<int> map(static_cast<std::size_t>(count),-1);map[0]=0;
  store.current="d2:st"+std::string(kind==st::OriginalStorageKind::Reordered?"i3e":"i4e")+"3:leni0e4:nexti1e6:pieces"+std::to_string(map.size()*4)+":"+mapBytes(map)+"6:opaqueli7e3:rawee";
  auto io=take(st::OriginalFileIo::open(a.fd,g,kind,store.current,store.publisher()));
  auto writePiece=[&](std::uint64_t piece){const auto start=piece?first+(piece-1)*g.pieceBytes:0;const auto bytes=std::min<std::uint64_t>(piece?g.pieceBytes:first,g.fileBytes-start);done(io.write(start,asBytes(std::string_view(expected).substr(static_cast<std::size_t>(start),static_cast<std::size_t>(bytes)))));};
  for(std::uint64_t piece=0;piece+2<count;++piece){writePiece(piece);}writePiece(count-1);writePiece(count-2);done(io.flush());return io;
}
void completeReorderedTail(){
  for(const auto kind:{st::OriginalStorageKind::Reordered,st::OriginalStorageKind::ReorderedCompact}){
    for(const auto& g:{st::OriginalFileGeometry{3,60,16},st::OriginalFileGeometry{3072,103304,16384}}){
      Area a;Store store(a.fd);const auto expected=completeBytes(static_cast<std::size_t>(g.fileBytes));auto io=completeOutOfOrder(a,store,kind,g,expected);
      struct stat before{};check(::fstat(a.fd,&before)==0&&static_cast<std::uint64_t>(before.st_size)>g.fileBytes,"actual fully readable original map still violates GM raw-size gate before finalization");
      const auto calls=store.calls;CopyProbe probe(a.fd);done(io.finishCompleteReordered());probe.stop();
      check(copyWriteCalls==2&&copyReadMaximum<=64*1024&&copyWriteMaximum<=64*1024,"short-tail relocation uses bounded real payload IO");
      check(store.calls==calls+5,"every redo, progress, map and journal-clear publication is exercised");
      struct stat after{};check(::fstat(a.fd,&after)==0&&static_cast<std::uint64_t>(after.st_size)==g.fileBytes&&after.st_ino==before.st_ino,"GM-compatible physical size keeps authenticated payload inode");
      check(read(io,0,expected.size())==expected&&!io.recoveryPending(),"complete reordered logical bytes survive tail normalization");
      auto document=biglybtc::core::Document::parse(*store.current);check(document&&document->find("opaque")&&document->find("BiglyBTC.reorderedCompletion")->children.empty(),"unknown control fields preserved and native redo cleared only after sync");
      auto reopened=take(st::OriginalFileIo::open(a.fd,g,kind,store.current,store.publisher()));check(read(reopened,0,expected.size())==expected,"original-format final map reopens exact full bytes");
      const auto stable=store.calls;done(reopened.finishCompleteReordered());check(store.calls==stable,"already fitting complete file needs no repeated normalization");
    }
  }
  // Other full-piece permutations need no canonicalization: just move the
  // short last logical piece to the final physical slot.
  Area a;Store store(a.fd);auto io=take(st::OriginalFileIo::open(a.fd,geometry,st::OriginalStorageKind::Reordered,{},store.publisher()));
  done(io.write(45,asBytes(std::string(15,'D'))));done(io.write(13,asBytes(std::string(16,'B'))));done(io.write(29,asBytes(std::string(16,'C'))));done(io.write(0,asBytes(std::string(13,'A'))));done(io.flush());done(io.finishCompleteReordered());
  auto final=biglybtc::core::Document::parse(*store.current);check(final&&final->find("pieces")->bytes==mapBytes({0,2,1,3})&&read(io,0,60)==std::string(13,'A')+std::string(16,'B')+std::string(16,'C')+std::string(15,'D'),"unrelated full-piece permutations remain compatible and exact");
}
void completeReorderedFailureReplay(){
  const auto expected=completeBytes(60);
  // Synthetic callback/error boundaries; payload writes and descriptor sizes
  // are real. Replay assertions never upgrade an engine/resume certificate.
  for(const bool after:{false,true}){
    for(const unsigned which:{1U,2U}){
      Area a;Store store(a.fd);auto io=completeOutOfOrder(a,store,st::OriginalStorageKind::Reordered,geometry,expected);
      CopyProbe probe(a.fd,which,after);const auto failed=io.finishCompleteReordered();probe.stop();
      check(!failed&&io.recoveryPending()&&!io.flush(),"before/after each real swap write is fail-closed");
      auto reopened=take(st::OriginalFileIo::open(a.fd,geometry,st::OriginalStorageKind::Reordered,store.current,store.publisher()));
      std::array<std::byte,1> one{};check(reopened.recoveryPending()&&!reopened.read(0,one),"pending native swap cannot be consumed as ordinary mapped data");
      done(reopened.recover());check(read(reopened,0,expected.size())==expected&&physical(a.fd).size()==expected.size(),"durable two-chunk redo repairs partial or fully effected reported-failed writes");
    }
    for(unsigned publication=1;publication<=5;++publication){
      Area a;Store store(a.fd);auto io=completeOutOfOrder(a,store,st::OriginalStorageKind::Reordered,geometry,expected);store.failureCall=store.calls+publication;store.failureAfter=after;
      const auto failed=io.finishCompleteReordered();check(!failed&&io.recoveryPending(),"every control publication before/after boundary fences the old object");store.failureCall=0;
      auto reopened=take(st::OriginalFileIo::open(a.fd,geometry,st::OriginalStorageKind::Reordered,store.current,store.publisher()));done(reopened.recover());done(reopened.finishCompleteReordered());
      check(read(reopened,0,expected.size())==expected&&physical(a.fd).size()==expected.size(),"actual selected control old/new outcome completes via explicit native replay");
    }
    Area a;Store store(a.fd);auto io=completeOutOfOrder(a,store,st::OriginalStorageKind::Reordered,geometry,expected);resizeFd=a.fd;failResize=true;failResizeAfter=after;
    const auto failed=io.finishCompleteReordered();failResize=false;failResizeAfter=false;
    check(!failed&&io.recoveryPending(),"truncate failure before/after effect retains pending map record");
    auto reopened=take(st::OriginalFileIo::open(a.fd,geometry,st::OriginalStorageKind::Reordered,store.current,store.publisher()));done(reopened.recover());
    check(read(reopened,0,expected.size())==expected&&physical(a.fd).size()==expected.size(),"truncate replay uses already durable corrected map and preserves all data");
  }
}
void reorderedTypeOnly(){Area a;Store store(a.fd);auto io=take(st::OriginalFileIo::open(a.fd,geometry,st::OriginalStorageKind::ReorderedCompact,{},store.publisher()));done(io.write(45,asBytes(std::string(15,'D'))));const auto before=physical(a.fd);auto old=biglybtc::core::Document::parse(*store.current);const auto map=std::string(old->find("pieces")->bytes);done(io.setReorderedKind(st::OriginalStorageKind::Reordered));auto now=biglybtc::core::Document::parse(*store.current);check(now&&now->find("st")->integer==3&&now->find("pieces")->bytes==map&&before==physical(a.fd),"RC to R preserves exact physical bytes/map");}
}
extern "C" ssize_t __real_pread(int,void*,std::size_t,off_t);
extern "C" ssize_t __real_pwrite(int,const void*,std::size_t,off_t);
extern "C" ssize_t __wrap_pread(int fd,void* buffer,std::size_t bytes,off_t offset){if(fd==copyFd){++copyReadCalls;copyReadMaximum=std::max(copyReadMaximum,bytes);}return __real_pread(fd,buffer,bytes,offset);}
extern "C" ssize_t __wrap_pwrite(int fd,const void* buffer,std::size_t bytes,off_t offset){if(fd==copyFd){++copyWriteCalls;copyWriteMaximum=std::max(copyWriteMaximum,bytes);if(copyFailWrite&&copyWriteCalls==copyFailWrite){if(copyFailAfter){const auto actual=__real_pwrite(fd,buffer,bytes,offset);if(actual<0){return actual;}}errno=EIO;return -1;}}return __real_pwrite(fd,buffer,bytes,offset);}
extern "C" int __real_ftruncate(int,off_t);
extern "C" int __wrap_ftruncate(int fd,off_t length){if(failResize&&fd==resizeFd){++resizeHits;if(failResizeAfter){const int actual=__real_ftruncate(fd,length);if(actual<0){return actual;}}errno=EIO;return -1;}return __real_ftruncate(fd,length);}
int main(){try{boundsAndLinear();compactEdgesAndUnknown();arbitraryReorderedMaps();virtualLengthsAndShortMarker();publicationFailuresKeepJavaMap();recoveryAndStrictControls();conversionKeepsCompactPrefix();reorderedTypeOnly();largePieceBoundaries();largeSparseCompactConversion();streamedConversionFailureReplay();completeReorderedTail();completeReorderedFailureReplay();std::cout<<"thirteen original physical IO families passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
