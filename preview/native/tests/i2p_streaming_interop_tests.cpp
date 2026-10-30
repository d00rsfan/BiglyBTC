// SPDX-License-Identifier: GPL-2.0-or-later
// Explicit original-JAR acceptance target, never a router/backend/default test.
#include "channel.hpp"
#include "stream.hpp"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QScopeGuard>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <algorithm>
#include <deque>
#include <optional>
#include <set>
namespace i2p=biglybtc::services::i2p;
namespace cp=i2p::i2cp;
namespace stream=i2p::streaming;
using Channel=cp::InitialChannel;
namespace {
constexpr auto Request="GET /owned-stream HTTP/1.0\r\nHost: owned.i2p\r\n\r\n";
QByteArray expectedResponse(bool backpressure=false){const int size=backpressure?96*1024:8192;QByteArray b="HTTP/1.0 200 OK\r\nContent-Length: "+QByteArray::number(size)+"\r\nConnection: close\r\n\r\n";for(int i=0;i<size;++i){b.append(static_cast<char>(i*31+7));}return b;}
i2p::View view(const QByteArray& b){return {reinterpret_cast<const i2p::Byte*>(b.constData()),static_cast<std::size_t>(b.size())};}
QByteArray bytes(i2p::View b){return {reinterpret_cast<const char*>(b.data()),static_cast<qsizetype>(b.size())};}
QByteArray readFile(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly)||f.size()<1||f.size()>4096){qFatal("Cannot read bounded synthetic fixture %s: %s",qPrintable(path),qPrintable(f.errorString()));}auto b=f.readAll();if(f.error()!=QFileDevice::NoError){qFatal("Cannot read %s",qPrintable(path));}return b;}
i2p::ClientIdentity signer(const char* family){const auto dir=QString::fromUtf8(BIGLYBTC_I2P_STREAMING_KEYS_DIR);auto d=readFile(dir+'/'+QString::fromLatin1(family)+".sender"),k=readFile(dir+'/'+QString::fromLatin1(family)+".known-test-key");auto id=i2p::makeClientIdentity(view(d),i2p::Bytes(256,0x51),view(k));if(!id){qFatal("Cannot import synthetic signer: %s",id.error().message.c_str());}return std::move(*id);}
struct Bridge {
  // Bounded public headers only: never payload, identities, keys or signatures.
  static constexpr std::size_t PacketTraceCapacity=256;
  QElapsedTimer traceClock;std::deque<QJsonObject> packetTrace;
  std::uint64_t traceDropped=0,traceErrors=0;QJsonObject nativeTeardown;

