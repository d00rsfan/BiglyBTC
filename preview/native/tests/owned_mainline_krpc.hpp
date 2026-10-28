#pragma once
#include "owned_engine_fixture.hpp"
#include <QNetworkDatagram>
#include <QTcpServer>
#include <QUdpSocket>
#include <map>
#include <optional>
#include <set>
#include <vector>
namespace biglybtc::test::mainline {
namespace lt=libtorrent;
inline QByteArray bytes(std::string_view value){return {value.data(),static_cast<qsizetype>(value.size())};}
inline QByteArray string(const QByteArray& value){return QByteArray::number(value.size())+':'+value;}
inline QByteArray dictionary(const std::map<QByteArray,QByteArray>& values){QByteArray result("d");for(const auto& [key,value]:values){result+=string(key)+value;}return result+'e';}
inline QByteArray list(const std::vector<QByteArray>& values){QByteArray result("l");for(const auto& value:values){result+=value;}return result+'e';}
inline QByteArray hashBytes(const lt::sha1_hash& hash){return {hash.data(),20};}
inline const core::Node* field(const core::Node& value,std::string_view key,core::Node::Type type){const auto* found=value.find(key);return found&&found->type==type?found:nullptr;}
inline QByteArray compact(unsigned short port){QByteArray result;result.append(char(127));result.append(char(0));result.append(char(0));result.append(char(1));result.append(static_cast<char>(port>>8));result.append(static_cast<char>(port&255));return result;}
// Independent KRPC wire fixture. The production service supplies neither its
// bencode nor its expected hash/token/port. Local IDs use LT's unchanged BEP42
// local-address exemption (kademlia/node_id.cpp), not a relaxed DHT setting.
class OwnedKrpc final {
  struct Request { QByteArray transaction,hash; QHostAddress address; unsigned short port=0; bool scrape=false; };
  QUdpSocket socket_;
  QTcpServer decoy_;
  const QByteArray nodeId_=QByteArray(20,'K');
  std::set<QByteArray> allowed_,noPeers_;
  std::vector<Request> heldPings_;
  std::optional<unsigned short> announcePort_;
  bool holdPings_=false;
  std::map<QByteArray,int> gets_,announces_;
  std::map<QByteArray,std::set<QByteArray>> clientIds_;
  std::map<QByteArray,QByteArray> tokens_;
  std::vector<Request> held_;
  QByteArray heldHash_;
  unsigned short peerPort_;
  bool noise_=false;
  QByteArray scrapeSeedKey_="BFse";
  bool scrapeZero_=false,scrapeWrongId_=false;
  bool scrapePeerItems_=true;
  int scrapeGets_=0;
  bool dropScrapes_=false,errorScrapes_=false,duplicateScrapes_=false;
  int scrapeNoSeeds_=0;
  // Independent wire fixture: fixed bit patterns, not production Bloom output.
  QByteArray scrapeFilter()const{QByteArray value(256,'\0');if(!scrapeZero_){value[0]=3;value[7]=8;}return value;}

