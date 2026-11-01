// SPDX-License-Identifier: GPL-2.0-or-later
#include "dht.hpp"
#include "messages.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <filesystem>
namespace i=biglybtc::services::i2p;namespace d=i::dht;namespace cp=i::i2cp;
void require(bool v,const char* why){if(!v){throw std::runtime_error(why);}}
template<class T,class E>T take(std::expected<T,E> v){if(!v){throw std::runtime_error(v.error().message);}return std::move(*v);}
i::Bytes read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);require(bool(f),"Missing test fixture");i::Bytes b((std::istreambuf_iterator<char>(f)),{});require(b.size()<=70000,"Oversize test fixture");return b;}
void write(const std::filesystem::path& p,i::View v){require(!std::filesystem::exists(p),"Refuse existing output");std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(v.data()),std::streamsize(v.size()));require(bool(f),"Cannot write output");}
d::Contact contact(i::Byte fill,std::uint16_t port){d::Contact c;c.hash.fill(fill);c.port=port;c.id=take(d::makeNodeId(c.hash,port,i::Bytes(14,fill)));return c;}
void controls(){
  auto a=contact(0x31,10001),b=contact(0x32,10003);auto wire=take(d::encodeContact(a));require(wire.size()==54,"Contact geometry");auto c=take(d::decodeContact(wire));require(c.id==a.id&&c.hash==a.hash&&c.port==a.port,"Contact exact roundtrip");wire[4]^=1;require(!d::decodeContact(wire),"Bad secure NID accepted");wire.pop_back();require(!d::decodeContact(wire),"Partial contact accepted");
  d::Message q;q.transaction={0,1,2,3,4,5,6,7};q.id=a.id;q.target.fill(0x51);q.method=d::Method::GetPeers;q.noSeed=true;
  auto encoded=take(d::encode(q));auto decoded=take(d::decode(encoded));require(decoded.method==q.method&&decoded.noSeed&&decoded.target==q.target&&decoded.transaction==q.transaction&&decoded.version==4,"Original get_peers fields");
  const std::string duplicate="d1:ad2:id20:123456789012345678902:id20:12345678901234567890e1:q4:ping1:t1:x1:y1:qe";require(!d::decode({reinterpret_cast<const i::Byte*>(duplicate.data()),duplicate.size()}),"Duplicate row accepted");
  d::Message response;response.kind=d::Kind::Reply;response.id=a.id;response.transaction=q.transaction;response.token={0,0xff,0x51};response.hasPeers=true;response.peers={{a.hash,true},{b.hash,false}};
  auto r=take(d::decode(take(d::encode(response))));require(r.peers.size()==2&&r.peers[0].seed&&!r.peers[1].seed&&r.token==response.token,"MSB-first AZ flag bitmap");
  response.hasNodes=true;response.nodes={b};require(!d::encode(response),"Ambiguous values/nodes accepted");response.hasPeers=false;response.peers.clear();require(take(d::decode(take(d::encode(response)))).nodes.size()==1,"Original compact nodes");
  auto mux=take(cp::encodeDhtMuxDatagram({18,10001,10004,encoded},cp::GzipMode::Deflate));require(!cp::decodeMuxDatagram(mux),"General19/20 decoder acquired raw18");require(take(cp::decodeDhtMuxDatagram(mux)).datagram==encoded,"Scoped DHT mux");require(!cp::decodeRawTrackerReply(take(cp::encodeDhtMuxDatagram({18,10001,10004,i::Bytes(1025,0x51)},cp::GzipMode::Deflate))),"Tracker bound widened");mux[mux.size()-8]^=1;require(!cp::decodeDhtMuxDatagram(mux),"DHT CRC error accepted");
  d::Lookup lookup;require(lookup.start(1,{q.target,false,true,120000,3},std::span(&a,1),100),"Start lookup");auto index=lookup.next(100);require(index&&*index==0,"Initial candidate");require(lookup.queued(0,q.transaction,7,100),"Real queue fact");require(!lookup.reply(response,a.port,101),"Reply before actual dispatch accepted");require(lookup.dispatched(7,true,101),"Actual dispatch fact");auto wrong=response;wrong.transaction.back()^=1;require(!lookup.reply(wrong,a.port,102),"Foreign transaction accepted");require(!lookup.reply(response,b.port,102),"Foreign port accepted");require(lookup.reply(response,a.port,103),"Correlated original nodes reply");
  // Announce requires token from that exact completed get_peers, never a new
  // arbitrary peer. Node discovered in nodes reply remains only a candidate.
  lookup.failed(1);index=lookup.next(104);require(index.has_value(),"Finite next candidate");require(index&&*index==0,"Token-owned announce candidate");q.transaction.back()=8;require(lookup.queued(0,q.transaction,8,106),"Announce queue");require(lookup.dispatched(8,true,107),"Announce actual dispatch");d::Message ack;ack.kind=d::Kind::Reply;ack.id=a.id;ack.transaction=q.transaction;require(lookup.reply(ack,a.port,108),"Announce matching reply");require(lookup.snapshot().phase==d::Phase::Complete&&lookup.snapshot().announced==1,"Exact terminal announce proof");
  require(lookup.start(2,{q.target,false,false,100,3},std::span(&a,1),200),"New operation");require(!lookup.reply(ack,a.port,201),"Old reply entered new op");lookup.tick(300);require(lookup.snapshot().phase==d::Phase::TimedOut,"Absolute deadline");
  require(lookup.start(3,{q.target,false,false,100,3},std::span(&a,1),400),"Cancel setup");lookup.cancel();require(!lookup.reply(ack,a.port,401)&&lookup.snapshot().phase==d::Phase::Cancelled,"Cancelled operation resurrected");
  d::Message excess=response;excess.nodes.assign(129,a);require(!d::encode(excess),"Oversize contacts encoded");excess=response;excess.hasNodes=false;excess.hasPeers=true;excess.peers.assign(65,{a.hash,false});require(!d::encode(excess),"Oversize peers encoded");
  std::vector<d::Contact> many;for(unsigned n=1;n<=25;++n){many.push_back(contact(i::Byte(n),std::uint16_t(11000+n)));}
  d::Lookup bounded;require(bounded.start(4,{q.target,false,true,120000,3},std::span(many).first(20),0),"Multi-node announce start");
  for(unsigned step=1;step<70&&bounded.running();++step){
    auto next=bounded.next(step*3);if(!next){break;}auto node=bounded.nodes().at(*next);i::Bytes tx(8,0);tx.back()=i::Byte(step);require(bounded.queued(*next,tx,step,step*3)&&bounded.dispatched(step,true,step*3+1),"Actual multi-node dispatch");
    d::Message reply;reply.kind=d::Kind::Reply;reply.id=node.contact.id;reply.transaction=tx;
    if(node.phase==d::NodePhase::Candidate){reply.token={i::Byte(step)};if(step==1){reply.hasNodes=true;reply.nodes.assign(many.begin()+20,many.end());}}
    require(bounded.reply(reply,node.contact.port,step*3+2),"Actual multi-node response");
  }
  require(bounded.snapshot().phase==d::Phase::Complete&&bounded.snapshot().contacts==25&&bounded.snapshot().announced==20,"Publish must select only20 closest actual token respondents");

}
void interop(const std::filesystem::path& root){
  for(const std::string family:{"dsa","ed25519"}){
    auto sender=take(i::decodeDestination(read(root/(family+".sender"))));auto key=read(root/(family+".key"));
    for(const std::string method:{"ping","find_node","get_peers","announce_peer","nodes","peers","ack"}){
      auto data=read(root/(family+"."+method+".java.krpc"));auto parsed=take(d::decode(data));auto again=take(d::encode(parsed));require(again==data,"Actual original BEncoder KRPC differs");write(root/(family+"."+method+".native.krpc"),again);
      if(method=="get_peers"){
        auto packet=take(i::decode1(read(root/(family+".java17"))));require(packet.sender.wire==sender.wire&&packet.payload==data,"Actual original legacy signature/payload");write(root/(family+".native17"),take(i::encode1(sender.wire,key,data)));
        auto corrupt=read(root/(family+".java17"));corrupt.back()^=1;require(!i::decode1(corrupt),"Legacy signature forgery");
      }
    }
    auto recipient=take(i::decodeDestination(read(root/(family+".recipient"))));
    for(const unsigned protocol:{17U,18U}){
      const auto label=family+".java36."+std::to_string(protocol);auto frame=take(cp::decodeFrame(read(root/label)));require(!cp::decodeSendDatagram(frame),"General Send decoder acquired DHT flags");auto sent=take(cp::decodeDhtSendDatagram(frame));auto mux=take(cp::decodeDhtMuxDatagram(sent.payload));
      require(sent.sessionId==7&&sent.nonce==0&&sent.expirationMilliseconds==1700000060123ULL&&sent.skipLeaseSet==(protocol==18)&&sent.recipient.wire==recipient.wire&&mux.protocol==protocol&&mux.fromPort==10000&&mux.toPort==(protocol==17?10002:10003),"Original DHT Send36 options/ports differ");
      auto body=protocol==17?take(i::decode1(mux.datagram)).payload:mux.datagram;auto krpc=take(d::decode(body));require(krpc.method==(protocol==17?d::Method::GetPeers:d::Method::AnnouncePeer),"Original DHT Send36 body differs");
      auto packet=protocol==17?take(i::encode1(sender.wire,key,body)):body;auto compressed=take(cp::encodeDhtMuxDatagram({i::Byte(protocol),mux.fromPort,mux.toPort,std::move(packet)},cp::GzipMode::Deflate));sent.payload=std::move(compressed);write(root/(family+".native36."+std::to_string(protocol)),take(cp::encodeDhtSendDatagram(sent)));
    }
  }
}
int main(int argc,char** argv){try{if(argc==3&&std::string_view(argv[1])=="--java-interop"){interop(argv[2]);std::cout<<"PASS actual Java KRPC and legacy17 objects\n";return 0;}require(argc==1,"Unknown test arguments");controls();std::cout<<"PASS bounded I2P DHT wire/control\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
