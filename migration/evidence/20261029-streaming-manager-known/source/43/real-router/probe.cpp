// Explicit, isolated acceptance subject; never an application backend.
#include "channel.hpp"
#include "stream.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <QElapsedTimer>
#include <openssl/crypto.h>
#include <iostream>
#include <stdexcept>
namespace i2p=biglybtc::services::i2p;
namespace cp=i2p::i2cp;
namespace stream=i2p::streaming;
namespace {
QByteArray read(const QString& path,qint64 bound){
  QFile f(path);if(!f.open(QIODevice::ReadOnly)||f.size()<1||f.size()>bound){throw std::runtime_error(("Cannot open bounded "+path+": "+f.errorString()).toStdString());}
  auto data=f.readAll();if(f.error()!=QFileDevice::NoError){throw std::runtime_error(("Cannot read "+path+": "+f.errorString()).toStdString());}return data;
}
i2p::View view(const QByteArray& b){return {reinterpret_cast<const i2p::Byte*>(b.constData()),static_cast<std::size_t>(b.size())};}
void write(const QString& path,const QJsonObject& value){
  QSaveFile out(path);const auto bytes=QJsonDocument(value).toJson();if(!out.open(QIODevice::WriteOnly)||out.write(bytes)!=bytes.size()||!out.commit()){throw std::runtime_error(("Cannot write "+path).toStdString());}
}
QString hex(const i2p::Hash& h){return QByteArray(reinterpret_cast<const char*>(h.data()),static_cast<qsizetype>(h.size())).toHex();}
}
int main(int argc,char** argv){
  QCoreApplication app(argc,argv);const auto args=app.arguments();
  if(args.size()!=3){std::cerr<<"probe PORT OWNED_WORK_DIRECTORY\n";return 2;}
  bool valid=false;const auto port=args[1].toUInt(&valid);if(!valid||port<1024||port>65535){return 2;}
  const auto work=args[2];QJsonObject result{{"status","FAIL"},{"scope","actual-isolated-router-original-manager-stream"}};
  try{
    auto secret=read(work+"/native.keys",1024);auto identity=i2p::decodeClientIdentity(view(secret));OPENSSL_cleanse(secret.data(),static_cast<std::size_t>(secret.size()));
    if(!identity){throw std::runtime_error(identity.error().message);}
    const auto local=identity->destination()->hash;auto remoteBytes=read(work+"/tracker.dest",1024);auto remote=i2p::decodeDestination(view(remoteBytes));
    if(!remote||remote->hash==local){throw std::runtime_error("Streaming peer needs a distinct actual Destination");}
    cp::InitialChannel channel;QTimer poll;QElapsedTimer elapsed;elapsed.start();bool creating=false,leaseReported=false,started=false,written=false,done=false;QByteArray received;
    const QByteArray request="GET /owned-stream HTTP/1.0\r\nHost: owned.i2p\r\n\r\n";
    QByteArray expected="HTTP/1.0 200 OK\r\nContent-Length: 8192\r\nConnection: close\r\n\r\n";for(int i=0;i<8192;++i){expected.append(static_cast<char>(i*31+7));}
    std::optional<std::uint64_t> operation;int code=1;
    auto finish=[&](bool ok,const QString& reason){if(done){return;}done=true;poll.stop();channel.close();result.insert("status",ok?"PASS":"FAIL");result.insert("reason",reason);result.insert("elapsed_ms",elapsed.elapsed());result.insert("native_destination_sha256",hex(local));result.insert("tracker_destination_sha256",hex(remote->hash));try{write(work+"/native-result.json",result);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';ok=false;}code=ok?0:1;app.quit();};
    QObject::connect(&poll,&QTimer::timeout,&app,[&]{try{
      if(elapsed.elapsed()>110000){finish(false,"Absolute native acceptance deadline");return;}
      if(channel.state()==cp::InitialChannel::State::Failed){finish(false,QString("Channel failure %1").arg(static_cast<int>(channel.failure())));return;}
      if(!creating&&channel.routerDate()){
        creating=true;cp::SessionOptions opts{{"inbound.length","0"},{"outbound.length","0"},{"inbound.lengthVariance","0"},{"outbound.lengthVariance","0"},{"inbound.quantity","1"},{"outbound.quantity","1"},{"inbound.backupQuantity","0"},{"outbound.backupQuantity","0"},{"inbound.allowZeroHop","true"},{"outbound.allowZeroHop","true"},{"i2cp.leaseSetType","3"},{"i2cp.leaseSetEncType","0"},{"i2cp.dontPublishLeaseSet","false"},{"i2cp.fastReceive","true"},{"i2cp.messageReliability","none"}};
        auto created=channel.createLeaseSession(std::move(*identity),opts,channel.routerDate()->milliseconds);if(!created){finish(false,QString::fromStdString(created.error().message));return;}
      }
      if(!leaseReported&&channel.sessionId()&&channel.leaseSetSubmission()){
        leaseReported=true;const auto lease=*channel.leaseSetSubmission();write(work+"/native-lease.json",{{"session_id",*channel.sessionId()},{"destination_sha256",hex(local)},{"revision",QString::number(lease.revision)},{"expires_ms",QString::number(lease.expiresMilliseconds)}});
      }
      // This marker is an explicit acceptance-fixture cache precondition, NOT
      // production discovery authority. Streaming uses the explicit full Destination.
      // The unchanged router helper also proves both accepted LS2/zero-hop pools.
      if(leaseReported&&!started&&QFile::exists(work+"/cache-precondition.json")){
        const auto marker=QJsonDocument::fromJson(read(work+"/cache-precondition.json",8192)).object();if(marker.value("native_destination_sha256").toString()!=hex(local)||marker.value("tracker_destination_sha256").toString()!=hex(remote->hash)||marker.value("status").toString()!="ACTUAL_SIGNED_LS2_CACHE_PRECONDITION"){finish(false,"Wrong explicit cache precondition");return;}
        started=true;auto issued=channel.connectStream(remote->wire,{9000,80,60000});
        if(!issued){finish(false,QString::fromStdString(issued.error().message));return;}operation=*issued;
      }
      const auto observation=channel.streamObservation();if(operation&&observation&&observation->operation==*operation){
        if(!written&&observation->phase==stream::Phase::Established){
          auto sent=channel.writeStream(*operation,view(request));if(!sent||!channel.finishStreamWrite(*operation)){finish(false,"Cannot enqueue actual Streaming request/FIN");return;}written=true;
        }
        if(written){auto part=channel.takeStreamBytes(*operation,4096);if(!part){finish(false,QString::fromStdString(part.error().message));return;}received.append(reinterpret_cast<const char*>(part->data()),static_cast<qsizetype>(part->size()));if(received.size()>expected.size()){finish(false,"Unbounded/duplicate Streaming bytes");return;}}
        if((observation->phase==stream::Phase::ReadEof||observation->phase==stream::Phase::Closed)&&received.size()==expected.size()&&QFile::exists(work+"/stream-application.json")){
          const auto peer=QJsonDocument::fromJson(read(work+"/stream-application.json",4096)).object();if(peer.value("status").toString()!="PASS"||!peer.value("external_i2cp").toBool()||peer.value("response_body_bytes").toInt()!=8192){finish(false,"Wrong actual Java application completion marker");return;}
          const auto rx=channel.receiverObservation();if(received!=expected||observation->transmittedPackets<3||rx.enabled||rx.queued!=0){finish(false,"Wrong real HTTP bytes/packet evidence or receiver isolation");return;}
          result.insert("response_bytes",static_cast<int>(received.size()));result.insert("body_bytes",8192);result.insert("request_bytes",static_cast<int>(request.size()));result.insert("transmitted_packets",QString::number(observation->transmittedPackets));result.insert("acknowledged_bytes",QString::number(observation->acknowledgedBytes));result.insert("phase",static_cast<int>(observation->phase));result.insert("general_receiver_enabled",false);finish(true,"Exact real-router Streaming bytes and peer EOF");return;
        }
        if(observation->phase!=stream::Phase::Connecting&&observation->phase!=stream::Phase::Established&&observation->phase!=stream::Phase::ReadEof&&observation->phase!=stream::Phase::Closed){finish(false,QString("Streaming terminal %1").arg(static_cast<int>(observation->phase)));return;}
      }
    }catch(const std::exception& e){finish(false,QString::fromUtf8(e.what()));}catch(...){finish(false,"Unexpected acceptance resource failure");}});
    if(!channel.begin({QHostAddress::LocalHost,static_cast<quint16>(port),"2.13.0",false,30000,false})){throw std::runtime_error("Explicit loopback channel refused begin");}
    poll.start(20);app.exec();return code;
  }catch(const std::exception& e){result.insert("reason",QString::fromUtf8(e.what()));try{write(work+"/native-result.json",result);}catch(...){}std::cerr<<e.what()<<'\n';return 1;}
}
