// SPDX-License-Identifier: GPL-2.0-or-later
#include "protocol.hpp"
#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
using namespace biglybtc::services::i2p::i2cp;
namespace {
void require(bool ok,const char* why){if(!ok){throw std::runtime_error(why);}}
template<class T>T take(Result<T> value){if(!value){throw std::runtime_error(value.error().message);}return std::move(*value);}
struct Case {std::string name;Message message;};
std::vector<Case> cases(){return {
  {"get-date-none",GetDate{std::nullopt}}, {"get-date-empty",GetDate{std::string{}}},
  {"get-date-version",GetDate{std::string("2.13.0")}}, {"get-date-maximum",GetDate{std::string(255,'A')}},
  {"set-date-legacy",SetDate{0x0102030405060708ULL,std::nullopt}},
  {"set-date-version",SetDate{1761739200123ULL,std::string("2.13.0")}},
  {"set-date-zero",SetDate{0,std::string{}}},
  {"disconnect-control",Disconnect{std::string("\0\tA\x7f",4)}}, {"disconnect-maximum",Disconnect{std::string(255,'Z')}},
  {"status-created",SessionStatus{0xabcd,SessionState::Created}}, {"status-refused",SessionStatus{65535,SessionState::Refused}},
  {"destroy-session",DestroySession{65535}}
};}
void knownWireAndTypedRoundtrips(){
  require(ClientGreeting==std::array<Byte,1>{0x2a},"Original I2CP client protocol byte changed");
  require(take(encodeMessage(GetDate{std::nullopt}))==Bytes({0,0,0,0,32}),"Absent GetDate version wire differs");
  require(take(encodeMessage(GetDate{std::string{}}))==Bytes({0,0,0,1,32,0}),"Empty version differs from absent version");
  require(take(encodeMessage(GetDate{std::string("2.13.0")}))==Bytes({0,0,0,7,32,6,'2','.','1','3','.','0'}),"GetDate length/type/version layout differs");
  require(take(encodeMessage(SetDate{0x0102030405060708ULL,std::nullopt}))==Bytes({0,0,0,8,33,1,2,3,4,5,6,7,8}),"SetDate big-endian milliseconds differ");
  require(take(encodeMessage(SessionStatus{0xabcd,SessionState::Created}))==Bytes({0,0,0,3,20,0xab,0xcd,1}),"SessionStatus token/status wire differs");
  require(take(encodeMessage(DestroySession{65535}))==Bytes({0,0,0,2,3,255,255}),"DestroySession token wire differs");
  for(const auto& item:cases()){
    const auto wire=take(encodeMessage(item.message));const auto frame=take(decodeFrame(wire));
    require(take(decodeMessage(frame))==item.message,"Typed I2CP message roundtrip changed fields");
    require(take(encodeFrame(frame.type,frame.payload))==wire,"Frame length excludes/contains incorrect type byte");
    for(std::size_t cut=0;cut<=wire.size();++cut){
      FrameDecoder decoder;auto first=take(decoder.consume(View(wire).first(cut)));
      require(first.consumed==cut,"Incremental decoder overconsumed prefix");
      if(cut<wire.size()){require(!first.frame,"Incomplete I2CP frame was emitted");}
      auto second=take(decoder.consume(View(wire).subspan(cut)));require(second.consumed==wire.size()-cut,"Incremental decoder lost suffix");
      const auto& actual=first.frame?first.frame:second.frame;
      require(actual&&*actual==frame&&decoder.bufferedBytes()==0&&decoder.finish().has_value(),"Split I2CP frame was not delivered exactly");
    }
    for(std::size_t cut=1;cut<wire.size();++cut){
      FrameDecoder decoder;require(decoder.consume(View(wire).first(cut)).has_value(),"Valid partial frame refused prematurely");
      auto ended=decoder.finish();require(!ended&&ended.error().code==ErrorCode::Truncated&&decoder.failed(),"Partial EOF did not poison stream");
      require(!decoder.consume(wire),"Poisoned EOF decoder silently resumed");
    }
  }
  for(Byte state=0;state<=4;++state){
    const Message message=SessionStatus{0,static_cast<SessionState>(state)};
    require(take(decodeMessage(take(decodeFrame(take(encodeMessage(message))))))==message,"Original session status value changed");
  }
}
void boundedFramingAndOwnership(){
  Bytes payload(MaximumPayloadBytes);for(std::size_t i=0;i<payload.size();++i){payload[i]=static_cast<Byte>(i*17);}
  const auto wire=take(encodeFrame(255,payload));require(wire.size()==MaximumFrameBytes,"Original MAX_LENGTH boundary changed");
  FrameDecoder decoder;std::optional<Frame> emitted;
  for(std::size_t offset=0;offset<wire.size();){
    const auto amount=std::min<std::size_t>(701,wire.size()-offset);auto step=take(decoder.consume(View(wire).subspan(offset,amount)));
    require(step.consumed==amount&&decoder.bufferedBytes()<=MaximumFrameBytes,"Framing exceeded consumed/storage bound");offset+=step.consumed;
    if(step.frame){require(!emitted,"Frame was emitted twice");emitted=std::move(step.frame);}
  }
  require(emitted&&emitted->payload==payload&&emitted->type==255,"Maximal opaque frame data changed");
  auto unsupported=decodeMessage(*emitted);require(!unsupported&&unsupported.error().code==ErrorCode::Unsupported,"Opaque unknown frame gained typed authority");
  require(!encodeFrame(1,Bytes(MaximumPayloadBytes+1)),"Oversized outbound frame accepted");
  for(const auto& header:{Bytes{0,2,0,1,32},Bytes{255,255,255,255,32}}){
    auto bad=decoder.consume(header);require(!bad&&bad.error().code==ErrorCode::TooLarge&&decoder.failed()&&decoder.bufferedBytes()==5,"Unsigned oversized header did not fail before body allocation");
    require(!decoder.consume(wire),"Poisoned decoder reused oversized stream");decoder.reset();
  }
  auto owned=take(encodeMessage(Disconnect{"owned"}));const auto expected=take(decodeFrame(owned));
  auto partial=take(decoder.consume(View(owned).first(7)));require(!partial.frame,"Partial owned body emitted");
  std::fill(owned.begin(),owned.begin()+7,0);auto end=take(decoder.consume(View(owned).subspan(7)));
  require(end.frame&&*end.frame==expected,"Framing retained borrowed partial input");
  std::fill(owned.begin(),owned.end(),0);require(*end.frame==expected,"Delivered frame borrowed source bytes");
  const auto a=take(encodeMessage(GetDate{std::nullopt})),b=take(encodeMessage(DestroySession{7}));Bytes joined(a.size()+b.size());
  std::copy(a.begin(),a.end(),joined.begin());std::copy(b.begin(),b.end(),joined.begin()+a.size());
  auto one=take(decoder.consume(joined));require(one.consumed==a.size()&&one.frame&&one.frame->type==GetDateType,"Decoder swallowed concatenated-frame suffix");
  auto two=take(decoder.consume(View(joined).subspan(one.consumed)));require(two.consumed==b.size()&&two.frame&&two.frame->type==DestroySessionType,"Decoder lost concatenated second frame");
  require(decoder.finish().has_value(),"Clean frame boundary EOF refused");
}
void explicitRefusals(){
  require(!encodeMessage(GetDate{std::string(256,'A')}),"Original string maximum bypassed");
  auto nonAscii=encodeMessage(Disconnect{std::string(1,static_cast<char>(0x80))});require(!nonAscii&&nonAscii.error().code==ErrorCode::Unsupported,"Lossy Java non-ASCII conversion silently guessed");
  require(!decodeMessage(Frame{DisconnectType,{1,128}}),"Inbound non-ASCII string silently decoded differently from Java");
  require(!decodeMessage(Frame{DisconnectType,{2,'a'}})&&!decodeMessage(Frame{DisconnectType,{0,0}}),"String truncation/trailer accepted");
  auto options=decodeMessage(Frame{GetDateType,{1,'v',0,0}});require(!options&&options.error().code==ErrorCode::Unsupported,"GetDate options silently ignored");
  require(!decodeMessage(Frame{SetDateType,Bytes(7)})&&!decodeMessage(Frame{DestroySessionType,Bytes(1)}),"Fixed message truncation accepted");
  require(!decodeMessage(Frame{DestroySessionType,Bytes(3)})&&!decodeMessage(Frame{SessionStatusType,Bytes(4)}),"Fixed message trailer accepted");
  auto internal=decodeMessage(Frame{SessionStatusType,{0,1,5}});require(!internal&&internal.error().code==ErrorCode::Unsupported,"Internal DUP_DEST status treated as supported wire acknowledgement");
  require(!encodeMessage(SessionStatus{1,static_cast<SessionState>(255)}),"Unknown outbound status accepted");
  require(!encodeMessage(SetDate{std::uint64_t{1}<<63,std::nullopt}),"Unsigned timestamp wrapped into negative Java date");
  Bytes negative(8);negative[0]=128;require(!decodeMessage(Frame{SetDateType,negative}),"Negative Java timestamp accepted as valid clock sample");
  for(const Byte type:{Byte{1},Byte{5},Byte{19},Byte{0},Byte{255}}){auto value=decodeMessage(Frame{type,{}});require(!value&&value.error().code==ErrorCode::Unsupported,"Unsupported session/data message granted typed dispatch");}
  const auto wire=take(encodeMessage(GetDate{std::nullopt}));
  for(std::size_t size=0;size<wire.size();++size){require(!decodeFrame(View(wire).first(size)),"Truncated exact frame accepted");}
  auto extra=wire;extra.resize(extra.size()+1);require(!decodeFrame(extra),"Exact frame silently consumed trailing packet");
}
class Descriptor {
public:explicit Descriptor(int value):value_(value){}~Descriptor(){if(value_>=0){::close(value_);}}
  Descriptor(const Descriptor&)=delete;Descriptor& operator=(const Descriptor&)=delete;
  int get()const noexcept{return value_;}
private:int value_;
};
Bytes readOwned(int dir,const std::string& name){
  Descriptor input(::openat(dir,name.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));struct stat info{};
  require(input.get()>=0&&::fstat(input.get(),&info)==0&&S_ISREG(info.st_mode)&&info.st_uid==::getuid()&&info.st_nlink==1&&info.st_size>=0&&info.st_size<=static_cast<off_t>(MaximumFrameBytes),"Unsafe/oversized owned Java I2CP fixture");
  Bytes data(static_cast<std::size_t>(info.st_size));std::size_t offset=0;
  while(offset<data.size()){const auto n=::read(input.get(),data.data()+offset,data.size()-offset);if(n<0&&errno==EINTR){continue;}require(n>0,"Short owned I2CP fixture read");offset+=static_cast<std::size_t>(n);}
  Byte extra=0;require(::read(input.get(),&extra,1)==0,"Owned I2CP fixture grew during read");return data;
}
void writeOwned(int dir,const std::string& name,View bytes){
  Descriptor output(::openat(dir,name.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600));require(output.get()>=0,"Cannot exclusively create owned native I2CP fixture");std::size_t offset=0;
  while(offset<bytes.size()){const auto n=::write(output.get(),bytes.data()+offset,bytes.size()-offset);if(n<0&&errno==EINTR){continue;}require(n>0,"Short owned I2CP fixture write");offset+=static_cast<std::size_t>(n);}
  require(::fsync(output.get())==0,"Cannot sync owned I2CP fixture");
}
void originalInterop(const std::filesystem::path& path){
  require(path.is_absolute(),"Offline I2CP fixture path must be absolute");Descriptor dir(::open(path.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));struct stat info{};
  require(dir.get()>=0&&::fstat(dir.get(),&info)==0&&info.st_uid==::getuid()&&(info.st_mode&0777)==0700,"Offline I2CP fixture directory is not private/owned");
  require(readOwned(dir.get(),"greeting.java")==Bytes(ClientGreeting.begin(),ClientGreeting.end()),"Actual original client greeting differs");
  for(const auto& item:cases()){
    const auto java=readOwned(dir.get(),item.name+".java");const auto original=take(decodeMessage(take(decodeFrame(java))));
    require(original==item.message,"Actual original I2CP field decoding differs");const auto native=take(encodeMessage(item.message));
    require(native==java,"Native I2CP serialization differs from actual original");writeOwned(dir.get(),item.name+".native",native);
    std::cout<<"PASS actual original/native I2CP "<<item.name<<'\n';
  }
  auto options=decodeMessage(take(decodeFrame(readOwned(dir.get(),"unsupported-auth.java"))));
  require(!options&&options.error().code==ErrorCode::Unsupported,"Actual original authentication options were silently dropped");
  require(::fsync(dir.get())==0,"Cannot sync owned I2CP fixture directory");
}
} // namespace
int main(int argc,char** argv){try{
  require(argc==1||(argc==3&&std::string(argv[1])=="--interop-dir"),"Usage: biglybtc-i2cp-protocol-tests [--interop-dir OWNED_JAVA_FIXTURES]");
  knownWireAndTypedRoundtrips();boundedFramingAndOwnership();explicitRefusals();if(argc==3){originalInterop(argv[2]);}
  std::cout<<"PASS bounded I2CP framing/initial session subset; no connection\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
