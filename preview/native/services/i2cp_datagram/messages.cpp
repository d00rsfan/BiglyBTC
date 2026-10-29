// SPDX-License-Identifier: GPL-2.0-or-later
#include "messages.hpp"
#include <zlib.h>
#include <algorithm>
#include <array>
#include <limits>
namespace biglybtc::services::i2p::i2cp {
namespace {
using Code=::biglybtc::services::i2p::ErrorCode;
auto error(Code c,const char* why){return std::unexpected(::biglybtc::services::i2p::Error{c,why});}
void put(Bytes& out,std::uint64_t value,unsigned n){for(unsigned i=n;i>0;--i){out.push_back(static_cast<Byte>(value>>((i-1)*8)));}}
struct Reader {View bytes;std::size_t at=0;bool take(std::size_t n,View& out){if(n>bytes.size()-at){return false;}out=bytes.subspan(at,n);at+=n;return true;}bool number(unsigned n,std::uint64_t& out){View b;if(!take(n,b)){return false;}out=0;for(auto v:b){out=(out<<8)|v;}return true;}};
ConfigResult<Bytes> frame(Byte type,View bytes){if(bytes.size()>MaximumPayloadBytes){return error(Code::TooLarge,"I2CP datagram frame exceeds bound");}Bytes out;out.reserve(5+bytes.size());put(out,bytes.size(),4);out.push_back(type);out.insert(out.end(),bytes.begin(),bytes.end());return out;}
ConfigResult<void> payloadValid(std::uint16_t id,View bytes){if(id==65535){return error(Code::InvalidFormat,"Unknown session cannot submit payload");}if(bytes.size()>MaximumI2cpPayloadBytes){return error(Code::TooLarge,"I2CP Payload exceeds64KiB");}return {};}
}
bool originalStatusRepliesEnabled(const SessionOptions& options)noexcept{
  for(const auto& [key,value]:options){if(key=="i2cp.messageReliability"&&value.size()==4&&(value[0]=='n'||value[0]=='N')&&(value[1]=='o'||value[1]=='O')&&(value[2]=='n'||value[2]=='N')&&(value[3]=='e'||value[3]=='E')){return false;}}return true;
}
ConfigResult<Bytes> encodeMuxDatagram(const MuxDatagram& data,GzipMode mode){
  if(data.protocol!=Datagram2Protocol&&data.protocol!=Datagram3Protocol){return error(Code::Unsupported,"Only explicit Datagram2/3 mux protocols supported");}
  if(data.datagram.size()>MaximumMuxDatagramBytes){return error(Code::TooLarge,"Original gzip input exceeds40KiB");}
  if(mode!=GzipMode::Stored&&mode!=GzipMode::Deflate){return error(Code::Unsupported,"Unknown gzip mode");}
  z_stream stream{};if(deflateInit2(&stream,mode==GzipMode::Stored?0:3,Z_DEFLATED,31,8,Z_DEFAULT_STRATEGY)!=Z_OK){return error(Code::CryptoFailure,"Gzip encoder resources unavailable");}
  struct End {z_stream* s;~End(){deflateEnd(s);}}end{&stream};Bytes out(MaximumI2cpPayloadBytes);
  stream.next_in=const_cast<Byte*>(data.datagram.data());stream.avail_in=static_cast<uInt>(data.datagram.size());stream.next_out=out.data();stream.avail_out=static_cast<uInt>(out.size());
  if(deflate(&stream,Z_FINISH)!=Z_STREAM_END||stream.avail_in||stream.total_out>out.size()||stream.total_out<18){return error(Code::TooLarge,"Gzip output exceeds finite Payload bound");}out.resize(stream.total_out);
  // Original ResettableGZIPOutputStream always uses XFL2, even level0/3;
  // mux overwrites MTIME/OS. CRC/ISIZE cover the data, not these header fields.
  out[4]=static_cast<Byte>(data.fromPort>>8);out[5]=static_cast<Byte>(data.fromPort);out[6]=static_cast<Byte>(data.toPort>>8);out[7]=static_cast<Byte>(data.toPort);out[8]=2;out[9]=data.protocol;return out;
}
ConfigResult<MuxDatagram> decodeMuxDatagram(View bytes){
  if(bytes.size()<18||bytes.size()>MaximumI2cpPayloadBytes){return error(Code::TooLarge,"Bounded mux gzip length refused");}
  if(bytes[0]!=0x1f||bytes[1]!=0x8b||bytes[2]!=8||bytes[3]!=0||(bytes[9]!=Datagram2Protocol&&bytes[9]!=Datagram3Protocol)){return error(Code::Unsupported,"Only fixed-header Datagram2/3 gzip supported");}
  z_stream stream{};if(inflateInit2(&stream,31)!=Z_OK){return error(Code::CryptoFailure,"Gzip decoder resources unavailable");}struct End {z_stream* s;~End(){inflateEnd(s);}}end{&stream};Bytes out(MaximumMuxDatagramBytes+1);
  stream.next_in=const_cast<Byte*>(bytes.data());stream.avail_in=static_cast<uInt>(bytes.size());stream.next_out=out.data();stream.avail_out=static_cast<uInt>(out.size());const auto status=inflate(&stream,Z_FINISH);
  if(status!=Z_STREAM_END||stream.avail_in||stream.total_out>MaximumMuxDatagramBytes){return error(Code::InvalidFormat,"Malformed/concatenated/oversize mux gzip or CRC/ISIZE mismatch");}out.resize(stream.total_out);
  return MuxDatagram{bytes[9],static_cast<std::uint16_t>((static_cast<unsigned>(bytes[4])<<8)|bytes[5]),static_cast<std::uint16_t>((static_cast<unsigned>(bytes[6])<<8)|bytes[7]),std::move(out)};
}
ConfigResult<Bytes> encodeSendDatagram(const SendDatagram& data){
  auto valid=payloadValid(data.sessionId,data.payload);if(!valid){return std::unexpected(valid.error());}auto dest=decodeDestination(data.recipient.wire);if(!dest){return std::unexpected(dest.error());}
  if(data.expirationMilliseconds&&(*data.expirationMilliseconds==0||*data.expirationMilliseconds>0xffffffffffffULL)){return error(Code::InvalidFormat,"Expires requires positive uint48 absolute milliseconds");}
  Bytes body;body.reserve(2+dest->wire.size()+4+data.payload.size()+4+8);put(body,data.sessionId,2);body.insert(body.end(),dest->wire.begin(),dest->wire.end());put(body,data.payload.size(),4);body.insert(body.end(),data.payload.begin(),data.payload.end());put(body,data.nonce,4);
  if(data.expirationMilliseconds){put(body,0,2);put(body,*data.expirationMilliseconds,6);}return frame(data.expirationMilliseconds?SendMessageExpiresType:SendMessageType,body);
}
ConfigResult<SendDatagram> decodeSendDatagram(const Frame& input){
  if(input.type!=SendMessageType&&input.type!=SendMessageExpiresType){return error(Code::Unsupported,"Expected Send5/Expires36");}Reader r{input.payload};std::uint64_t id=0,length=0,nonce=0;if(!r.number(2,id)||input.payload.size()-r.at<387){return error(Code::InvalidFormat,"Truncated Send destination");}
  const auto start=r.at;const auto size=387+(static_cast<std::size_t>(input.payload[start+385])<<8)+input.payload[start+386];View destBytes,payload;if(!r.take(size,destBytes)){return error(Code::InvalidFormat,"Truncated Send destination");}auto dest=decodeDestination(destBytes);if(!dest){return std::unexpected(dest.error());}
  if(!r.number(4,length)||length>MaximumI2cpPayloadBytes||!r.take(static_cast<std::size_t>(length),payload)||!r.number(4,nonce)){return error(Code::InvalidFormat,"Invalid Send Payload/nonce");}std::optional<std::uint64_t> expires;
  if(input.type==SendMessageExpiresType){std::uint64_t flags=0,date=0;if(!r.number(2,flags)||flags||!r.number(6,date)||!date){return error(Code::Unsupported,"Only default flags0/positive Expires date supported");}expires=date;}
  if(r.at!=r.bytes.size()){return error(Code::InvalidFormat,"Trailing Send bytes");}SendDatagram out{static_cast<std::uint16_t>(id),std::move(*dest),Bytes(payload.begin(),payload.end()),static_cast<std::uint32_t>(nonce),expires};auto valid=payloadValid(out.sessionId,out.payload);if(!valid){return std::unexpected(valid.error());}return out;
}
ConfigResult<StatusMeaning> messageStatusMeaning(Byte code){switch(code){case 0:return StatusMeaning::IncomingAvailable;case 1:return StatusMeaning::RouterAccepted;case 2:return StatusMeaning::UnusedLegacySuccess;case 3:case 5:return StatusMeaning::ProbableFailure;case 4:return StatusMeaning::ProbableSuccess;case 6:return StatusMeaning::LocalSuccess;default:if(code<=23){return StatusMeaning::Failure;}return error(Code::Unsupported,"Unknown original MessageStatus code");}}
ConfigResult<Bytes> encodeMessageStatus(const MessageStatus& data){auto meaning=messageStatusMeaning(data.code);if(!meaning){return std::unexpected(meaning.error());}if(data.sessionId==65535){return error(Code::InvalidFormat,"Unknown status session");}Bytes out;put(out,data.sessionId,2);put(out,data.messageId,4);out.push_back(data.code);put(out,data.size,4);put(out,data.nonce,4);return frame(MessageStatusType,out);}
ConfigResult<MessageStatus> decodeMessageStatus(const Frame& input){
  if(input.type!=MessageStatusType||input.payload.size()!=15){return error(Code::InvalidFormat,"MessageStatus22 must contain15 bytes");}Reader r{input.payload};std::uint64_t id=0,message=0,code=0,size=0,nonce=0;
  if(!r.number(2,id)||!r.number(4,message)||!r.number(1,code)||!r.number(4,size)||!r.number(4,nonce)||id==65535){return error(Code::InvalidFormat,"Malformed status");}auto meaning=messageStatusMeaning(static_cast<Byte>(code));if(!meaning){return std::unexpected(meaning.error());}return MessageStatus{static_cast<std::uint16_t>(id),static_cast<std::uint32_t>(message),static_cast<Byte>(code),static_cast<std::uint32_t>(size),static_cast<std::uint32_t>(nonce)};
}
ConfigResult<Bytes> encodeMessagePayload(const MessagePayload& data){auto valid=payloadValid(data.sessionId,data.payload);if(!valid){return std::unexpected(valid.error());}Bytes body;put(body,data.sessionId,2);put(body,data.messageId,4);put(body,data.payload.size(),4);body.insert(body.end(),data.payload.begin(),data.payload.end());return frame(MessagePayloadType,body);}
ConfigResult<MessagePayload> decodeMessagePayload(const Frame& input){
  if(input.type!=MessagePayloadType){return error(Code::Unsupported,"Expected inbound Payload31");}Reader r{input.payload};std::uint64_t id=0,message=0,length=0;View payload;if(!r.number(2,id)||!r.number(4,message)||!r.number(4,length)||length>MaximumI2cpPayloadBytes||!r.take(static_cast<std::size_t>(length),payload)||r.at!=r.bytes.size()){return error(Code::InvalidFormat,"Malformed inbound Payload31");}MessagePayload out{static_cast<std::uint16_t>(id),static_cast<std::uint32_t>(message),Bytes(payload.begin(),payload.end())};auto valid=payloadValid(out.sessionId,out.payload);if(!valid){return std::unexpected(valid.error());}return out;
}
} // namespace biglybtc::services::i2p::i2cp
