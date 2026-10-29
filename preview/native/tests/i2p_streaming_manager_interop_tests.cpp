// SPDX-License-Identifier: GPL-2.0-or-later
// Explicit actual modified-manager packet acceptance, never a real router.
#include "channel.hpp"
#include "stream.hpp"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QScopeGuard>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <algorithm>
#include <array>
#include <deque>
#include <set>
#include <vector>
#include <exception>
namespace p=biglybtc::services::i2p;
namespace cp=p::i2cp;
namespace st=p::streaming;
using Channel=cp::InitialChannel;
namespace {
p::View view(const QByteArray& b){return {reinterpret_cast<const p::Byte*>(b.constData()),static_cast<std::size_t>(b.size())};}
QByteArray bytes(p::View b){return {reinterpret_cast<const char*>(b.data()),static_cast<qsizetype>(b.size())};}
QByteArray fixture(const char* family,const char* suffix){QFile file(QString::fromUtf8(BIGLYBTC_I2P_MANAGER_KEYS_DIR)+'/'+QString::fromLatin1(family)+QString::fromLatin1(suffix));if(!file.open(QIODevice::ReadOnly)||file.size()<1||file.size()>1024){qFatal("Missing bounded synthetic manager fixture: %s",qPrintable(file.errorString()));}auto result=file.readAll();if(file.error()!=QFileDevice::NoError){qFatal("Cannot read synthetic manager fixture");}return result;}
p::ClientIdentity signer(const char* family){const auto d=fixture(family,".sender"),k=fixture(family,".known-test-key");auto result=p::makeClientIdentity(view(d),p::Bytes(256,0x51),view(k));if(!result){qFatal("Cannot import generated manager signer: %s",result.error().message.c_str());}return std::move(*result);}
QByteArray request(unsigned round){return QByteArray("GET /manager/")+QByteArray::number(round)+" HTTP/1.1\r\nHost: owned.i2p\r\nConnection: keep-alive\r\n\r\n";}
QByteArray response(unsigned port,unsigned round){QByteArray result="HTTP/1.1 200 OK\r\nContent-Length: 512\r\nConnection: keep-alive\r\n\r\n";for(unsigned i=0;i<512;++i){result.append(static_cast<char>(port+round*13+i));}return result;}
QByteArray handshake(){QByteArray result(68,0);result[0]=19;result.replace(1,19,"BitTorrent protocol");for(qsizetype i=28;i<68;++i){result[i]=static_cast<char>(i*7+3);}return result;}
bool javaRoute(std::uint16_t from,std::uint16_t to){return (from==80&&(to==9000||to==9001||to==9002))||(from==8020&&to==6881);}
bool save(const QString& path,const QJsonObject& object){QFile file(path);const auto data=QJsonDocument(object).toJson();return file.open(QIODevice::WriteOnly|QIODevice::NewOnly)&&file.write(data)==data.size();}
QJsonObject json(const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()>65536){return {};}return QJsonDocument::fromJson(file.readAll()).object();}
struct Bridge {
  // Bounded public headers only: never payload, identities, keys or signatures.
  static constexpr std::size_t PacketTraceCapacity=256;
  QElapsedTimer traceClock;std::deque<QJsonObject> packetTrace;
  std::uint64_t traceDropped=0,traceErrors=0;QJsonObject nativeTeardown;
  std::array<std::uint64_t,4> traceOperations{};