  QTcpServer server;QPointer<QTcpSocket> socket;QProcess java;QString work,failure;QByteArray input,output,errors;
  QTimer delayedTimer;QElapsedTimer routeClock;bool upload=false;
  struct Delayed {qint64 due=0;i2p::Bytes mux;};std::deque<Delayed> delayed;
  static constexpr qint64 RouteDelayMilliseconds=40;
  std::size_t delayedBytes=0,maximumDelayedBytes=0,maximumDelayedFrames=0,maximumDataFlight=0,firstAckDataFlight=0;
  std::set<std::uint32_t> nativeOutstandingData,nativeDataSequences;
  std::uint64_t delayedJavaFrames=0;
  cp::FrameDecoder decoder;bool greeting=false,leaseSeen=false,closing=false,recovery=false,holdLate=false;
  bool droppedNative=false,retriedNative=false,reorderedJava=false;std::uint32_t droppedSequence=0,droppedSend=0,droppedReceive=0,heldSequence=0,messageId=100;
  bool nativeChoke=false,nativeUnchoke=false,readingResumed=false;
  i2p::Bytes droppedPayload;
  std::optional<i2p::Bytes> heldJava;std::vector<i2p::Bytes> late;std::set<std::uint32_t> javaDataSequences;
  std::uint64_t now=static_cast<std::uint64_t>(QDateTime::currentMSecsSinceEpoch());
  std::uint64_t nativeFrames=0,javaFrames=0,javaDataFrames=0,updates=0;
  QFile outbound,inbound,producedNative,forwardedJava;
  i2p::Destination native=*signer("dsa").destination(),remote=*signer("ed25519").destination();
  explicit Bridge(QString row,QString mode,QString root):work(root+'/'+row),upload(mode=="upload"),recovery(mode=="recovery"),holdLate(mode=="cancel"){
    traceClock.start();
    if(QFileInfo::exists(work)||!QDir().mkdir(work)||!QFile::setPermissions(work,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner)){fail("Fresh private owned row directory required");return;}
    outbound.setFileName(work+"/forwarded-native-to-java.frames");inbound.setFileName(work+"/java-produced.frames");producedNative.setFileName(work+"/native-produced.frames");forwardedJava.setFileName(work+"/forwarded-java-to-native.frames");
    if(!outbound.open(QIODevice::WriteOnly|QIODevice::NewOnly)||!inbound.open(QIODevice::WriteOnly|QIODevice::NewOnly)||!producedNative.open(QIODevice::WriteOnly|QIODevice::NewOnly)||!forwardedJava.open(QIODevice::WriteOnly|QIODevice::NewOnly)){fail("Cannot create owned packet evidence");return;}
    routeClock.start();delayedTimer.setSingleShot(true);
    QObject::connect(&delayedTimer,&QTimer::timeout,&server,[this]{try{flushDelayed();}catch(const std::exception& e){fail(QString::fromUtf8(e.what()));}});
    QObject::connect(&server,&QTcpServer::newConnection,&server,[this]{if(socket){fail("Unexpected second I2CP connection");return;}socket=server.nextPendingConnection();socket->setReadBufferSize(2*static_cast<qint64>(cp::MaximumFrameBytes));QObject::connect(socket,&QTcpSocket::readyRead,&server,[this]{collectNative();});collectNative();});
    QObject::connect(&java,&QProcess::readyReadStandardOutput,&server,[this]{collectJava();});
    QObject::connect(&java,&QProcess::readyReadStandardError,&server,[this]{auto chunk=java.readAllStandardError();if(errors.size()+chunk.size()>65536){fail("Original peer diagnostic output exceeded bound");return;}errors+=chunk;});
    QObject::connect(&java,&QProcess::errorOccurred,&server,[this](QProcess::ProcessError){if(!closing){fail("Original peer process: "+java.errorString());}});
    if(!server.listen(QHostAddress::LocalHost,0)){fail(server.errorString());return;}
    QProcessEnvironment env=QProcessEnvironment::systemEnvironment();for(const auto& key:{"JAVA_TOOL_OPTIONS","_JAVA_OPTIONS","JDK_JAVA_OPTIONS","CLASSPATH"}){env.remove(QString::fromLatin1(key));}env.insert("HOME",work);env.insert("TMPDIR",work);java.setProcessEnvironment(env);java.setWorkingDirectory(work);
    java.setProgram(QString::fromUtf8(BIGLYBTC_I2P_STREAMING_JAVA));
    java.setArguments({"-Xms16m","-Xmx128m","-Djava.awt.headless=true","-Dtime.disabled=true","-Djbigi.enable=false","-Djcpuid.enable=false","-Duser.home="+work,"-Djava.io.tmpdir="+work,"-cp",QString::fromUtf8(BIGLYBTC_I2P_STREAMING_CLASSES)+':'+QString::fromUtf8(BIGLYBTC_I2P_STREAMING_JAR),"StreamingPeer",mode,work,QString::fromUtf8(BIGLYBTC_I2P_STREAMING_KEYS_DIR),QString::fromUtf8(BIGLYBTC_I2P_STREAMING_JAR)});QFile command(work+"/java-command.json");const auto data=QJsonDocument(QJsonObject{{"program",java.program()},{"arguments",QJsonArray::fromStringList(java.arguments())}}).toJson();if(!command.open(QIODevice::WriteOnly|QIODevice::NewOnly)||command.write(data)!=data.size()){fail("Cannot retain actual Java command");return;}command.close();java.start();
  }
  ~Bridge(){closing=true;delayedTimer.stop();QObject::disconnect(&delayedTimer,nullptr,&server,nullptr);QObject::disconnect(&server,nullptr,&server,nullptr);QObject::disconnect(&java,nullptr,&server,nullptr);if(socket){QObject::disconnect(socket,nullptr,&server,nullptr);socket->abort();}if(java.state()!=QProcess::NotRunning){java.kill();const bool reaped=java.waitForFinished(3000);if(!reaped&&failure.isEmpty()){failure="Owned Java child did not join";}}const auto trailing=java.readAllStandardError();if(trailing.size()>65536-errors.size()){if(failure.isEmpty()){failure="Original peer final diagnostic output exceeded bound";}}else{errors+=trailing;}QFile log(work+"/java.stderr");if(log.open(QIODevice::WriteOnly)){const auto count=log.write(errors);if(count!=errors.size()&&failure.isEmpty()){failure="Cannot retain peer diagnostics";}}outbound.close();inbound.close();producedNative.close();forwardedJava.close();QFile summary(work+"/bridge.json");if(summary.open(QIODevice::WriteOnly)){const auto data=QJsonDocument(QJsonObject{{"native_frames",QString::number(nativeFrames)},{"java_frames",QString::number(javaFrames)},{"java_data_frames",QString::number(javaDataFrames)},{"dropped_native",droppedNative},{"retried_native",retriedNative},{"reordered_java",reorderedJava},{"native_choke",nativeChoke},{"native_unchoke",nativeUnchoke},{"failure",failure}}).toJson();const auto count=summary.write(data);if(count!=data.size()){qWarning("Cannot retain streaming bridge summary");}}retainTrace();}
  void tracePacket(const char* direction,const stream::Mux& mux,const stream::Packet& packet)noexcept{try{
    QJsonArray nacks;for(const auto nack:packet.nacks){nacks.append(QString::number(nack));}
    QJsonObject row{{"elapsed_ms",QString::number(traceClock.elapsed())},{"direction",QString::fromLatin1(direction)},
      {"from_port",static_cast<int>(mux.fromPort)},{"to_port",static_cast<int>(mux.toPort)},
      {"send",QString::number(packet.send)},{"receive",QString::number(packet.receive)},
      {"sequence",QString::number(packet.sequence)},{"flags",static_cast<int>(packet.flags)},
      {"ack_through",QString::number(packet.ackThrough)},{"nacks",nacks},
      {"payload_bytes",QString::number(static_cast<qulonglong>(packet.payload.size()))}};
    if(packetTrace.size()==PacketTraceCapacity){packetTrace.pop_front();++traceDropped;}packetTrace.push_back(std::move(row));
  }catch(...){++traceErrors;}}
  void traceMux(const char* direction,i2p::View wire,const i2p::Destination& sender)noexcept{try{
    auto mux=stream::decodeMux(wire);if(!mux){++traceErrors;return;}auto packet=stream::decode(mux->packet,sender);if(!packet){++traceErrors;return;}tracePacket(direction,*mux,*packet);
  }catch(...){++traceErrors;}}
  static QJsonObject traceObservation(const stream::Observation& state){
    return {{"operation",QString::number(state.operation)},{"phase",static_cast<int>(state.phase)},
      {"queued_tx",QString::number(static_cast<qulonglong>(state.queuedTx))},{"queued_rx",QString::number(static_cast<qulonglong>(state.queuedRx))},
      {"in_flight_packets",QString::number(static_cast<qulonglong>(state.inFlightPackets))},{"in_flight_bytes",QString::number(static_cast<qulonglong>(state.inFlightBytes))},
      {"transmitted_packets",QString::number(state.transmittedPackets)},{"retransmissions",QString::number(state.retransmissions)},
      {"rejected_packets",QString::number(state.rejectedPackets)},{"acknowledged_bytes",QString::number(state.acknowledgedBytes)},
      {"rto_ms",static_cast<int>(state.rtoMilliseconds)}};
  }
  void observeTeardown(const Channel& channel)noexcept{try{
    nativeTeardown={{"elapsed_ms",QString::number(traceClock.elapsed())},{"channel_state",static_cast<int>(channel.state())},{"channel_failure",static_cast<int>(channel.failure())},
      {"channel_socket_active",channel.socketActive()},{"channel_deadline_active",channel.deadlineActive()},
      {"java_process_state",static_cast<int>(java.state())},{"java_queued_input_bytes",QString::number(java.bytesToWrite())},
      {"java_available_output_bytes",QString::number(java.bytesAvailable())},
      {"router_queued_bytes",QString::number(socket?socket->bytesToWrite():-1)},{"router_available_bytes",QString::number(socket?socket->bytesAvailable():-1)}};
    const auto state=channel.streamObservation();nativeTeardown.insert("stream_present",state.has_value());if(state){nativeTeardown.insert("stream",traceObservation(*state));}
  }catch(...){++traceErrors;}}
  void retainTrace()noexcept{try{
    QJsonArray rows;for(const auto& row:packetTrace){rows.append(row);}
    const auto data=QJsonDocument(QJsonObject{{"status","DIAGNOSTIC_METADATA_ONLY"},{"capacity",static_cast<int>(PacketTraceCapacity)},
      {"dropped_events",QString::number(traceDropped)},{"diagnostic_errors",QString::number(traceErrors)},
      {"bridge_failure",failure},{"native_teardown",nativeTeardown},{"events",rows}}).toJson();
    QFile file(work+"/packet-metadata-tail.json");if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)||file.write(data)!=data.size()){qWarning("Cannot retain bounded packet metadata tail");}
  }catch(...){qWarning("Cannot allocate bounded packet metadata tail");}}
  void fail(const QString& text){if(failure.isEmpty()){failure=text;}if(socket){socket->abort();}if(java.state()!=QProcess::NotRunning){java.kill();}}
  bool ready()const{return QFileInfo::exists(work+"/peer-ready.json");}
  bool applicationDone()const{return QFileInfo::exists(work+"/peer-application.json");}
  bool resultPass()const{QFile file(work+"/peer-result.json");return file.open(QIODevice::ReadOnly)&&QJsonDocument::fromJson(file.readAll()).object().value("status").toString()=="PASS";}
  bool send(i2p::View frame){if(!socket||socket->state()!=QAbstractSocket::ConnectedState||socket->bytesToWrite()+static_cast<qint64>(frame.size())>256*1024){fail("Owned I2CP output backpressure/state");return false;}if(socket->write(reinterpret_cast<const char*>(frame.data()),static_cast<qint64>(frame.size()))!=static_cast<qint64>(frame.size())){fail("Owned I2CP enqueue failed");return false;}return true;}
  QByteArray frame(i2p::View mux){QByteArray b(4,Qt::Uninitialized);auto n=static_cast<std::uint32_t>(mux.size());for(int i=3;i>=0;--i){b[3-i]=static_cast<char>(n>>(8*i));}b+=bytes(mux);return b;}
  void toJava(i2p::View mux){auto f=frame(mux);if(java.state()!=QProcess::Running||java.bytesToWrite()+f.size()>65536){fail("Original peer input backpressure/state");return;}if(outbound.write(f)!=f.size()||java.write(f)!=f.size()){fail("Original peer packet enqueue/evidence failed");}else{traceMux("native-to-java-enqueued",mux,native);}}
  void deliveredAcknowledgement(i2p::View mux){
    if(!upload){return;}auto decoded=stream::decodeMux(mux);if(!decoded){fail("Wrong delayed peer mux");return;}auto packet=stream::decode(decoded->packet,remote);if(!packet){fail("Wrong delayed actual peer packet");return;}
    if(packet->flags&stream::NoAck){return;}bool first=true;
    for(auto it=nativeOutstandingData.begin();it!=nativeOutstandingData.end();){if(*it<=packet->ackThrough&&std::ranges::find(packet->nacks,*it)==packet->nacks.end()){
      if(first&&firstAckDataFlight==0){firstAckDataFlight=nativeOutstandingData.size();}first=false;it=nativeOutstandingData.erase(it);
    }else{++it;}}
  }
  void delayedDeliver(i2p::View mux){
    if(!upload||nativeDataSequences.empty()){deliver(mux);return;}
    if(delayed.size()==64||mux.size()>65536-delayedBytes){fail("Controlled actual-ACK route buffer exceeded bound");return;}
    delayed.push_back({routeClock.elapsed()+RouteDelayMilliseconds,i2p::Bytes(mux.begin(),mux.end())});delayedBytes+=mux.size();++delayedJavaFrames;
    maximumDelayedBytes=std::max(maximumDelayedBytes,delayedBytes);maximumDelayedFrames=std::max(maximumDelayedFrames,delayed.size());
    if(!delayedTimer.isActive()){delayedTimer.start(static_cast<int>(RouteDelayMilliseconds));}
  }
  void flushDelayed(){
    while(!delayed.empty()&&failure.isEmpty()&&delayed.front().due<=routeClock.elapsed()){
      auto pending=std::move(delayed.front());delayed.pop_front();delayedBytes-=pending.mux.size();deliver(pending.mux);
    }
    if(!delayed.empty()&&failure.isEmpty()){delayedTimer.start(static_cast<int>(std::max<qint64>(0,delayed.front().due-routeClock.elapsed())));}
  }
  void deliver(i2p::View mux){if(closing){return;}deliveredAcknowledgement(mux);if(!failure.isEmpty()){return;}auto payload=cp::encodeMessagePayload({7,messageId++,i2p::Bytes(mux.begin(),mux.end())});if(!payload||!send(*payload)){fail("Cannot wrap real peer Payload31");return;}auto f=frame(mux);if(forwardedJava.write(f)!=f.size()){fail("Cannot retain forwarded peer packet");}traceMux("java-to-native-enqueued",mux,remote);}
  void collectNative(){
    if(!socket){return;}const auto size=socket->bytesAvailable();if(size<0||size>2*static_cast<qint64>(cp::MaximumFrameBytes)-input.size()){fail("Unbounded owned I2CP input");return;}input+=socket->read(size);
    try{if(!greeting){if(input.isEmpty()){return;}if(input.front()!='*'){fail("Wrong actual I2CP greeting");return;}input.remove(0,1);greeting=true;}
      while(!input.isEmpty()&&failure.isEmpty()){
        auto step=decoder.consume(view(input));if(!step){fail(QString::fromStdString(step.error().message));return;}input.remove(0,static_cast<qsizetype>(step->consumed));if(step->frame){nativeFrame(*step->frame);}if(!step->consumed){break;}
      }
    }catch(const std::exception& e){fail(QString::fromUtf8(e.what()));}
  }
  void nativeFrame(const cp::Frame& f){
    if(f.type==cp::GetDateType){auto message=cp::encodeMessage(cp::SetDate{now,std::string("2.13.0")});if(!message||!send(*message)){fail("Cannot send owned SetDate");}return;}
    if(f.type==cp::CreateSessionType||f.type==cp::ReconfigureSessionType){
      auto message=cp::decodeSessionMessage(f);if(!message){fail(QString::fromStdString(message.error().message));return;}
      const auto& config=std::visit([](const auto& x)->const cp::SessionConfig&{return x.config;},*message);
      if(config.destination.wire!=native.wire||!cp::verifySessionConfig(config,now)){fail("Wrong actual signed native session");return;}
      if(f.type==cp::ReconfigureSessionType){const auto* update=std::get_if<cp::ReconfigureSession>(&*message);if(!update||update->sessionId!=7){fail("Wrong actual reconfigure ID");return;}auto ack=cp::encodeMessage(cp::SessionStatus{7,cp::SessionState::Updated});if(!ack||!send(*ack)){fail("Cannot send actual-request UPDATED");}++updates;return;}
      auto ack=cp::encodeMessage(cp::SessionStatus{7,cp::SessionState::Created});cp::RequestedLease l;l.gateway.fill(0x51);l.tunnelId=33;l.expiresMilliseconds=now+600000;auto lease=cp::encodeLeaseRequest({7,{l}});if(!ack||!lease||!send(*ack)||!send(*lease)){fail("Cannot send owned initial session/lease request");}return;
    }
    if(f.type==cp::CreateLeaseSet2Type){auto lease=cp::decodeCreateLeaseSet2(f);if(!lease||lease->sessionId!=7||lease->leaseSet.destination.wire!=native.wire||lease->leaseSet.leases.size()!=1||lease->leaseSet.leases.front().tunnelId!=33){fail("Wrong actual signed/key-paired native LS2");return;}leaseSeen=true;return;}
    if(f.type==cp::SendMessageType||f.type==cp::SendMessageExpiresType){
      auto message=cp::decodeSendDatagram(f);if(!message||message->sessionId!=7||message->recipient.wire!=remote.wire||message->nonce!=0||!leaseSeen){fail("Wrong actual Streaming Send recipient/session/lease");return;}
      auto mux=stream::decodeMux(message->payload);if(!mux||mux->fromPort!=9000||mux->toPort!=80){fail("Wrong native Streaming mux");return;}auto packet=stream::decode(mux->packet,native);if(!packet){fail(QString::fromStdString(packet.error().message));return;}tracePacket("native-produced",*mux,*packet);++nativeFrames;auto produced=frame(message->payload);if(producedNative.write(produced)!=produced.size()){fail("Cannot retain native-produced packet");return;}if(packet->flags&stream::Delay){if(packet->delay>=60001){if(!packet->signatureVerified||!(packet->flags&stream::Signature)){fail("Unverified native choke packet");return;}nativeChoke=true;}else if(nativeChoke&&readingResumed){if(!packet->signatureVerified||!(packet->flags&stream::Signature)){fail("Unverified native unchoke packet");return;}nativeUnchoke=true;}}
      if(upload&&!packet->payload.empty()){
        if(nativeDataSequences.insert(packet->sequence).second){nativeOutstandingData.insert(packet->sequence);maximumDataFlight=std::max(maximumDataFlight,nativeOutstandingData.size());}
      }
      if(recovery&&!packet->payload.empty()){
        if(!droppedNative){droppedNative=true;droppedSequence=packet->sequence;droppedSend=packet->send;droppedReceive=packet->receive;droppedPayload=packet->payload;return;}
        if(packet->sequence==droppedSequence&&!retriedNative){if(packet->send!=droppedSend||packet->receive!=droppedReceive||packet->payload!=droppedPayload){fail("Retransmission changed real stream/payload identity");return;}retriedNative=true;toJava(message->payload);toJava(message->payload);return;}
      }
      toJava(message->payload);return;
    }
    fail(QString("Unexpected actual native I2CP type %1").arg(f.type));
  }
  void collectJava(){
    const auto size=java.bytesAvailable();if(size<0||size>65536-output.size()){fail("Original peer frame buffer bound");return;}output+=java.readAllStandardOutput();
    while(output.size()>=4&&failure.isEmpty()){
      std::uint32_t n=0;for(int i=0;i<4;++i){n=(n<<8)|static_cast<unsigned char>(output.at(i));}if(n<20||n>40960){fail("Wrong actual Java mux frame length");return;}if(output.size()<4+static_cast<qsizetype>(n)){return;}
      auto encoded=output.first(4+n);if(inbound.write(encoded)!=encoded.size()){fail("Cannot retain actual peer packet");return;}auto muxBytes=output.sliced(4,n);output.remove(0,4+n);if(closing){continue;}
      auto mux=stream::decodeMux(view(muxBytes));if(!mux||mux->fromPort!=80||mux->toPort!=9000){fail("Wrong actual Java protocol6 mux");return;}auto packet=stream::decode(mux->packet,remote);if(!packet){fail(QString::fromStdString(packet.error().message));return;}tracePacket("java-produced",*mux,*packet);++javaFrames;
      if(!packet->payload.empty()){
        ++javaDataFrames;const auto inserted=javaDataSequences.insert(packet->sequence).second;
        if(holdLate){if(late.size()==16){fail("Late peer packet capacity");return;}late.emplace_back(view(muxBytes).begin(),view(muxBytes).end());continue;}
        if(recovery&&heldJava&&packet->sequence==heldSequence){continue;}
        if(recovery&&inserted&&!reorderedJava){
          if(javaDataSequences.size()==2){heldSequence=packet->sequence;heldJava=i2p::Bytes(view(muxBytes).begin(),view(muxBytes).end());continue;}
          if(heldJava&&packet->sequence>heldSequence){deliver(view(muxBytes));deliver(*heldJava);heldJava.reset();reorderedJava=true;continue;}
        }
      }
      delayedDeliver(view(muxBytes));
    }
  }
  void releaseLate(){holdLate=false;for(const auto& b:late){deliver(b);}late.clear();}
};
}

