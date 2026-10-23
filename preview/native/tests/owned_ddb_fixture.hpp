#pragma once
#include "owned_transport.hpp"
#include <QDateTime>
#include <QNetworkDatagram>
#include <QNetworkProxy>
#include <stdexcept>
#include <algorithm>

namespace biglybtc::test {
namespace ddb=services::azureus_udp;
inline services::azureus_dht::Address ddbAddress(const ddb::Peer& peer){
  services::azureus_dht::Address value;value.port=peer.port;
  if(peer.address.protocol()==QAbstractSocket::IPv4Protocol){value.length=4;const auto address=peer.address.toIPv4Address();for(unsigned i=0;i<4;++i){value.bytes[i]=static_cast<std::uint8_t>(address>>(24-8*i));}}
  else{value.length=16;const auto address=peer.address.toIPv6Address();std::copy_n(address.c,16,value.bytes.begin());}return value;
}
inline ddb::lookup::nodes::Snapshot ddbContact(const ddb::Peer& peer){return {true,55,ddbAddress(peer)};}
// A generated, explicitly owned numeric-loopback responder. All controller
// inputs come through actual encoded UDP replies; this fixture has no borrowed
// profile, DB, transport/controller mock or public network. Literal returned
// values are test data, not claims of remotely authenticated/replicated storage.
class OwnedDdbPeer final {
public:
  explicit OwnedDdbPeer(const char* address){
    socket_.setProxy(QNetworkProxy::NoProxy);
    if(!socket_.bind(QHostAddress(QString::fromLatin1(address)),0,QUdpSocket::DontShareAddress)){throw std::runtime_error("Cannot bind generated DDB peer");}
    QObject::connect(&socket_,&QUdpSocket::readyRead,&socket_,[this]{consume();});
  }
  ddb::Peer endpoint()const{return {socket_.localAddress(),socket_.localPort()};}
  void contacts(std::vector<ddb::Peer> peers){mode_=Mode::Contacts;contacts_.clear();for(const auto& peer:peers){contacts_.push_back(ddbContact(peer));}}
  void values(std::vector<std::vector<std::string>> pieces){mode_=Mode::Values;pieces_=std::move(pieces);}
  void silent(){mode_=Mode::Silent;}
  std::size_t received()const noexcept{return received_;}
  bool valid()const noexcept{return valid_;}
  const std::vector<std::string>& targets()const noexcept{return targets_;}
  const std::vector<QByteArray>& replies()const noexcept{return replies_;}
private:
  enum class Mode {Contacts,Values,Silent};
  void consume()noexcept{
    try{
      while(socket_.hasPendingDatagrams()){
        const auto datagram=socket_.receiveDatagram(8192);if(!datagram.isValid()||received_==64){valid_=false;return;}
        const auto raw=datagram.data();ddb::lookup::Context context;context.header.network=4;context.header.now=QDateTime::currentMSecsSinceEpoch();
        auto request=ddb::lookup::decode(ddb::packet::udp::Kind::Request,std::string(raw.constData(),static_cast<std::size_t>(raw.size())),context,cache_);
        if(!request){valid_=false;return;}
        const auto* body=std::get_if<ddb::lookup::RequestBody>(&request->body());if(!body){valid_=false;return;}
        ++received_;targets_.push_back(body->target);if(mode_==Mode::Silent){continue;}
        const auto& incoming=request->header().fields();
        auto send=[&](bool continuation,const std::vector<std::string>* strings){
          ddb::lookup::ReplyFields fields;fields.header.network=4;fields.header.localVersion=55;fields.header.remoteVersion=incoming.originatorVersion;
          fields.header.connection=incoming.connection;fields.header.transaction=incoming.transaction;fields.header.now=context.header.now;fields.header.receivedAt=context.header.now;
          fields.continuation=continuation;std::vector<std::optional<ddb::lookup::values::Snapshot>> snapshots;
          if(strings){
            for(const auto& bytes:*strings){ddb::lookup::values::Snapshot value;value.version=1;value.created=1704153600000LL;value.bytes=bytes;value.originator=ddbContact(endpoint());snapshots.emplace_back(value);}
            fields.values=std::span<const std::optional<ddb::lookup::values::Snapshot>>{snapshots};
          }else{
            fields.contacts=std::span<const ddb::lookup::nodes::Snapshot>{contacts_};
            fields.positions=std::span<const std::optional<ddb::lookup::positions::WirePosition>>{};
          }
          auto encoded=ddb::lookup::encodeReply(fields);if(!encoded||encoded->size()>1400){valid_=false;return;}
          const QByteArray reply(encoded->data(),static_cast<qsizetype>(encoded->size()));
          if(socket_.writeDatagram(reply,datagram.senderAddress(),static_cast<quint16>(datagram.senderPort()))!=reply.size()){valid_=false;return;}
          replies_.push_back(reply);
        };
        if(mode_==Mode::Contacts){send(false,nullptr);}
        else{if(pieces_.empty()||pieces_.size()>16){valid_=false;return;}for(std::size_t i=0;i<pieces_.size();++i){send(i+1<pieces_.size(),&pieces_[i]);}}
      }
    }catch(...){valid_=false;}
  }
  QUdpSocket socket_;
  ddb::lookup::nodes::NodeCache cache_;
  Mode mode_=Mode::Contacts;
  bool valid_=true;
  std::size_t received_=0;
  std::vector<ddb::lookup::nodes::Snapshot> contacts_;
  std::vector<std::vector<std::string>> pieces_;
  std::vector<std::string> targets_;
  std::vector<QByteArray> replies_;
};
} // namespace biglybtc::test
