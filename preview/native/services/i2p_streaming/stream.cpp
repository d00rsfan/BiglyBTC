// SPDX-License-Identifier: GPL-2.0-or-later
#include "stream.hpp"
#include <algorithm>
#include <iterator>
#include <limits>
#include <zlib.h>

namespace biglybtc::services::i2p::streaming {
namespace {
auto error(ErrorCode code,const char* text){return std::unexpected(Error{code,text});}
void put(Bytes& out,std::uint64_t value,unsigned count){for(unsigned i=count;i>0;--i){out.push_back(static_cast<Byte>(value>>((i-1)*8)));}}
struct Reader {
  View bytes;std::size_t at=0;
  bool number(unsigned count,std::uint32_t& out){if(count>bytes.size()-at){return false;}out=0;while(count--){out=(out<<8)|bytes[at++];}return true;}
};
constexpr std::uint16_t Supported=Syn|Close|Reset|Signature|SignatureRequested|From|Delay|MaxSize|Interactive|NoAck;
void increment(std::uint64_t& value)noexcept{if(value!=std::numeric_limits<std::uint64_t>::max()){++value;}}
}
Result<Bytes> encode(const Packet& p,const ClientIdentity& signer){
  if(!signer.valid()||(p.flags&~Supported)||p.nacks.size()>255||p.payload.size()>MaximumPayload){return error(ErrorCode::Unsupported,"Bounded online Streaming packet required");}
  if(bool(p.flags&From)!=p.from.has_value()||((p.flags&From)&&p.from->wire!=signer.destination()->wire)){return error(ErrorCode::KeyMismatch,"Streaming FROM must be the owned signer");}
  Bytes options;if(p.flags&Delay){put(options,p.delay,2);}if(p.from){options.insert(options.end(),p.from->wire.begin(),p.from->wire.end());}if(p.flags&MaxSize){put(options,p.maxSize,2);}
  const auto signatureOffset=options.size();const std::size_t signatureSize=(p.flags&Signature)?(signer.destination()->signingType==SigningType::DsaSha1?40U:64U):0U;options.resize(options.size()+signatureSize,0);
  Bytes out;out.reserve(22+4*p.nacks.size()+options.size()+p.payload.size());put(out,p.send,4);put(out,p.receive,4);put(out,p.sequence,4);put(out,p.ackThrough,4);put(out,p.nacks.size(),1);for(auto n:p.nacks){put(out,n,4);}put(out,p.resendDelay,1);put(out,p.flags,2);put(out,options.size(),2);
  const auto signatureAt=out.size()+signatureOffset;out.insert(out.end(),options.begin(),options.end());out.insert(out.end(),p.payload.begin(),p.payload.end());
  if(signatureSize){auto signature=signData(signer.destination()->signingType,signer.signingPrivateKey(),out);if(!signature){return std::unexpected(signature.error());}std::copy(signature->begin(),signature->end(),out.begin()+static_cast<std::ptrdiff_t>(signatureAt));}return out;
}
Result<Packet> decode(View wire,const Destination& peer){
  if(wire.size()<22||wire.size()>MaximumPayload+2048){return error(ErrorCode::TooLarge,"Streaming packet length outside bound");}
  Reader r{wire};Packet p;std::uint32_t n=0,flags=0,optionSize=0,resend=0;
  if(!r.number(4,p.send)||!r.number(4,p.receive)||!r.number(4,p.sequence)||!r.number(4,p.ackThrough)||!r.number(1,n)||wire.size()<22+4*n){return error(ErrorCode::InvalidFormat,"Truncated Streaming header/NACKs");}
  for(std::uint32_t i=0;i<n;++i){std::uint32_t value=0;if(!r.number(4,value)){return error(ErrorCode::InvalidFormat,"Truncated NACK");}p.nacks.push_back(value);}
  if(!r.number(1,resend)||!r.number(2,flags)||!r.number(2,optionSize)||optionSize>wire.size()-r.at){return error(ErrorCode::InvalidFormat,"Truncated Streaming options");}
  if(flags&~Supported){return error(ErrorCode::Unsupported,"Offline/ECHO/unknown Streaming flags unsupported");}p.flags=static_cast<std::uint16_t>(flags);p.resendDelay=static_cast<Byte>(resend);
  const auto payloadAt=r.at+optionSize;const auto readOption=[&](unsigned count,std::uint32_t& value){return count<=payloadAt-r.at&&r.number(count,value);};
  if(p.flags&Delay){std::uint32_t value=0;if(!readOption(2,value)){return error(ErrorCode::InvalidFormat,"Missing delay option");}p.delay=static_cast<std::uint16_t>(value);}
  if(p.flags&From){
    if(payloadAt-r.at<387){return error(ErrorCode::InvalidFormat,"Missing FROM destination");}const auto length=387+(static_cast<std::size_t>(wire[r.at+385])<<8)+wire[r.at+386];if(length>payloadAt-r.at){return error(ErrorCode::InvalidFormat,"Truncated FROM destination");}
    auto from=decodeDestination(wire.subspan(r.at,length));if(!from){return std::unexpected(from.error());}if(from->wire!=peer.wire){return error(ErrorCode::KeyMismatch,"Streaming peer FROM changed");}p.from=std::move(*from);r.at+=length;
  }
  if(p.flags&MaxSize){std::uint32_t value=0;if(!readOption(2,value)){return error(ErrorCode::InvalidFormat,"Missing maximum-size option");}p.maxSize=static_cast<std::uint16_t>(value);}
  if(p.flags&Signature){
    const std::size_t length=peer.signingType==SigningType::DsaSha1?40U:64U;if(payloadAt-r.at!=length){return error(ErrorCode::InvalidFormat,"Signature width or trailing options changed");}
    Bytes preimage(wire.begin(),wire.end());std::fill(preimage.begin()+static_cast<std::ptrdiff_t>(r.at),preimage.begin()+static_cast<std::ptrdiff_t>(payloadAt),0);
    auto verified=verifyData(peer.signingType,peer.signingPublicKey,wire.subspan(r.at,length),preimage);if(!verified){return std::unexpected(verified.error());}p.signatureVerified=true;r.at=payloadAt;
  }
  if(r.at!=payloadAt||wire.size()-payloadAt>MaximumPayload){return error(ErrorCode::InvalidFormat,"Unknown options or excessive Streaming payload");}
  if((p.flags&(Syn|Close|Reset))&&!p.signatureVerified){return error(ErrorCode::BadSignature,"SYN/CLOSE/RESET requires original peer signature");}
  p.payload.assign(wire.begin()+static_cast<std::ptrdiff_t>(payloadAt),wire.end());return p;
}
Result<Bytes> encodeMux(const Mux& mux){
  if(mux.packet.size()>MaximumPayload+2048){return error(ErrorCode::TooLarge,"Streaming mux packet exceeds bound");}
  z_stream z{};if(deflateInit2(&z,mux.packet.size()>50?3:0,Z_DEFLATED,31,8,Z_DEFAULT_STRATEGY)!=Z_OK){return error(ErrorCode::CryptoFailure,"Streaming gzip resources unavailable");}
  struct End{z_stream* z;~End(){deflateEnd(z);}}end{&z};Bytes bytes(65536);z.next_in=const_cast<Byte*>(mux.packet.data());z.avail_in=static_cast<uInt>(mux.packet.size());z.next_out=bytes.data();z.avail_out=static_cast<uInt>(bytes.size());
  if(deflate(&z,Z_FINISH)!=Z_STREAM_END||z.avail_in||z.total_out<18){return error(ErrorCode::TooLarge,"Streaming gzip output exceeds bound");}bytes.resize(z.total_out);bytes[4]=static_cast<Byte>(mux.fromPort>>8);bytes[5]=static_cast<Byte>(mux.fromPort);bytes[6]=static_cast<Byte>(mux.toPort>>8);bytes[7]=static_cast<Byte>(mux.toPort);bytes[8]=2;bytes[9]=Protocol;return bytes;
}
Result<Mux> decodeMux(View bytes){
  if(bytes.size()<18||bytes.size()>65536||bytes[0]!=31||bytes[1]!=139||bytes[2]!=8||bytes[3]!=0||bytes[9]!=Protocol){return error(ErrorCode::Unsupported,"Fixed-header Streaming protocol6 required");}
  z_stream z{};if(inflateInit2(&z,31)!=Z_OK){return error(ErrorCode::CryptoFailure,"Streaming inflate resources unavailable");}struct End{z_stream* z;~End(){inflateEnd(z);}}end{&z};Bytes out(MaximumPayload+2049);z.next_in=const_cast<Byte*>(bytes.data());z.avail_in=static_cast<uInt>(bytes.size());z.next_out=out.data();z.avail_out=static_cast<uInt>(out.size());
  if(inflate(&z,Z_FINISH)!=Z_STREAM_END||z.avail_in||z.total_out>MaximumPayload+2048){return error(ErrorCode::InvalidFormat,"Malformed/concatenated/oversize Streaming gzip");}out.resize(z.total_out);return Mux{static_cast<std::uint16_t>((static_cast<unsigned>(bytes[4])<<8)|bytes[5]),static_cast<std::uint16_t>((static_cast<unsigned>(bytes[6])<<8)|bytes[7]),std::move(out)};
}

Connection::Connection(std::uint64_t operation,std::uint32_t id,Destination local,Destination peer):local_(std::move(local)),peer_(std::move(peer)),localId_(id){observed_.operation=operation;}
bool Connection::active()const noexcept{return observed_.phase==Phase::Connecting||observed_.phase==Phase::Established||observed_.phase==Phase::ReadEof;}
Observation Connection::observation()const noexcept{auto out=observed_;out.queuedTx=tx_.size()+(pending_?pending_->packet.payload.size():0);out.queuedRx=rx_.size()+reorderBytes_;return out;}
void Connection::reject()noexcept{increment(observed_.rejectedPackets);}
void Connection::terminate(Phase phase)noexcept{observed_.phase=phase;pending_.reset();tx_.clear();reorder_.clear();reorderBytes_=0;ackWanted_=false;if(phase!=Phase::Closed){rx_.clear();}}
Result<void> Connection::write(View input){
  if(!active()||finish_){return error(ErrorCode::Unsupported,"Streaming output is closed");}if(input.size()>MaximumBuffered-observation().queuedTx){return error(ErrorCode::TooLarge,"Streaming write backpressure");}tx_.insert(tx_.end(),input.begin(),input.end());return {};
}
Bytes Connection::read(std::size_t maximum){
  const auto count=std::min(maximum,rx_.size());Bytes out;out.reserve(count);for(std::size_t i=0;i<count;++i){out.push_back(rx_.front());rx_.pop_front();}if(count&&choking_){choking_=false;ackWanted_=true;}return out;
}
void Connection::sample(std::uint64_t elapsed){
  const auto value=static_cast<unsigned>(std::clamp<std::uint64_t>(elapsed,1,120000));
  if(samples_==0){rtt_=std::min(value,60000U);deviation_=rtt_/2;rto_=rtt_+rtt_/2;}else{const auto delta=value>rtt_?value-rtt_:rtt_-value;deviation_=(3*deviation_+delta)/4;rtt_=std::min((7*rtt_+value)/8,60000U);rto_=rtt_+4*deviation_;}samples_=std::min(samples_+1,2U);rto_=std::clamp(rto_,100U,45000U);
}
void Connection::acknowledgements(Packet& p)const{
  if(contiguous_<0&&reorder_.empty()){p.flags|=NoAck;p.ackThrough=0;return;}
  p.flags&=static_cast<std::uint16_t>(~NoAck);p.ackThrough=reorder_.empty()?static_cast<std::uint32_t>(contiguous_):reorder_.rbegin()->first;
  for(std::uint64_t seq=static_cast<std::uint64_t>(contiguous_+1);seq<=p.ackThrough;++seq){if(!reorder_.contains(static_cast<std::uint32_t>(seq))){p.nacks.push_back(static_cast<std::uint32_t>(seq));}}
}
Packet Connection::base()const{Packet p;p.send=remoteId_;p.receive=localId_;p.resendDelay=1;p.flags=Signature|Delay;p.delay=choking_?61000:0;acknowledgements(p);return p;}
void Connection::receive(Packet p,std::uint64_t now){
  if(!active()&&observed_.phase!=Phase::Closed){reject();return;}
  if(p.flags&Reset){if(p.send==localId_&&p.signatureVerified){terminate(Phase::RemoteReset);}else{reject();}return;}
  if(p.send!=localId_||!p.receive||(remoteId_&&p.receive!=remoteId_)){reject();return;}
  if(!remoteId_){if(!pending_||!pending_->sends||!(pending_->packet.flags&Syn)||!(p.flags&Syn)||!p.from||!p.signatureVerified||p.sequence!=0||(p.flags&NoAck)||p.ackThrough!=0||!p.nacks.empty()){reject();return;}remoteId_=p.receive;segment_=p.flags&MaxSize?std::clamp<std::uint16_t>(p.maxSize,512,DefaultSegment):DefaultSegment;observed_.phase=Phase::Established;}
  choked_=(p.flags&Delay)&&p.delay>60000;
  if(!(p.flags&NoAck)&&pending_&&pending_->sends&&p.ackThrough>=pending_->packet.sequence&&std::find(p.nacks.begin(),p.nacks.end(),pending_->packet.sequence)==p.nacks.end()){
    // An ACK may cover our only actually transmitted packet, never future data.
    if(p.ackThrough>=nextSequence_){reject();return;}
    if(pending_->sends==1&&now>=pending_->sent){sample(now-pending_->sent);}observed_.acknowledgedBytes+=pending_->packet.payload.size();if(pending_->packet.flags&Close){localFinAcked_=true;}pending_.reset();
  }
  const bool sequenceBearing=!p.payload.empty()||(p.flags&(Syn|Close));
  if(sequenceBearing){
    if(static_cast<std::int64_t>(p.sequence)<=contiguous_||reorder_.contains(p.sequence)){ackWanted_=true;}
    else if(remoteFin_||static_cast<std::uint64_t>(p.sequence)>static_cast<std::uint64_t>(contiguous_+1)+MaximumReorder||reorder_.size()>=MaximumReorder||p.payload.size()>MaximumBuffered-rx_.size()-reorderBytes_){choking_=true;ackWanted_=true;reject();}
    else{
      if(!reorder_.contains(p.sequence)){reorderBytes_+=p.payload.size();reorder_.emplace(p.sequence,std::move(p));}
      while(true){auto it=reorder_.find(static_cast<std::uint32_t>(contiguous_+1));if(it==reorder_.end()){break;}if((it->second.flags&Close)&&std::next(it)!=reorder_.end()){terminate(Phase::Failed);return;}rx_.insert(rx_.end(),it->second.payload.begin(),it->second.payload.end());reorderBytes_-=it->second.payload.size();if(it->second.flags&Close){remoteFin_=true;}reorder_.erase(it);++contiguous_;}
      ackWanted_=true;
    }
  }
  if(remoteFin_){observed_.phase=localFinAcked_?Phase::Closed:Phase::ReadEof;}
}
std::optional<Packet> Connection::next(std::uint64_t now){
  if(!active()&&observed_.phase!=Phase::Closed){return std::nullopt;}
  if(!pending_&&active()){
    if(nextSequence_==std::numeric_limits<std::uint32_t>::max()){terminate(Phase::Failed);return std::nullopt;}
    if(nextSequence_==0){auto p=base();p.sequence=nextSequence_++;p.flags|=Syn|From|MaxSize|NoAck;p.from=local_;p.maxSize=DefaultSegment;p.nacks.clear();for(unsigned i=0;i<8;++i){std::uint32_t value=0;for(unsigned j=0;j<4;++j){value=(value<<8)|peer_.hash[4*i+j];}p.nacks.push_back(value);}pending_=Pending{std::move(p)};}
    else if(remoteId_&&!choked_&&(!tx_.empty()||(finish_&&!localFinSent_))){auto p=base();p.sequence=nextSequence_++;const auto count=std::min<std::size_t>(segment_,tx_.size());for(std::size_t i=0;i<count;++i){p.payload.push_back(tx_.front());tx_.pop_front();}if(finish_&&tx_.empty()){p.flags|=Close;localFinSent_=true;}pending_=Pending{std::move(p)};}
  }
  if(pending_&&(!pending_->sends||now>=pending_->due)){
    if(pending_->sends>=9){terminate(Phase::Timeout);return std::nullopt;}auto p=pending_->packet;
    if(!(p.flags&Syn)){p.nacks.clear();acknowledgements(p);p.delay=choking_?61000:0;}return p;
  }
  if(ackWanted_&&remoteId_){return base();}return std::nullopt;
}
void Connection::dispatched(const Packet& p,std::uint64_t now){
  increment(observed_.transmittedPackets);ackWanted_=false;
  if(pending_&&p.sequence==pending_->packet.sequence&&(p.flags&(Syn|Close)||!p.payload.empty())){
    if(pending_->sends){increment(observed_.retransmissions);rto_=std::min(45000U,rto_*2);}++pending_->sends;pending_->sent=now;pending_->due=now+rto_;
  }
}
std::optional<std::uint64_t> Connection::wakeup()const noexcept{if(pending_&&pending_->sends){return pending_->due;}return std::nullopt;}
std::optional<unsigned> Connection::transmitTimeout(const Packet& p)const noexcept{
  if(!pending_||p.sequence!=pending_->packet.sequence||(!(p.flags&(Syn|Close))&&p.payload.empty())){return std::nullopt;}return pending_->sends?std::min(45000U,2*rto_):rto_;
}
} // namespace biglybtc::services::i2p::streaming
