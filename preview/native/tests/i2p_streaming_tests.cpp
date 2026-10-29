// SPDX-License-Identifier: GPL-2.0-or-later
#include "stream.hpp"
#include "messages.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace p=biglybtc::services::i2p;
namespace st=p::streaming;
void require(bool value,const char* why){if(!value){throw std::runtime_error(why);}}
template<class T,class E>T take(std::expected<T,E> value){if(!value){throw std::runtime_error(value.error().message);}return std::move(*value);}
p::Bytes read(const std::string& name){std::ifstream in(std::filesystem::path(BIGLYBTC_I2P_TEST_KEYS_DIR)/name,std::ios::binary);require(bool(in),"Missing synthetic key fixture");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
p::ClientIdentity identity(const std::string& family){return take(p::makeClientIdentity(read(family+".sender"),p::Bytes(256,0),read(family+".known-test-key")));}
st::Packet checked(st::Packet packet,const p::ClientIdentity& signer){return take(st::decode(take(st::encode(packet,signer)),*signer.destination()));}
st::Packet peerPacket(const p::ClientIdentity& peer,std::uint32_t sequence,p::Bytes payload={},std::uint16_t flags=st::Signature){st::Packet p;p.send=100;p.receive=200;p.sequence=sequence;p.flags=flags;p.payload=std::move(payload);return checked(std::move(p),peer);}
void establish(st::Connection& c,const p::ClientIdentity& local,const p::ClientIdentity& peer){
  auto syn=c.next(100);require(syn.has_value(),"Missing actual SYN");auto parsed=checked(*syn,local);require(parsed.send==0&&parsed.receive==100&&parsed.sequence==0&&(parsed.flags&st::NoAck)&&parsed.nacks.size()==8&&parsed.signatureVerified,"Initial original SYN fields changed");
  for(unsigned i=0;i<8;++i){std::uint32_t word=0;for(unsigned j=0;j<4;++j){word=(word<<8)|peer.destination()->hash[4*i+j];}require(parsed.nacks[i]==word,"SYN destination hash NACK binding changed");}
  c.dispatched(*syn,100);st::Packet response;response.send=100;response.receive=200;response.flags=st::Syn|st::From|st::Signature|st::MaxSize;response.from=*peer.destination();response.maxSize=1730;c.receive(checked(std::move(response),peer),200);require(c.observation().phase==st::Phase::Established,"Signed full-peer SYN ACK did not establish");
}
void codec(const p::ClientIdentity& local,const p::ClientIdentity& peer){
  st::Packet packet;packet.send=1;packet.receive=2;packet.sequence=3;packet.ackThrough=4;packet.nacks={1,2};packet.flags=st::Signature|st::From|st::Delay|st::MaxSize;packet.from=*local.destination();packet.delay=61000;packet.maxSize=1730;packet.payload={0,255,1};
  const auto bytes=take(st::encode(packet,local));auto decoded=take(st::decode(bytes,*local.destination()));require(decoded.send==1&&decoded.receive==2&&decoded.sequence==3&&decoded.ackThrough==4&&decoded.nacks==packet.nacks&&decoded.delay==61000&&decoded.payload==packet.payload&&decoded.signatureVerified,"Original packet fields/signature differ");
  auto bad=bytes;bad.back()^=1;require(!st::decode(bad,*local.destination()),"Tampered signed payload accepted");require(!st::decode(bytes,*peer.destination()),"Wrong signing identity accepted");
  for(std::size_t n=0;n<22;++n){require(!st::decode(p::View(bytes).first(n),*local.destination()),"Truncated header accepted");}
  bad=bytes;bad[18]|=8;require(!st::decode(bad,*local.destination()),"Offline signing flag accepted");bad=bytes;bad[20]=255;bad[21]=255;require(!st::decode(bad,*local.destination()),"Oversize options accepted");
  st::Packet unsignedClose;unsignedClose.flags=st::Close;require(!st::decode(take(st::encode(unsignedClose,local)),*local.destination()),"Unsigned CLOSE accepted");
  auto mux=take(st::encodeMux({9000,80,bytes}));auto unwrapped=take(st::decodeMux(mux));require(unwrapped.fromPort==9000&&unwrapped.toPort==80&&unwrapped.packet==bytes,"Streaming gzip/ports differ");require(!p::i2cp::decodeMuxDatagram(mux)&&!p::i2cp::decodeRawTrackerReply(mux),"Streaming acquired datagram/raw decoder permission");
  auto corrupt=mux;corrupt.at(corrupt.size()-8)^=1;require(!st::decodeMux(corrupt),"Streaming CRC error accepted");auto concat=mux;concat.insert(concat.end(),mux.begin(),mux.end());require(!st::decodeMux(concat),"Concatenated gzip accepted");mux[9]=19;require(!st::decodeMux(mux),"General datagram accepted as stream");
}
void ordered(const p::ClientIdentity& local,const p::ClientIdentity& peer){
  st::Connection c(7,100,*local.destination(),*peer.destination());establish(c,local,peer);
  c.receive(peerPacket(peer,2,{'B'}),210);auto ack=c.next(210);require(ack&&ack->ackThrough==2&&ack->nacks==std::vector<std::uint32_t>{1},"Out-of-order NACK lost");c.dispatched(*ack,210);require(c.read(10).empty(),"Out-of-order bytes delivered early");
  c.receive(peerPacket(peer,1,{'A'}),220);require(c.read(10)==p::Bytes({'A','B'}),"Ordered byte stream differs");c.receive(peerPacket(peer,1,{'X'}),230);require(c.read(10).empty(),"Duplicate data delivered twice");
  auto wrong=peerPacket(peer,3,{'Z'});wrong.send=99;c.receive(checked(std::move(wrong),peer),240);require(c.read(10).empty()&&c.observation().rejectedPackets==1,"Wrong stream changed delivery");
  c.receive(peerPacket(peer,3,{'C'},st::Signature|st::Close),250);require(c.observation().phase==st::Phase::ReadEof&&c.read(10)==p::Bytes{'C'},"Signed ordered CLOSE lost data/EOF");
}
void capacity(const p::ClientIdentity& local,const p::ClientIdentity& peer){
  st::Connection c(8,100,*local.destination(),*peer.destination());establish(c,local,peer);
  require(!c.write(p::Bytes(st::MaximumBuffered+1,1))&&c.observation().queuedTx==0,"Oversize write partially mutated queue");require(bool(c.write(p::Bytes(st::MaximumBuffered,2))),"Exact bounded write refused");auto data=c.next(201);require(data&&data->payload.size()==1730,"Original maximum segment changed");c.dispatched(*data,201);require(!c.write(p::Bytes{1})&&c.observation().queuedTx==st::MaximumBuffered,"In-flight bytes escaped TX budget");
  c.receive(peerPacket(peer,1,p::Bytes(32768,3)),202);c.receive(peerPacket(peer,2,p::Bytes(32768,4)),203);require(c.observation().queuedRx==65536,"Actual RX budget differs");c.receive(peerPacket(peer,3,{'X'}),204);auto choke=c.next(204);require(choke&&choke->delay==61000&&choke->ackThrough==2&&c.observation().queuedRx==65536,"Full RX falsely acknowledged excess or omitted original choke");c.dispatched(*choke,204);
  auto all=c.read(65536);require(all.size()==65536&&all.front()==3&&all.back()==4,"Bounded RX content changed");auto unchoke=c.next(205);require(unchoke&&unchoke->delay==0,"Read did not release backpressure");c.dispatched(*unchoke,205);c.receive(peerPacket(peer,3,{'X'}),206);require(c.read(1)==p::Bytes{'X'},"Retransmitted unacknowledged capacity drop was lost");
  c.terminate(st::Phase::Cancelled);require(!c.write(p::Bytes{1})&&c.read(1).empty()&&!c.next(999999),"Cancelled stream retained write/delivery authority");
}
void timers(const p::ClientIdentity& local,const p::ClientIdentity& peer){
  st::Connection c(9,100,*local.destination(),*peer.destination());
  st::Packet premature;premature.send=100;premature.receive=200;premature.flags=st::Syn|st::From|st::Signature;premature.from=*peer.destination();c.receive(checked(std::move(premature),peer),0);require(c.observation().phase==st::Phase::Connecting&&c.observation().rejectedPackets==1,"Undispatched SYN acquired handshake authority");
  auto syn=c.next(0);require(syn&&c.transmitTimeout(*syn)==9000,"Original initial RTO changed");c.dispatched(*syn,0);require(!c.next(8999),"Retry fired before original deadline");auto retry=c.next(9000);require(retry&&retry->sequence==syn->sequence&&c.transmitTimeout(*retry)==18000,"SYN retry correlation/backoff changed");c.dispatched(*retry,9000);require(c.wakeup()==27000&&c.observation().retransmissions==1,"Actual dispatch did not arm doubled retry");
  st::Packet reset;reset.send=100;reset.flags=st::Signature|st::Reset;c.receive(checked(std::move(reset),peer),9001);require(c.observation().phase==st::Phase::RemoteReset&&!c.next(999999),"Authenticated pre-handshake reset not terminal");
  st::Connection timeout(10,100,*local.destination(),*peer.destination());std::uint64_t now=0;for(unsigned i=0;i<9;++i){auto packet=timeout.next(now);require(packet.has_value(),"Missing bounded retry");timeout.dispatched(*packet,now);now=*timeout.wakeup();}require(!timeout.next(now)&&timeout.observation().phase==st::Phase::Timeout,"Original finite max-resend bound lost");
}
void closed(const p::ClientIdentity& local,const p::ClientIdentity& peer){
  st::Connection c(11,100,*local.destination(),*peer.destination());establish(c,local,peer);require(bool(c.write(p::Bytes{'Q'})),"Cannot write request byte");c.finishWrite();auto fin=c.next(201);require(fin&&(fin->flags&st::Close)&&fin->payload==p::Bytes{'Q'},"Output close discarded queued final byte");c.dispatched(*fin,201);
  auto ack=peerPacket(peer,0);ack.ackThrough=1;c.receive(checked(std::move(ack),peer),250);require(c.observation().acknowledgedBytes==1&&c.observation().queuedTx==0,"Final data acknowledged before actual matching ACK");
  c.receive(peerPacket(peer,1,{'R'},st::Signature|st::Close),260);require(c.observation().phase==st::Phase::Closed&&c.read(1)==p::Bytes{'R'},"Bidirectional close lost readable final byte");auto last=c.next(260);require(last&&last->ackThrough==1,"Missing final peer-close ACK");c.dispatched(*last,260);require(!c.next(9999),"Closed connection created more output");
}

st::Packet acknowledgement(const p::ClientIdentity& peer,std::uint32_t through,std::vector<std::uint32_t> nacks={}){
  auto packet=peerPacket(peer,0);packet.ackThrough=through;packet.nacks=std::move(nacks);return checked(std::move(packet),peer);
}
void slidingCredit(const p::ClientIdentity& local,const p::ClientIdentity& peer){
  st::Connection c(20,100,*local.destination(),*peer.destination());establish(c,local,peer);require(c.observation().sendWindow==3,"Original initial window must be3");require(bool(c.write(p::Bytes(st::MaximumBuffered,1))),"Bounded upload refused");
  auto burst=[&](unsigned count,std::uint32_t first,std::uint64_t now){for(unsigned i=0;i<count;++i){auto packet=c.next(now+i);require(packet&&packet->sequence==first+i&&packet->payload.size()==st::DefaultSegment,"Sender failed to use available window credit");c.dispatched(*packet,now+i);}require(!c.next(now+count),"Sender exceeded actual unacked window");};
  burst(3,1,210);require(c.observation().maximumInFlightPackets==3&&c.observation().queuedTx==st::MaximumBuffered,"Initial flight/buffer bound differs");
  c.receive(acknowledgement(peer,3),250);require(c.observation().sendWindow==6&&c.observation().acknowledgedBytes==3*st::DefaultSegment,"Original slow-start ACK batch did not release exact credit");burst(6,4,260);
  c.receive(acknowledgement(peer,9),300);require(c.observation().sendWindow==8,"Native max8 clamp lost");burst(8,10,310);require(c.observation().maximumInFlightPackets==8&&c.observation().inFlightPackets==8,"Actual dispatch high-water differs");
  const auto before=c.observation();c.receive(acknowledgement(peer,18),330);require(c.observation().acknowledgedBytes==before.acknowledgedBytes&&c.observation().inFlightPackets==8&&c.observation().rejectedPackets==before.rejectedPackets+1,"Future ACK manufactured credit");
  c.terminate(st::Phase::Cancelled);require(c.observation().queuedTx==0&&c.observation().inFlightPackets==0&&!c.wakeup()&&!c.next(100000),"Cancel retained a sent packet or retry");
  for(unsigned cap:{2U,8U}){st::Connection bounded(21,100,*local.destination(),*peer.destination(),cap);establish(bounded,local,peer);require(bounded.observation().sendWindow==std::min(3U,cap)&&bounded.observation().maximumWindow==cap,"Explicit bounded cap changed");}
  for(unsigned cap:{1U,9U}){st::Connection invalid(22,100,*local.destination(),*peer.destination(),cap);require(!invalid.active()&&!invalid.next(0),"Invalid native window gained wire authority");}
}
void undispatchedAndSelectiveAcknowledgements(const p::ClientIdentity& local,const p::ClientIdentity& peer){
  st::Connection c(23,100,*local.destination(),*peer.destination());establish(c,local,peer);require(bool(c.write(p::Bytes(3*st::DefaultSegment,4))),"Cannot queue selective upload");auto reserved=c.next(210);require(reserved&&reserved->sequence==1,"Missing reserved sequence");c.receive(acknowledgement(peer,1),211);require(c.observation().acknowledgedBytes==0&&c.observation().inFlightPackets==0,"Unsent reservation was acknowledged");c.dispatched(*reserved,212);
  for(unsigned seq=2;seq<=3;++seq){auto packet=c.next(212+seq);require(packet&&packet->sequence==seq,"Missing selective-flight sequence");c.dispatched(*packet,212+seq);}
  c.receive(acknowledgement(peer,3,{2}),250);require(c.observation().inFlightPackets==1&&c.observation().acknowledgedBytes==2*st::DefaultSegment,"Explicit NACK did not retain the hole");const auto rto=c.observation().rtoMilliseconds;const auto due=c.wakeup();
  c.receive(acknowledgement(peer,3,{2}),251);require(!c.next(251),"Fast retransmit occurred before third explicit NACK");c.receive(acknowledgement(peer,3,{2}),252);auto retry=c.next(252);require(retry&&retry->sequence==2&&c.transmitTimeout(*retry)==std::min(45000U,2*rto),"Third NACK failed original fast recovery");c.dispatched(*retry,252);require(c.observation().retransmissions==1&&c.wakeup()==due,"Fast retry postponed an already running global timer");
  c.receive(acknowledgement(peer,3),300);const auto acknowledged=c.observation().acknowledgedBytes;c.receive(acknowledgement(peer,3),301);require(acknowledged==3*st::DefaultSegment&&c.observation().acknowledgedBytes==acknowledged&&!c.wakeup(),"Duplicate ACK double-counted payload or retained retry");
  st::Connection noImplicit(24,100,*local.destination(),*peer.destination());establish(noImplicit,local,peer);require(bool(noImplicit.write(p::Bytes(3*st::DefaultSegment,5))),"Cannot queue implicit-NACK control");for(unsigned i=0;i<3;++i){auto packet=noImplicit.next(210+i);require(bool(packet),"Missing control packet");noImplicit.dispatched(*packet,210+i);}for(unsigned i=0;i<4;++i){noImplicit.receive(acknowledgement(peer,1),250+i);}require(!noImplicit.next(254)&&noImplicit.observation().retransmissions==0,"Packets above ACK-through were implicitly NACKed");
}
void oneTimerBackoffAndFreshRtt(const p::ClientIdentity& local,const p::ClientIdentity& peer){
  std::vector<unsigned> draws;st::Connection c(25,100,*local.destination(),*peer.destination(),8,0,[&](unsigned bound){draws.push_back(bound);return draws.size()==1?bound-1:0;});establish(c,local,peer);require(bool(c.write(p::Bytes(3*st::DefaultSegment,6))),"Cannot queue timeout batch");for(unsigned i=0;i<3;++i){auto packet=c.next(210+i);require(bool(packet),"Missing timer flight");c.dispatched(*packet,210+i);}
  require(c.wakeup()==360&&!c.next(359),"New flight incorrectly restarted one global timer");auto first=c.next(360);require(first&&first->sequence==1&&c.observation().sendWindow==1&&c.transmitTimeout(*first)==300,"Timeout failed single backoff/window cut");c.dispatched(*first,360);auto second=c.next(360);require(second&&second->sequence==2&&c.transmitTimeout(*second)==300,"Oldest half retry order/RTO differs");c.dispatched(*second,360);require(!c.next(360)&&c.observation().retransmissions==2&&c.wakeup()==660,"Timeout resent more than ceil(flight/2) or multiplied RTO per packet");
  c.receive(acknowledgement(peer,3),400);require(c.observation().rtoMilliseconds==347&&c.observation().sendWindow==2,"RTT sample did not exclude retries/use highest fresh188ms sample");require(bool(c.write(p::Bytes(2*st::DefaultSegment,7))),"Cannot queue post-congestion data");for(unsigned i=0;i<2;++i){auto packet=c.next(410+i);require(packet&&packet->sequence==4+i,"Recovery credit did not resume");c.dispatched(*packet,410+i);}
  c.receive(acknowledgement(peer,4),450);require(c.observation().sendWindow==2,"Original additive draw above ACK count grew window");require(bool(c.write(p::Bytes(st::DefaultSegment,7))),"Cannot queue remaining-flight growth control");auto tail=c.next(450);require(tail&&tail->sequence==6,"Missing remaining flight for original growth predicate");c.dispatched(*tail,450);c.receive(acknowledgement(peer,5),451);require(c.observation().sendWindow==3&&draws==std::vector<unsigned>({2,2}),"Original bounded additive-growth draw differs");
}
void chokeAndFinWithSelectiveHole(const p::ClientIdentity& local,const p::ClientIdentity& peer){
  st::Connection c(26,100,*local.destination(),*peer.destination());establish(c,local,peer);require(bool(c.write(p::Bytes(4*st::DefaultSegment,8))),"Cannot queue choke data");for(unsigned i=0;i<3;++i){auto packet=c.next(210+i);require(bool(packet),"Missing choke flight");c.dispatched(*packet,210+i);}
  auto choke=acknowledgement(peer,3);choke.flags|=st::Delay;choke.delay=61000;c.receive(checked(std::move(choke),peer),250);require(c.observation().peerChoked&&c.observation().sendWindow==1&&!c.next(251),"Remote choke did not stop fresh data");auto unchoke=acknowledgement(peer,3);unchoke.flags|=st::Delay;c.receive(checked(std::move(unchoke),peer),260);auto resumed=c.next(261);require(resumed&&resumed->sequence==4&&!c.observation().peerChoked,"Actual unchoke did not release bounded credit");c.dispatched(*resumed,261);
  st::Connection closing(27,100,*local.destination(),*peer.destination());establish(closing,local,peer);require(bool(closing.write(p::Bytes(3*st::DefaultSegment,9))),"Cannot queue FIN/hole control");closing.finishWrite();for(unsigned i=0;i<3;++i){auto packet=closing.next(210+i);require(packet&&bool(packet->flags&st::Close)==(i==2),"FIN not bound to last queued segment");closing.dispatched(*packet,210+i);}
  closing.receive(acknowledgement(peer,3,{1,2}),250);auto remote=peerPacket(peer,1,{'R'},st::Signature|st::Close);remote.ackThrough=3;remote.nacks={1,2};closing.receive(checked(std::move(remote),peer),251);require(closing.observation().phase==st::Phase::ReadEof&&closing.observation().inFlightPackets==2,"Selective ACK of FIN discarded earlier sent holes");closing.receive(acknowledgement(peer,3),260);require(closing.observation().phase==st::Phase::Closed&&closing.read(1)==p::Bytes{'R'},"ACK of remaining holes failed final ordered close");
}
void originalPacketBandwidthEstimator(){
  st::BandwidthEstimator estimator(0);require(estimator.estimate(0,0)==0,"Unsampled estimator fabricated bandwidth");estimator.sample(3,100,100);require(std::abs(estimator.estimate(599,100)-0.006F)<1e-9F,"Original first500ms floor differs");estimator.sample(2,600,100);require(std::abs(estimator.estimate(600,100)-0.00596875F)<1e-9F,"Original two-stage float filter differs");require(std::abs(estimator.estimate(1100,100)-0.0058515625F)<1e-9F,"Original no-ACK decay differs");
  auto afterIdle=estimator.estimate(18100,100);require(afterIdle>0.001F&&afterIdle<0.003F,"Bounded virtual-null decay failed after long idle");require(estimator.estimate(18100,100)==afterIdle,"Repeated same-time estimator query decayed twice");
}


void firstAckAndCreatedPacketAge(const p::ClientIdentity& local,const p::ClientIdentity& peer){
  st::Connection first(28,100,*local.destination(),*peer.destination());auto syn=first.next(0);require(bool(syn),"Missing first-ACK SYN");first.dispatched(*syn,0);auto retry=first.next(9000);require(bool(retry),"Missing first-ACK retry");first.dispatched(*retry,9000);
  st::Packet reply;reply.send=100;reply.receive=200;reply.flags=st::Syn|st::From|st::Signature;reply.from=*peer.destination();first.receive(checked(std::move(reply),peer),10000);require(first.observation().phase==st::Phase::Established&&first.observation().rtoMilliseconds==15000,"First measured ACK lost original retransmitted packet-age exception");
  st::Connection age(29,100,*local.destination(),*peer.destination());establish(age,local,peer);require(bool(age.write(p::Bytes(2*st::DefaultSegment,10))),"Cannot queue packet-age control");auto old=age.next(210);require(old&&old->sequence==1,"Missing older packet reservation");age.dispatched(*old,300);auto recent=age.next(301);require(recent&&recent->sequence==2,"Missing newer packet reservation");age.dispatched(*recent,301);age.receive(acknowledgement(peer,2),400);require(age.observation().rtoMilliseconds==351,"ACK batch failed highest190ms packet-creation sample");
}

void partialAcknowledgementRestartsBeforeRttUpdate(const p::ClientIdentity& local,const p::ClientIdentity& peer){
  st::Connection c(30,100,*local.destination(),*peer.destination());establish(c,local,peer);require(c.observation().rtoMilliseconds==150,"Initial measured RTO differs");require(bool(c.write(p::Bytes(3*st::DefaultSegment,11))),"Cannot queue partial-ACK timer control");for(unsigned i=0;i<3;++i){auto packet=c.next(210+i);require(packet&&packet->sequence==1+i,"Missing partial-ACK flight");c.dispatched(*packet,210+i);}
  c.receive(acknowledgement(peer,1),250);require(c.observation().rtoMilliseconds==300&&c.observation().inFlightPackets==2&&c.wakeup()==400,"Partial new ACK did not restart with pre-sample150ms RTO before updating to300ms");c.receive(acknowledgement(peer,1),251);require(c.wakeup()==400&&!c.next(399),"Duplicate ACK changed original partial-flight retry deadline");auto retry=c.next(400);require(retry&&retry->sequence==2&&c.transmitTimeout(*retry)==600,"Original400ms retry did not back off current300ms RTO once");c.dispatched(*retry,400);require(c.wakeup()==1000&&c.observation().retransmissions==1,"Partial-flight retry failed actual-dispatch600ms backoff");
}

int main(){try{auto local=identity("dsa"),peer=identity("ed25519");codec(local,peer);codec(peer,local);ordered(local,peer);capacity(local,peer);timers(local,peer);closed(local,peer);slidingCredit(local,peer);undispatchedAndSelectiveAcknowledgements(local,peer);oneTimerBackoffAndFreshRtt(local,peer);chokeAndFinWithSelectiveHole(local,peer);originalPacketBandwidthEstimator();firstAckAndCreatedPacketAge(local,peer);partialAcknowledgementRestartsBeforeRttUpdate(local,peer);std::cout<<"PASS original online Streaming codec/order/capacity/retry/reset and bounded sliding-window/ACK/Westwood controls\n";return 0;}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
