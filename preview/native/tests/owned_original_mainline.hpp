#pragma once
#include "mainline_dht.hpp"
#include "mainline_identity.hpp"
#include "owned_engine_fixture.hpp"
#include <QNetworkDatagram>
#include <QUdpSocket>
#include <QtTest>
#include <array>
#include <map>
#include <sys/stat.h>
#include <string_view>
namespace biglybtc::test::original_mainline {
using namespace biglybtc;
namespace lt=libtorrent;
inline QByteArray str(const QByteArray& value){return QByteArray::number(value.size())+':'+value;}
inline QByteArray dict(const std::map<QByteArray,QByteArray>& values){
  QByteArray result("d");for(const auto& [key,value]:values){result+=str(key);result+=value;}return result+'e';
}
struct Source {
  QTemporaryDir root{QStringLiteral("/tmp/biglybtc-original-wire-identity-XXXXXX")};
  QString profile,leaf;QByteArray bytes;struct stat original{};
  std::shared_ptr<storage::ProfileLease> lease;
  std::shared_ptr<storage::OriginalMainlineIdentity> identity;
  explicit Source(const QByteArray& key){
    test::require(root.isValid(),"Owned original-ID directory");profile=root.filePath("profile");
    test::require(QDir().mkpath(profile+"/plugins/mldht"),"Owned original-ID parent");
    // These generated parents also exercise first creation, which correctly
    // rejects group/other writers. Do not inherit the caller's umask here.
    for(const auto& parent:{profile,profile+"/plugins",profile+"/plugins/mldht"}){
      test::require(QFile::setPermissions(parent,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner),"Private owned original-ID parent");
    }
    leaf=profile+"/plugins/mldht/baseID.config";bytes="# retained original\r\n"+key.toHex().toUpper()+"\r\n# future\n";
    QFile file(leaf);test::require(file.open(QIODevice::WriteOnly),"Owned original-ID file");
    test::require(file.write(bytes)==bytes.size()&&file.flush(),"Owned original-ID publish");file.close();
    test::require(::lstat(QFile::encodeName(leaf).constData(),&original)==0,"Owned original-ID stat");
    auto held=storage::ProfileLease::acquire(profile.toStdString());test::require(held.has_value(),"Owned original-ID lease");
    lease=std::make_shared<storage::ProfileLease>(std::move(*held));
    auto observed=storage::OriginalMainlineIdentity::load(*lease,"mldht");test::require(observed.has_value(),"Actual original-ID reader");
    identity=std::make_shared<storage::OriginalMainlineIdentity>(std::move(*observed));
  }
  bool unchanged()const{
    struct stat after{};if(::lstat(QFile::encodeName(leaf).constData(),&after)){return false;}
    QFile file(leaf);if(!file.open(QIODevice::ReadOnly)){return false;}
    return file.readAll()==bytes&&file.error()==QFileDevice::NoError&&after.st_dev==original.st_dev
        &&after.st_ino==original.st_ino&&after.st_mode==original.st_mode&&after.st_size==original.st_size
        &&after.st_mtim.tv_sec==original.st_mtim.tv_sec&&after.st_mtim.tv_nsec==original.st_mtim.tv_nsec
        &&after.st_ctim.tv_sec==original.st_ctim.tv_sec&&after.st_ctim.tv_nsec==original.st_ctim.tv_nsec;
  }
};
// Exact expected ORIGINAL local ID, including zero, is checked on each real
// datagram. Remote replies retain a distinct nonzero ID. No public addresses.
struct Krpc final : QObject {
  QUdpSocket socket;QByteArray expected;unsigned short sourcePort=0;int queries=0,gets=0,scrapes=0,bootstraps=0;QString failure;
  explicit Krpc(bool v6,QByteArray key):expected(std::move(key)){
    test::require(socket.bind(v6?QHostAddress::LocalHostIPv6:QHostAddress::LocalHost,0,QUdpSocket::DontShareAddress),"Owned exact-ID KRPC bind");
    QObject::connect(&socket,&QUdpSocket::readyRead,this,[this]{
      while(socket.hasPendingDatagrams()){
        const auto packet=socket.receiveDatagram();auto parsed=core::Document::parse(packet.data().toStdString());
        if(!parsed){failure="Malformed owned KRPC";continue;}
        const auto* arguments=parsed->find("a");const auto* query=parsed->find("q");const auto* transaction=parsed->find("t");
        const auto* id=arguments?arguments->find("id"):nullptr;
        if(!id||id->type!=core::Node::Type::Bytes||QByteArray(id->bytes.data(),static_cast<qsizetype>(id->bytes.size()))!=expected
            ||!query||query->type!=core::Node::Type::Bytes||!transaction||transaction->type!=core::Node::Type::Bytes
            ||packet.senderPort()!=sourcePort||packet.senderAddress()!=socket.localAddress()){
          failure="Actual KRPC source or exact original local ID differs";continue;
        }
        if(query->bytes!="ping"&&query->bytes!="find_node"&&query->bytes!="get_peers"){
          failure="Unexpected query in exact-ID lookup fixture";continue;
        }
        ++queries;std::map<QByteArray,QByteArray> response{{"id",str(QByteArray(20,'K'))},{"nodes",str({})}};
        if(query->bytes=="get_peers"){
          const auto* requested=arguments->find("info_hash");
          const auto* bootstrap=arguments->find("bs");
          if(bootstrap){
            // LT refresh.cpp bootstrap::invoke uses the current local ID with
            // only its final8 bytes secretized, explicitly marked bs=1. This
            // is routing bootstrap, never the caller's requested info hash.
            if(bootstrap->type!=core::Node::Type::Integer||bootstrap->integer!=1
                ||!requested||requested->type!=core::Node::Type::Bytes||requested->bytes.size()!=20
                ||requested->bytes.substr(0,12)!=std::string_view(expected.constData(),12)
                ||arguments->find("scrape")||arguments->find("noseed")){
              failure="Exact-ID bootstrap used the wrong marker or local-ID prefix";continue;
            }
            ++bootstraps; // Nodes-only response below: no peers, token, gets or scrape credit.
          }else{
            if(!requested||requested->type!=core::Node::Type::Bytes||requested->bytes!=std::string(20,'h')){
              failure="Exact-ID lookup used the wrong info hash";continue;
            }
          QByteArray compact;if(packet.senderAddress().protocol()==QAbstractSocket::IPv4Protocol){compact=QByteArray::fromHex("7f000001");}
          else{compact=QByteArray(15,'\0')+'\1';}
          if(const auto* scrape=arguments->find("scrape")){
            const auto* noseed=arguments->find("noseed");
            if(scrape->type!=core::Node::Type::Integer||scrape->integer!=1||!noseed
                ||noseed->type!=core::Node::Type::Integer||noseed->integer!=0){failure="Wrong actual scrape/noseed request";continue;}
            ++scrapes;QByteArray filter(256,'\0');filter[0]=3;filter[7]=8;
            response["BFse"]=str(filter);response["BFpe"]=str(filter);compact+=QByteArray::fromHex("0001");
          }else{++gets;compact+=QByteArray::fromHex("3039");}
          response["token"]=str("owned");response["values"]='l'+str(compact)+'e';
          }
        }
        const auto result=dict({{"r",dict(response)},{"t",str(QByteArray(transaction->bytes.data(),static_cast<qsizetype>(transaction->bytes.size())))},{"y",str("r")}});
        if(socket.writeDatagram(result,packet.senderAddress(),packet.senderPort())!=result.size()){failure="Owned exact-ID reply failed";}
      }
    });
  }
};
inline unsigned short availablePort(bool v6){QUdpSocket socket;test::require(socket.bind(v6?QHostAddress::LocalHostIPv6:QHostAddress::LocalHost,0,QUdpSocket::DontShareAddress),"Owned DHT port");return socket.localPort();}
inline engine::NetworkPolicy policy(bool v6){
  engine::NetworkPolicy result;result.mode=engine::NetworkMode::Normal;result.dht=true;result.ipv6=v6;
  result.mainlineIpv4=!v6;result.mainlineIpv6=v6;result.bindAddresses={v6?"::1":"127.0.0.1"};return result;
}
inline engine::MainlineDhtOptions options(Source& source,bool v6){
  engine::MainlineDhtOptions result;result.port=availablePort(v6);result.ipv4=!v6;result.ipv6=v6;
  result.bootstrapNodes.clear();result.originalIdentity=source.identity;result.originalIdentityLease=source.lease;return result;
}
}
