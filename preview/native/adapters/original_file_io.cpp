#include "original_file_io.hpp"
#include "biglybtc/torrent_layout.hpp"
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <fcntl.h>
#include <limits>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace biglybtc::storage {
namespace {
using Kind=OriginalStorageKind;
constexpr std::uint64_t MaxPiece=static_cast<std::uint64_t>(compat::OriginalMaximumPieceLength),MaxSlots=1000000,MaxIo=64ULL*1024*1024;
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
  require(g.pieceBytes&&g.pieceBytes<=MaxPiece,ErrorCode::Unsupported,"Nonlinear piece size exceeds supported 256 MiB bound");
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
  bool dirty=false,broken=false,recheck=false,busy=false,expanded=false,completionPending=false;
  Impl(int f,OriginalFileGeometry g,Kind k,Shape s,std::optional<std::string> r,OriginalControlPublisher p):fd(f),geometry(g),kind(k),form(s),publisher(std::move(p)),raw(std::move(r)){}
  bool controls()const{return form.compact||kind==Kind::Reordered||kind==Kind::ReorderedCompact;}
  void refresh(){require(::fstat(fd,&observed)==0&&S_ISREG(observed.st_mode)&&observed.st_uid==::geteuid()&&observed.st_nlink==links&&observed.st_size>=0,ErrorCode::UnsafePath,"Payload descriptor is not an owned authenticated-link regular file");}
  void check(bool allowCompletion=false){struct stat now{};require(::fstat(fd,&now)==0&&same(now,observed),ErrorCode::Conflict,"Authenticated payload changed outside layout owner");require(!broken,ErrorCode::Conflict,"Uncertain original IO requires control reload/reopen");require(allowCompletion||!completionPending,ErrorCode::Conflict,"Interrupted reordered completion requires native recovery");}
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
  void publishBytes(const std::string& after){
    if(raw&&*raw==after){dirty=false;return;}require(bool(publisher),ErrorCode::Missing,"Original control publication callback is unavailable");
    try{const auto result=publisher(raw,after);if(!result){broken=true;throw Failure{result.error()};}}catch(...){broken=true;throw;}
    raw=after;dirty=false;
  }
  void publish(const State& next,Kind outputKind){publishBytes(encode(next,outputKind));}
  static constexpr std::string_view CompletionKey="BiglyBTC.reorderedCompletion";
  static constexpr std::size_t CompletionChunk=64*1024;
  struct Completion{std::uint64_t source=0,target=0,peer=0,progress=0,phase=1;std::string left,right;};
  std::optional<Completion> completion()const{
    if(!raw){return {};}
    const auto d=parse(*raw);const auto* n=d.find(CompletionKey);if(!n){return {};}
    require(n->type==core::Node::Type::Dictionary,ErrorCode::InvalidFormat,"Invalid reordered completion record");if(n->children.empty()){return {};}
    require(form.reordered&&n->children.size()==28,ErrorCode::InvalidFormat,"Reordered completion record shape differs");
    std::set<std::string_view> keys;for(std::size_t i=0;i<n->children.size();i+=2){require(keys.insert(n->children[i].bytes).second,ErrorCode::InvalidFormat,"Duplicate reordered completion key");}
    const auto value=[&](std::string_view key){const auto* v=n->find(key);require(v&&v->type==core::Node::Type::Integer&&v->integer>=0,ErrorCode::InvalidFormat,"Invalid reordered completion integer");return static_cast<std::uint64_t>(v->integer);};
    const auto text=[&](std::string_view key){const auto* v=n->find(key);require(v&&v->type==core::Node::Type::Bytes&&v->bytes.size()<=CompletionChunk,ErrorCode::InvalidFormat,"Invalid reordered completion chunk");return std::string(v->bytes);};
    require(value("version")==1&&value("device")==static_cast<std::uint64_t>(observed.st_dev)&&value("inode")==static_cast<std::uint64_t>(observed.st_ino)&&value("offset")==geometry.torrentOffset&&value("length")==geometry.fileBytes&&value("piece")==geometry.pieceBytes&&value("kind")==static_cast<unsigned>(kind),ErrorCode::Conflict,"Reordered completion identity/geometry differs");
    Completion j{value("source"),value("target"),value("peer"),value("progress"),value("phase"),text("left"),text("right")};
    require(j.target==form.count-1&&j.source>0&&j.source<=j.target&&j.peer>0&&j.peer<=j.target&&j.progress<=geometry.pieceBytes&&(j.phase==1||j.phase==2)&&state.length==geometry.fileBytes,ErrorCode::InvalidFormat,"Invalid reordered completion plan");
    if(j.phase==1){
      require(j.source<j.target&&j.peer<j.target&&state.map.back()==static_cast<std::int32_t>(j.source)&&state.map[static_cast<std::size_t>(j.peer)]==static_cast<std::int32_t>(j.target),ErrorCode::Conflict,"Reordered completion old map differs");
      const auto wanted=std::min<std::uint64_t>(CompletionChunk,geometry.pieceBytes-j.progress);
      const auto tail=j.progress<form.tail?std::min<std::uint64_t>(wanted,form.tail-j.progress):0;
      require((j.right.empty()&&j.left.empty())||(j.right.size()==wanted&&wanted&&j.left.size()==tail),ErrorCode::InvalidFormat,"Reordered completion redo length differs");
    }else{require(j.progress==geometry.pieceBytes&&j.left.empty()&&j.right.empty()&&state.map.back()==static_cast<std::int32_t>(j.target)&&state.map[static_cast<std::size_t>(j.peer)]==static_cast<std::int32_t>(j.source),ErrorCode::Conflict,"Reordered completion new map differs");}
    return j;
  }
  void saveCompletion(const State& next,const Completion* j){
    auto d=parse(encode(next,kind));std::string record="de";
    if(j){
      require(static_cast<std::uint64_t>(observed.st_dev)<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())&&static_cast<std::uint64_t>(observed.st_ino)<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),ErrorCode::Unsupported,"Reordered completion identity exceeds signed journal range");
      record="d";for(const auto& [key,value]:{std::pair<std::string_view,std::uint64_t>{"version",1},{"device",static_cast<std::uint64_t>(observed.st_dev)},{"inode",static_cast<std::uint64_t>(observed.st_ino)},{"offset",geometry.torrentOffset},{"length",geometry.fileBytes},{"piece",geometry.pieceBytes},{"kind",static_cast<unsigned>(kind)},{"source",j->source},{"target",j->target},{"peer",j->peer},{"progress",j->progress},{"phase",j->phase}}){record+=bytes(key)+num(value);}record+=bytes("left")+bytes(j->left)+bytes("right")+bytes(j->right)+'e';
    }
    put(d,CompletionKey,std::move(record));const auto after=d.serialize();require(after.size()<=MaxControl,ErrorCode::InvalidFormat,"Completion redo and original map exceed control bound");publishBytes(after);
  }
  void finishReordered(bool recovering){
    auto pending=completion();if(recovering&&!pending){return;}
    if(!pending){
      if(!form.reordered||static_cast<std::uint64_t>(observed.st_size)<=geometry.fileBytes){return;}
      require(state.length==geometry.fileBytes&&state.next==form.count&&std::ranges::find(state.map,-1)==state.map.end(),ErrorCode::Conflict,"Only a fully present reordered file can be finalized");
      for(std::size_t piece=0;piece<state.map.size();++piece){require(physical(static_cast<std::uint64_t>(state.map[piece]))+size(piece)<=static_cast<std::uint64_t>(observed.st_size),ErrorCode::Conflict,"Complete reordered piece has missing physical bytes");}
      const auto target=form.count-1,source=static_cast<std::uint64_t>(state.map.back());const auto at=std::ranges::find(state.map,static_cast<std::int32_t>(target));
      require(source>0&&at!=state.map.end(),ErrorCode::InvalidFormat,"Complete reordered tail has no target slot");
      pending=Completion{source,target,static_cast<std::uint64_t>(at-state.map.begin()),0,1,{},{}};
      if(source==target){pending->progress=geometry.pieceBytes;pending->phase=2;}
      completionPending=true;recheck=true;saveCompletion(state,&*pending);
    }
    completionPending=true;recheck=true;auto& j=*pending;
    while(j.phase==1&&j.progress<geometry.pieceBytes){
      check(true);
      if(j.right.empty()){
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(CompletionChunk,geometry.pieceBytes-j.progress));
        j.right.resize(count);require(preadAt(physical(j.target)+j.progress,std::as_writable_bytes(std::span(j.right.data(),j.right.size())))==count,ErrorCode::Io,"Cannot read complete reordered swap piece");
        const auto tail=j.progress<form.tail?static_cast<std::size_t>(std::min<std::uint64_t>(count,form.tail-j.progress)):0;j.left.resize(tail);
        if(tail){require(preadAt(physical(j.source)+j.progress,std::as_writable_bytes(std::span(j.left.data(),j.left.size())))==tail,ErrorCode::Io,"Cannot read complete reordered tail");}check(true);saveCompletion(state,&j);
      }
      pwriteAt(physical(j.source)+j.progress,std::as_bytes(std::span(j.right.data(),j.right.size())));
      if(!j.left.empty()){pwriteAt(physical(j.target)+j.progress,std::as_bytes(std::span(j.left.data(),j.left.size())));}sync();
      j.progress+=j.right.size();j.left.clear();j.right.clear();saveCompletion(state,&j);
    }
    if(j.phase==1){State next=state;next.map.back()=static_cast<std::int32_t>(j.target);next.map[static_cast<std::size_t>(j.peer)]=static_cast<std::int32_t>(j.source);j.phase=2;saveCompletion(next,&j);state=std::move(next);}
    if(static_cast<std::uint64_t>(observed.st_size)!=geometry.fileBytes){truncate(geometry.fileBytes);}sync();saveCompletion(state,nullptr);completionPending=false;
  }
  std::size_t preadAt(std::uint64_t offset,std::span<std::byte> out){std::size_t done=0;while(done<out.size()){const auto n=::pread(fd,out.data()+done,out.size()-done,static_cast<off_t>(offset+done));if(n<0&&errno==EINTR){continue;}require(n>=0,ErrorCode::Io,"Original payload pread failed");if(!n){break;}done+=static_cast<std::size_t>(n);}return done;}
  void pwriteAt(std::uint64_t offset,std::span<const std::byte> input){std::size_t done=0;while(done<input.size()){const auto n=::pwrite(fd,input.data()+done,input.size()-done,static_cast<off_t>(offset+done));if(n<0&&errno==EINTR){continue;}if(n<=0){broken=true;refresh();fail(ErrorCode::Io,"Original payload pwrite failed; partial write requires recheck");}done+=static_cast<std::size_t>(n);}refresh();}
  void truncate(std::uint64_t length){if(::ftruncate(fd,static_cast<off_t>(length))!=0){broken=true;fail(ErrorCode::Io,"Original payload resize failed");}refresh();}
};
std::expected<std::uint64_t,Error> originalMaximumPhysicalSize(OriginalFileGeometry g,OriginalStorageKind k){return guarded([&]()->std::expected<std::uint64_t,Error>{return shape(g,k).maximum;});}
std::string originalControlLeaf(std::size_t index,OriginalStorageKind k){if(k==Kind::Linear){return {};}return "fmfile"+std::to_string(index)+".dat"+(k==Kind::Reordered||k==Kind::ReorderedCompact?".2":"");}
std::expected<void,Error> OriginalFileIo::validateControl(OriginalFileGeometry g,Kind k,std::string_view raw){return guarded([&]()->std::expected<void,Error>{
  require(k!=Kind::Linear&&raw.size()<=MaxControl,ErrorCode::InvalidFormat,"Invalid original control validation input");
  const auto form=shape(g,k);Impl candidate(-1,g,k,form,std::nullopt,{});const std::string bytes(raw);(void)candidate.decode(bytes);
  // Optimized short layouts do not consume a map in open(), but a selected
  // control still needs its source-required envelope before it can supersede
  // a backup containing native recovery state. No second map decoder.
  if(!form.compact&&!form.reordered){const auto d=parse(bytes);
    if(k==Kind::Compact){const auto n=integer(d,"length");require(integer(d,"version")==0&&n>=0&&static_cast<std::uint64_t>(n)<=g.fileBytes,ErrorCode::InvalidFormat,"Invalid short compact control");}
    else{const auto n=integer(d,"len"),next=integer(d,"next");const auto* pieces=d.find("pieces"),*st=d.find("st");require(n>=0&&static_cast<std::uint64_t>(n)<=g.fileBytes&&next>=1&&static_cast<std::uint64_t>(next)<=form.count&&pieces&&pieces->type==core::Node::Type::Bytes&&pieces->bytes.size()==form.count*4&&(!st||(st->type==core::Node::Type::Integer&&(st->integer==3||st->integer==4))),ErrorCode::InvalidFormat,"Invalid short reordered control");}
  }return {};
});}
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
  if(p->form.reordered&&p->completion()){p->completionPending=true;p->recheck=true;}
  return OriginalFileIo(std::move(p));
});}
std::uint64_t OriginalFileIo::length()const noexcept{return impl_->state.length;}
bool OriginalFileIo::requiresRecheck()const noexcept{return impl_->recheck;}
bool OriginalFileIo::recoveryPending()const noexcept{return impl_->broken||impl_->completionPending;}
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
  struct Part{std::uint64_t logical,physical;std::size_t input,length;};std::vector<Part> parts;std::optional<State> next;std::optional<std::uint64_t> tailSource;std::size_t used=0;
  while(used<input.size()){
    const auto at=offset+used;std::uint64_t n=input.size()-used,physical=at;bool discard=false;
    if(p.form.compact&&!p.expanded){if(at<p.form.first){n=std::min(n,p.form.first-at);}else if(at<p.form.tailStart){n=std::min(n,p.form.tailStart-at);discard=true;}else{physical=p.form.first+at-p.form.tailStart;}}
    else if(p.form.reordered){const auto piece=p.pieceAt(at);n=std::min(n,p.size(piece)-(at-p.start(piece)));const auto index=static_cast<std::size_t>(piece);auto slot=next?next->map[index]:p.state.map[index];if(slot<0){
      // Existing slots need no map copy. A first allocation creates one private
      // working state, still fully planned before any payload mutation.
      if(!next){next.emplace(p.state);}require(next->next<p.form.count,ErrorCode::InvalidFormat,"Original allocation map is exhausted");slot=static_cast<std::int32_t>(next->next++);next->map[index]=slot;
      if(static_cast<std::uint64_t>(slot)==p.form.count-1&&piece!=p.form.count-1&&p.state.map.back()>0){
        // The old tail remains readable until its copied preimage and new map
        // are durable. The final physical slot was not referenced by old map.
        tailSource=static_cast<std::uint64_t>(p.state.map.back());next->map.back()=slot;
        slot=static_cast<std::int32_t>(*tailSource);next->map[index]=slot;
      }
    }physical=p.physical(static_cast<std::uint64_t>(slot))+at-p.start(piece);}
    if(!discard){parts.push_back({at,physical,used,static_cast<std::size_t>(n)});}used+=static_cast<std::size_t>(n);
  }
  const auto length=std::max(p.state.length,offset+input.size());if(next){next->length=length;require(bool(p.publisher),ErrorCode::Missing,"Allocation requires durable original control publisher");static_cast<void>(p.encode(*next,p.kind));}
  std::uint64_t availableTail=0;std::vector<std::byte> scratch;
  if(tailSource){
    const auto eof=static_cast<std::uint64_t>(p.observed.st_size),source=p.physical(*tailSource),target=p.physical(p.form.count-1);
    availableTail=std::min(p.form.tail,eof>source?eof-source:0);
    // Moving retained bytes across EOF must not make another old allocated
    // piece's absent suffix appear present. Such a damaged/incomplete extent
    // needs an explicit missing-data repair rather than this optimization.
    if(availableTail&&target+availableTail>eof){for(std::size_t piece=0;piece<p.state.map.size();++piece){const auto slot=p.state.map[piece];if(slot>=0&&piece+1<p.state.map.size()){require(p.physical(static_cast<std::uint64_t>(slot))+p.size(piece)<=eof,ErrorCode::Conflict,"Tail allocation would hide another original piece's missing EOF bytes");}}}
    scratch.resize(static_cast<std::size_t>(std::min<std::uint64_t>(availableTail,64*1024)));
    // Re-resolve every planned fragment after the allocation permutation;
    // a request may also contain a later fragment of the retained tail.
    for(auto& part:parts){const auto piece=p.pieceAt(part.logical);part.physical=p.physical(static_cast<std::uint64_t>(next->map[static_cast<std::size_t>(piece)]))+part.logical-p.start(piece);}
  }
  // Fresh nonlinear files need their original marker before any payload IO.
  // open itself remains read-only; a crash cannot reinterpret an unmapped
  // append as an ordinary linear prefix.
  if(p.controls()&&!p.raw){p.sync();p.publish(p.state,p.kind);}
  if(tailSource){
    try{
      const auto source=p.physical(*tailSource),target=p.physical(p.form.count-1);std::uint64_t copied=0;
      while(copied<availableTail){
        p.check();const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(scratch.size(),availableTail-copied));
        require(p.preadAt(source+copied,std::span<std::byte>(scratch).first(count))==count,ErrorCode::Io,"Cannot read retained original tail preimage");p.check();
        p.pwriteAt(target+copied,std::span<const std::byte>(scratch).first(count));copied+=count;
      }
      // An unpublished earlier append may have left orphan bytes. They must
      // not become a fake available suffix of the relocated logical tail.
      if(static_cast<std::uint64_t>(p.observed.st_size)>target+availableTail){p.truncate(target+availableTail);}
      p.sync();p.publish(*next,p.kind);p.state=std::move(*next);next.reset();p.check();
    }catch(...){p.broken=true;throw;}
  }
  for(const auto& part:parts){p.pwriteAt(part.physical,input.subspan(part.input,part.length));}
  if(tailSource){p.sync();}
  if(next){p.sync();p.publish(*next,p.kind);p.state=std::move(*next);}else{if(p.controls()){p.dirty=p.dirty||length!=p.state.length;}p.state.length=length;}
  if(!p.form.compact&&!p.form.reordered){p.state.length=static_cast<std::uint64_t>(p.observed.st_size);}return {};
});}
std::expected<void,Error> OriginalFileIo::setLength(std::uint64_t length){return guarded([&]()->std::expected<void,Error>{auto& p=*impl_;Impl::Call call(p);p.check();require(length<=p.geometry.fileBytes,ErrorCode::InvalidFormat,"Original virtual length exceeds geometry");if(p.form.compact||p.form.reordered){p.dirty=p.dirty||p.state.length!=length;p.state.length=length;}else{p.truncate(length);p.state.length=length;}return {};});}
std::expected<bool,Error> OriginalFileIo::pieceComplete(std::uint64_t global,std::span<const std::byte> fragment){return guarded([&]()->std::expected<bool,Error>{auto& p=*impl_;Impl::Call call(p);p.check();require(p.geometry.pieceBytes!=0,ErrorCode::InvalidFormat,"Piece completion requires nonzero geometry piece size");const auto first=p.geometry.torrentOffset/p.geometry.pieceBytes;require(global>=first,ErrorCode::InvalidFormat,"Completed piece precedes file");if(p.form.reordered){const auto piece=global-first;require(piece<p.form.count&&fragment.size()==p.size(piece)&&p.state.map[static_cast<std::size_t>(piece)]>=0,ErrorCode::InvalidFormat,"Completed fragment does not match allocated original piece");}return false;});}
std::expected<void,Error> OriginalFileIo::flush(){return guarded([&]()->std::expected<void,Error>{auto& p=*impl_;Impl::Call call(p);p.check();p.sync();if(p.controls()&&(p.dirty||!p.raw)){p.publish(p.state,p.kind);}return {};});}
std::expected<void,Error> OriginalFileIo::recover(){return guarded([&]()->std::expected<void,Error>{auto& p=*impl_;Impl::Call call(p);p.check(true);try{p.finishReordered(true);}catch(...){p.broken=true;throw;}return {};});}
std::expected<bool,Error> OriginalFileIo::finishAllocatedReordered(){return guarded([&]()->std::expected<bool,Error>{
  auto& p=*impl_;Impl::Call call(p);p.check();
  if(!p.form.reordered||static_cast<std::uint64_t>(p.observed.st_size)<=p.geometry.fileBytes
      ||p.state.length!=p.geometry.fileBytes||p.state.next!=p.form.count
      ||std::ranges::find(p.state.map,-1)!=p.state.map.end()){return false;}
  for(std::size_t piece=0;piece<p.state.map.size();++piece){
    if(p.physical(static_cast<std::uint64_t>(p.state.map[piece]))+p.size(piece)>static_cast<std::uint64_t>(p.observed.st_size)){return false;}
  }
  try{p.finishReordered(false);}catch(...){p.broken=true;throw;}return true;
});}
std::expected<void,Error> OriginalFileIo::finishCompleteReordered(){return guarded([&]()->std::expected<void,Error>{auto& p=*impl_;Impl::Call call(p);p.check(true);try{p.finishReordered(false);}catch(...){p.broken=true;throw;}return {};});}
std::expected<void,Error> OriginalFileIo::convertToLinear(){return guarded([&]()->std::expected<void,Error>{auto& p=*impl_;Impl::Call call(p);p.check();require(p.kind==Kind::Compact&&!p.expanded,ErrorCode::Unsupported,"Only compact edge storage expands directly to linear");
  if(p.form.compact&&p.form.tail&&p.form.tailStart!=p.form.first){
    // Destinations begin beyond the entire packed tail, so streaming never
    // overwrites the old Java-readable edges. A caller-authenticated expansion
    // journal keeps these edges authoritative until original storetype L wins.
    require(p.form.tailStart>=p.form.first+p.form.tail,ErrorCode::InvalidFormat,"Compact conversion ranges overlap");
    // Snapshot EOF before writes extend this same descriptor. Otherwise our
    // own sparse expansion could turn an absent source tail into extra input.
    const auto available=std::min(p.form.tail,static_cast<std::uint64_t>(p.observed.st_size)>p.form.first?static_cast<std::uint64_t>(p.observed.st_size)-p.form.first:0);
    if(available){
      constexpr std::size_t chunkBytes=64*1024;
      std::vector<std::byte> chunk(static_cast<std::size_t>(std::min<std::uint64_t>(available,chunkBytes)));
      std::uint64_t copied=0;
      try{
        while(copied<available){
          p.check();const auto wanted=static_cast<std::size_t>(std::min<std::uint64_t>(chunk.size(),available-copied));
          const auto count=p.preadAt(p.form.first+copied,std::span<std::byte>(chunk).first(wanted));p.check();
          if(!count){break;}p.pwriteAt(p.form.tailStart+copied,std::span<const std::byte>(chunk).first(count));copied+=count;
          if(count<wanted){break;}
        }
      }catch(...){p.broken=true;throw;}
    }
  }
  p.truncate(p.geometry.fileBytes);p.sync();p.recheck=true;p.expanded=true;p.kind=Kind::Linear;p.form.compact=false;p.state.length=p.geometry.fileBytes;p.dirty=false;return {};
});}
std::expected<void,Error> OriginalFileIo::setReorderedKind(Kind kind){return guarded([&]()->std::expected<void,Error>{auto& p=*impl_;Impl::Call call(p);p.check();require((p.kind==Kind::Reordered||p.kind==Kind::ReorderedCompact)&&(kind==Kind::Reordered||kind==Kind::ReorderedCompact),ErrorCode::Unsupported,"Original reordered type change requires R/RC");p.sync();p.publish(p.state,kind);p.kind=kind;return {};});}
}
