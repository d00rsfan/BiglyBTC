// SPDX-License-Identifier: GPL-2.0-or-later
// I2P9134 SessionConfig/DataHelper/I2CPMessageProducer and Create/Reconfigure.
#include "session.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <string_view>
#include <type_traits>

namespace biglybtc::services::i2p::i2cp {
namespace {
using Code=::biglybtc::services::i2p::ErrorCode;
std::unexpected<::biglybtc::services::i2p::Error> error(Code code,const char* text){return std::unexpected(::biglybtc::services::i2p::Error{code,text});}
bool unsupported(std::string_view key){
  return key=="i2cp.username"||key=="i2cp.password"||key=="i2cp.leaseSetOfflineExpiration"||
    key=="i2cp.leaseSetTransientPublicKey"||key=="i2cp.leaseSetOfflineSignature";
}
ConfigResult<std::u16string> utf16(std::string_view bytes){
  std::u16string out;out.reserve(bytes.size());
  for(std::size_t i=0;i<bytes.size();){
    const auto first=static_cast<unsigned char>(bytes[i++]);std::uint32_t cp=first,minimum=0;unsigned count=0;
    if(first<0x80){}
    else if(first>=0xc2&&first<=0xdf){cp=first&31;count=1;minimum=0x80;}
    else if(first>=0xe0&&first<=0xef){cp=first&15;count=2;minimum=0x800;}
    else if(first>=0xf0&&first<=0xf4){cp=first&7;count=3;minimum=0x10000;}
    else{return error(Code::Unsupported,"Invalid UTF-8 session option");}
    if(bytes.size()-i<count){return error(Code::Unsupported,"Truncated UTF-8 session option");}
    while(count--){const auto next=static_cast<unsigned char>(bytes[i++]);if((next&0xc0)!=0x80){return error(Code::Unsupported,"Invalid UTF-8 session option continuation");}cp=(cp<<6)|(next&63);}
    if(cp<minimum||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)){return error(Code::Unsupported,"Non-scalar/overlong UTF-8 session option");}
    if(cp<0x10000){out.push_back(static_cast<char16_t>(cp));}
    else{cp-=0x10000;out.push_back(static_cast<char16_t>(0xd800+(cp>>10)));out.push_back(static_cast<char16_t>(0xdc00+(cp&1023)));}
  }
  return out;
}
std::uint16_t u16(View bytes,std::size_t offset){return static_cast<std::uint16_t>((std::uint16_t(bytes[offset])<<8)|bytes[offset+1]);}
void put16(Bytes& bytes,std::size_t value){bytes.push_back(static_cast<Byte>(value>>8));bytes.push_back(static_cast<Byte>(value));}
bool dateValid(std::uint64_t date){return date&&date<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());}
ConfigResult<Destination> actualDestination(const SessionConfig& cfg){
  auto decoded=decodeDestination(cfg.destination.wire);if(!decoded){return std::unexpected(decoded.error());}
  if(decoded->encryptionType!=0){return error(Code::Unsupported,"Original online I2CP requires EncType0");}return decoded;
}
ConfigResult<Bytes> properties(const SessionOptions& options){
  auto ordered=canonicalSessionOptions(options);if(!ordered){return std::unexpected(ordered.error());}
  std::size_t size=0;for(const auto& [key,value]:*ordered){size+=4+key.size()+value.size();}
  Bytes out;out.reserve(size+2);put16(out,size);
  for(const auto& [key,value]:*ordered){
    out.push_back(static_cast<Byte>(key.size()));out.insert(out.end(),key.begin(),key.end());out.push_back('=');
    out.push_back(static_cast<Byte>(value.size()));out.insert(out.end(),value.begin(),value.end());out.push_back(';');
  }return out;
}
ConfigResult<Bytes> frame(Byte type,View body){
  auto encoded=encodeFrame(type,body);if(!encoded){return error(Code::TooLarge,"Session message exceeds I2CP frame bound");}return std::move(*encoded);
}
} // namespace
ConfigResult<SessionOptions> canonicalSessionOptions(const SessionOptions& input){
  if(input.size()>MaximumPropertiesBytes/4){return error(Code::TooLarge,"Too many session options");}
  struct Entry {std::u16string units;std::pair<std::string,std::string> value;};
  std::vector<Entry> sorted;sorted.reserve(input.size());std::size_t size=0;
  for(const auto& entry:input){
    const auto& [key,value]=entry;if(unsupported(key)){return error(Code::Unsupported,"Offline/authentication session options are unsupported");}
    if(key.size()>255||value.size()>255){return error(Code::TooLarge,"Session option exceeds255 UTF-8 bytes");}
    if(size>MaximumPropertiesBytes-(4+key.size()+value.size())){return error(Code::TooLarge,"Session properties exceed65535 bytes");}
    size+=4+key.size()+value.size();auto units=utf16(key);if(!units){return std::unexpected(units.error());}
    auto checked=utf16(value);if(!checked){return std::unexpected(checked.error());}sorted.push_back({std::move(*units),entry});
  }
  std::sort(sorted.begin(),sorted.end(),[](const Entry& a,const Entry& b){return a.units<b.units;});
  SessionOptions out;out.reserve(sorted.size());
  for(std::size_t i=0;i<sorted.size();++i){if(i&&sorted[i-1].units==sorted[i].units){return error(Code::InvalidFormat,"Duplicate session option");}out.push_back(std::move(sorted[i].value));}
  return out;
}
ConfigResult<SessionOptions> projectRouterOptions(const SessionOptions& input){
  if(input.size()>MaximumPropertiesBytes/4){return error(Code::TooLarge,"Too many client options");}
  constexpr std::array<std::string_view,13> clientOnly{
    "i2cp.closeIdleTime","i2cp.closeOnIdle","i2cp.encryptLeaseSet","i2cp.gzip","i2cp.leaseSetKey","i2cp.leaseSetPrivateKey",
    "i2cp.leaseSetSigningPrivateKey","i2cp.reduceIdleTime","i2cp.reduceOnIdle","i2cp.SSL","i2cp.tcp.host","i2cp.tcp.port","i2p.reseedURL"};
  SessionOptions out;std::size_t encoded=0;
  for(const auto& [key,value]:input){
    if(std::find(clientOnly.begin(),clientOnly.end(),key)!=clientOnly.end()){continue;}
    if(unsupported(key)){return error(Code::Unsupported,"Offline/authentication session options are unsupported");}
    // No valid UTF-8 string over1020 bytes can fit255 Java UTF-16 units.
    if(key.size()>1020||value.size()>1020){continue;}
    auto k=utf16(key),v=utf16(value);if(!k){return std::unexpected(k.error());}if(!v){return std::unexpected(v.error());}
    if(k->size()>255||v->size()>255){continue;}
    if(key.size()>255||value.size()>255){return error(Code::TooLarge,"Session option exceeds255 UTF-8 bytes");}
    if(encoded>MaximumPropertiesBytes-(4+key.size()+value.size())){return error(Code::TooLarge,"Session properties exceed65535 bytes");}
    encoded+=4+key.size()+value.size();out.emplace_back(key,value);
  }
  return canonicalSessionOptions(out);
}
ConfigResult<SessionOptions> mergeSessionOptions(const SessionOptions& current,const SessionOptions& update){
  auto base=canonicalSessionOptions(current),patch=canonicalSessionOptions(update);
  if(!base){return std::unexpected(base.error());}if(!patch){return std::unexpected(patch.error());}
  std::map<std::string,std::string> merged;
  for(auto& [key,value]:*base){merged.emplace(std::move(key),std::move(value));}
  for(auto& [key,value]:*patch){merged.insert_or_assign(std::move(key),std::move(value));}
  SessionOptions out;out.reserve(merged.size());for(auto& [key,value]:merged){out.emplace_back(key,std::move(value));}
  return canonicalSessionOptions(out);
}
ConfigResult<Bytes> sessionConfigPreimage(const SessionConfig& cfg){
  if(!dateValid(cfg.creationMilliseconds)){return error(Code::InvalidFormat,"Session creation date is null/negative/wrapped");}
  auto dest=actualDestination(cfg);if(!dest){return std::unexpected(dest.error());}auto props=properties(cfg.options);if(!props){return std::unexpected(props.error());}
  Bytes out=std::move(dest->wire);out.insert(out.end(),props->begin(),props->end());
  for(int shift=56;shift>=0;shift-=8){out.push_back(static_cast<Byte>(cfg.creationMilliseconds>>shift));}return out;
}
ConfigResult<SessionConfig> signSessionConfig(const ClientIdentity& identity,const SessionOptions& options,std::uint64_t creation){
  if(!identity.valid()||!identity.destination()){return error(Code::KeyMismatch,"No live online client identity");}
  auto ordered=canonicalSessionOptions(options);if(!ordered){return std::unexpected(ordered.error());}
  SessionConfig cfg{*identity.destination(),std::move(*ordered),creation,{}};
  auto data=sessionConfigPreimage(cfg);if(!data){return std::unexpected(data.error());}
  auto signature=signData(cfg.destination.signingType,identity.signingPrivateKey(),*data);if(!signature){return std::unexpected(signature.error());}
  cfg.signature=std::move(*signature);auto checked=verifySessionConfigSignature(cfg);if(!checked){return std::unexpected(checked.error());}return cfg;
}
ConfigResult<void> verifySessionConfigSignature(const SessionConfig& cfg){
  auto dest=actualDestination(cfg);if(!dest){return std::unexpected(dest.error());}auto data=sessionConfigPreimage(cfg);if(!data){return std::unexpected(data.error());}
  return verifyData(dest->signingType,dest->signingPublicKey,cfg.signature,*data);
}
ConfigResult<void> verifySessionConfig(const SessionConfig& cfg,std::uint64_t now){
  if(!dateValid(now)||!dateValid(cfg.creationMilliseconds)){return error(Code::InvalidFormat,"Invalid explicit session clock/date");}
  if(now>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())-SessionConfigDateTolerance){return error(Code::InvalidFormat,"Original session clock window overflows signed date");}
  const auto delta=now>cfg.creationMilliseconds?now-cfg.creationMilliseconds:cfg.creationMilliseconds-now;
  if(delta>SessionConfigDateTolerance){return error(Code::InvalidFormat,"Session date is outside original three-minute window");}return verifySessionConfigSignature(cfg);
}
ConfigResult<Bytes> encodeSessionConfig(const SessionConfig& cfg){
  auto out=sessionConfigPreimage(cfg);if(!out){return std::unexpected(out.error());}auto dest=actualDestination(cfg);if(!dest){return std::unexpected(dest.error());}
  const auto expected=dest->signingType==SigningType::DsaSha1?40U:64U;if(cfg.signature.size()!=expected){return error(Code::InvalidFormat,"Session signature width changed");}
  out->insert(out->end(),cfg.signature.begin(),cfg.signature.end());return out;
}
ConfigResult<SessionConfig> decodeSessionConfig(View wire){
  if(wire.size()>MaximumSessionConfigBytes){return error(Code::TooLarge,"Session configuration exceeds bound");}
  if(wire.size()<387){return error(Code::InvalidFormat,"Truncated session Destination");}
  const auto dsize=387+std::size_t(u16(wire,385));if(dsize>wire.size()){return error(Code::InvalidFormat,"Truncated session Destination certificate");}
  auto dest=decodeDestination(wire.first(dsize));if(!dest){return std::unexpected(dest.error());}if(dest->encryptionType!=0){return error(Code::Unsupported,"Original online I2CP requires EncType0");}
  std::size_t pos=dsize;if(wire.size()-pos<2){return error(Code::InvalidFormat,"Truncated session properties length");}
  const auto size=u16(wire,pos);pos+=2;if(wire.size()-pos<size){return error(Code::InvalidFormat,"Truncated session properties");}const auto end=pos+size;
  SessionOptions options;
  auto string=[&]()->ConfigResult<std::string>{if(pos>=end){return error(Code::InvalidFormat,"Truncated session property string");}const auto length=wire[pos++];if(end-pos<length){return error(Code::InvalidFormat,"Truncated session property bytes");}std::string text(reinterpret_cast<const char*>(wire.data()+pos),length);pos+=length;return text;};
  while(pos<end){auto key=string();if(!key){return std::unexpected(key.error());}if(pos>=end||wire[pos++]!='='){return error(Code::InvalidFormat,"Bad session property key delimiter");}auto value=string();if(!value){return std::unexpected(value.error());}if(pos>=end||wire[pos++]!=';'){return error(Code::InvalidFormat,"Bad session property value delimiter");}options.emplace_back(std::move(*key),std::move(*value));}
  auto ordered=canonicalSessionOptions(options);if(!ordered){return std::unexpected(ordered.error());}
  const auto siglen=dest->signingType==SigningType::DsaSha1?40U:64U;if(wire.size()-pos!=8+siglen){return error(Code::InvalidFormat,"Session date/signature/trailing size changed");}
  std::uint64_t date=0;for(unsigned i=0;i<8;++i){date=(date<<8)|wire[pos++];}if(!dateValid(date)){return error(Code::InvalidFormat,"Session creation date is null/negative/wrapped");}
  return SessionConfig{std::move(*dest),std::move(*ordered),date,Bytes(wire.begin()+static_cast<std::ptrdiff_t>(pos),wire.end())};
}
ConfigResult<Bytes> encodeSessionMessage(const SessionMessage& message){
  return std::visit([](const auto& value)->ConfigResult<Bytes>{
    auto bytes=encodeSessionConfig(value.config);if(!bytes){return std::unexpected(bytes.error());}
    if constexpr(std::is_same_v<std::decay_t<decltype(value)>,CreateSession>){return frame(CreateSessionType,*bytes);}
    else{Bytes body;body.reserve(bytes->size()+2);put16(body,value.sessionId);body.insert(body.end(),bytes->begin(),bytes->end());return frame(ReconfigureSessionType,body);}
  },message);
}
ConfigResult<SessionMessage> decodeSessionMessage(const Frame& message){
  if(message.type==CreateSessionType){auto cfg=decodeSessionConfig(message.payload);if(!cfg){return std::unexpected(cfg.error());}return SessionMessage{CreateSession{std::move(*cfg)}};}
  if(message.type==ReconfigureSessionType){if(message.payload.size()<2){return error(Code::InvalidFormat,"Truncated reconfigure session ID");}auto cfg=decodeSessionConfig(View(message.payload).subspan(2));if(!cfg){return std::unexpected(cfg.error());}return SessionMessage{ReconfigureSession{u16(message.payload,0),std::move(*cfg)}};}
  return error(Code::Unsupported,"Not a CreateSession/ReconfigureSession message");
}
} // namespace biglybtc::services::i2p::i2cp
