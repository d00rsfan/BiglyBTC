// SPDX-License-Identifier: GPL-2.0-or-later
#include "stream.hpp"
#include "messages.hpp"
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
int main(){try{auto local=identity("dsa"),peer=identity("ed25519");codec(local,peer);codec(peer,local);ordered(local,peer);capacity(local,peer);timers(local,peer);closed(local,peer);std::cout<<"PASS original online Streaming codec/order/capacity/retry/reset controls\n";return 0;}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
