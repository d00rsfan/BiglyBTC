// SPDX-License-Identifier: GPL-2.0-or-later
#include "manager.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace p=biglybtc::services::i2p;
namespace st=p::streaming;
void require(bool value,const char* why){if(!value){throw std::runtime_error(why);}}
template<class E>void take(std::expected<void,E> value){if(!value){throw std::runtime_error(value.error().message);}}
template<class T,class E>T take(std::expected<T,E> value){if(!value){throw std::runtime_error(value.error().message);}return std::move(*value);}
p::ClientIdentity identity(const char* family){
  const auto read=[&](const char* suffix){std::ifstream in(std::filesystem::path(BIGLYBTC_I2P_TEST_KEYS_DIR)/(std::string(family)+suffix),std::ios::binary);require(bool(in),"Missing synthetic key");return p::Bytes(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());};
  return take(p::makeClientIdentity(read(".sender"),p::Bytes(256,0x51),read(".known-test-key")));
}
st::Packet checked(st::Packet packet,const p::ClientIdentity& signer){return take(st::decode(take(st::encode(packet,signer)),*signer.destination()));}
st::Mux mux(st::Packet packet,const p::ClientIdentity& signer,std::uint16_t from,std::uint16_t to){return {from,to,take(st::encode(packet,signer))};}
st::Packet response(const p::ClientIdentity& peer,std::uint32_t id,std::uint32_t remote,std::uint32_t sequence=0,p::Bytes bytes={}){
  st::Packet packet;packet.send=id;packet.receive=remote;packet.sequence=sequence;packet.flags=st::Signature;packet.payload=std::move(bytes);return checked(std::move(packet),peer);
}
void establish(st::Manager& manager,const p::ClientIdentity& local,const p::ClientIdentity& peer,std::uint64_t operation=1,std::uint32_t id=100,std::uint16_t port=9000){
  take(manager.connect(operation,id,*peer.destination(),{{port,80},100,8},0));
  auto syn=manager.next(1);require(syn&&syn->operation==operation,"Missing managed SYN");manager.dispatched(*syn,1);
  auto inbound=take(st::Connection::accept(10,200,*peer.destination(),*local.destination(),take(st::decodeInitialSyn(take(st::encode(syn->packet,local)),*peer.destination())),8,1));
  auto reply=inbound.next(2);require(bool(reply),"Missing actual peer SYN/ACK");inbound.dispatched(*reply,2);manager.receive(mux(*reply,peer,80,port),2);
  require(manager.observation(operation)->stream.phase==st::Phase::Established,"Manager failed signed handshake");
  auto ack=manager.next(3);require(ack&&ack->operation==operation,"Missing final handshake ACK");manager.dispatched(*ack,3);
}
void inboundCodecAndRole(const p::ClientIdentity& local,const p::ClientIdentity& peer){
  st::Connection outgoing(1,300,*peer.destination(),*local.destination());auto syn=outgoing.next(0);require(bool(syn),"Missing peer SYN");outgoing.dispatched(*syn,0);
  const auto encoded=take(st::encode(*syn,peer));auto parsed=take(st::decodeInitialSyn(encoded,*local.destination()));require(parsed.from&&parsed.from->wire==peer.destination()->wire,"Verified incoming FROM differs");
  auto incoming=take(st::Connection::accept(2,100,*local.destination(),*peer.destination(),parsed));auto reply=incoming.next(1);require(reply&&reply->send==300&&reply->receive==100&&(reply->flags&st::Syn)&&!(reply->flags&st::NoAck)&&reply->ackThrough==0&&reply->nacks.empty(),"Inbound SYN/ACK fields differ");
  incoming.dispatched(*reply,1);outgoing.receive(checked(*reply,local),2);require(outgoing.observation().phase==st::Phase::Established,"Initiator failed accepted inbound handshake");
  auto ack=outgoing.next(3);require(bool(ack),"Missing initiator handshake ACK");outgoing.dispatched(*ack,3);incoming.receive(checked(*ack,peer),3);require(incoming.observation().inFlightPackets==0,"Inbound handshake ACK did not release SYN/ACK");
  auto tampered=encoded;tampered.back()^=1;require(!st::decodeInitialSyn(tampered,*local.destination()),"Tampered incoming SYN accepted");
  require(!st::decodeInitialSyn(encoded,*peer.destination()),"Wrong recipient hash accepted");auto noHash=*syn;noHash.nacks.clear();require(!st::decodeInitialSyn(take(st::encode(noHash,peer)),*local.destination()),"Unbound recipient SYN accepted");
}
void listenerBoundsAndExpiry(const p::ClientIdentity& local,const p::ClientIdentity& peer){
  st::Manager manager(*local.destination(),{});take(manager.listen({{6881,80},{*peer.destination()},8},0));
  for(std::uint32_t i=1;i<=5;++i){st::Connection remote(i,300+i,*peer.destination(),*local.destination());auto syn=remote.next(1);require(bool(syn),"Missing flood fixture SYN");manager.receive(mux(*syn,peer,80,6881),1);}
  require(manager.observation().pendingSyns==4&&manager.observation().rejectedPackets==1&&manager.observation().pendingSynBytes<=st::MaximumPendingSynBytes,"Listener exceeded fixed pending capacity");
  manager.tick(3000);require(manager.observation().pendingSyns==4,"SYN expired before original3s boundary");manager.tick(3001);require(manager.observation().pendingSyns==0&&manager.observation().expiredSyns==4,"SYN backlog did not retire at original3s boundary");require(!manager.accept(1,100,3001),"Expired SYN acquired stream");
  manager.stopListening();require(!manager.receives(),"Stopped listener retained receive authority");
}
void multiplexFairnessAndMemory(const p::ClientIdentity& local,const p::ClientIdentity& peer){
  st::Manager manager(*local.destination(),{});
  for(unsigned i=0;i<4;++i){take(manager.connect(i+1,100+i,*peer.destination(),{{static_cast<std::uint16_t>(9000+i),80},100,8},0));take(manager.write(i+1,p::Bytes(st::MaximumBuffered,static_cast<p::Byte>(i)),0));}
  require(manager.observation().streams==4&&manager.observation().queuedTx==4*st::MaximumBuffered,"Global bounded TX envelope differs");require(!manager.connect(5,105,*peer.destination(),{{9010,80},100,8},0),"Fifth stream exceeded fixed capacity");
  for(unsigned i=0;i<4;++i){auto next=manager.next(1);require(next&&next->operation==i+1,"SYN dispatch did not rotate fairly");manager.dispatched(*next,1);auto reply=response(peer,100+i,200+i);reply.flags|=st::Syn|st::From;reply.from=*peer.destination();manager.receive(mux(reply,peer,80,static_cast<std::uint16_t>(9000+i)),2);}
  for(unsigned turn=0;turn<12;++turn){auto next=manager.next(3);require(next&&next->operation==turn%4+1&&!next->packet.payload.empty(),"Available payload credit did not rotate fairly");manager.dispatched(*next,3);}
  require(!manager.next(3),"Global scheduler exceeded per-stream initial window");require(!manager.write(1,p::Bytes{1},4),"In-flight payload escaped per-stream TX cap");
  for(unsigned i=0;i<4;++i){for(unsigned sequence=1;sequence<=2;++sequence){auto packet=response(peer,100+i,200+i,sequence,p::Bytes(32768,static_cast<p::Byte>(i)));packet.flags|=st::NoAck;manager.receive(mux(packet,peer,80,static_cast<std::uint16_t>(9000+i)),4);}}
  require(manager.observation().queuedRx==4*st::MaximumBuffered,"Global bounded RX envelope differs");require(manager.cancel(1)&&manager.release(1),"Cancelled stream slot did not reclaim");require(manager.observation(2)->stream.phase==st::Phase::Established,"One stream cancel retired an unrelated peer");
  take(manager.connect(5,105,*peer.destination(),{{9010,80},100,8},5));require(manager.observation().streams==4,"Released slot could not be reused with fresh identity");const auto rejected=manager.observation().rejectedPackets;manager.receive(mux(response(peer,100,200,3,{'X'}),peer,80,9000),6);require(manager.observation().rejectedPackets==rejected+1&&manager.observation(5)->stream.queuedRx==0,"Retired stream packet entered fresh operation");
}
void finiteWaitAndLongActivity(const p::ClientIdentity& local,const p::ClientIdentity& peer){
  st::Manager manager(*local.destination(),{90000,st::IdleAction::Send});establish(manager,local,peer);
  require(manager.wait(1,st::WaitFor::Readable,1,30,10),"Finite read wait refused");manager.tick(40);require(manager.observation(1)->wait->state==st::WaitState::TimedOut&&manager.observation(1)->stream.phase==st::Phase::Established,"Read timeout incorrectly disconnected session");
  require(manager.wait(1,st::WaitFor::Readable,1,30,41),"Settled wait did not rearm");manager.receive(mux(response(peer,100,200,1,{'A'}),peer,80,9000),50);require(manager.observation(1)->wait->state==st::WaitState::Ready&&take(manager.read(1,1,50))==p::Bytes{'A'},"Actual ordered byte failed readable wait");
  // Activity beyond the legacy120s total budget is legal in explicit manager
  // mode. These pure timestamps do not constitute a real long-lived session.
  manager.receive(mux(response(peer,100,200,2,{'B'}),peer,80,9000),130000);require(manager.observation(1)->stream.phase==st::Phase::Established,"Active manager inherited legacy total120s timeout");
  take(manager.read(1,1,130000));auto keepalive=manager.next(220000);require(keepalive&&keepalive->packet.sequence>0&&keepalive->packet.payload.empty()&&!(keepalive->packet.flags&st::Syn),"Idle send failed original empty sequence-bearing probe");manager.dispatched(*keepalive,220000);require(manager.observation(1)->stream.inFlightPackets==1,"Empty keepalive did not own an ACK/retry slot");
  manager.receive(mux(response(peer,100,200),peer,80,9000),220001);require(manager.observation(1)->stream.inFlightPackets==1,"Unrelated ACK incorrectly acknowledged keepalive");
  st::Manager disconnect(*local.destination(),{10,st::IdleAction::Disconnect});establish(disconnect,local,peer);disconnect.tick(13);require(disconnect.observation(1)->stream.phase==st::Phase::IdleTimeout,"Explicit idle disconnect boundary differs");
}
void originalOneShotIdleJitter(const p::ClientIdentity& local,const p::ClientIdentity& peer){
  std::vector<unsigned> draws;st::Manager manager(*local.destination(),{10,st::IdleAction::Disconnect},[&](unsigned bound){draws.push_back(bound);return bound==10000?37U:0U;});establish(manager,local,peer);require(manager.observation(1)->idleJitterMilliseconds==37&&draws==std::vector<unsigned>{10000},"Original once-per-connection idle entropy differs");require(manager.wakeup()==50,"Original base plus idle jitter deadline differs");manager.tick(49);require(manager.observation(1)->stream.phase==st::Phase::Established,"Idle jitter fired early");manager.tick(50);require(manager.observation(1)->stream.phase==st::Phase::IdleTimeout&&draws.size()==1,"Idle jitter was redrawn on timer turns");
}
void postVerificationFence(const p::ClientIdentity& local,const p::ClientIdentity& peer){
  st::Manager manager(*local.destination(),{});take(manager.connect(1,100,*peer.destination(),{{9000,80},10,8},0));auto syn=manager.next(1);require(bool(syn),"Missing short-budget SYN");manager.dispatched(*syn,1);
  auto reply=response(peer,100,200);reply.flags|=st::Syn|st::From;reply.from=*peer.destination();manager.receive(mux(reply,peer,80,9000),2,[]{return 10;},[]{return true;});require(manager.observation(1)->stream.phase==st::Phase::Timeout,"Decode crossing handshake deadline acquired Established state");
  st::Manager refused(*local.destination(),{});take(refused.listen({{6881,80},{*peer.destination()},8},0));st::Connection remote(1,300,*peer.destination(),*local.destination());auto request=remote.next(1);require(bool(request),"Missing current-gate SYN");refused.receive(mux(*request,peer,80,6881),1,[]{return 2;},[]{return false;});require(refused.observation().pendingSyns==0,"Post-signature source revocation admitted SYN");
  refused.terminate(st::Phase::LeaseExpired);require(!refused.receives()&&refused.observation().pendingSyns==0,"Lease retirement left listener authority");
}
void hardDeadlinesRemainArmedWithoutOutputCredit(const p::ClientIdentity& local,const p::ClientIdentity& peer){
  st::Manager outgoing(*local.destination(),{});take(outgoing.connect(1,100,*peer.destination(),{{9000,80},100,8},0));auto syn=outgoing.next(1);require(bool(syn),"Missing blocked-output SYN");outgoing.dispatched(*syn,1);
  require(outgoing.hardWakeup()==100,"Blocked output hid finite outgoing handshake");require(outgoing.wait(1,st::WaitFor::Readable,1,20,2),"Finite blocked read wait refused");require(outgoing.hardWakeup()==22,"Blocked output hid application wait");outgoing.tick(22);require(outgoing.observation(1)->wait->state==st::WaitState::TimedOut&&outgoing.observation(1)->stream.phase==st::Phase::Connecting,"Blocked wait changed connection lifetime");require(outgoing.hardWakeup()==100,"Settled wait retained a due/spinning deadline");outgoing.tick(100);require(outgoing.observation(1)->stream.phase==st::Phase::Timeout&&!outgoing.hardWakeup(),"Expired outgoing handshake retained a due/spinning deadline");
  st::Manager incoming(*local.destination(),{});take(incoming.listen({{6881,80},{*peer.destination()},8},0));st::Connection remote(1,300,*peer.destination(),*local.destination());auto request=remote.next(1);require(bool(request),"Missing incoming deadline SYN");incoming.receive(mux(*request,peer,80,6881),1);require(incoming.hardWakeup()==3001,"Blocked output hid pending-SYN expiry");take(incoming.accept(2,200,2990));require(incoming.hardWakeup()==3001,"Accepted unsent SYN lost original absolute expiry");incoming.tick(3001);require(incoming.observation(2)->stream.phase==st::Phase::Timeout&&!incoming.hardWakeup(),"Unsent inbound handshake did not expire once");
  st::Manager cancelled(*local.destination(),{});take(cancelled.listen({{6881,80},{*peer.destination()},8},0));cancelled.receive(mux(*request,peer,80,6881),1);take(cancelled.accept(3,201,2));require(cancelled.cancel(3),"Unsent accepted stream cancellation refused");cancelled.tick(4000);require(cancelled.observation(3)->stream.phase==st::Phase::Cancelled&&!cancelled.hardWakeup(),"Later handshake expiry relabelled a cancelled stream");
  st::Manager idle(*local.destination(),{10,st::IdleAction::Disconnect});establish(idle,local,peer);require(idle.hardWakeup()==13,"Blocked output hid idle disconnect");idle.tick(13);require(idle.observation(1)->stream.phase==st::Phase::IdleTimeout&&!idle.hardWakeup(),"Idle disconnect left an expired timer");
}
void preparationCannotExtendAbsoluteHandshake(const p::ClientIdentity& local,const p::ClientIdentity& peer){
  st::Manager prepared(*local.destination(),{});take(prepared.connect(1,100,*peer.destination(),{{9000,80},20,8},110,120));require(prepared.hardWakeup()==120,"Preparation moved API-entry absolute deadline");prepared.tick(119);require(prepared.observation(1)->stream.phase==st::Phase::Connecting,"Preparation budget fired early");prepared.tick(120);require(prepared.observation(1)->stream.phase==st::Phase::Timeout,"Preparation extended handshake lifetime");
  st::Manager expired(*local.destination(),{});require(!expired.connect(1,100,*peer.destination(),{{9000,80},20,8},120,120)&&expired.observation().streams==0,"Expired preparation acquired a stream slot");require(!expired.connect(1,100,*peer.destination(),{{9000,80},20,8},110,131)&&expired.observation().streams==0,"Oversized absolute budget acquired a stream slot");
}
void outgoingUnspecifiedLocalPortRetainsExactRemoteAndReplyTuple(const p::ClientIdentity& local,const p::ClientIdentity& peer){
  st::Manager manager(*local.destination(),{});take(manager.connect(1,100,*peer.destination(),{{0,80},100,8},0));
  auto syn=manager.next(1);require(syn&&syn->operation==1&&(syn->packet.flags&st::Syn)&&syn->ports.local==0&&syn->ports.remote==80,"Original outgoing0-to80 SYN tuple was not retained");
  const auto wire=take(st::encodeMux(mux(syn->packet,local,syn->ports.local,syn->ports.remote)));const auto sent=take(st::decodeMux(wire));
  require(sent.fromPort==0&&sent.toPort==80,"Actual protocol6 mux changed unspecified source port");
  auto decoded=take(st::decodeInitialSyn(sent.packet,*peer.destination()));manager.dispatched(*syn,1);
  auto incoming=take(st::Connection::accept(2,200,*peer.destination(),*local.destination(),decoded,8,1));
  auto reply=incoming.next(2);require(bool(reply),"Missing actual signed80-to0 SYN/ACK");incoming.dispatched(*reply,2);
  const auto rejected=manager.observation().rejectedPackets;
  manager.receive(mux(*reply,peer,80,1),2);manager.receive(mux(*reply,peer,81,0),3);
  require(manager.observation().rejectedPackets==rejected+2&&manager.observation(1)->stream.phase==st::Phase::Connecting,"Unspecified local port became a wildcard for wrong reply ports");
  auto returned=take(st::decodeMux(take(st::encodeMux(mux(*reply,peer,80,0)))));manager.receive(returned,4);
  require(manager.observation(1)->stream.phase==st::Phase::Established,"Exact signed80-to0 response was refused");
  auto ack=manager.next(5);require(ack&&ack->ports.local==0&&ack->ports.remote==80,"Final ACK changed exact outgoing ports");manager.dispatched(*ack,5);incoming.receive(checked(ack->packet,local),6);
  require(incoming.observation().inFlightPackets==0,"Actual final ACK failed to release peer SYN/ACK");
  st::Manager listener(*local.destination(),{});require(!listener.listen({{0,80},{*peer.destination()},8},0)&&!listener.receives(),"Listener acquired zero-port wildcard authority");
  st::Manager remoteZero(*local.destination(),{});require(!remoteZero.connect(1,100,*peer.destination(),{{0,0},100,8},0)&&!remoteZero.connect(1,100,*peer.destination(),{{9000,0},100,8},0)&&remoteZero.observation().streams==0,"Unspecified remote port acquired an outgoing slot");
}
void explicitCapacityIsBoundedAndReclaimsOnlyTheExactOperation(const p::ClientIdentity& local,const p::ClientIdentity& peer){
  st::ManagerOptions policy;policy.maximumStreams=64;st::Manager manager(*local.destination(),policy);
  require(manager.valid()&&manager.maximumStreams()==64,"Explicit shared capacity did not size actual storage");
  for(unsigned i=0;i<64;++i){take(manager.connect(i+1,100+i,*peer.destination(),{{static_cast<std::uint16_t>(9000+i),80},100,8},0));}
  require(manager.observation().streams==64&&!manager.connect(65,164,*peer.destination(),{{9100,80},100,8},0),"Configured shared stream bound was not exact");
  for(unsigned i=0;i<64;++i){auto packet=manager.next(1);require(packet&&packet->operation==i+1&&packet->ports.local==9000+i,"Scheduler did not visit every configured stream fairly");manager.dispatched(*packet,1);}
  auto reply=response(peer,163,263);reply.flags|=st::Syn|st::From;reply.from=*peer.destination();manager.receive(mux(reply,peer,80,9063),2);
  require(manager.observation(64)->stream.phase==st::Phase::Established,"Signed reply beyond original four slots was not demultiplexed");
  require(manager.cancel(17)&&manager.release(17)&&!manager.observation(17)&&manager.observation(64)->stream.phase==st::Phase::Established,"Exact release damaged an unrelated shared operation");
  take(manager.connect(1000,1000,*peer.destination(),{{9999,80},100,8},3));require(manager.observation().streams==64,"Exact reclaimed slot was not reused");
  const auto rejected=manager.observation().rejectedPackets;manager.receive(mux(response(peer,116,216),peer,80,9016),4);
  require(manager.observation().rejectedPackets==rejected+1&&manager.observation(1000)->stream.phase==st::Phase::Connecting,"Old operation reply entered reused shared slot");
  for(unsigned count:{0U,65U}){policy.maximumStreams=count;st::Manager invalid(*local.destination(),policy);require(!invalid.valid()&&invalid.maximumStreams()==0&&!invalid.connect(1,100,*peer.destination(),{{9000,80},100,8},0)&&!invalid.next(0),"Invalid capacity allocated or admitted a stream");}
  policy.maximumStreams=1;st::Manager one(*local.destination(),policy);take(one.connect(1,100,*peer.destination(),{{9000,80},100,8},0));require(!one.connect(2,101,*peer.destination(),{{9001,80},100,8},0),"Small configured capacity was widened");
}
void sharedListenerExtensionPreservesSignedPendingSynAndOriginalScope(const p::ClientIdentity& local,const p::ClientIdentity& peer){
  std::ifstream input(std::filesystem::path(BIGLYBTC_I2P_TEST_KEYS_DIR)/"ed25519.recipient",std::ios::binary);require(bool(input),"Missing additional public Destination fixture");
  auto additional=take(p::decodeDestination(p::Bytes(std::istreambuf_iterator<char>(input),{})));require(additional.hash!=local.destination()->hash&&additional.hash!=peer.destination()->hash,"Additional listener fixture is not distinct");
  std::ifstream otherInput(std::filesystem::path(BIGLYBTC_I2P_TEST_KEYS_DIR)/"dsa.recipient",std::ios::binary);require(bool(otherInput),"Missing second additional public Destination fixture");
  auto third=take(p::decodeDestination(p::Bytes(std::istreambuf_iterator<char>(otherInput),{})));require(third.hash!=local.destination()->hash&&third.hash!=peer.destination()->hash&&third.hash!=additional.hash,"Second additional listener fixture is not distinct");
  st::ManagerOptions policy;policy.maximumStreams=64;st::Manager manager(*local.destination(),policy);take(manager.listen({{6881,80},{*peer.destination()},8},0));
  st::Connection remote(1,300,*peer.destination(),*local.destination());auto syn=remote.next(1);require(bool(syn),"Missing actual signed pending SYN");remote.dispatched(*syn,1);manager.receive(mux(*syn,peer,80,6881),1);
  const auto before=manager.observation();require(before.pendingSyns==1,"Actual signed SYN was not pending before extension");
  take(manager.listen({{6881,80},{*peer.destination(),additional},8},2));const auto after=manager.observation();
  require(after.pendingSyns==before.pendingSyns&&after.pendingSynBytes==before.pendingSynBytes&&after.rejectedPackets==before.rejectedPackets&&manager.hardWakeup()==3001,"Listener extension changed original queued SYN identity/budget");
  require(!manager.listen({{6882,80},{*peer.destination(),additional,third},8},2)&&!manager.listen({{6881,81},{*peer.destination(),additional,third},8},2)&&!manager.listen({{6881,80},{*peer.destination(),additional,third},7},2),"Shared extension changed actual ports/window");
  require(!manager.listen({{6881,80},{additional},8},2)&&!manager.listen({{6881,80},{*peer.destination(),*peer.destination()},8},2),"Shared extension removed an old peer or accepted duplicate full identity");
  take(manager.accept(2,100,3));auto reply=manager.next(4);require(reply&&reply->operation==2&&reply->ports.local==6881&&reply->ports.remote==80,"Preserved SYN was not accepted with exact tuple");manager.dispatched(*reply,4);remote.receive(checked(reply->packet,local),4);
  require(remote.observation().phase==st::Phase::Established&&manager.observation(2)->inbound,"Preserved actual signed handshake lost its original peer");
  st::Manager singleton(*local.destination(),{});take(singleton.listen({{6881,80},{*peer.destination()},8},0));require(!singleton.listen({{6881,80},{*peer.destination(),additional},8},1),"Default singleton listener scope was broadened");
}
void closedOperationMustWaitForRealUnreadBytesAndAckBeforeRelease(const p::ClientIdentity& local,const p::ClientIdentity& peer){
  st::Manager manager(*local.destination(),{});establish(manager,local,peer);require(manager.finishWrite(1,4),"Local FIN refused");
  auto fin=manager.next(4);require(fin&&(fin->packet.flags&st::Close),"Missing actual local FIN");manager.dispatched(*fin,4);
  auto close=response(peer,100,200,1,{'R'});close.flags|=st::Close;close.ackThrough=fin->packet.sequence;manager.receive(mux(close,peer,80,9000),5);
  require(manager.observation(1)->stream.phase==st::Phase::Closed&&manager.observation(1)->stream.queuedRx==1&&!manager.cancel(1)&&!manager.release(1),"Closed unread operation was discarded early");
  require(take(manager.read(1,st::MaximumBuffered,5))==p::Bytes{'R'}&&!manager.release(1),"RX drain fabricated acknowledgement completion");
  auto ack=manager.next(6);require(ack&&ack->operation==1&&ack->packet.ackThrough==1,"Closed FIN lacked its real pending ACK");
  require(!manager.release(1),"Merely selecting ACK falsely released the operation");manager.dispatched(*ack,6);
  require(manager.release(1)&&!manager.observation(1),"Actual Closed ACK dispatch failed to release exact operation");
  take(manager.connect(2,101,*peer.destination(),{{9001,80},100,8},7));require(manager.observation().streams==1,"Released Closed slot was not reusable");
}
int main(){try{auto local=identity("dsa"),peer=identity("ed25519");inboundCodecAndRole(local,peer);listenerBoundsAndExpiry(local,peer);multiplexFairnessAndMemory(local,peer);finiteWaitAndLongActivity(local,peer);originalOneShotIdleJitter(local,peer);postVerificationFence(local,peer);hardDeadlinesRemainArmedWithoutOutputCredit(local,peer);preparationCannotExtendAbsoluteHandshake(local,peer);outgoingUnspecifiedLocalPortRetainsExactRemoteAndReplyTuple(local,peer);explicitCapacityIsBoundedAndReclaimsOnlyTheExactOperation(local,peer);sharedListenerExtensionPreservesSignedPendingSynAndOriginalScope(local,peer);closedOperationMustWaitForRealUnreadBytesAndAckBeforeRelease(local,peer);std::cout<<"PASS bounded session Streaming manager source-authored controls\n";return 0;}catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';return 1;}}