class StreamingInteropTests final:public QObject {
  Q_OBJECT
  QString root;
  void registered(Bridge& bridge,Channel& channel){
    QTRY_VERIFY_WITH_TIMEOUT(bridge.ready()||!bridge.failure.isEmpty()||bridge.java.state()==QProcess::NotRunning,60000);QVERIFY2(bridge.ready()&&bridge.failure.isEmpty(),qPrintable(bridge.failure+QString::fromUtf8(bridge.errors)));
    QVERIFY(channel.begin({QHostAddress::LocalHost,bridge.server.serverPort(),"2.13.0",false,30000,false}));QTRY_VERIFY(channel.routerDate());auto id=signer("dsa");auto armed=channel.createLeaseSession(std::move(id),{{"i2cp.fastReceive","true"},{"i2cp.messageReliability","none"}},bridge.now);QVERIFY2(armed,armed?"":armed.error().message.c_str());QTRY_VERIFY(channel.sessionId()&&channel.leaseSetSubmission()&&bridge.leaseSeen);QCOMPARE(channel.state(),Channel::State::SessionCreated);
  }
  void finish(Bridge& bridge,Channel& channel){
    QTRY_VERIFY_WITH_TIMEOUT(bridge.applicationDone()||!bridge.failure.isEmpty(),10000);QVERIFY2(bridge.failure.isEmpty(),qPrintable(bridge.failure));QVERIFY(bridge.applicationDone());bridge.closing=true;channel.close();bridge.java.closeWriteChannel();QTRY_COMPARE_WITH_TIMEOUT(bridge.java.state(),QProcess::NotRunning,10000);bridge.collectJava();const auto last=bridge.java.readAllStandardError();QVERIFY(bridge.errors.size()+last.size()<=65536);bridge.errors+=last;QVERIFY2(bridge.failure.isEmpty(),qPrintable(bridge.failure));QVERIFY2(bridge.java.exitStatus()==QProcess::NormalExit&&bridge.java.exitCode()==0,qPrintable(QString::fromUtf8(bridge.errors)));QVERIFY(bridge.resultPass());QVERIFY(bridge.nativeFrames>0&&bridge.javaFrames>0);
  }
private slots:
  void initTestCase(){
    root=qEnvironmentVariable("BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT");QVERIFY2(QDir::isAbsolutePath(root)&&!QFileInfo::exists(root),"Explicit fresh absolute acceptance output required; no absent-Java skip");QVERIFY(QDir().mkdir(root));QVERIFY(QFile::setPermissions(root,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner));
    const auto jar=QString::fromUtf8(BIGLYBTC_I2P_STREAMING_JAR);QFile file(jar);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.size(),qint64(4288652));QCOMPARE(QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256).toHex(),QByteArray("b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5"));
    const auto classes=QString::fromUtf8(BIGLYBTC_I2P_STREAMING_CLASSES);std::set<QString> actual;QDirIterator entries(classes,{"*.class"},QDir::Files,QDirIterator::Subdirectories);while(entries.hasNext()){actual.insert(QDir(classes).relativeFilePath(entries.next()));}QVERIFY((actual==std::set<QString>{"StreamingPeer.class","StreamingPeer$Adapter.class"}));
  }
  void actualOriginalManagerCarriesHttpAndRecoverablePackets_data(){QTest::addColumn<QString>("mode");QTest::addColumn<bool>("backpressure");QTest::newRow("http")<<QString("http")<<false;QTest::newRow("loss-reorder")<<QString("recovery")<<false;QTest::newRow("queue-backpressure")<<QString("backpressure")<<true;}
  void actualOriginalManagerCarriesHttpAndRecoverablePackets(){
    QFETCH(QString,mode);QFETCH(bool,backpressure);Bridge bridge(QString::fromLatin1(QTest::currentDataTag()),mode,root);Channel channel;const auto diagnostic=qScopeGuard([&]{bridge.observeTeardown(channel);});registered(bridge,channel);QVERIFY2(bridge.failure.isEmpty(),qPrintable(bridge.failure));auto connected=channel.connectStream(bridge.remote.wire,{9000,80,60000});QVERIFY2(connected,connected?"":connected.error().message.c_str());const auto operation=*connected;
    QTRY_VERIFY_WITH_TIMEOUT(channel.streamObservation()&&channel.streamObservation()->phase!=stream::Phase::Connecting,15000);QVERIFY2(bridge.failure.isEmpty(),qPrintable(bridge.failure));QCOMPARE(channel.streamObservation()->phase,stream::Phase::Established);
    if(backpressure){const auto before=channel.streamObservation()->queuedTx;const i2p::Bytes oversized(stream::MaximumBuffered+1,0x44);auto refused=channel.writeStream(operation,oversized);QVERIFY(!refused&&refused.error().code==i2p::ErrorCode::TooLarge);QCOMPARE(channel.streamObservation()->queuedTx,before);}
    auto written=channel.writeStream(operation,view(QByteArray(Request)));QVERIFY2(written,written?"":written.error().message.c_str());QVERIFY(channel.finishStreamWrite(operation));QByteArray received;
    if(backpressure){QTRY_VERIFY_WITH_TIMEOUT(bridge.nativeChoke||!bridge.failure.isEmpty(),30000);QVERIFY2(bridge.failure.isEmpty(),qPrintable(bridge.failure));QVERIFY(bridge.nativeChoke);QVERIFY(channel.streamObservation()&&channel.streamObservation()->queuedRx<=stream::MaximumBuffered);QVERIFY(channel.streamObservation()->queuedRx>stream::MaximumBuffered-stream::DefaultSegment);QVERIFY(channel.streamObservation()->rejectedPackets>0);bridge.readingResumed=true;}
    const auto expected=expectedResponse(backpressure);
    QTRY_VERIFY_WITH_TIMEOUT(([&]{auto part=channel.takeStreamBytes(operation,4096);if(!part){bridge.fail(QString::fromStdString(part.error().message));return true;}received+=bytes(*part);if(received.size()>expected.size()){bridge.fail("Unexpected application bytes/duplicates");return true;}return received.size()==expected.size()||!bridge.failure.isEmpty();})(),60000);
    QVERIFY2(bridge.failure.isEmpty(),qPrintable(bridge.failure+QString::fromUtf8(bridge.errors)));QVERIFY2(received==expected,"Exact HTTP-shaped response differs");QTRY_VERIFY_WITH_TIMEOUT(channel.streamObservation()&&(channel.streamObservation()->phase==stream::Phase::ReadEof||channel.streamObservation()->phase==stream::Phase::Closed),10000);if(backpressure){QVERIFY(bridge.nativeUnchoke);}
    QVERIFY(bridge.javaDataSequences.size()>1);QVERIFY(!channel.receiverObservation().enabled);QCOMPARE(channel.receiverObservation().queued,std::size_t(0));if(mode=="recovery"){QVERIFY(bridge.droppedNative&&bridge.retriedNative&&bridge.reorderedJava);QVERIFY(channel.streamObservation()->retransmissions>0);}
    finish(bridge,channel);
  }
  void actualOriginalManagerReceives128KiBWithBoundedNativeWindow_data(){
    QTest::addColumn<unsigned>("maximumWindow");QTest::newRow("cap2")<<2U;QTest::newRow("cap8")<<8U;
  }
  void actualOriginalManagerReceives128KiBWithBoundedNativeWindow(){
    QFETCH(unsigned,maximumWindow);Bridge bridge(QString("upload-")+QString::fromLatin1(QTest::currentDataTag()),"upload",root);Channel channel;const auto diagnostic=qScopeGuard([&]{bridge.observeTeardown(channel);});registered(bridge,channel);QVERIFY2(bridge.failure.isEmpty(),qPrintable(bridge.failure));
    Channel::StreamOptions options{9000,80,60000};options.maximumWindow=maximumWindow;auto connected=channel.connectStream(bridge.remote.wire,options);QVERIFY2(connected,connected?"":connected.error().message.c_str());const auto operation=*connected;
    QTRY_VERIFY_WITH_TIMEOUT(channel.streamObservation()&&channel.streamObservation()->phase!=stream::Phase::Connecting,15000);QVERIFY2(bridge.failure.isEmpty(),qPrintable(bridge.failure));QCOMPARE(channel.streamObservation()->phase,stream::Phase::Established);QCOMPARE(channel.streamObservation()->maximumWindow,maximumWindow);
    constexpr qsizetype BodySize=128*1024;QByteArray body(BodySize,Qt::Uninitialized);for(qsizetype i=0;i<body.size();++i){body[i]=static_cast<char>(i*17+11);}const auto digest=QCryptographicHash::hash(body,QCryptographicHash::Sha256).toHex();
    const auto header=QByteArray("POST /owned-upload HTTP/1.0\r\nHost: owned.i2p\r\nContent-Length: ")+QByteArray::number(static_cast<qlonglong>(BodySize))+"\r\n\r\n";const auto upload=header+body;const auto digestBody=digest+'\n';const auto expected=QByteArray("HTTP/1.0 200 OK\r\nContent-Length: ")+QByteArray::number(digestBody.size())+"\r\nConnection: close\r\n\r\n"+digestBody;
    qsizetype sent=0;QByteArray received;bool fin=false;std::size_t maximumQueued=0,maximumObservedFlight=0;QElapsedTimer transfer;transfer.start();
    const auto pump=[&]{
      auto observation=channel.streamObservation();if(!observation||observation->operation!=operation){bridge.fail("Upload lost its actual stream operation");return true;}
      maximumQueued=std::max(maximumQueued,observation->queuedTx);maximumObservedFlight=std::max(maximumObservedFlight,observation->inFlightPackets);
      if(observation->queuedTx>stream::MaximumBuffered||observation->inFlightPackets>maximumWindow){bridge.fail("Actual upload exceeded native queue/window bound");return true;}
      while(sent<upload.size()){
        observation=channel.streamObservation();const auto count=std::min<qsizetype>(4096,upload.size()-sent);if(observation->queuedTx+static_cast<std::size_t>(count)>stream::MaximumBuffered){break;}
        const auto write=channel.writeStream(operation,view(upload.sliced(sent,count)));if(!write){bridge.fail(QString::fromStdString(write.error().message));return true;}sent+=count;
        observation=channel.streamObservation();maximumQueued=std::max(maximumQueued,observation->queuedTx);maximumObservedFlight=std::max(maximumObservedFlight,observation->inFlightPackets);
      }
      if(sent==upload.size()&&!fin){if(!channel.finishStreamWrite(operation)){bridge.fail("Actual upload FIN refused");return true;}fin=true;}
      auto part=channel.takeStreamBytes(operation,4096);if(!part){bridge.fail(QString::fromStdString(part.error().message));return true;}received+=bytes(*part);if(received.size()>expected.size()){bridge.fail("Upload response exceeded exact digest body");return true;}
      return received.size()==expected.size()||!bridge.failure.isEmpty();
    };
    QTRY_VERIFY_WITH_TIMEOUT(pump(),60000);const auto elapsedUs=std::max<qint64>(1,transfer.nsecsElapsed()/1000);QVERIFY2(bridge.failure.isEmpty(),qPrintable(bridge.failure+QString::fromUtf8(bridge.errors)));QCOMPARE(received,expected);QCOMPARE(sent,upload.size());QVERIFY(fin);
    QTRY_VERIFY_WITH_TIMEOUT(channel.streamObservation()&&(channel.streamObservation()->phase==stream::Phase::ReadEof||channel.streamObservation()->phase==stream::Phase::Closed),10000);
    const auto observation=*channel.streamObservation();QCOMPARE(observation.operation,operation);QCOMPARE(observation.maximumWindow,maximumWindow);QVERIFY(observation.acknowledgedBytes>=static_cast<std::uint64_t>(upload.size()));QVERIFY(observation.maximumInFlightPackets<=maximumWindow);QVERIFY(maximumObservedFlight<=maximumWindow);QVERIFY(maximumQueued<=stream::MaximumBuffered);
    QVERIFY(bridge.delayedJavaFrames>0);QVERIFY(bridge.maximumDelayedBytes<=65536);QVERIFY(bridge.maximumDelayedFrames<=64);QVERIFY(bridge.nativeDataSequences.size()>8);QVERIFY(bridge.nativeOutstandingData.empty());QVERIFY(bridge.maximumDataFlight<=maximumWindow);
    if(maximumWindow==8){QVERIFY2(bridge.firstAckDataFlight>=3,"Actual peer ACK was delivered before three distinct native data sequences were in flight");QVERIFY(observation.maximumInFlightPackets>=3);}else{QCOMPARE(bridge.firstAckDataFlight,std::size_t{2});QCOMPARE(bridge.maximumDataFlight,std::size_t{2});}
    QTRY_VERIFY_WITH_TIMEOUT(bridge.applicationDone()||!bridge.failure.isEmpty(),10000);QVERIFY2(bridge.failure.isEmpty(),qPrintable(bridge.failure));QVERIFY(bridge.applicationDone());
    QFile peerFile(bridge.work+"/peer-application.json");QVERIFY(peerFile.open(QIODevice::ReadOnly));const auto peer=QJsonDocument::fromJson(peerFile.readAll()).object();QCOMPARE(peer.value("upload_bytes").toInt(),static_cast<int>(BodySize));QCOMPARE(peer.value("upload_sha256").toString(),QString::fromLatin1(digest));QVERIFY(peer.value("actual_manager_socket").toBool());
    const QJsonObject metrics{{"status","PASS"},{"body_bytes",static_cast<int>(BodySize)},{"body_sha256",QString::fromLatin1(digest)},{"maximum_window",static_cast<int>(maximumWindow)},{"fixed_peer_frame_delay_ms",static_cast<int>(Bridge::RouteDelayMilliseconds)},{"elapsed_microseconds",QString::number(elapsedUs)},{"body_bytes_per_second",QString::number(static_cast<qulonglong>(BodySize)*1000000/static_cast<qulonglong>(elapsedUs))},{"acknowledged_bytes",QString::number(observation.acknowledgedBytes)},{"retransmissions",QString::number(observation.retransmissions)},{"actual_maximum_in_flight",static_cast<int>(observation.maximumInFlightPackets)},{"actual_distinct_data_before_first_ack",static_cast<int>(bridge.firstAckDataFlight)},{"actual_maximum_unacknowledged_data",static_cast<int>(bridge.maximumDataFlight)},{"peak_observed_queue_bytes",static_cast<int>(maximumQueued)},{"rto_milliseconds",static_cast<int>(observation.rtoMilliseconds)},{"performance_parity_claimed",false}};
    QVERIFY(!channel.receiverObservation().enabled);QCOMPARE(channel.receiverObservation().queued,std::size_t{0});finish(bridge,channel);QVERIFY(bridge.resultPass());QCOMPARE(bridge.java.state(),QProcess::NotRunning);QCOMPARE(bridge.java.exitCode(),0);
    QFile measured(bridge.work+"/upload-metrics.json");QVERIFY(measured.open(QIODevice::WriteOnly|QIODevice::NewOnly));const auto encoded=QJsonDocument(metrics).toJson();QCOMPARE(measured.write(encoded),encoded.size());measured.close();
  }
  void actualOriginalResetHasTypedNativeTerminal(){
    Bridge bridge("reset","reset",root);Channel channel;const auto diagnostic=qScopeGuard([&]{bridge.observeTeardown(channel);});registered(bridge,channel);auto op=channel.connectStream(bridge.remote.wire,{9000,80,60000});QVERIFY(op);QTRY_VERIFY_WITH_TIMEOUT(channel.streamObservation()&&channel.streamObservation()->phase==stream::Phase::RemoteReset,15000);QVERIFY(!channel.writeStream(*op,view(QByteArray("stale"))));QVERIFY(!channel.takeStreamBytes(*op,1));finish(bridge,channel);
  }
  void localCancelRejectsGenuineLatePeerBytesBeforeUpdatedBarrier(){
    Bridge bridge("cancel","cancel",root);Channel channel;const auto diagnostic=qScopeGuard([&]{bridge.observeTeardown(channel);});registered(bridge,channel);auto op=channel.connectStream(bridge.remote.wire,{9000,80,60000});QVERIFY(op);QTRY_VERIFY_WITH_TIMEOUT(channel.streamObservation()&&channel.streamObservation()->phase==stream::Phase::Established,15000);QVERIFY(channel.writeStream(*op,view(QByteArray("C"))));
    QTRY_VERIFY_WITH_TIMEOUT(!bridge.late.empty()&&bridge.applicationDone(),10000);QVERIFY(channel.cancelStream(*op));QCOMPARE(channel.streamObservation()->phase,stream::Phase::Cancelled);QVERIFY(!channel.takeStreamBytes(*op,1024));QVERIFY(!channel.writeStream(*op,view(QByteArray("stale"))));bridge.releaseLate();QSignalSpy updated(&channel,&Channel::sessionUpdated);bool requested=false;
    QTRY_VERIFY_WITH_TIMEOUT(([&]{if(requested){return true;}auto result=channel.reconfigureLeaseSession({{"outbound.quantity","1"}},bridge.now);if(result){requested=true;return true;}return false;})(),5000);
    QTRY_COMPARE(updated.size(),1);QCOMPARE(bridge.updates,std::uint64_t(1));QCOMPARE(channel.streamObservation()->operation,*op);QCOMPARE(channel.streamObservation()->phase,stream::Phase::Cancelled);QVERIFY(!channel.takeStreamBytes(*op,1024));QVERIFY(!channel.writeStream(*op,view(QByteArray("stale"))));finish(bridge,channel);
  }
};
QTEST_GUILESS_MAIN(StreamingInteropTests)
#include "i2p_streaming_interop_tests.moc"
