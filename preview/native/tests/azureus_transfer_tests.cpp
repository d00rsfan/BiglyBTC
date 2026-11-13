// SPDX-License-Identifier: GPL-2.0-or-later
#include "owned_azureus_service.hpp"
#include "../services/azureus_transfer/call.hpp"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QNetworkDatagram>
#include <QScopeGuard>
#include <QNetworkProxy>
#include <QDateTime>
#include <QProcess>
#include <QProcessEnvironment>
#include <QElapsedTimer>
#include <bit>
#include <algorithm>
#include <map>
namespace {
using namespace biglybtc;using namespace std::chrono_literals;
namespace wire=services::azureus_transfer;namespace udp=services::azureus_udp;
const std::string Handler(20,'H');
bool terminal(const app::OwnedAzureusService& service,std::uint64_t id){const auto value=service.observation();return value.lastResult&&value.lastResult->terminal&&value.lastResult->request==id;}
bool success(const app::OwnedAzureusService& service,std::uint64_t id){const auto value=service.observation();return value.lastResult&&value.lastResult->terminal&&value.lastResult->request==id&&!value.lastResult->error&&!value.lastResult->transportFailure;}
struct Normal {
  QTemporaryDir root;std::optional<storage::ProfileLease> lease;
  std::shared_ptr<storage::AzureusValueVersions> versions;std::unique_ptr<app::OwnedAzureusService> owner;
  Normal(const char* ip,bool acks){
    if(!root.isValid()){throw std::runtime_error("Owned DATA profile");}auto acquired=storage::ProfileLease::acquire(root.path().toStdString());if(!acquired){throw std::runtime_error(acquired.error().message);}lease.emplace(std::move(*acquired));
    auto config=storage::ConfigFile::createMissing(*lease,"biglybt.config","d22:Plugin.DHT.dht.enabledi1ee");if(!config){throw std::runtime_error(config.error().message);}auto opened=storage::AzureusValueVersions::open(*lease,0);if(!opened){throw std::runtime_error(opened.error().message);}versions=*opened;
    app::AzureusServiceOptions options;options.enabled=true;options.network=0;options.bindAddress=QHostAddress(QString::fromLatin1(ip));options.serveValues=true;options.valueVersions=versions;options.transferHandler=app::AzureusTransferHandler{Handler,!acks};auto made=app::OwnedAzureusService::create(options);if(!made){throw std::runtime_error("Owned normal DATA service");}owner=std::move(*made);
  }
};
// Test-only direct owner: generated loopback actors, real router/DB bindings,
// stable source epoch and the same packet state machine as the normal service.
struct Actor {
 udp::OwnedPingTransport transport;QObject receiver;std::vector<std::shared_ptr<const udp::TransferRequest>> incoming;
 explicit Actor(const char* ip):transport([]{udp::Config c;c.network=0;c.servePings=true;return c;}()){
  if(!transport.start(QHostAddress(QString::fromLatin1(ip)))){throw std::runtime_error("Generated DATA actor");}
  services::azureus_nodes::ResolvedEndpoint endpoint;endpoint.address.length=4;endpoint.address.port=transport.endpoint().port;const auto address=transport.endpoint().address.toIPv4Address();for(unsigned i=0;i<4;++i){endpoint.address.bytes[i]=static_cast<std::uint8_t>(address>>(24-8*i));}
  services::azureus_nodes::NodeCache cache;auto contact=cache.lookup(endpoint,55);if(!contact){throw std::runtime_error("Generated contact");}
  auto router=services::azureus_router::RouterOwner::create(contact->id);auto database=services::azureus_db_runtime::Owner::create();
  if(!router||!database||!(*database)->bindLocal(services::azureus_nodes::Contact{.protocol=55,.address=endpoint.address,.id=contact->id})){throw std::runtime_error("Generated normal binding");}
  std::shared_ptr<services::azureus_router::RouterOwner> r=std::move(*router);std::shared_ptr<services::azureus_db_runtime::Owner> d=std::move(*database);
  if(!transport.serveDatabase(std::move(r),std::move(d),[]{return true;},[](std::move_only_function<void(bool)> callback){callback(true);return true;})){throw std::runtime_error("Generated DATA service");}
 }
 bool registerRoom(const std::string& key){return bool(transport.registerTransferHandler(key,true,&receiver,[this](auto request){incoming.push_back(std::move(request));},[]()->std::optional<std::uint64_t>{return 1;}));}
};
wire::udp::EncodeFields header(bool ipv6=false){wire::udp::EncodeFields h;h.action=1035;h.connection=std::bit_cast<std::int64_t>(std::uint64_t{1}<<63);h.network=0;h.transaction=99;h.now=1234;wire::udp::Address address;address.length=ipv6?16:4;address.bytes[ipv6?15:0]=127;address.port=12345;h.originator=address;return h;}
}
class AzureusTransferTests final:public QObject {
  Q_OBJECT
private slots:
  void originalDataHeadersRangesAndPhysicalBounds(){
    wire::Data part{wire::Type::WriteRequest,Handler,std::string(20,'K'),0,1317,4096,std::string(1317,'x')};wire::udp::Context context;context.network=0;context.now=1234;
    for(bool v6:{false,true}){auto encoded=wire::encode(header(v6),part);QVERIFY(encoded);QCOMPARE(encoded->size(),v6?std::size_t{1430}:std::size_t{1418});auto decoded=wire::decode(*encoded,context);QVERIFY(decoded);QCOMPARE(decoded->data,part);QCOMPARE(decoded->header.fields().action,1035);QCOMPARE(decoded->header.fields().kind,wire::udp::Kind::Request);encoded->push_back('x');QVERIFY(!wire::decode(*encoded,context));}
    auto wrong=header();wrong.action=1034;QVERIFY(!wire::encode(wrong,part));part.total=16*1024+1;QVERIFY(!wire::encodeBody(part));part.total=4096;part.length=-1;QVERIFY(!wire::encodeBody(part));
    wire::Data gap{wire::Type::ReadRequest,Handler,std::string(20,'K'),1317,200,0,{}};auto encoded=wire::encodeBody(gap);QVERIFY(encoded);QCOMPARE(wire::decodeBody(*encoded)->start,1317);QCOMPARE(wire::decodeBody(*encoded)->length,200);QVERIFY(wire::decodeBody(*encoded)->payload.empty());
  }
  void reorderedFragmentsRecoverGapsButRejectConflictsAndFalseAckCompletion(){
    const std::string bytes(4096,'x');auto pieces=wire::fragments(wire::Type::WriteRequest,Handler,std::string(20,'K'),bytes);QVERIFY(pieces);QCOMPARE(pieces->size(),std::size_t{4});wire::Reassembly received;
    QVERIFY(received.add((*pieces)[3]));QCOMPARE(received.missing(),std::vector<wire::Range>({{0,3951}}));QVERIFY(received.add((*pieces)[0]));QVERIFY(received.add((*pieces)[0]));QCOMPARE(received.pieces(),std::size_t{2});auto conflict=(*pieces)[0];conflict.payload[0]='z';QVERIFY(!received.add(conflict));
    QVERIFY(received.add((*pieces)[2]));auto completed=received.add((*pieces)[1]);QVERIFY(completed&&*completed);QCOMPARE(received.value(),std::optional{bytes});
    wire::WriteProgress write;wire::Data ack{wire::Type::WriteReply,Handler,std::string(20,'K'),0,1317,0,{}};QVERIFY(write.acknowledge(ack,4096));QVERIFY(write.receivedAck&&!write.complete);ack.length=4096;QVERIFY(write.acknowledge(ack,4096));QVERIFY(write.complete);QVERIFY(!received.add(wire::Data{wire::Type::WriteRequest,Handler,std::string(20,'K'),0,0,0,{}}));
  }
  void actualNormalServicesFragmentRequestAndReply_data(){QTest::addColumn<bool>("acks");QTest::addColumn<int>("requestBytes");QTest::addColumn<int>("replyBytes");QTest::newRow("original-acks")<<true<<4096<<6000;QTest::newRow("disable-call-acks-fragmented")<<false<<4096<<6000;QTest::newRow("disable-call-acks-single")<<false<<40<<64;QTest::newRow("disable-call-acks-fragmented-reply")<<false<<40<<6000;}
  void actualNormalServicesFragmentRequestAndReply(){
    QFETCH(bool,acks);QFETCH(int,requestBytes);QFETCH(int,replyBytes);Normal a("127.101.0.1",acks),b("127.102.0.1",acks);
    const auto aa=a.owner->allowPeer(b.owner->endpoint()),bb=b.owner->allowPeer(a.owner->endpoint());QVERIFY(aa&&bb);QTRY_VERIFY_WITH_TIMEOUT(success(*a.owner,*aa)&&success(*b.owner,*bb),3000);
    const auto beforeA=a.owner->observation().snapshot->transport().counters().sent,beforeB=b.owner->observation().snapshot->transport().counters().sent;const std::string request(static_cast<std::size_t>(requestBytes),'q'),reply(static_cast<std::size_t>(replyBytes),'r');const auto call=a.owner->callTransfer(b.owner->endpoint(),Handler,request,5s);QVERIFY(call);QTRY_VERIFY_WITH_TIMEOUT(b.owner->observation().transferRequest!=nullptr,3000);const auto incoming=b.owner->observation().transferRequest;QCOMPARE(incoming->request,request);QCOMPARE(incoming->peer,a.owner->endpoint());QCOMPARE(incoming->network,0);QCOMPARE(incoming->handler,Handler);
    const auto answer=b.owner->answerTransfer(incoming->receipt,reply);QVERIFY(answer);QTRY_VERIFY_WITH_TIMEOUT(terminal(*a.owner,*call),5000);const auto result=a.owner->observation().transferSnapshot;QVERIFY(result&&result->terminal&&!result->failure);QCOMPARE(result->reply,reply);QVERIFY(success(*a.owner,*call));QTRY_VERIFY_WITH_TIMEOUT(success(*b.owner,*answer),3000);if(!acks&&requestBytes==40&&replyBytes==64){QTRY_COMPARE_WITH_TIMEOUT(a.owner->observation().snapshot->transport().counters().sent,beforeA+1,3000);QTRY_COMPARE_WITH_TIMEOUT(b.owner->observation().snapshot->transport().counters().sent,beforeB+1,3000);}
    const auto repeated=b.owner->answerTransfer(incoming->receipt,"second answer");QVERIFY(!repeated);QVERIFY(!b.owner->observation().transferRequest);
    QVERIFY(!QFile::exists(a.root.path()+"/dht/version.dat"));QVERIFY(!QFile::exists(b.root.path()+"/dht/version.dat"));QCOMPARE(a.owner->observation().databaseSnapshot->stores,std::uint64_t{0});QCOMPARE(b.owner->observation().databaseSnapshot->stores,std::uint64_t{0});
  }
  void cancellationAndSourceWithdrawalNeverPublishLateReply(){
    Normal a("127.103.0.1",false),b("127.104.0.1",false);auto aa=a.owner->allowPeer(b.owner->endpoint()),bb=b.owner->allowPeer(a.owner->endpoint());QVERIFY(aa&&bb);QTRY_VERIFY_WITH_TIMEOUT(success(*a.owner,*aa)&&success(*b.owner,*bb),3000);
    const auto call=a.owner->callTransfer(b.owner->endpoint(),Handler,"held request",3s);QVERIFY(call);QTRY_VERIFY_WITH_TIMEOUT(b.owner->observation().transferRequest!=nullptr,2000);const auto incoming=b.owner->observation().transferRequest;const auto cancel=a.owner->cancel(*call);QVERIFY(cancel);QTRY_VERIFY_WITH_TIMEOUT(a.owner->observation().transferSnapshot&&a.owner->observation().transferSnapshot->terminal,2000);QCOMPARE(a.owner->observation().transferSnapshot->failure,std::optional{udp::Failure::Cancelled});
    auto config=storage::ConfigFile::load(*b.lease,"biglybt.config");QVERIFY(config);QVERIFY(config->document().setInteger("Plugin.DHT.dht.enabled",0));QVERIFY(config->save(*b.lease));QVERIFY(!b.owner->observation().transferRequest);
    const auto answer=b.owner->answerTransfer(incoming->receipt,"late reply");QVERIFY(answer);QTRY_VERIFY_WITH_TIMEOUT(terminal(*b.owner,*answer),3000);QVERIFY(!success(*b.owner,*answer));QCOMPARE(a.owner->observation().transferSnapshot->failure,std::optional{udp::Failure::Cancelled});QVERIFY(a.owner->observation().transferSnapshot->reply.empty());
    QVERIFY(a.owner->close());QVERIFY(b.owner->close());QVERIFY(a.owner->observation().joined&&b.owner->observation().joined);
  }
  void writeAckIsNotReplyAndCloseRetiresTheHeldCall(){
    Normal a("127.107.0.1",true),b("127.108.0.1",true);auto aa=a.owner->allowPeer(b.owner->endpoint()),bb=b.owner->allowPeer(a.owner->endpoint());QVERIFY(aa&&bb);QTRY_VERIFY_WITH_TIMEOUT(success(*a.owner,*aa)&&success(*b.owner,*bb),3000);
    const auto timed=a.owner->callTransfer(b.owner->endpoint(),Handler,"ack alone",2s);QVERIFY(timed);QTRY_VERIFY_WITH_TIMEOUT(b.owner->observation().transferRequest!=nullptr,2000);QTRY_VERIFY_WITH_TIMEOUT(a.owner->observation().transferSnapshot&&a.owner->observation().transferSnapshot->terminal,3000);QCOMPARE(a.owner->observation().transferSnapshot->failure,std::optional{udp::Failure::Timeout});QVERIFY(a.owner->observation().transferSnapshot->reply.empty());
    // Use a distinct receiver: the first request intentionally remains held
    // until its actual30s remote budget. No fake remote CANCEL is inferred.
    Normal c("127.109.0.1",true);auto ac=a.owner->allowPeer(c.owner->endpoint()),ca=c.owner->allowPeer(a.owner->endpoint());QVERIFY(ac&&ca);QTRY_VERIFY_WITH_TIMEOUT(success(*a.owner,*ac)&&success(*c.owner,*ca),3000);
    const auto held=a.owner->callTransfer(c.owner->endpoint(),Handler,"close held",5s);QVERIFY(held);QTRY_VERIFY_WITH_TIMEOUT(c.owner->observation().transferRequest!=nullptr,3000);QVERIFY(a.owner->close());auto result=a.owner->observation();QVERIFY(result.joined&&result.transferSnapshot&&result.transferSnapshot->terminal);QCOMPARE(result.transferSnapshot->failure,std::optional{udp::Failure::Closed});QVERIFY(result.transferSnapshot->reply.empty());
  }
  void wrongCallKeyNetworkAndOriginalConnectionNeverCompleteTheCall(){
    Normal caller("127.113.0.1",false);QUdpSocket remote;remote.setProxy(QNetworkProxy::NoProxy);QVERIFY(remote.bind(QHostAddress(QStringLiteral("127.114.0.1")),0));const udp::Peer actual{remote.localAddress(),remote.localPort()};const auto grant=caller.owner->allowPeer(actual);QVERIFY(grant);QTRY_VERIFY_WITH_TIMEOUT(success(*caller.owner,*grant),3000);
    const auto call=caller.owner->callTransfer(actual,Handler,"request",5s);QVERIFY(call);QTRY_VERIFY_WITH_TIMEOUT(remote.hasPendingDatagrams(),3000);const auto incoming=remote.receiveDatagram();const auto requestBytes=incoming.data();wire::udp::Context context;context.network=0;context.now=QDateTime::currentMSecsSinceEpoch();auto request=wire::decode(std::string(requestBytes.constData(),static_cast<std::size_t>(requestBytes.size())),context);QVERIFY(request);
    auto h=header();h.now=context.now;h.connection=request->header.fields().connection^1;h.originator->bytes={127,114,0,1};h.originator->port=remote.localPort();wire::Data reply{wire::Type::ReadReply,Handler,request->data.key,0,2,2,"ok"};
    const auto send=[&](const wire::udp::EncodeFields& fields,const wire::Data& part){auto encoded=wire::encode(fields,part);return encoded&&remote.writeDatagram(encoded->data(),static_cast<qint64>(encoded->size()),caller.owner->endpoint().address,caller.owner->endpoint().port)==static_cast<qint64>(encoded->size());};
    reply.key[0]^=1;QVERIFY(send(h,reply));reply.key=request->data.key;reply.type=wire::Type::WriteRequest;h.network=4;QVERIFY(send(h,reply));h.network=0;h.connection=request->header.fields().connection;reply.payload="NO";QVERIFY(send(h,reply));h.connection^=1;reply.payload="ok";QVERIFY(send(h,reply));
    QTRY_VERIFY_WITH_TIMEOUT(terminal(*caller.owner,*call),5000);auto result=caller.owner->observation().transferSnapshot;QVERIFY(result&&result->terminal&&!result->failure);QCOMPARE(result->reply,std::string("ok"));QVERIFY(!caller.owner->observation().transferRequest);QTRY_VERIFY_WITH_TIMEOUT(caller.owner->observation().snapshot->transport().counters().foreign>0&&caller.owner->observation().snapshot->transport().counters().malformed>0,3000);
  }
  void concurrentRoomsHaveIndependentCallsAndReceipts(){
    Actor a("127.121.0.1"),b("127.122.0.1");const std::string first(20,'A'),second(20,'B');
    QVERIFY(a.transport.allowPeer(b.transport.endpoint()));QVERIFY(b.transport.allowPeer(a.transport.endpoint()));
    for(const auto& key:{first,second}){QVERIFY(a.registerRoom(key)&&b.registerRoom(key));}
    std::map<std::string,std::string> replies;std::vector<udp::Ticket> tickets;
    for(int i=0;i<7;++i){const auto key=i<6?first:second;const auto value=std::to_string(i);auto result=a.transport.callTransfer(b.transport.endpoint(),55,key,value,&a.receiver,[&,value](udp::TransferOutcome reply){replies[value]=reply?*reply:"failed";},std::chrono::steady_clock::now()+3s);QVERIFY(result);tickets.push_back(*result);}
    auto overflow=a.transport.callTransfer(b.transport.endpoint(),55,first,"overflow",&a.receiver,[](auto){},std::chrono::steady_clock::now()+3s);QVERIFY(!overflow&&overflow.error()==udp::Failure::Capacity);
    QTRY_COMPARE_WITH_TIMEOUT(b.incoming.size(),std::size_t{7},2000);
    for(std::size_t i=0;i<b.incoming.size();++i){for(std::size_t j=i+1;j<b.incoming.size();++j){QVERIFY(b.incoming[i]->receipt!=b.incoming[j]->receipt);}}
    for(auto i=b.incoming.rbegin();i!=b.incoming.rend();++i){QVERIFY(b.transport.answerTransfer((*i)->receipt,"reply "+(*i)->request));}
    QTRY_COMPARE_WITH_TIMEOUT(replies.size(),std::size_t{7},2000);for(int i=0;i<7;++i){QCOMPARE(replies[std::to_string(i)],"reply "+std::to_string(i));}
    b.incoming.clear();auto held=a.transport.callTransfer(b.transport.endpoint(),55,first,"held",&a.receiver,[&](auto result){replies["held"]=result?*result:"cancelled";},std::chrono::steady_clock::now()+3s);QVERIFY(held);QTRY_COMPARE_WITH_TIMEOUT(b.incoming.size(),std::size_t{1},2000);
    const auto stale=b.incoming.front()->receipt;QVERIFY(a.transport.unregisterTransferHandler(first));QTRY_VERIFY_WITH_TIMEOUT(replies.contains("held"),1000);QCOMPARE(replies["held"],std::string("cancelled"));QVERIFY(b.transport.unregisterTransferHandler(first));QVERIFY(b.registerRoom(first));QVERIFY(!b.transport.answerTransfer(stale,"stale"));
    QVERIFY(a.transport.unregisterTransferHandler(second));QVERIFY(!a.transport.cancelTransfer(tickets.front()));
  }
  void lostSmallReplyIsResentWithoutApplicationRedelivery(){
    Actor receiver("127.123.0.1");QVERIFY(receiver.registerRoom(Handler));QUdpSocket remote;remote.setProxy(QNetworkProxy::NoProxy);QVERIFY(remote.bind(QHostAddress(QStringLiteral("127.124.0.1")),0));QVERIFY(receiver.transport.allowPeer({remote.localAddress(),remote.localPort()}));
    auto h=header();h.now=QDateTime::currentMSecsSinceEpoch();h.originator->bytes={127,124,0,1};h.originator->port=remote.localPort();wire::Data request{wire::Type::WriteRequest,Handler,std::string(20,'R'),0,5,5,"hello"};auto encoded=wire::encode(h,request);QVERIFY(encoded);
    const auto send=[&]{return remote.writeDatagram(encoded->data(),static_cast<qint64>(encoded->size()),receiver.transport.endpoint().address,receiver.transport.endpoint().port)==static_cast<qint64>(encoded->size());};QVERIFY(send());QTRY_COMPARE_WITH_TIMEOUT(receiver.incoming.size(),std::size_t{1},1000);QVERIFY(receiver.transport.answerTransfer(receiver.incoming.front()->receipt,"answer"));QTRY_VERIFY_WITH_TIMEOUT(remote.hasPendingDatagrams(),1000);const auto first=remote.receiveDatagram().data();QVERIFY(send());QTRY_VERIFY_WITH_TIMEOUT(remote.hasPendingDatagrams(),1000);const auto repeated=remote.receiveDatagram().data();
    wire::udp::Context context;context.network=0;context.now=QDateTime::currentMSecsSinceEpoch();const auto original=wire::decode(first.toStdString(),context),retry=wire::decode(repeated.toStdString(),context);QVERIFY(original&&retry);QCOMPARE(original->data,retry->data);QCOMPARE(original->header.fields().connection,retry->header.fields().connection);QCOMPARE(receiver.incoming.size(),std::size_t{1});
  }
  void actualOriginalHandlerPeer_data(){QTest::addColumn<bool>("originalCaller");QTest::addColumn<bool>("acks");for(bool caller:{false,true}){for(bool acks:{false,true}){const auto tag=QByteArray(caller?"original-caller-":"original-receiver-")+(acks?"acks":"disable-call-acks");QTest::newRow(tag.constData())<<caller<<acks;}}}
  void actualOriginalHandlerPeer(){
    QFETCH(bool,originalCaller);QFETCH(bool,acks);const auto classes=qEnvironmentVariable("BIGLYBTC_AZUREUS_DATA_REFERENCE_CLASSPATH"),java=qEnvironmentVariable("BIGLYBTC_AZUREUS_DATA_REFERENCE_JAVA");QVERIFY2(!classes.isEmpty()&&!java.isEmpty(),"root must prepare/pin actual original DATA helper; no SKIP or model substitution");
    Normal native("127.111.0.1",acks);QTemporaryDir javaRoot;QVERIFY(javaRoot.isValid());QVERIFY(QDir().mkpath(javaRoot.path()+"/home"));QVERIFY(QDir().mkpath(javaRoot.path()+"/tmp"));QProcess peer;
    auto environment=QProcessEnvironment::systemEnvironment();for(const auto* name:{"JAVA_TOOL_OPTIONS","_JAVA_OPTIONS","JDK_JAVA_OPTIONS","CLASSPATH"}){environment.remove(QString::fromLatin1(name));}peer.setProcessEnvironment(environment);peer.setProcessChannelMode(QProcess::SeparateChannels);peer.setProgram(java);peer.setArguments({"-Xmx128m","-XX:ActiveProcessorCount=1","-Duser.home="+javaRoot.path()+"/home","-Djava.io.tmpdir="+javaRoot.path()+"/tmp","-Dazureus.config.path="+javaRoot.path()+"/profile","-Djava.security.manager=allow","-cp",classes,"com.biglybt.core.dht.transport.udp.impl.OriginalDataPeer","127.112.0.1","0","127.111.0.1",QString::number(native.owner->endpoint().port),originalCaller?"caller":"receiver",acks?"acks":"noacks"});
    const auto joined=qScopeGuard([&]{if(peer.state()!=QProcess::NotRunning){peer.kill();peer.waitForFinished(3000);}});peer.start();QVERIFY(peer.waitForStarted(3000));QByteArray output;QElapsedTimer ready;ready.start();while(ready.elapsed()<10000&&!output.contains("READY ")){peer.waitForReadyRead(50);output+=peer.readAllStandardOutput();QCoreApplication::processEvents();}QVERIFY2(output.contains("READY "),peer.readAllStandardError().constData());
    const auto lines=output.split('\n');QByteArray line;for(const auto& candidate:lines){if(candidate.startsWith("READY ")){line=candidate;break;}}bool ok=false;const auto port=line.mid(6).toInt(&ok);QVERIFY(ok&&port>0&&port<=65535);udp::Peer actual{QHostAddress(QStringLiteral("127.112.0.1")),static_cast<quint16>(port)};
    const auto grant=native.owner->allowPeer(actual);QVERIFY(grant);QTRY_VERIFY_WITH_TIMEOUT(success(*native.owner,*grant),3000);
    if(originalCaller){QCOMPARE(peer.write("GO\n"),qint64{3});QVERIFY(peer.waitForBytesWritten(1000));QTRY_VERIFY_WITH_TIMEOUT(native.owner->observation().transferRequest!=nullptr,5000);auto request=native.owner->observation().transferRequest;QCOMPARE(request->peer,actual);QCOMPARE(request->request,std::string(acks?4096:40,'q'));const auto answer=native.owner->answerTransfer(request->receipt,std::string(6000,'r'));QVERIFY(answer);QTRY_VERIFY_WITH_TIMEOUT(success(*native.owner,*answer),3000);}
    else{const auto call=native.owner->callTransfer(actual,Handler,std::string(acks?4096:40,'q'),10s);QVERIFY(call);QTRY_VERIFY_WITH_TIMEOUT(terminal(*native.owner,*call),10000);const auto response=native.owner->observation().transferSnapshot;QVERIFY(response&&response->terminal&&!response->failure);QCOMPARE(response->reply,std::string(6000,'r'));}
    QTRY_VERIFY_WITH_TIMEOUT(peer.state()==QProcess::NotRunning,12000);output+=peer.readAllStandardOutput();QVERIFY2(peer.exitStatus()==QProcess::NormalExit&&peer.exitCode()==0,peer.readAllStandardError().constData());QVERIFY(output.contains(originalCaller?"COMPLETE caller reply=6000":"COMPLETE receiver requests=1"));QVERIFY(!QFile::exists(native.root.path()+"/dht/version.dat"));
  }
  void originalDdbReadUsesKeyAndSameConnection_data(){QTest::addColumn<bool>("originalCaller");QTest::newRow("java-reads-native")<<true;QTest::newRow("native-reads-java")<<false;}
  void originalDdbReadUsesKeyAndSameConnection(){
    QFETCH(bool,originalCaller);const auto classes=qEnvironmentVariable("BIGLYBTC_AZUREUS_DATA_REFERENCE_CLASSPATH"),java=qEnvironmentVariable("BIGLYBTC_AZUREUS_DATA_REFERENCE_JAVA");QVERIFY(!classes.isEmpty()&&!java.isEmpty());
    Actor native("127.111.0.1");QVERIFY(native.transport.registerTransferHandler(Handler,false,&native.receiver,[&](auto request){native.incoming.push_back(std::move(request));},[]()->std::optional<std::uint64_t>{return 1;},udp::TransferMode::Read));
    QTemporaryDir root;QVERIFY(root.isValid());QVERIFY(QDir().mkpath(root.path()+"/home"));QVERIFY(QDir().mkpath(root.path()+"/tmp"));QProcess peer;auto environment=QProcessEnvironment::systemEnvironment();for(const auto* name:{"JAVA_TOOL_OPTIONS","_JAVA_OPTIONS","JDK_JAVA_OPTIONS","CLASSPATH"}){environment.remove(QString::fromLatin1(name));}peer.setProcessEnvironment(environment);peer.setProcessChannelMode(QProcess::SeparateChannels);peer.setProgram(java);peer.setArguments({"-Xmx128m","-XX:ActiveProcessorCount=1","-Duser.home="+root.path()+"/home","-Djava.io.tmpdir="+root.path()+"/tmp","-Dazureus.config.path="+root.path()+"/profile","-Djava.security.manager=allow","-cp",classes,"com.biglybt.core.dht.transport.udp.impl.OriginalDdbReadPeer","127.112.0.1","0","127.111.0.1",QString::number(native.transport.endpoint().port),originalCaller?"caller":"receiver","acks"});
    const auto joined=qScopeGuard([&]{if(peer.state()!=QProcess::NotRunning){peer.kill();peer.waitForFinished(3000);}});peer.start();QVERIFY(peer.waitForStarted(3000));QByteArray output;QElapsedTimer ready;ready.start();while(ready.elapsed()<10000&&!output.contains("READY ")){peer.waitForReadyRead(50);output+=peer.readAllStandardOutput();QCoreApplication::processEvents();}QVERIFY2(output.contains("READY "),peer.readAllStandardError().constData());QByteArray line;for(const auto& candidate:output.split('\n')){if(candidate.startsWith("READY ")){line=candidate;break;}}bool ok=false;const auto port=line.mid(6).toInt(&ok);QVERIFY(ok&&port>0&&port<=65535);udp::Peer actual{QHostAddress(QStringLiteral("127.112.0.1")),quint16(port)};QVERIFY(native.transport.allowPeer(actual));
    if(originalCaller){QCOMPARE(peer.write("GO\n"),qint64{3});QVERIFY(peer.waitForBytesWritten(1000));QTRY_COMPARE_WITH_TIMEOUT(native.incoming.size(),std::size_t{1},5000);QCOMPARE(native.incoming[0]->request,std::string(200,'q'));QVERIFY(native.transport.answerTransfer(native.incoming[0]->receipt,std::string(6000,'r')));}
    else{std::optional<udp::TransferOutcome> reply;auto ticket=native.transport.readTransfer(actual,55,Handler,std::string(200,'q'),&native.receiver,[&](auto value){reply.emplace(std::move(value));},std::chrono::steady_clock::now()+10s);QVERIFY(ticket);QTRY_VERIFY_WITH_TIMEOUT(reply.has_value(),10000);QVERIFY(*reply);QCOMPARE(**reply,std::string(6000,'r'));}
    QTRY_VERIFY_WITH_TIMEOUT(peer.state()==QProcess::NotRunning,12000);QVERIFY2(peer.exitStatus()==QProcess::NormalExit&&peer.exitCode()==0,peer.readAllStandardError().constData());
  }
  void readCancellationAndRevokedReceiptsCannotComplete(){
    Actor a("127.121.0.1"),b("127.122.0.1");bool current=true;for(auto* actor:{&a,&b}){QVERIFY(actor->transport.registerTransferHandler(Handler,false,&actor->receiver,[actor](auto request){actor->incoming.push_back(std::move(request));},[&]()->std::optional<std::uint64_t>{return current?std::optional<std::uint64_t>{1}:std::nullopt;},udp::TransferMode::Read));}QVERIFY(a.transport.allowPeer(b.transport.endpoint())&&b.transport.allowPeer(a.transport.endpoint()));
    std::optional<udp::TransferOutcome> result;auto ticket=a.transport.readTransfer(b.transport.endpoint(),55,Handler,"d1:t5:alphae",&a.receiver,[&](auto value){result.emplace(std::move(value));},std::chrono::steady_clock::now()+3s);QVERIFY(ticket);QTRY_COMPARE_WITH_TIMEOUT(b.incoming.size(),std::size_t{1},2000);QVERIFY(a.transport.cancelTransfer(*ticket));QTRY_VERIFY_WITH_TIMEOUT(result.has_value(),1000);QVERIFY(!*result&&result->error()==udp::Failure::Cancelled);current=false;QVERIFY(!b.transport.answerTransfer(b.incoming[0]->receipt,"de"));QVERIFY(!a.transport.readTransfer(b.transport.endpoint(),55,Handler,std::string(256,'q'),&a.receiver,[](auto){},std::chrono::steady_clock::now()+3s));
  }
  void oversizedForeignActionAndUnregisteredHandlerCannotOpenRequest(){
    Normal target("127.105.0.1",false);QUdpSocket sender;sender.setProxy(QNetworkProxy::NoProxy);QVERIFY(sender.bind(QHostAddress(QStringLiteral("127.106.0.1")),0));const auto allowed=target.owner->allowPeer({sender.localAddress(),sender.localPort()});QVERIFY(allowed);QTRY_VERIFY_WITH_TIMEOUT(success(*target.owner,*allowed),3000);
    wire::Data part{wire::Type::WriteRequest,std::string(20,'!'),std::string(20,'K'),0,1317,1317,std::string(1317,'x')};auto h=header();h.originator->bytes={127,106,0,1};h.originator->port=sender.localPort();h.now=QDateTime::currentMSecsSinceEpoch();auto encoded=wire::encode(h,part);QVERIFY(encoded);QCOMPARE(encoded->size(),std::size_t{1418});QCOMPARE(sender.writeDatagram(encoded->data(),static_cast<qint64>(encoded->size()),target.owner->endpoint().address,target.owner->endpoint().port),static_cast<qint64>(encoded->size()));QTRY_VERIFY_WITH_TIMEOUT(target.owner->observation().snapshot->transport().counters().foreign>0,3000);QVERIFY(!target.owner->observation().transferRequest);
    (*encoded)[10]=4;(*encoded)[11]=0;QCOMPARE(sender.writeDatagram(encoded->data(),static_cast<qint64>(encoded->size()),target.owner->endpoint().address,target.owner->endpoint().port),static_cast<qint64>(encoded->size()));QTRY_VERIFY_WITH_TIMEOUT(target.owner->observation().snapshot->transport().counters().oversized>0,3000);QVERIFY(!target.owner->observation().transferRequest);
  }
};
QTEST_MAIN(AzureusTransferTests)
#include "azureus_transfer_tests.moc"