  void fail(const char* text) { if (failure.isEmpty()) { failure=QString::fromLatin1(text); } }
  QByteArray tokenKey(const Request& request) const {
    return request.hash + request.address.toString().toUtf8() + ':' + QByteArray::number(request.port);
  }
  void send(const Request& request,const QByteArray& packet) {
    if (socket_.writeDatagram(packet,request.address,request.port)!=packet.size()) { fail("Owned KRPC reply write failed"); }
  }
  QByteArray response(const Request& request,const std::map<QByteArray,QByteArray>& result) const {
    return dictionary({{"r",dictionary(result)},{"t",string(request.transaction)},{"y",string("r")}});
  }
  void answerPeers(const Request& request) {
    if(request.scrape){
      if(dropScrapes_){return;}
      if(errorScrapes_){send(request,dictionary({{"e",list({"i203e",string("owned scrape error")})},{"t",string(request.transaction)},{"y",string("e")}}));return;}
      const auto id=scrapeWrongId_?QByteArray(20,'W'):nodeId_;
      std::map<QByteArray,QByteArray> fields{{"id",string(id)},{"nodes",string({})},
          {scrapeSeedKey_,string(scrapeFilter())},{"BFpe",string(scrapeFilter())}};
      // One decoded bogon item exercises the original caller gate, but supplies
      // no permitted direct peer and never grants peer-injection authority.
      if(scrapePeerItems_){fields["values"]=list({string(compact(1))});}
      const auto reply=response(request,fields);
      send(request,reply);if(duplicateScrapes_){send(request,reply);}return;
    }
    const auto token=QCryptographicHash::hash(tokenKey(request),QCryptographicHash::Sha1).first(8);
    tokens_[tokenKey(request)]=token;
    std::vector<QByteArray> peers;
    if(!noPeers_.contains(request.hash)){peers.push_back(string(compact(peerPort_)));}
    if (noise_) {
      // Unsolicited and malformed packets precede a valid owned progress
      // barrier. An unrequested hash in a reply is never lookup authority.
      auto unrelated=request; unrelated.transaction += "unrelated";
      send(unrelated,response(unrelated,{{"id",string(nodeId_)},{"info_hash",string(QByteArray(20,'Z'))},
          {"values",list({string(compact(decoy_.serverPort()))})}}));
      send(request,QByteArray("d1:ri-no-integer-ee"));
      peers.push_back(string({})); peers.push_back(string(QByteArray(5,'x')));
      peers.push_back(string(QByteArray(6,'\0'))); peers.push_back(string(compact(0)));
      ++noiseReplies;
    }
    send(request,response(request,{{"id",string(nodeId_)},{"token",string(token)},{"values",list(peers)}}));
  }
  void receive() {
    while (socket_.hasPendingDatagrams()) {
      const auto packet=socket_.receiveDatagram(4096);
      if (!packet.senderAddress().isLoopback() || packet.senderPort()==0 || packet.data().size()>2048) {
        fail("KRPC query has invalid owned source or size"); continue;
      }
      auto parsed=core::Document::parse(packet.data().toStdString(),{4096,2048,128,8});
      if (!parsed || parsed->root().type!=core::Node::Type::Dictionary) { fail("Invalid KRPC query bencode"); continue; }
      const auto& root=parsed->root();
      const auto* transaction=field(root,"t",core::Node::Type::Bytes);
      const auto* kind=field(root,"y",core::Node::Type::Bytes);
      const auto* query=field(root,"q",core::Node::Type::Bytes);
      const auto* args=field(root,"a",core::Node::Type::Dictionary);
      const auto* id=args?field(*args,"id",core::Node::Type::Bytes):nullptr;
      if (!transaction || transaction->bytes.empty() || transaction->bytes.size()>16 || !kind || kind->bytes!="q"
          || !query || !args || !id || id->bytes.size()!=20 || bytes(id->bytes)==QByteArray(20,'\0')) {
        fail("KRPC query lacks an actual transaction/node identity"); continue;
      }
      const auto sourcePort=packet.senderPort();
      if (sourcePort<1 || sourcePort>65535) { fail("Invalid owned datagram source port"); continue; }
      Request request{bytes(transaction->bytes),{},packet.senderAddress(),static_cast<unsigned short>(sourcePort)};
      sourcePorts.insert(request.port);
      if (query->bytes=="ping") {
        ++bootstrapQueries;
        if(holdPings_){heldPings_.push_back(request);}else{send(request,response(request,{{"id",string(nodeId_)}}));}
      } else if (query->bytes=="find_node") {
        const auto* target=field(*args,"target",core::Node::Type::Bytes);
        if (!target || target->bytes.size()!=20) { fail("Invalid find_node target"); continue; }
        ++bootstrapQueries; send(request,response(request,{{"id",string(nodeId_)},{"nodes",string({})}}));
      } else if (query->bytes=="get_peers" || query->bytes=="announce_peer") {
        const auto* info=field(*args,"info_hash",core::Node::Type::Bytes);
        if (!info || info->bytes.size()!=20 || bytes(info->bytes)==QByteArray(20,'\0')) { fail("Invalid KRPC info_hash"); continue; }
        request.hash=bytes(info->bytes);
        if (query->bytes=="get_peers") {
          // LT can probe a random routing bucket with get_peers. Such a query
          // receives no values; only explicitly registered subjects get peers.
          if (!allowed_.contains(request.hash)) {
            ++bootstrapQueries; send(request,response(request,{{"id",string(nodeId_)},{"nodes",string({})}})); continue;
          }
          if(const auto* scrape=args->find("scrape")){
            const auto* noseed=args->find("noseed");
            if(scrape->type!=core::Node::Type::Integer||scrape->integer!=1||!noseed||noseed->type!=core::Node::Type::Integer||noseed->integer!=scrapeNoSeeds_){fail("Invalid actual scrape/noseed query");continue;}
            request.scrape=true;++scrapeGets_;
          }
          clientIds_[request.hash].insert(bytes(id->bytes)); ++gets_[request.hash];
          if (request.hash==heldHash_) { held_.push_back(request); } else { answerPeers(request); }
        } else {
          const auto* token=field(*args,"token",core::Node::Type::Bytes);
          const auto* port=field(*args,"port",core::Node::Type::Integer);
          const auto* implied=args->find("implied_port");
          const auto issued=tokens_.find(tokenKey(request));
          if (!allowed_.contains(request.hash) || !token || issued==tokens_.end() || bytes(token->bytes)!=issued->second
              || !port || port->integer!=announcePort_.value_or(peerPort_) || announcePort_.value_or(peerPort_)==request.port
              || (implied && (implied->type!=core::Node::Type::Integer || implied->integer!=0))) {
            fail("announce_peer did not use the issued hash/source token and explicit TCP peer port"); continue;
          }
          clientIds_[request.hash].insert(bytes(id->bytes)); ++announces_[request.hash]; send(request,response(request,{{"id",string(nodeId_)}}));
        }
      } else { fail("Unexpected KRPC query outside owned fixture protocol"); }
    }
  }
public:
  QString failure;
  int bootstrapQueries=0,noiseReplies=0;
  std::set<unsigned short> sourcePorts;
  explicit OwnedKrpc(unsigned short peerPort,QHostAddress address=QHostAddress::LocalHost) : peerPort_(peerPort) {
    test::require(peerPort_!=0,"Owned KRPC fixture needs a real TCP peer");
    test::require(socket_.bind(address,0,QUdpSocket::DontShareAddress),"Cannot bind owned KRPC fixture");
    test::require(decoy_.listen(QHostAddress::LocalHost,0),"Cannot bind owned decoy endpoint");
    QObject::connect(&socket_,&QUdpSocket::readyRead,&socket_,[this]{receive();});
  }
  unsigned short port() const { return socket_.localPort(); }
  void peerPort(unsigned short port) { test::require(port!=0,"Actual owned peer port required"); peerPort_=port; }
  std::set<QByteArray> clientIds(const lt::sha1_hash& value) const {
    const auto found=clientIds_.find(hashBytes(value)); return found==clientIds_.end()?std::set<QByteArray>{}:found->second;
  }
  void allow(const lt::sha1_hash& value) { allowed_.insert(hashBytes(value)); }
  void allowWithoutPeers(const lt::sha1_hash& value){allow(value);noPeers_.insert(hashBytes(value));}
  void announcePort(unsigned short value){test::require(value!=0,"Actual owner port required");announcePort_=value;}
  void holdPings(bool value){holdPings_=value;}
  void releasePings(){holdPings_=false;auto rows=std::move(heldPings_);heldPings_.clear();for(const auto& row:rows){send(row,response(row,{{"id",string(nodeId_)}}));}}
  void noise(bool enabled) { noise_=enabled; }
  void hold(const lt::sha1_hash& value) { heldHash_=hashBytes(value); }
  void release() { heldHash_.clear(); auto pending=std::move(held_); held_.clear(); for (const auto& request:pending) { answerPeers(request); } }
  void scrapeReply(QByteArray key="BFse",bool zero=false,bool wrongId=false){scrapeSeedKey_=std::move(key);scrapeZero_=zero;scrapeWrongId_=wrongId;}
  void scrapePeerItems(bool enabled){scrapePeerItems_=enabled;}
  void scrapeNoSeeds(int value){test::require(value==0||value==1,"Invalid owned noseed expectation");scrapeNoSeeds_=value;}
  void dropScrapes(bool value){dropScrapes_=value;}
  void errorScrapes(bool value){errorScrapes_=value;}
  void duplicateScrapes(bool value){duplicateScrapes_=value;}
  int scrapes()const{return scrapeGets_;}
  int gets(const lt::sha1_hash& value) const { const auto it=gets_.find(hashBytes(value)); return it==gets_.end()?0:it->second; }
  int announced(const lt::sha1_hash& value) const { const auto it=announces_.find(hashBytes(value)); return it==announces_.end()?0:it->second; }
};
} // namespace biglybtc::test::mainline