  QTcpServer server;QPointer<QTcpSocket> socket;QProcess java;QString work,failure;QByteArray input,output,errors;
  cp::FrameDecoder decoder;bool greeting=false,closing=false,holdLate=true;std::vector<p::Bytes> late;
  std::uint32_t message=100;std::uint64_t nativeFrames=0,javaFrames=0,leases=0,renewals=0,lateDelivered=0;
  const std::uint64_t now=static_cast<std::uint64_t>(QDateTime::currentMSecsSinceEpoch());
  p::Destination native=*signer("dsa").destination(),remote=*signer("ed25519").destination();
  p::Bytes encryptionPublic;std::array<std::uint64_t,4> fromNative{},fromJava{};
  explicit Bridge(const QString& root):work(root+"/manager"){
    traceClock.start();
    if(QFileInfo::exists(work)||!QDir().mkdir(work)||!QFile::setPermissions(work,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner)){fail("Fresh private row required");return;}
    server.setProxy(QNetworkProxy::NoProxy);
    QObject::connect(&server,&QTcpServer::newConnection,&server,[this]{try{if(socket){fail("Unexpected second router connection");return;}socket=server.nextPendingConnection();socket->setReadBufferSize(2*static_cast<qint64>(cp::MaximumFrameBytes));QObject::connect(socket,&QTcpSocket::readyRead,&server,[this]{try{collectNative();}catch(const std::exception& e){fail(QString::fromUtf8(e.what()));}});collectNative();}catch(const std::exception& e){fail(QString::fromUtf8(e.what()));}});
    QObject::connect(&java,&QProcess::readyReadStandardOutput,&server,[this]{try{collectJava();}catch(const std::exception& e){fail(QString::fromUtf8(e.what()));}});
    QObject::connect(&java,&QProcess::readyReadStandardError,&server,[this]{auto b=java.readAllStandardError();if(b.size()>65536-errors.size()){fail("Java diagnostic bound");return;}errors+=b;});
    QObject::connect(&java,&QProcess::errorOccurred,&server,[this](QProcess::ProcessError){if(!closing){fail("Actual Java peer: "+java.errorString());}});
    if(!server.listen(QHostAddress::LocalHost,0)){fail(server.errorString());return;}
    auto env=QProcessEnvironment::systemEnvironment();for(const auto& key:{"JAVA_TOOL_OPTIONS","_JAVA_OPTIONS","JDK_JAVA_OPTIONS","CLASSPATH"}){env.remove(QString::fromLatin1(key));}env.insert("HOME",work);env.insert("TMPDIR",work);java.setProcessEnvironment(env);java.setWorkingDirectory(work);
    java.setProgram(QString::fromUtf8(BIGLYBTC_I2P_MANAGER_JAVA));java.setArguments({"-Xms16m","-Xmx128m","-XX:ActiveProcessorCount=1","-XX:+UseSerialGC","-Djava.awt.headless=true","-Dtime.disabled=true","-Djbigi.enable=false","-Djcpuid.enable=false","-Duser.home="+work,"-Djava.io.tmpdir="+work,"-cp",QString::fromUtf8(BIGLYBTC_I2P_MANAGER_CLASSES)+':'+QString::fromUtf8(BIGLYBTC_I2P_MANAGER_JAR),"ManagerPeer",work,QString::fromUtf8(BIGLYBTC_I2P_MANAGER_KEYS_DIR),QString::fromUtf8(BIGLYBTC_I2P_MANAGER_JAR)});
    if(!save(work+"/java-command.json",{{"program",java.program()},{"arguments",QJsonArray::fromStringList(java.arguments())}})){fail("Cannot retain actual Java command");return;}java.start();
  }
  ~Bridge(){closing=true;QObject::disconnect(&server,nullptr,&server,nullptr);QObject::disconnect(&java,nullptr,&server,nullptr);if(socket){QObject::disconnect(socket,nullptr,&server,nullptr);socket->abort();}if(java.state()!=QProcess::NotRunning){java.kill();if(!java.waitForFinished(3000)&&failure.isEmpty()){failure="Actual child not joined";}}collectErrors();QFile file(work+"/java.stderr");if(file.open(QIODevice::WriteOnly)){const auto written=file.write(errors);if(written!=errors.size()){qWarning("Cannot retain final Java diagnostic bytes");}}if(!save(work+"/bridge.json",{{"failure",failure},{"native_frames",QString::number(nativeFrames)},{"java_frames",QString::number(javaFrames)},{"leases",QString::number(leases)},{"renewals",QString::number(renewals)},{"late_delivered",QString::number(lateDelivered)}})){qWarning("Cannot retain manager bridge summary");}retainTrace();}
  void tracePacket(const char* direction,const st::Mux& mux,const st::Packet& packet)noexcept{try{
    QJsonArray nacks;for(const auto nack:packet.nacks){nacks.append(QString::number(nack));}
    QJsonObject row{{"elapsed_ms",QString::number(traceClock.elapsed())},{"direction",QString::fromLatin1(direction)},
      {"from_port",static_cast<int>(mux.fromPort)},{"to_port",static_cast<int>(mux.toPort)},
      {"send",QString::number(packet.send)},{"receive",QString::number(packet.receive)},
      {"sequence",QString::number(packet.sequence)},{"flags",static_cast<int>(packet.flags)},
      {"ack_through",QString::number(packet.ackThrough)},{"nacks",nacks},
      {"payload_bytes",QString::number(static_cast<qulonglong>(packet.payload.size()))}};
    if(packetTrace.size()==PacketTraceCapacity){packetTrace.pop_front();++traceDropped;}packetTrace.push_back(std::move(row));
  }catch(...){++traceErrors;}}
  void traceMux(const char* direction,p::View wire,const p::Destination& sender)noexcept{try{
    auto mux=st::decodeMux(wire);if(!mux){++traceErrors;return;}auto packet=st::decode(mux->packet,sender);if(!packet){++traceErrors;return;}tracePacket(direction,*mux,*packet);
  }catch(...){++traceErrors;}}
  static QJsonObject traceObservation(const st::Observation& state){
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
    const auto aggregate=channel.streamManagerObservation();nativeTeardown.insert("manager",QJsonObject{{"streams",QString::number(static_cast<qulonglong>(aggregate.streams))},{"pending_syns",QString::number(static_cast<qulonglong>(aggregate.pendingSyns))},{"queued_tx",QString::number(static_cast<qulonglong>(aggregate.queuedTx))},{"queued_rx",QString::number(static_cast<qulonglong>(aggregate.queuedRx))},{"rejected_packets",QString::number(aggregate.rejectedPackets)}});
    QJsonArray streams;for(const auto operation:traceOperations){if(!operation){continue;}auto state=channel.managedStreamObservation(operation);QJsonObject row{{"operation",QString::number(operation)},{"present",state.has_value()}};if(state){row.insert("stream",traceObservation(state->stream));row.insert("from_port",static_cast<int>(state->ports.local));row.insert("to_port",static_cast<int>(state->ports.remote));}streams.append(row);}nativeTeardown.insert("streams",streams);
  }catch(...){++traceErrors;}}
  void retainTrace()noexcept{try{
    QJsonArray rows;for(const auto& row:packetTrace){rows.append(row);}
    const auto data=QJsonDocument(QJsonObject{{"status","DIAGNOSTIC_METADATA_ONLY"},{"capacity",static_cast<int>(PacketTraceCapacity)},
      {"dropped_events",QString::number(traceDropped)},{"diagnostic_errors",QString::number(traceErrors)},
      {"bridge_failure",failure},{"native_teardown",nativeTeardown},{"events",rows}}).toJson();
    QFile file(work+"/packet-metadata-tail.json");if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)||file.write(data)!=data.size()){qWarning("Cannot retain bounded packet metadata tail");}
  }catch(...){qWarning("Cannot allocate bounded packet metadata tail");}}
  void fail(const QString& why){if(failure.isEmpty()){failure=why;}if(socket){socket->abort();}if(java.state()!=QProcess::NotRunning){java.kill();}}
  void collectErrors(){auto b=java.readAllStandardError();if(b.size()>65536-errors.size()){if(failure.isEmpty()){failure="Final Java diagnostic bound";}}else{errors+=b;}}
  bool send(p::View b){if(!socket||socket->state()!=QAbstractSocket::ConnectedState||socket->bytesToWrite()+static_cast<qint64>(b.size())>256*1024||socket->write(reinterpret_cast<const char*>(b.data()),static_cast<qint64>(b.size()))!=static_cast<qint64>(b.size())){fail("Owned I2CP enqueue/state/backpressure");return false;}return true;}
  bool lease(bool renewal){cp::RequestedLease l;l.gateway.fill(renewal?0x52:0x51);l.tunnelId=renewal?34:33;l.expiresMilliseconds=now+(renewal?700000:600000);auto encoded=cp::encodeLeaseRequest({7,{l}});return encoded&&send(*encoded);}
  void nativeFrame(cp::Frame& f){
    if(f.type==cp::GetDateType){auto encoded=cp::encodeMessage(cp::SetDate{now,std::string("2.13.0")});if(!encoded||!send(*encoded)){fail("Owned SetDate");}return;}
    if(f.type==cp::CreateSessionType){auto decoded=cp::decodeSessionMessage(f);if(!decoded){fail("Actual signed registration decode");return;}const auto& config=std::visit([](const auto& value)->const cp::SessionConfig&{return value.config;},*decoded);if(config.destination.wire!=native.wire||!cp::verifySessionConfig(config,now)){fail("Wrong actual signed identity/config");return;}auto encoded=cp::encodeMessage(cp::SessionStatus{7,cp::SessionState::Created});if(!encoded||!send(*encoded)||!lease(false)){fail("Owned CREATED/request37");}return;}
    if(f.type==cp::CreateLeaseSet2Type){auto decoded=cp::decodeCreateLeaseSet2(f);std::ranges::fill(f.payload,0);if(!decoded||decoded->sessionId!=7||decoded->leaseSet.destination.wire!=native.wire||decoded->leaseSet.leases.size()!=1){fail("Wrong genuine signed LS2/session/pair");return;}const auto tunnel=decoded->leaseSet.leases.front().tunnelId;const auto key=decoded->key.publicKey();if(leases==0){if(tunnel!=33){fail("Wrong first requested lease");return;}encryptionPublic.assign(key.begin(),key.end());}else{if(tunnel!=34||!std::ranges::equal(encryptionPublic,key)||decoded->leaseSet.expiresMilliseconds<=now+600000){fail("Renewal changed independent key/identity or ignored request");return;}++renewals;}++leases;return;}
    if(f.type!=cp::SendMessageType&&f.type!=cp::SendMessageExpiresType){fail("Unexpected native I2CP control");return;}
    auto sent=cp::decodeSendDatagram(f);if(!sent||sent->sessionId!=7||sent->recipient.wire!=remote.wire||sent->nonce!=0||leases==0){fail("Wrong current actual manager recipient/session");return;}auto mux=st::decodeMux(sent->payload);if(!mux||!javaRoute(mux->toPort,mux->fromPort)){fail("Wrong native mux tuple");return;}auto packet=st::decode(mux->packet,native);if(!packet){fail(QString::fromStdString(packet.error().message));return;}
    tracePacket("native-produced",*mux,*packet);
    const auto index=mux->fromPort==6881?3:static_cast<unsigned>(mux->fromPort-9000);++fromNative[index];if(++nativeFrames>4096){fail("Native packet lifetime bound");return;}QByteArray frame(4,Qt::Uninitialized);const auto n=static_cast<std::uint32_t>(sent->payload.size());for(unsigned i=0;i<4;++i){frame[i]=static_cast<char>(n>>(24-8*i));}frame+=bytes(sent->payload);
    if(java.state()!=QProcess::Running||java.bytesToWrite()+frame.size()>65536||java.write(frame)!=frame.size()){fail("Actual Java packet queue/backpressure");}else{tracePacket("native-to-java-enqueued",*mux,*packet);}
  }
  void collectNative(){if(!socket){return;}const auto n=socket->bytesAvailable();if(n<0||n>2*static_cast<qint64>(cp::MaximumFrameBytes)-input.size()){fail("Owned native input bound");return;}input+=socket->read(n);if(!greeting){if(input.isEmpty()){return;}if(input.front()!='*'){fail("Wrong I2CP greeting");return;}greeting=true;input.remove(0,1);}while(!input.isEmpty()&&failure.isEmpty()){auto step=decoder.consume(view(input));if(!step){fail(QString::fromStdString(step.error().message));return;}input.remove(0,static_cast<qsizetype>(step->consumed));if(step->frame){nativeFrame(*step->frame);}if(!step->consumed){break;}}}
  void deliver(p::View mux){auto encoded=cp::encodeMessagePayload({7,message++,p::Bytes(mux.begin(),mux.end())});if(!encoded||!send(*encoded)){fail("Cannot wrap actual manager Payload31");}else{traceMux("java-to-native-enqueued",mux,remote);}}
  void collectJava(){const auto available=java.bytesAvailable();if(available<0||available>65536-output.size()){fail("Actual manager frame buffer bound");return;}output+=java.readAllStandardOutput();while(output.size()>=4&&failure.isEmpty()){std::uint32_t n=0;for(unsigned i=0;i<4;++i){n=(n<<8)|static_cast<unsigned char>(output.at(i));}if(n<20||n>40960){fail("Actual Java mux size bound");return;}if(output.size()<4+static_cast<qsizetype>(n)){return;}auto raw=output.sliced(4,n);output.remove(0,4+n);if(closing){continue;}auto mux=st::decodeMux(view(raw));if(!mux||!javaRoute(mux->fromPort,mux->toPort)){fail("Actual Java mux scope");return;}auto packet=st::decode(mux->packet,remote);if(!packet){fail(QString::fromStdString(packet.error().message));return;}tracePacket("java-produced",*mux,*packet);const auto index=mux->toPort==6881?3:static_cast<unsigned>(mux->toPort-9000);++fromJava[index];if(++javaFrames>4096){fail("Java packet lifetime bound");return;}if(holdLate&&mux->toPort==9002&&!packet->payload.empty()){if(late.size()==16){fail("Late actual peer capacity");return;}late.emplace_back(view(raw).begin(),view(raw).end());}else{deliver(view(raw));}}}
  void releaseLate(){holdLate=false;for(const auto& mux:late){deliver(mux);++lateDelivered;}late.clear();}
};
}
class ManagerInteropTests final:public QObject {
  Q_OBJECT
  QString root;
private slots:
  void initTestCase(){root=qEnvironmentVariable("BIGLYBTC_I2P_MANAGER_ACCEPTANCE_ROOT");QVERIFY2(QDir::isAbsolutePath(root)&&!QFileInfo::exists(root),"Fresh explicit absolute manager output required; no skip");QVERIFY(QDir().mkdir(root));QVERIFY(QFile::setPermissions(root,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner));QFile jar(QString::fromUtf8(BIGLYBTC_I2P_MANAGER_JAR));QVERIFY(jar.open(QIODevice::ReadOnly));QCOMPARE(jar.size(),qint64(4288652));QCOMPARE(QCryptographicHash::hash(jar.readAll(),QCryptographicHash::Sha256).toHex(),QByteArray("b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5"));const auto classes=QString::fromUtf8(BIGLYBTC_I2P_MANAGER_CLASSES);std::set<QString> actual;QDirIterator entries(classes,{"*.class"},QDir::Files,QDirIterator::Subdirectories);while(entries.hasNext()){actual.insert(QDir(classes).relativeFilePath(entries.next()));}QVERIFY((actual==std::set<QString>{"ManagerPeer.class","ManagerPeer$Adapter.class"}));}
  void actualManagerConcurrentHttpInboundBtRenewalAndCancelBeyond120Seconds(){
    Bridge bridge(root);Channel channel;const auto diagnostic=qScopeGuard([&]{bridge.observeTeardown(channel);});QVERIFY2(bridge.failure.isEmpty(),qPrintable(bridge.failure));QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(bridge.work+"/peer-ready.json")||!bridge.failure.isEmpty(),15000);QVERIFY2(bridge.failure.isEmpty(),qPrintable(bridge.failure));QVERIFY(channel.begin({QHostAddress::LocalHost,bridge.server.serverPort(),"2.13.0",false,3000,false}));QTRY_VERIFY(channel.routerDate());auto identity=signer("dsa");auto registration=channel.createLeaseSession(std::move(identity),{{"i2cp.fastReceive","true"},{"i2cp.messageReliability","none"}},bridge.now);QVERIFY2(registration,registration?"":registration.error().message.c_str());QTRY_VERIFY(channel.leaseSetSubmission()&&bridge.leases==1);QVERIFY(channel.sessionId()&&*channel.sessionId()==7);const auto initialLease=*channel.leaseSetSubmission();QVERIFY(channel.enableStreamManager({90000,st::IdleAction::Send}));QVERIFY(channel.listenStreams({{6881,8020},{bridge.remote},8}));
    auto a=channel.connectStream(bridge.remote.wire,{9000,80,30000}),b=channel.connectStream(bridge.remote.wire,{9001,80,30000});QVERIFY2(a,a?"":a.error().message.c_str());QVERIFY2(b,b?"":b.error().message.c_str());const std::array<std::uint64_t,2> http{*a,*b};bridge.traceOperations[0]=*a;bridge.traceOperations[1]=*b;QVERIFY(http[0]!=http[1]);
    const auto established=[&](std::uint64_t op){auto state=channel.managedStreamObservation(op);return state&&state->stream.phase==st::Phase::Established;};QTRY_VERIFY_WITH_TIMEOUT((established(http[0])&&established(http[1]))||!bridge.failure.isEmpty(),30000);QVERIFY2(bridge.failure.isEmpty(),qPrintable(bridge.failure));QVERIFY(established(http[0])&&established(http[1]));QVERIFY(save(bridge.work+"/start-inbound.json",{{"listener_armed",true}}));QTRY_VERIFY_WITH_TIMEOUT(channel.streamManagerObservation().pendingSyns==1||!bridge.failure.isEmpty(),30000);QVERIFY2(bridge.failure.isEmpty(),qPrintable(bridge.failure));auto inbound=channel.acceptStream();QVERIFY2(inbound,inbound?"":inbound.error().message.c_str());bridge.traceOperations[2]=*inbound;QVERIFY(channel.managedStreamObservation(*inbound));QVERIFY(channel.managedStreamObservation(*inbound)->inbound);QTRY_VERIFY_WITH_TIMEOUT(established(*inbound)||!bridge.failure.isEmpty(),30000);QVERIFY2(bridge.failure.isEmpty(),qPrintable(bridge.failure));
    QByteArray hello;QTRY_VERIFY_WITH_TIMEOUT(([&]{auto part=channel.takeStreamBytes(*inbound,68);if(!part){bridge.fail(QString::fromStdString(part.error().message));return true;}hello+=bytes(*part);return hello.size()>=68||!bridge.failure.isEmpty();})(),15000);QVERIFY2(bridge.failure.isEmpty(),qPrintable(bridge.failure));QCOMPARE(hello,handshake());QVERIFY(channel.writeStream(*inbound,view(hello)));
    auto cancel=channel.connectStream(bridge.remote.wire,{9002,80,30000});QVERIFY2(cancel,cancel?"":cancel.error().message.c_str());bridge.traceOperations[3]=*cancel;QTRY_VERIFY_WITH_TIMEOUT(established(*cancel)||!bridge.failure.isEmpty(),30000);QVERIFY2(bridge.failure.isEmpty(),qPrintable(bridge.failure));QCOMPARE(channel.streamManagerObservation().streams,std::size_t{4});QVERIFY(channel.writeStream(*cancel,view(QByteArray("C"))));QTRY_VERIFY_WITH_TIMEOUT(!bridge.late.empty()||!bridge.failure.isEmpty(),15000);QVERIFY2(bridge.failure.isEmpty(),qPrintable(bridge.failure));QVERIFY(channel.cancelStream(*cancel));QVERIFY(channel.managedStreamObservation(*cancel));QCOMPARE(channel.managedStreamObservation(*cancel)->stream.phase,st::Phase::Cancelled);bridge.releaseLate();QVERIFY(bridge.lateDelivered>0);QVERIFY(!channel.takeStreamBytes(*cancel,1));QVERIFY(!channel.writeStream(*cancel,view(QByteArray("stale"))));
    // Per-round completion anchors the next send. Both actual Java clocks see
    // >=25*5005ms activity, not just an idle native wait or fabricated clock.
    QElapsedTimer activity;activity.start();qint64 nextRoundAt=0;unsigned round=0;bool roundSent=false,renewRequested=false,cancelBarrier=false;std::array<QByteArray,2> received;QByteArray keepalive;std::size_t peakTx=0,peakRx=0;std::set<std::uint64_t> progressed;
    const auto pump=[&]{
      if(!bridge.failure.isEmpty()){return true;}if(round==26){return true;}if(activity.elapsed()>=65000&&!renewRequested){if(!bridge.lease(true)){bridge.fail("Genuine renewal request refused");return true;}renewRequested=true;}
      const auto aggregate=channel.streamManagerObservation();peakTx=std::max(peakTx,aggregate.queuedTx);peakRx=std::max(peakRx,aggregate.queuedRx);if(aggregate.streams!=4||aggregate.queuedTx>4*st::MaximumBuffered||aggregate.queuedRx>4*st::MaximumBuffered){bridge.fail("Manager aggregate/operation bound changed");return true;}
      for(const auto op:{http[0],http[1],*inbound}){const auto state=channel.managedStreamObservation(op);if(!state||state->stream.operation!=op||state->stream.queuedTx>st::MaximumBuffered||state->stream.queuedRx>st::MaximumBuffered||state->stream.phase!=st::Phase::Established){bridge.fail("Current live stream lost during concurrent activity");return true;}}
      if(!roundSent&&activity.elapsed()>=nextRoundAt){for(const auto op:http){const auto written=channel.writeStream(op,view(request(round)));if(!written){bridge.fail(QString::fromStdString(written.error().message));return true;}}if(!channel.writeStream(*inbound,p::Bytes(4,0))){bridge.fail("Inbound keepalive write refused");return true;}roundSent=true;}
      if(!roundSent){return false;}for(unsigned i=0;i<2;++i){auto part=channel.takeStreamBytes(http[i],4096);if(!part){bridge.fail(QString::fromStdString(part.error().message));return true;}received[i]+=bytes(*part);if(received[i].size()>response(9000+i,round).size()){bridge.fail("Concurrent HTTP byte/duplicate bound");return true;}}
      auto part=channel.takeStreamBytes(*inbound,4);if(!part){bridge.fail(QString::fromStdString(part.error().message));return true;}keepalive+=bytes(*part);if(keepalive.size()>4){bridge.fail("Inbound keepalive byte bound");return true;}
      if(received[0].size()==response(9000,round).size()&&received[1].size()==response(9001,round).size()&&keepalive.size()==4){if(received[0]!=response(9000,round)||received[1]!=response(9001,round)||keepalive!=QByteArray(4,0)){bridge.fail("Actual manager application bytes differ");return true;}for(const auto op:http){progressed.insert(op);}progressed.insert(*inbound);if(!cancelBarrier){if(channel.managedStreamObservation(*cancel)->stream.phase!=st::Phase::Cancelled||channel.takeStreamBytes(*cancel,1)||channel.writeStream(*cancel,view(QByteArray("stale")))){bridge.fail("Late peer bytes resurrected cancelled operation");return true;}cancelBarrier=true;}++round;if(round==26){return true;}for(auto& bytes:received){bytes.clear();}keepalive.clear();roundSent=false;nextRoundAt=activity.elapsed()+5005;}
      return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(pump(),180000);QVERIFY2(bridge.failure.isEmpty(),qPrintable(bridge.failure+QString::fromUtf8(bridge.errors)));QCOMPARE(round,26U);QVERIFY(activity.elapsed()>=125000);QVERIFY(cancelBarrier&&progressed.size()==3);QVERIFY(renewRequested&&bridge.renewals==1&&bridge.leases==2);QVERIFY(channel.leaseSetSubmission()&&channel.leaseSetSubmission()->revision>initialLease.revision);QVERIFY(channel.sessionId()&&*channel.sessionId()==7);QVERIFY(channel.leaseSetSubmission()->expiresMilliseconds>initialLease.expiresMilliseconds);
    for(const auto op:{http[0],http[1],*inbound}){QVERIFY(channel.finishStreamWrite(op));}const auto finished=[&]{for(const auto op:{http[0],http[1],*inbound}){auto state=channel.managedStreamObservation(op);if(!state||state->stream.phase!=st::Phase::Closed){return false;}}return true;};QTRY_VERIFY_WITH_TIMEOUT(finished()||!bridge.failure.isEmpty(),15000);QVERIFY2(bridge.failure.isEmpty(),qPrintable(bridge.failure));QVERIFY(finished());
    for(const auto name:{"http-9000.json","http-9001.json","inbound-application.json","cancel-application.json"}){QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(bridge.work+'/'+QString::fromLatin1(name))||!bridge.failure.isEmpty(),10000);const auto evidence=json(bridge.work+'/'+QString::fromLatin1(name));QCOMPARE(evidence.value("status").toString(),QString("PASS"));QVERIFY(evidence.value("actual_manager_socket").toBool());if(evidence.contains("actual_activity_ns")){QVERIFY(evidence.value("actual_activity_ns").toDouble()>=125000000000.0);}}
    for(unsigned i=0;i<4;++i){QVERIFY(bridge.fromNative[i]>0&&bridge.fromJava[i]>0);}QVERIFY(!channel.receiverObservation().enabled);QCOMPARE(channel.receiverObservation().queued,std::size_t{0});for(const auto op:{http[0],http[1],*inbound,*cancel}){QVERIFY(channel.releaseStream(op));}channel.closeStreamListener();QCOMPARE(channel.streamManagerObservation().streams,std::size_t{0});QVERIFY(!channel.streamManagerObservation().listening);
    bridge.closing=true;channel.close();bridge.java.closeWriteChannel();QTRY_COMPARE_WITH_TIMEOUT(bridge.java.state(),QProcess::NotRunning,10000);bridge.collectErrors();QVERIFY2(bridge.failure.isEmpty(),qPrintable(bridge.failure));QVERIFY2(bridge.java.exitStatus()==QProcess::NormalExit&&bridge.java.exitCode()==0,qPrintable(QString::fromUtf8(bridge.errors)));const auto result=json(bridge.work+"/peer-result.json");QCOMPARE(result.value("status").toString(),QString("PASS"));QCOMPARE(result.value("applications").toInt(),4);QVERIFY(result.value("status_enqueue_ids").toInt()>0);QCOMPARE(result.value("fabricated_status_callbacks").toInt(),0);QVERIFY(!result.value("router_or_tunnels_claimed").toBool());
    QVERIFY(save(bridge.work+"/native-acceptance.json",{{"status","PASS"},{"actual_activity_ms",QString::number(activity.elapsed())},{"http_streams",2},{"inbound_handshake_bytes",68},{"rounds",26},{"actual_renewals",QString::number(bridge.renewals)},{"session_id",7},{"native_destination_sha256",QString::fromLatin1(QCryptographicHash::hash(bytes(bridge.native.wire),QCryptographicHash::Sha256).toHex())},{"unchanged_lease_public_key_sha256",QString::fromLatin1(QCryptographicHash::hash(bytes(bridge.encryptionPublic),QCryptographicHash::Sha256).toHex())},{"cancel_late_payloads",QString::number(bridge.lateDelivered)},{"peak_tx_bytes",QString::number(peakTx)},{"peak_rx_bytes",QString::number(peakRx)},{"real_router_or_bt_admission_claimed",false}}));
  }
};
QTEST_GUILESS_MAIN(ManagerInteropTests)
#include "i2p_streaming_manager_interop_tests.moc"
