// SPDX-License-Identifier: GPL-2.0-or-later
// Original I2P9134: I2CPMessageImpl/Handler, DataHelper and five message classes.
#include "protocol.hpp"
#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

namespace biglybtc::services::i2p::i2cp {
namespace {
std::unexpected<Error> error(ErrorCode code,const char* message){return std::unexpected(Error{code,message});}
std::uint32_t length(View input){return (std::uint32_t(input[0])<<24)|(std::uint32_t(input[1])<<16)|(std::uint32_t(input[2])<<8)|input[3];}
std::uint16_t id(View input){return static_cast<std::uint16_t>((std::uint16_t(input[0])<<8)|input[1]);}
void writeId(Bytes& output,std::uint16_t value){output[0]=static_cast<Byte>(value>>8);output[1]=static_cast<Byte>(value);}
Result<void> checkString(const std::string& value){
  if(value.size()>255){return error(ErrorCode::TooLarge,"I2CP string exceeds original one-byte length");}
  for(const unsigned char byte:value){if(byte>127){return error(ErrorCode::Unsupported,"Non-ASCII original I2CP string conversion is unsupported");}}
  return {};
}
void writeString(Bytes& output,std::size_t offset,const std::string& value){
  output[offset]=static_cast<Byte>(value.size());std::copy(value.begin(),value.end(),output.begin()+offset+1);
}
struct ParsedString {std::string value;std::size_t end;};
Result<ParsedString> readString(View input,std::size_t offset){
  if(offset>=input.size()){return error(ErrorCode::Truncated,"I2CP string length is missing");}
  const auto count=std::size_t(input[offset]);const auto begin=offset+1;
  if(count>input.size()-begin){return error(ErrorCode::Truncated,"I2CP string bytes are truncated");}
  std::string value(count,'\0');
  for(std::size_t i=0;i<count;++i){
    if(input[begin+i]>127){return error(ErrorCode::Unsupported,"Non-ASCII original I2CP string conversion is unsupported");}
    value[i]=static_cast<char>(input[begin+i]);
  }
  return ParsedString{std::move(value),begin+count};
}
Result<void> exactSize(View input,std::size_t expected){
  if(input.size()!=expected){return error(input.size()<expected?ErrorCode::Truncated:ErrorCode::InvalidFormat,"I2CP fixed message payload size is invalid");}
  return {};
}
} // namespace

Result<Bytes> encodeFrame(Byte type,View payload){
  if(payload.size()>MaximumPayloadBytes){return error(ErrorCode::TooLarge,"I2CP payload exceeds original frame limit");}
  Bytes output(FrameHeaderBytes+payload.size());const auto size=static_cast<std::uint32_t>(payload.size());
  output[0]=static_cast<Byte>(size>>24);output[1]=static_cast<Byte>(size>>16);
  output[2]=static_cast<Byte>(size>>8);output[3]=static_cast<Byte>(size);output[4]=type;
  std::copy(payload.begin(),payload.end(),output.begin()+FrameHeaderBytes);return output;
}
Result<Frame> decodeFrame(View input){
  if(input.size()<FrameHeaderBytes){return error(ErrorCode::Truncated,"I2CP frame header is truncated");}
  const auto size=std::size_t(length(input));
  if(size>MaximumPayloadBytes){return error(ErrorCode::TooLarge,"I2CP payload exceeds original frame limit");}
  if(input.size()!=FrameHeaderBytes+size){return error(input.size()<FrameHeaderBytes+size?ErrorCode::Truncated:ErrorCode::InvalidFormat,"I2CP frame length does not match exact input");}
  Bytes payload(size);std::copy(input.begin()+FrameHeaderBytes,input.end(),payload.begin());return Frame{input[4],std::move(payload)};
}
Result<FrameStep> FrameDecoder::consume(View input){
  if(failed_){return error(ErrorCode::Failed,"I2CP frame decoder requires explicit stream reset");}
  FrameStep step;
  if(headerUsed_<FrameHeaderBytes){
    const auto count=std::min(FrameHeaderBytes-headerUsed_,input.size());
    std::copy_n(input.begin(),count,header_.begin()+headerUsed_);headerUsed_+=count;step.consumed+=count;input=input.subspan(count);
    if(headerUsed_<FrameHeaderBytes){return step;}
  }
  if(!lengthKnown_){
    payloadLength_=length(header_);
    if(payloadLength_>MaximumPayloadBytes){failed_=true;return error(ErrorCode::TooLarge,"I2CP payload exceeds original frame limit");}
    // Header bytes are already consumed: a failed allocation must retire this
    // decoder, so callers cannot retry with an ambiguous remaining-input offset.
    try{payload_.resize(payloadLength_);}catch(...){failed_=true;throw;}
    lengthKnown_=true;
  }
  const auto count=std::min(payloadLength_-payloadUsed_,input.size());
  std::copy_n(input.begin(),count,payload_.begin()+payloadUsed_);payloadUsed_+=count;step.consumed+=count;
  if(payloadUsed_==payloadLength_){step.frame=Frame{header_[4],std::move(payload_)};reset();}
  return step;
}
Result<void> FrameDecoder::finish(){
  if(failed_){return error(ErrorCode::Failed,"I2CP frame decoder requires explicit stream reset");}
  if(headerUsed_!=0){failed_=true;return error(ErrorCode::Truncated,"I2CP stream ended inside a frame");}
  return {};
}
void FrameDecoder::reset() noexcept{header_.fill(0);headerUsed_=0;payloadLength_=0;payloadUsed_=0;lengthKnown_=false;failed_=false;payload_.clear();}

Result<Bytes> encodeMessage(const Message& message){
  return std::visit([](const auto& value)->Result<Bytes>{
    using T=std::decay_t<decltype(value)>;
    if constexpr(std::is_same_v<T,GetDate>){
      if(!value.version){return encodeFrame(GetDateType,{});}
      auto checked=checkString(*value.version);if(!checked){return std::unexpected(checked.error());}
      Bytes body(1+value.version->size());writeString(body,0,*value.version);return encodeFrame(GetDateType,body);
    }else if constexpr(std::is_same_v<T,SetDate>){
      if(value.milliseconds>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())){return error(ErrorCode::Unsupported,"Negative/wrapped Java I2CP date is unsupported");}
      if(value.version){auto checked=checkString(*value.version);if(!checked){return std::unexpected(checked.error());}}
      Bytes body(8+(value.version?1+value.version->size():0));
      for(std::size_t i=0;i<8;++i){body[i]=static_cast<Byte>(value.milliseconds>>((7-i)*8));}
      if(value.version){writeString(body,8,*value.version);}return encodeFrame(SetDateType,body);
    }else if constexpr(std::is_same_v<T,Disconnect>){
      auto checked=checkString(value.reason);if(!checked){return std::unexpected(checked.error());}
      Bytes body(1+value.reason.size());writeString(body,0,value.reason);return encodeFrame(DisconnectType,body);
    }else if constexpr(std::is_same_v<T,SessionStatus>){
      if(static_cast<Byte>(value.state)>4){return error(ErrorCode::Unsupported,"Internal/unknown I2CP session status is unsupported");}
      Bytes body(3);writeId(body,value.sessionId);body[2]=static_cast<Byte>(value.state);return encodeFrame(SessionStatusType,body);
    }else{
      Bytes body(2);writeId(body,value.sessionId);return encodeFrame(DestroySessionType,body);
    }
  },message);
}
Result<Message> decodeMessage(const Frame& frame){
  if(frame.payload.size()>MaximumPayloadBytes){return error(ErrorCode::TooLarge,"I2CP payload exceeds original frame limit");}
  const View body(frame.payload);
  switch(frame.type){
  case GetDateType:{
    if(body.empty()){return Message{GetDate{std::nullopt}};}
    auto version=readString(body,0);if(!version){return std::unexpected(version.error());}
    if(version->end!=body.size()){return error(ErrorCode::Unsupported,"I2CP GetDate authentication/options are unsupported");}
    return Message{GetDate{std::move(version->value)}};
  }
  case SetDateType:{
    if(body.size()<8){return error(ErrorCode::Truncated,"I2CP date bytes are truncated");}
    std::uint64_t milliseconds=0;for(std::size_t i=0;i<8;++i){milliseconds=(milliseconds<<8)|body[i];}
    if(milliseconds>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())){return error(ErrorCode::Unsupported,"Negative/wrapped Java I2CP date is unsupported");}
    if(body.size()==8){return Message{SetDate{milliseconds,std::nullopt}};}
    auto version=readString(body,8);if(!version){return std::unexpected(version.error());}
    if(version->end!=body.size()){return error(ErrorCode::InvalidFormat,"I2CP SetDate payload has trailing bytes");}
    return Message{SetDate{milliseconds,std::move(version->value)}};
  }
  case DisconnectType:{
    auto reason=readString(body,0);if(!reason){return std::unexpected(reason.error());}
    if(reason->end!=body.size()){return error(ErrorCode::InvalidFormat,"I2CP Disconnect payload has trailing bytes");}
    return Message{Disconnect{std::move(reason->value)}};
  }
  case SessionStatusType:{
    auto checked=exactSize(body,3);if(!checked){return std::unexpected(checked.error());}
    if(body[2]>4){return error(ErrorCode::Unsupported,"Internal/unknown I2CP session status is unsupported");}
    return Message{SessionStatus{id(body),static_cast<SessionState>(body[2])}};
  }
  case DestroySessionType:{
    auto checked=exactSize(body,2);if(!checked){return std::unexpected(checked.error());}
    return Message{DestroySession{id(body)}};
  }
  default:return error(ErrorCode::Unsupported,"I2CP message type is outside initial session subset");
  }
}
} // namespace biglybtc::services::i2p::i2cp
