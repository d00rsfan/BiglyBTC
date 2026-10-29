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
namespace {
bool initialSyn(const Packet& p,const Destination& recipient){
  if(p.send||!p.receive||p.sequence||!(p.flags&Syn)||!(p.flags&From)||!(p.flags&Signature)||!(p.flags&NoAck)||(p.flags&(Close|Reset))||!p.from||!p.signatureVerified||p.nacks.size()!=8){return false;}
  for(unsigned i=0;i<8;++i){std::uint32_t word=0;for(unsigned j=0;j<4;++j){word=(word<<8)|recipient.hash[4*i+j];}if(p.nacks[i]!=word){return false;}}return true;
}
}
Result<Packet> decodeInitialSyn(View wire,const Destination& recipient){
  if(wire.size()<22||wire.size()>MaximumPayload+2048){return error(ErrorCode::TooLarge,"Bounded incoming SYN required");}
  const auto nacks=static_cast<std::size_t>(wire[16]);const auto header=22+4*nacks;if(header>wire.size()){return error(ErrorCode::InvalidFormat,"Truncated SYN header");}
  const auto flags=(static_cast<unsigned>(wire[18+4*nacks])<<8)|wire[19+4*nacks];const auto options=(static_cast<std::size_t>(wire[20+4*nacks])<<8)|wire[21+4*nacks];
  if(options>wire.size()-header||!(flags&From)){return error(ErrorCode::InvalidFormat,"SYN FROM options required");}
  const auto offset=header+((flags&Delay)?2U:0U);const auto end=header+options;
  if(offset>end||end-offset<387){return error(ErrorCode::InvalidFormat,"Truncated SYN FROM");}
  const auto length=387+((static_cast<std::size_t>(wire[offset+385])<<8)|static_cast<std::size_t>(wire[offset+386]));
  if(length>end-offset){return error(ErrorCode::InvalidFormat,"Truncated SYN Destination");}
  auto peer=decodeDestination(wire.subspan(offset,length));if(!peer){return std::unexpected(peer.error());}
  auto packet=decode(wire,*peer);if(!packet){return std::unexpected(packet.error());}
  if(!initialSyn(*packet,recipient)||peer->hash==recipient.hash){return error(ErrorCode::BadSignature,"Incoming SYN identity/recipient binding refused");}return packet;
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

// Original SimpleBandwidthEstimator.java: packets/ms, not payload bytes/ms.
namespace {float westwood(float a,float b)noexcept{return (7.0F*a+b)/8.0F;}}
void BandwidthEstimator::decay()noexcept{stage_*=7.0F/8.0F;filtered_=westwood(filtered_,stage_);}
void BandwidthEstimator::sample(unsigned packets,std::uint64_t now,unsigned rtt)noexcept{
  if(now<at_){return;}
  if(acked_<0){const auto dt=std::max<std::uint64_t>(now-at_,500);filtered_=stage_=static_cast<float>(packets)/static_cast<float>(dt);acked_=0;at_=now;}
  else{acked_=std::min<std::int64_t>(0x7fffffff,acked_+packets);if(now-at_>=std::max(rtt,500U)){static_cast<void>(update(now,rtt));}}
}
float BandwidthEstimator::update(std::uint64_t now,unsigned rtt)noexcept{
  if(acked_<0||now<at_){return filtered_;}auto dt=now-at_;const auto period=std::max(rtt,500U);if(dt<period){return filtered_;}
  if(dt>2*static_cast<std::uint64_t>(period)){const auto count=std::min<std::uint64_t>(dt/period-1,16);for(std::uint64_t i=0;i<count;++i){decay();}dt-=count*period;}
  if(acked_>0){const auto rate=static_cast<float>(acked_)/static_cast<float>(dt);stage_=westwood(stage_,rate);filtered_=westwood(filtered_,stage_);}else{decay();}
  acked_=0;at_=now;return filtered_;
}
float BandwidthEstimator::estimate(std::uint64_t now,unsigned rtt)noexcept{return now>=at_&&now-at_>=std::max(rtt,500U)?update(now,rtt):filtered_;}

Connection::Connection(std::uint64_t operation,std::uint32_t id,Destination local,Destination peer,unsigned maximumWindow,std::uint64_t started,Entropy entropy)
    :local_(std::move(local)),peer_(std::move(peer)),localId_(id),maximumWindow_(maximumWindow),window_(std::min(3U,maximumWindow)),entropy_(std::move(entropy)),bandwidth_(started){
  observed_.operation=operation;congestionWindowEnd_=static_cast<std::int64_t>(window_)-1;if(maximumWindow<2||maximumWindow>MaximumWindow){observed_.phase=Phase::Failed;}
}
Result<Connection> Connection::accept(std::uint64_t operation,std::uint32_t id,Destination local,Destination peer,const Packet& syn,unsigned maximumWindow,std::uint64_t now,Entropy entropy){
  if(!operation||!id||!initialSyn(syn,local)||!syn.from||syn.from->wire!=peer.wire||peer.hash==local.hash||maximumWindow<2||maximumWindow>MaximumWindow){return error(ErrorCode::Unsupported,"Valid owned incoming SYN and bounded window required");}
  Connection out(operation,id,std::move(local),std::move(peer),maximumWindow,now,std::move(entropy));out.inbound_=true;out.remoteId_=syn.receive;out.segment_=syn.flags&MaxSize?std::clamp<std::uint16_t>(syn.maxSize,512,DefaultSegment):DefaultSegment;out.observed_.phase=Phase::Established;
  auto initial=syn;initial.send=id;out.receive(std::move(initial),now);if(!out.active()||out.observation().rejectedPackets){return error(ErrorCode::Unsupported,"Incoming SYN exceeds stream envelope");}return out;
}
bool Connection::keepalive()noexcept{if(!active()||finish_||remoteFin_){return false;}keepaliveWanted_=true;return true;}
bool Connection::active()const noexcept{return observed_.phase==Phase::Connecting||observed_.phase==Phase::Established||observed_.phase==Phase::ReadEof;}
std::size_t Connection::flight()const noexcept{std::size_t count=0;for(const auto& p:sent_){count+=p&&p->sends;}return count;}
std::size_t Connection::activeResends()const noexcept{std::size_t count=0;for(const auto& p:sent_){count+=p&&p->sends>1;}return count;}
Observation Connection::observation()const noexcept{
  auto out=observed_;out.queuedTx=tx_.size();out.queuedRx=rx_.size()+reorderBytes_;out.sendWindow=window_;out.maximumWindow=maximumWindow_;out.slowStartThreshold=ssthresh_;out.rtoMilliseconds=rto_;out.peerChoked=choked_;
  for(const auto& p:sent_){if(p){out.queuedTx+=p->packet.payload.size();if(p->sends){++out.inFlightPackets;out.inFlightBytes+=p->packet.payload.size();}}}return out;
}
Connection::Pending* Connection::oldest(bool unsent)noexcept{
  Pending* result=nullptr;for(auto& p:sent_){if(p&&((p->sends==0)==unsent)&&(!result||p->packet.sequence<result->packet.sequence)){result=&*p;}}return result;
}
void Connection::reject()noexcept{increment(observed_.rejectedPackets);}
void Connection::terminate(Phase phase)noexcept{observed_.phase=phase;for(auto& p:sent_){p.reset();}retryAt_.reset();timeoutArmPending_=false;tx_.clear();reorder_.clear();reorderBytes_=0;ackWanted_=false;keepaliveWanted_=false;if(phase!=Phase::Closed){rx_.clear();}}
Result<void> Connection::write(View input){
  if(!active()||finish_){return error(ErrorCode::Unsupported,"Streaming output is closed");}if(input.size()>MaximumBuffered-observation().queuedTx){return error(ErrorCode::TooLarge,"Streaming write backpressure");}tx_.insert(tx_.end(),input.begin(),input.end());return {};
}
Bytes Connection::read(std::size_t maximum){
  const auto count=std::min(maximum,rx_.size());Bytes out;out.reserve(count);for(std::size_t i=0;i<count;++i){out.push_back(rx_.front());rx_.pop_front();}if(count&&choking_){choking_=false;ackWanted_=true;}return out;
}
void Connection::sample(std::uint64_t elapsed){
  if(!elapsed){return;}const auto value=static_cast<unsigned>(std::min<std::uint64_t>(elapsed,120000));minimumRtt_=std::min(minimumRtt_,value);
  if(samples_==0){rtt_=std::min(value,60000U);deviation_=rtt_/2;rto_=rtt_+rtt_/2;}else{const auto delta=value>rtt_?value-rtt_:rtt_-value;deviation_=(3*deviation_+delta)/4;rtt_=std::min((7*rtt_+value)/8,60000U);rto_=rtt_+4*deviation_;}samples_=std::min(samples_+1,2U);rto_=std::clamp(rto_,100U,45000U);
}
void Connection::congestion()noexcept{if(ackSinceCongestion_){ackSinceCongestion_=false;lastCongestionHighest_=highestSent_;}}
void Connection::recoveryThreshold(std::uint64_t now)noexcept{
  const float value=bandwidth_.estimate(now,rtt_)*static_cast<float>(minimumRtt_);ssthresh_=static_cast<unsigned>(std::clamp(value,2.0F,64.0F));
}
void Connection::adjustWindow(bool isNew,std::uint32_t sequence,unsigned acked){
  const bool congested=choked_||(!isNew&&sequence>0);
  if(highestAcked_>=congestionWindowEnd_||acked>1||flight()>0){
    if(!congested&&acked){if(window_<ssthresh_){window_=std::min(ssthresh_,window_+acked);}else if(entropy_){const auto draw=entropy_(window_);if(draw>=window_){throw Error{ErrorCode::CryptoFailure,"Invalid bounded Streaming entropy"};}if(draw<acked){++window_;}}}
    window_=std::clamp(window_,1U,maximumWindow_);congestionWindowEnd_=highestAcked_+window_;
  }
}
bool Connection::sendCredit()const noexcept{
  return !choked_&&flight()<window_&&activeResends()<(window_+1)/2&&highestSent_-highestAcked_<static_cast<std::int64_t>(std::min(128U,2*window_));
}
void Connection::acknowledgements(Packet& p)const{
  if(contiguous_<0&&reorder_.empty()){p.flags|=NoAck;p.ackThrough=0;return;}
  p.flags&=static_cast<std::uint16_t>(~NoAck);p.ackThrough=reorder_.empty()?static_cast<std::uint32_t>(contiguous_):reorder_.rbegin()->first;
  for(std::uint64_t seq=static_cast<std::uint64_t>(contiguous_+1);seq<=p.ackThrough;++seq){if(!reorder_.contains(static_cast<std::uint32_t>(seq))){p.nacks.push_back(static_cast<std::uint32_t>(seq));}}
}
Packet Connection::base()const{Packet p;p.send=remoteId_;p.receive=localId_;p.resendDelay=1;p.flags=Signature|Delay;p.delay=choking_?61000:0;acknowledgements(p);return p;}
void Connection::receive(Packet p,std::uint64_t now){
  lastReceiveAccepted_=false;
  if(!active()&&observed_.phase!=Phase::Closed){reject();return;}
  if(p.flags&Reset){if(p.send==localId_&&p.signatureVerified){terminate(Phase::RemoteReset);}else{reject();}return;}
  if(p.send!=localId_||!p.receive||(remoteId_&&p.receive!=remoteId_)){reject();return;}
  if(!(p.flags&NoAck)&&(static_cast<std::int64_t>(p.ackThrough)>highestSent_||std::ranges::any_of(p.nacks,[&](auto n){return n>p.ackThrough;}))){reject();return;}
  if(!remoteId_){const auto* syn=oldest();if(!syn||!(syn->packet.flags&Syn)||!(p.flags&Syn)||!p.from||!p.signatureVerified||p.sequence!=0||(p.flags&NoAck)||p.ackThrough!=0||!p.nacks.empty()){reject();return;}remoteId_=p.receive;segment_=p.flags&MaxSize?std::clamp<std::uint16_t>(p.maxSize,512,DefaultSegment):DefaultSegment;observed_.phase=Phase::Established;}
  const bool sequenceBearing=p.sequence>0||!p.payload.empty()||(p.flags&(Syn|Close));
  const bool isNew=sequenceBearing&&static_cast<std::int64_t>(p.sequence)>contiguous_&&!reorder_.contains(p.sequence);
  if(isNew&&(remoteFin_||static_cast<std::uint64_t>(p.sequence)>static_cast<std::uint64_t>(contiguous_+1)+MaximumReorder||reorder_.size()>=MaximumReorder||p.payload.size()>MaximumBuffered-rx_.size()-reorderBytes_)){choking_=true;ackWanted_=true;reject();return;}
  lastReceiveAccepted_=true;
  choked_=(p.flags&Delay)&&p.delay>60000;if(choked_){congestion();window_=1;}
  if(!(p.flags&NoAck)){
    if(static_cast<std::int64_t>(p.ackThrough)>=highestAcked_){if(p.nacks.empty()){highestAcked_=p.ackThrough;}else{const auto first=*std::min_element(p.nacks.begin(),p.nacks.end());highestAcked_=std::max(highestAcked_,static_cast<std::int64_t>(first)-1);}}
    unsigned acked=0;std::uint64_t highestRtt=0;
    for(auto& slot:sent_){if(!slot||!slot->sends||slot->packet.sequence>p.ackThrough){continue;}auto& pending=*slot;
      if(std::ranges::find(p.nacks,pending.packet.sequence)!=p.nacks.end()){
        if(pending.nacks<std::numeric_limits<unsigned>::max()){++pending.nacks;}
        if(pending.nacks>=3&&!pending.fastUsed&&(pending.sends==1||(now>=pending.sent&&now-pending.sent>4000))){pending.fastUsed=true;pending.fast=true;pending.retry=true;}
      }else{
        // ConnectionPacketHandler captures receivedAck before the whole batch:
        // only the first measured ACK may sample an already retransmitted
        // packet. PacketLocal.getAckTime starts at creation, not last send.
        if((pending.sends==1||samples_==0)&&now>=pending.created){highestRtt=std::max(highestRtt,now-pending.created);}observed_.acknowledgedBytes+=pending.packet.payload.size();if(pending.packet.flags&Close){localFinAcked_=true;}slot.reset();++acked;
      }
    }
    if(acked){ackSinceCongestion_=true;bandwidth_.sample(acked,now,rtt_);if(flight()){retryAt_=now+rto_;}else{retryAt_.reset();timeoutArmPending_=false;}sample(highestRtt);}
    adjustWindow(isNew,p.sequence,acked);
  }
  if(sequenceBearing){
    if(!isNew){ackWanted_=true;}
    else{
      reorderBytes_+=p.payload.size();reorder_.emplace(p.sequence,std::move(p));
      while(true){auto it=reorder_.find(static_cast<std::uint32_t>(contiguous_+1));if(it==reorder_.end()){break;}if((it->second.flags&Close)&&std::next(it)!=reorder_.end()){terminate(Phase::Failed);return;}rx_.insert(rx_.end(),it->second.payload.begin(),it->second.payload.end());reorderBytes_-=it->second.payload.size();if(it->second.flags&Close){remoteFin_=true;}reorder_.erase(it);++contiguous_;}
      ackWanted_=true;
    }
  }
  if(remoteFin_){observed_.phase=localFinAcked_&&flight()==0?Phase::Closed:Phase::ReadEof;}
}
void Connection::retryTimeout(std::uint64_t now){
  auto* first=oldest();if(!first){retryAt_.reset();return;}congestion();rto_=std::min(45000U,rto_*2);
  if(first->sends==1){recoveryThreshold(now);window_=1;}
  retryAt_.reset();timeoutArmPending_=true;
  const auto limit=std::min<std::size_t>(16,(flight()+1)/2);std::int64_t previous=-1;
  for(std::size_t i=0;i<limit;++i){Pending* chosen=nullptr;for(auto& slot:sent_){if(slot&&slot->sends&&static_cast<std::int64_t>(slot->packet.sequence)>previous&&(!chosen||slot->packet.sequence<chosen->packet.sequence)){chosen=&*slot;}}
    if(!chosen){break;}if(chosen->sends>=9){terminate(Phase::Timeout);return;}chosen->retry=true;chosen->fast=false;previous=chosen->packet.sequence;
  }
}
std::optional<Packet> Connection::next(std::uint64_t now){
  if(!active()&&observed_.phase!=Phase::Closed){return std::nullopt;}
  if(retryAt_&&now>=*retryAt_){retryTimeout(now);if(!active()){return std::nullopt;}}
  Pending* selected=oldest(true);
  if(!selected){for(auto& p:sent_){if(p&&p->retry&&(!selected||p->packet.sequence<selected->packet.sequence)){selected=&*p;}}}
  if(selected){
    if(selected->sends>=9){terminate(Phase::Timeout);return std::nullopt;}
    if(selected->fast){selected->fast=false;if(choked_){congestion();window_=1;}else if(ackSinceCongestion_&&static_cast<std::int64_t>(selected->packet.sequence)>lastCongestionHighest_){congestion();rto_=std::min(45000U,rto_*2);if(selected->sends==1){recoveryThreshold(now);window_=std::min(ssthresh_,window_);}}}
    auto p=selected->packet;if(!(p.flags&Syn)){p.nacks.clear();acknowledgements(p);p.delay=choking_?61000:0;}return p;
  }
  if(active()&&(nextSequence_==0||(remoteId_&&sendCredit()&&(!tx_.empty()||keepaliveWanted_||(finish_&&!localFinSent_))))){
    if(nextSequence_==std::numeric_limits<std::uint32_t>::max()){terminate(Phase::Failed);return std::nullopt;}auto free=std::ranges::find_if(sent_,[](const auto& slot){return !slot;});if(free==sent_.end()){return std::nullopt;}
    auto p=base();p.sequence=nextSequence_++;
    if(p.sequence==0){p.flags|=Syn|From|MaxSize;p.from=local_;p.maxSize=DefaultSegment;if(!inbound_){p.flags|=NoAck;p.nacks.clear();for(unsigned i=0;i<8;++i){std::uint32_t value=0;for(unsigned j=0;j<4;++j){value=(value<<8)|peer_.hash[4*i+j];}p.nacks.push_back(value);}}}
    else{keepaliveWanted_=false;const auto count=std::min<std::size_t>(segment_,tx_.size());for(std::size_t i=0;i<count;++i){p.payload.push_back(tx_.front());tx_.pop_front();}if(finish_&&tx_.empty()){p.flags|=Close;localFinSent_=true;}}
    Pending prepared;prepared.packet=std::move(p);prepared.created=now;free->emplace(std::move(prepared));return (*free)->packet;
  }
  if(ackWanted_&&remoteId_){return base();}return std::nullopt;
}
void Connection::dispatched(const Packet& p,std::uint64_t now){
  increment(observed_.transmittedPackets);ackWanted_=false;
  auto found=std::ranges::find_if(sent_,[&](const auto& slot){return slot&&slot->packet.sequence==p.sequence&&(p.sequence>0||(p.flags&(Syn|Close))||!p.payload.empty());});
  if(found==sent_.end()){return;}auto& pending=**found;
  if(pending.sends){increment(observed_.retransmissions);}++pending.sends;pending.sent=now;pending.retry=false;highestSent_=std::max(highestSent_,static_cast<std::int64_t>(p.sequence));
  if(timeoutArmPending_||!retryAt_){retryAt_=now+rto_;timeoutArmPending_=false;}observed_.maximumInFlightPackets=std::max(observed_.maximumInFlightPackets,flight());
}
std::optional<std::uint64_t> Connection::wakeup()const noexcept{
  return flight()?retryAt_:std::nullopt;
}
std::optional<unsigned> Connection::transmitTimeout(const Packet& p)const noexcept{
  for(const auto& slot:sent_){if(slot&&slot->packet.sequence==p.sequence&&(p.sequence>0||(p.flags&(Syn|Close))||!p.payload.empty())){return rto_;}}return std::nullopt;
}
} // namespace biglybtc::services::i2p::streaming
