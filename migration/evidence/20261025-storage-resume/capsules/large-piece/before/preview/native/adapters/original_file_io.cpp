#include "original_file_io.hpp"
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <fcntl.h>
#include <limits>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace biglybtc::storage {
namespace {
using Kind=OriginalStorageKind;
constexpr std::uint64_t MaxPiece=64ULL*1024*1024,MaxSlots=1000000,MaxIo=64ULL*1024*1024;
constexpr std::size_t MaxControl=16*1024*1024;
struct Failure{Error error;};
[[noreturn]]void fail(ErrorCode code,const char* message){throw Failure{{code,message}};}
void require(bool yes,ErrorCode code,const char* message){if(!yes){fail(code,message);}}
template<class F>auto guarded(F&& f)->decltype(f()){try{return f();}catch(const Failure& e){return std::unexpected(e.error);}catch(const std::bad_alloc&){return std::unexpected(Error{ErrorCode::Io,"Original layout allocation failed"});}catch(const std::exception& e){return std::unexpected(Error{ErrorCode::Io,e.what()});}}
struct Shape{std::uint64_t first=0,tail=0,tailStart=0,count=0,maximum=0;bool compact=false,reordered=false;};
Shape shape(OriginalFileGeometry g,Kind kind){
  const auto max=static_cast<std::uint64_t>(std::numeric_limits<off_t>::max());
  require(kind>=Kind::Linear&&kind<=Kind::ReorderedCompact&&g.fileBytes<=max&&g.torrentOffset<=max-g.fileBytes,ErrorCode::InvalidFormat,"Invalid original storage geometry/kind");
  if(kind==Kind::Linear){return {.maximum=g.fileBytes};}
  require(g.pieceBytes&&g.pieceBytes<=MaxPiece,ErrorCode::Unsupported,"Nonlinear piece size exceeds qualified 64 MiB bound");
  Shape s;const auto offset=g.torrentOffset%g.pieceBytes;
  if(kind==Kind::Compact){s.first=std::min<std::uint64_t>(g.fileBytes,(g.pieceBytes-offset)%g.pieceBytes);s.tail=(g.fileBytes-s.first)%g.pieceBytes;s.tailStart=g.fileBytes-s.tail;s.compact=s.first<g.fileBytes;s.maximum=s.compact?s.first+s.tail:g.fileBytes;return s;}
  s.count=g.fileBytes?1+(offset+g.fileBytes-1)/g.pieceBytes:1;
  require(s.count<=MaxSlots,ErrorCode::Unsupported,"Original reordered map exceeds one million slots");
  s.first=std::min<std::uint64_t>(g.fileBytes,g.pieceBytes-offset);s.reordered=s.count>=3;s.tail=g.fileBytes?((offset+g.fileBytes-1)%g.pieceBytes)+1:0;
  require(!s.reordered||s.count-1<=(max-s.first)/g.pieceBytes,ErrorCode::InvalidFormat,"Original physical bound overflows");s.maximum=s.reordered?s.first+(s.count-1)*g.pieceBytes:g.fileBytes;return s;
}
core::Limits limits(){return {.inputBytes=MaxControl,.stringBytes=MaxControl,.nodes=100000,.depth=64};}
core::Document parse(std::string bytes){auto d=core::Document::parse(std::move(bytes),limits());require(d&&d->root().type==core::Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid original layout control dictionary");return std::move(*d);}
std::int64_t integer(const core::Document& d,std::string_view key){const auto* n=d.find(key);require(n&&n->type==core::Node::Type::Integer,ErrorCode::InvalidFormat,"Missing/incompatible original control integer");return n->integer;}
void put(core::Document& d,std::string_view key,std::string value){require(d.replaceValue(key,std::move(value)).has_value(),ErrorCode::InvalidFormat,"Original control edit exceeds bound");}
std::string num(std::uint64_t n){return 'i'+std::to_string(n)+'e';}
std::string bytes(std::string_view s){return std::to_string(s.size())+':'+std::string(s);}
bool same(const struct stat& a,const struct stat& b){return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_mode==b.st_mode&&a.st_uid==b.st_uid&&a.st_nlink==b.st_nlink&&a.st_size==b.st_size&&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;}
struct State{std::uint64_t length=0,next=1;std::vector<std::int32_t> map;};
}
struct OriginalFileIo::Impl {
  int fd;unsigned links=1;OriginalFileGeometry geometry;Kind kind;Shape form;OriginalControlPublisher publisher;
  std::optional<std::string> raw;State state;struct stat observed{};
  bool dirty=false,broken=false,recheck=false,busy=false,expanded=false;
  Impl(int f,OriginalFileGeometry g,Kind k,Shape s,std::optional<std::string> r,OriginalControlPublisher p):fd(f),geometry(g),kind(k),form(s),publisher(std::move(p)),raw(std::move(r)){}
  bool controls()const{return form.compact||kind==Kind::Reordered||kind==Kind::ReorderedCompact;}
  void refresh(){require(::fstat(fd,&observed)==0&&S_ISREG(observed.st_mode)&&observed.st_uid==::geteuid()&&observed.st_nlink==links&&observed.st_size>=0,ErrorCode::UnsafePath,"Payload descriptor is not an owned authenticated-link regular file");}
  void check(){struct stat now{};require(::fstat(fd,&now)==0&&same(now,observed),ErrorCode::Conflict,"Authenticated payload changed outside layout owner");require(!broken,ErrorCode::Conflict,"Uncertain original IO requires control reload/reopen");}
  struct Call{Impl& self;explicit Call(Impl& p):self(p){require(!p.busy,ErrorCode::Busy,"Reentrant original file IO");p.busy=true;}~Call(){self.busy=false;}};
  void range(std::uint64_t offset,std::size_t length)const{require(offset<=geometry.fileBytes&&length<=geometry.fileBytes-offset&&length<=MaxIo,ErrorCode::InvalidFormat,"Original IO range exceeds geometry/64 MiB request bound");}
  std::uint64_t start(std::uint64_t piece)const{return piece?form.first+(piece-1)*geometry.pieceBytes:0;}
  std::uint64_t size(std::uint64_t piece)const{return std::min<std::uint64_t>(piece?geometry.pieceBytes:form.first,geometry.fileBytes-start(piece));}
  std::uint64_t physical(std::uint64_t slot)const{return slot?form.first+(slot-1)*geometry.pieceBytes:0;}
  std::uint64_t pieceAt(std::uint64_t position)const{return position<form.first?0:1+(position-form.first)/geometry.pieceBytes;}
  State decode(const std::string& value)const{
    const auto d=parse(value);State s;
    if(form.compact){require(integer(d,"version")==0,ErrorCode::InvalidFormat,"Unknown compact control version");const auto length=integer(d,"length");require(length>=0&&static_cast<std::uint64_t>(length)<=geometry.fileBytes,ErrorCode::InvalidFormat,"Compact length exceeds file");s.length=static_cast<std::uint64_t>(length);return s;}
    if(!form.reordered){return s;}
    const auto length=integer(d,"len"),next=integer(d,"next");const auto* map=d.find("pieces"),*previous=d.find("st");
    require(length>=0&&static_cast<std::uint64_t>(length)<=geometry.fileBytes&&next>=1&&static_cast<std::uint64_t>(next)<=form.count&&map&&map->type==core::Node::Type::Bytes&&map->bytes.size()==form.count*4&&(!previous||(previous->type==core::Node::Type::Integer&&(previous->integer==3||previous->integer==4))),ErrorCode::InvalidFormat,"Invalid original reordered length/map");
    s.length=static_cast<std::uint64_t>(length);s.next=static_cast<std::uint64_t>(next);s.map.reserve(static_cast<std::size_t>(form.count));std::vector<bool> used(static_cast<std::size_t>(s.next));
    for(std::size_t i=0;i<form.count;++i){std::uint32_t v=0;for(std::size_t j=0;j<4;++j){v=(v<<8)|static_cast<unsigned char>(map->bytes[i*4+j]);}if(v==0xffffffffU){s.map.push_back(-1);}else{require(v<s.next&&!used[v],ErrorCode::InvalidFormat,"Duplicate/out-of-range original physical slot");used[v]=true;s.map.push_back(static_cast<std::int32_t>(v));}}
    require(s.map[0]==0&&std::ranges::find(used,false)==used.end(),ErrorCode::InvalidFormat,"Original map has missing allocated slot");return s;
  }
  std::string encode(const State& s,Kind outputKind)const{
    auto d=raw?parse(*raw):parse("de");
    if(form.compact){put(d,"version","i0e");put(d,"length",num(s.length));}
    else{put(d,"st",num(static_cast<unsigned>(outputKind)));put(d,"len",num(form.reordered?s.length:0));put(d,"next",num(form.reordered?s.next:1));std::string map;const auto count=static_cast<std::size_t>(form.count);map.reserve(count*4);for(std::size_t i=0;i<count;++i){const auto n=form.reordered?s.map[i]:(i? -1:0);const auto v=static_cast<std::uint32_t>(n);for(int shift:{24,16,8,0}){map+=static_cast<char>((v>>shift)&255);}}put(d,"pieces",bytes(map));}
    auto out=d.serialize();require(out.size()<=MaxControl,ErrorCode::InvalidFormat,"Original control size exceeds bound");return out;
  }
  void sync(){if(::fsync(fd)!=0){broken=true;fail(ErrorCode::Io,"Cannot sync original payload");}refresh();}
  void publish(const State& next,Kind outputKind){
    const auto after=encode(next,outputKind);if(raw&&*raw==after){dirty=false;return;}require(bool(publisher),ErrorCode::Missing,"Original control publication callback is unavailable");
    try{const auto result=publisher(raw,after);if(!result){broken=true;throw Failure{result.error()};}}catch(...){broken=true;throw;}
    raw=after;dirty=false;
  }
  std::size_t preadAt(std::uint64_t offset,std::span<std::byte> out){std::size_t done=0;while(done<out.size()){const auto n=::pread(fd,out.data()+done,out.size()-done,static_cast<off_t>(offset+done));if(n<0&&errno==EINTR){continue;}require(n>=0,ErrorCode::Io,"Original payload pread failed");if(!n){break;}done+=static_cast<std::size_t>(n);}return done;}
  void pwriteAt(std::uint64_t offset,std::span<const std::byte> input){std::size_t done=0;while(done<input.size()){const auto n=::pwrite(fd,input.data()+done,input.size()-done,static_cast<off_t>(offset+done));if(n<0&&errno==EINTR){continue;}if(n<=0){broken=true;refresh();fail(ErrorCode::Io,"Original payload pwrite failed; partial write requires recheck");}done+=static_cast<std::size_t>(n);}refresh();}
  void truncate(std::uint64_t length){if(::ftruncate(fd,static_cast<off_t>(length))!=0){broken=true;fail(ErrorCode::Io,"Original payload resize failed");}refresh();}
};
std::expected<std::uint64_t,Error> originalMaximumPhysicalSize(OriginalFileGeometry g,OriginalStorageKind k){return guarded([&]()->std::expected<std::uint64_t,Error>{return shape(g,k).maximum;});}
std::string originalControlLeaf(std::size_t index,OriginalStorageKind k){if(k==Kind::Linear){return {};}return "fmfile"+std::to_string(index)+".dat"+(k==Kind::Reordered||k==Kind::ReorderedCompact?".2":"");}
OriginalFileIo::OriginalFileIo(std::unique_ptr<Impl> p):impl_(std::move(p)){}
OriginalFileIo::OriginalFileIo(OriginalFileIo&&)noexcept=default;
OriginalFileIo& OriginalFileIo::operator=(OriginalFileIo&&)noexcept=default;
OriginalFileIo::~OriginalFileIo()=default;
std::expected<OriginalFileIo,Error> OriginalFileIo::open(int fd,OriginalFileGeometry g,Kind k,std::optional<std::string> control,OriginalControlPublisher publisher,bool recoverMissing,bool expansionPending,unsigned authenticatedLinks){return guarded([&]()->std::expected<OriginalFileIo,Error>{
  const auto form=shape(g,k);auto p=std::make_unique<Impl>(fd,g,k,form,std::move(control),std::move(publisher));require(authenticatedLinks==1||authenticatedLinks==2,ErrorCode::UnsafePath,"Only authenticated one/two-link payloads are supported");p->links=authenticatedLinks;p->refresh();require(!expansionPending||k==Kind::Compact,ErrorCode::InvalidFormat,"Expansion intent applies only to compact storage");require(static_cast<std::uint64_t>(p->observed.st_size)<=(expansionPending?g.fileBytes:form.maximum),ErrorCode::InvalidFormat,"Physical original payload exceeds layout bound");
  if(p->controls()){
    if(p->raw){p->state=p->decode(*p->raw);}
    else{
      require(p->observed.st_size==0||recoverMissing,ErrorCode::Missing,"Existing nonlinear payload requires original control or explicit recovery");p->dirty=true;
      if(form.reordered){p->state.map.assign(static_cast<std::size_t>(form.count),-1);p->state.map[0]=0;}
      if(p->observed.st_size&&recoverMissing){p->recheck=true;
        if(form.compact){const auto actual=static_cast<std::uint64_t>(p->observed.st_size);require(actual==form.first||actual==form.first+form.tail,ErrorCode::InvalidFormat,"Compact recovery physical length is not original edge layout");p->state.length=actual==form.first+form.tail?g.fileBytes:actual;}
        else if(form.reordered){p->state.length=std::min<std::uint64_t>(g.fileBytes,static_cast<std::uint64_t>(p->observed.st_size));p->state.next=std::min<std::uint64_t>(form.count,(p->state.length+g.pieceBytes-1)/g.pieceBytes+1);for(std::size_t i=1;i<p->state.next;++i){p->state.map[i]=static_cast<std::int32_t>(i);}}
      }
    }
  }
  if(!form.compact&&!form.reordered){p->state.length=static_cast<std::uint64_t>(p->observed.st_size);}
  return OriginalFileIo(std::move(p));
});}
std::uint64_t OriginalFileIo::length()const noexcept{return impl_->state.length;}
bool OriginalFileIo::requiresRecheck()const noexcept{return impl_->recheck;}
bool OriginalFileIo::recoveryPending()const noexcept{return impl_->broken;}
const std::optional<std::string>& OriginalFileIo::controlBytes()const noexcept{return impl_->raw;}
std::expected<OriginalReadResult,Error> OriginalFileIo::read(std::uint64_t offset,std::span<std::byte> output){return guarded([&]()->std::expected<OriginalReadResult,Error>{auto& p=*impl_;Impl::Call call(p);p.check();p.range(offset,output.size());OriginalReadResult result;std::fill(output.begin(),output.end(),std::byte{});std::size_t used=0;
  auto missing=[&](std::uint64_t start,std::uint64_t length){if(!length){return;}if(!result.missing.empty()&&result.missing.back().offset+result.missing.back().length==start){result.missing.back().length+=length;}else{require(result.missing.size()<65536,ErrorCode::Unsupported,"Missing segment report exceeds bound");result.missing.push_back({start,length});}};
  while(used<output.size()){
    const auto at=offset+used;std::uint64_t n=output.size()-used,physical=at;bool absent=false;
    if(p.form.compact&&!p.expanded){if(at<p.form.first){n=std::min(n,p.form.first-at);}else if(at<p.form.tailStart){n=std::min(n,p.form.tailStart-at);absent=true;}else{physical=p.form.first+at-p.form.tailStart;}}
    else if(p.form.reordered){const auto piece=p.pieceAt(at);n=std::min(n,p.size(piece)-(at-p.start(piece)));const auto slot=p.state.map[static_cast<std::size_t>(piece)];if(slot<0){absent=true;}else{physical=p.physical(static_cast<std::uint64_t>(slot))+at-p.start(piece);}}
    if(absent){missing(at,n);}else{const auto count=p.preadAt(physical,output.subspan(used,static_cast<std::size_t>(n)));result.physicalBytes+=count;missing(at+count,n-count);}used+=static_cast<std::size_t>(n);
  }
  p.check();if(p.form.compact&&!p.expanded&&offset+output.size()>p.state.length){p.state.length=offset+output.size();p.dirty=true;}return result;
});}
std::expected<void,Error> OriginalFileIo::write(std::uint64_t offset,std::span<const std::byte> input){return guarded([&]()->std::expected<void,Error>{auto& p=*impl_;Impl::Call call(p);p.check();p.range(offset,input.size());const int flags=::fcntl(p.fd,F_GETFL);require(flags>=0&&(flags&O_ACCMODE)!=O_RDONLY,ErrorCode::Io,"Original payload is not writable");
  struct Part{std::uint64_t physical;std::size_t input,length;};std::vector<Part> parts;std::optional<State> next;std::size_t used=0;
  while(used<input.size()){
    const auto at=offset+used;std::uint64_t n=input.size()-used,physical=at;bool discard=false;
    if(p.form.compact&&!p.expanded){if(at<p.form.first){n=std::min(n,p.form.first-at);}else if(at<p.form.tailStart){n=std::min(n,p.form.tailStart-at);discard=true;}else{physical=p.form.first+at-p.form.tailStart;}}
    else if(p.form.reordered){const auto piece=p.pieceAt(at);n=std::min(n,p.size(piece)-(at-p.start(piece)));const auto index=static_cast<std::size_t>(piece);auto slot=next?next->map[index]:p.state.map[index];if(slot<0){
      // Existing slots need no map copy. A first allocation creates one private
      // working state, still fully planned before any payload mutation.
      if(!next){next.emplace(p.state);}require(next->next<p.form.count,ErrorCode::InvalidFormat,"Original allocation map is exhausted");slot=static_cast<std::int32_t>(next->next++);next->map[index]=slot;
    }physical=p.physical(static_cast<std::uint64_t>(slot))+at-p.start(piece);}
    if(!discard){parts.push_back({physical,used,static_cast<std::size_t>(n)});}used+=static_cast<std::size_t>(n);
  }
  const auto length=std::max(p.state.length,offset+input.size());if(next){next->length=length;require(bool(p.publisher),ErrorCode::Missing,"Allocation requires durable original control publisher");static_cast<void>(p.encode(*next,p.kind));}
  // Fresh nonlinear files need their original marker before any payload IO.
  // open itself remains read-only; a crash cannot reinterpret an unmapped
  // append as an ordinary linear prefix.
  if(p.controls()&&!p.raw){p.sync();p.publish(p.state,p.kind);}
  for(const auto& part:parts){p.pwriteAt(part.physical,input.subspan(part.input,part.length));}
  if(next){p.sync();p.publish(*next,p.kind);p.state=std::move(*next);}else{if(p.controls()){p.dirty=p.dirty||length!=p.state.length;}p.state.length=length;}
  if(!p.form.compact&&!p.form.reordered){p.state.length=static_cast<std::uint64_t>(p.observed.st_size);}return {};
});}
std::expected<void,Error> OriginalFileIo::setLength(std::uint64_t length){return guarded([&]()->std::expected<void,Error>{auto& p=*impl_;Impl::Call call(p);p.check();require(length<=p.geometry.fileBytes,ErrorCode::InvalidFormat,"Original virtual length exceeds geometry");if(p.form.compact||p.form.reordered){p.dirty=p.dirty||p.state.length!=length;p.state.length=length;}else{p.truncate(length);p.state.length=length;}return {};});}
std::expected<bool,Error> OriginalFileIo::pieceComplete(std::uint64_t global,std::span<const std::byte> fragment){return guarded([&]()->std::expected<bool,Error>{auto& p=*impl_;Impl::Call call(p);p.check();require(p.geometry.pieceBytes!=0,ErrorCode::InvalidFormat,"Piece completion requires nonzero geometry piece size");const auto first=p.geometry.torrentOffset/p.geometry.pieceBytes;require(global>=first,ErrorCode::InvalidFormat,"Completed piece precedes file");if(p.form.reordered){const auto piece=global-first;require(piece<p.form.count&&fragment.size()==p.size(piece)&&p.state.map[static_cast<std::size_t>(piece)]>=0,ErrorCode::InvalidFormat,"Completed fragment does not match allocated original piece");}return false;});}
std::expected<void,Error> OriginalFileIo::flush(){return guarded([&]()->std::expected<void,Error>{auto& p=*impl_;Impl::Call call(p);p.check();p.sync();if(p.controls()&&(p.dirty||!p.raw)){p.publish(p.state,p.kind);}return {};});}
std::expected<void,Error> OriginalFileIo::recover(){return guarded([&]()->std::expected<void,Error>{auto& p=*impl_;Impl::Call call(p);p.check();return {};});}
std::expected<void,Error> OriginalFileIo::convertToLinear(){return guarded([&]()->std::expected<void,Error>{auto& p=*impl_;Impl::Call call(p);p.check();require(p.kind==Kind::Compact&&!p.expanded,ErrorCode::Unsupported,"Only compact edge storage expands directly to linear");
  if(p.form.compact&&p.form.tail){std::vector<std::byte> tail(static_cast<std::size_t>(p.form.tail));const auto n=p.preadAt(p.form.first,tail);if(n){p.pwriteAt(p.form.tailStart,std::span<const std::byte>(tail).first(n));}}
  p.truncate(p.geometry.fileBytes);p.sync();p.recheck=true;p.expanded=true;p.kind=Kind::Linear;p.form.compact=false;p.state.length=p.geometry.fileBytes;p.dirty=false;return {};
});}
std::expected<void,Error> OriginalFileIo::setReorderedKind(Kind kind){return guarded([&]()->std::expected<void,Error>{auto& p=*impl_;Impl::Call call(p);p.check();require((p.kind==Kind::Reordered||p.kind==Kind::ReorderedCompact)&&(kind==Kind::Reordered||kind==Kind::ReorderedCompact),ErrorCode::Unsupported,"Original reordered type change requires R/RC");p.sync();p.publish(p.state,kind);p.kind=kind;return {};});}
}
