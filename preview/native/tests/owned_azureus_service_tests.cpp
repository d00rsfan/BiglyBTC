#include "owned_azureus_service.hpp"
#include "owned_ddb_fixture.hpp"
#include <QCoreApplication>
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <QNetworkDatagram>
#include <limits>
#include <unistd.h>
#include <atomic>
#include <future>
#include <stdexcept>
#include <thread>

namespace {
using namespace biglybtc;
namespace udp=services::azureus_udp;
using namespace std::chrono_literals;
auto options(bool reply=true){app::AzureusServiceOptions result;result.enabled=true;result.servePings=reply;return result;}
auto make(bool reply=true){auto result=app::OwnedAzureusService::create(options(reply));if(!result){throw std::runtime_error("Explicit owned Azureus service failed to start");}return std::move(*result);}
bool finished(const app::OwnedAzureusService& owner,std::uint64_t request){const auto value=owner.observation();return value.lastResult&&value.lastResult->request==request&&value.lastResult->terminal;}
bool successful(const app::OwnedAzureusService& owner,std::uint64_t request){const auto value=owner.observation();return value.lastResult&&value.lastResult->request==request&&value.lastResult->terminal&&!value.lastResult->error&&!value.lastResult->transportFailure;}
bool hasEvent(const app::AzureusServiceObservation& value,udp::OperationEventKind kind){if(!value.snapshot){return false;}for(const auto& event:value.snapshot->events()){if(event.kind==kind){return true;}}return false;}
}
class OwnedAzureusServiceTests final:public QObject {
 Q_OBJECT
private slots:
 void originalVersionReservationPreservesSignedJavaWidthsAndChunkBoundary(){
   using storage::originalAzureusVersionReservation;auto first=originalAzureusVersionReservation({},1000000);QCOMPARE(first.first,1000);QCOMPARE(first.next,1128);
   auto rollback=originalAzureusVersionReservation(2000,1000000);QCOMPARE(rollback.first,2000);QCOMPARE(rollback.next,2128);
   auto wide=originalAzureusVersionReservation(0x100000005LL,0);QCOMPARE(wide.first,5);QCOMPARE(wide.next,133);
   auto wrap=originalAzureusVersionReservation(std::numeric_limits<std::int32_t>::max()-63,0);QCOMPARE(wrap.next,std::numeric_limits<std::int32_t>::min()+64);
 }
 void profileVersionsReserveBeforeUsePreserveUnknownsAndRecoverOnlyCanonicalPending(){
   QTemporaryDir root;QVERIFY(root.isValid());QDir dir(root.path());QVERIFY(dir.mkpath("dht/net4"));
   const auto path=root.path()+"/dht/net4/version.dat";auto write=[&](const QString& name,const QByteArray& bytes){QFile file(name);return file.open(QIODevice::WriteOnly|QIODevice::Truncate)&&file.write(bytes)==bytes.size()&&file.flush();};
   QVERIFY(write(path,"d4:nexti2000e6:future3:rawe"));auto held=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(held);auto versions=storage::AzureusValueVersions::open(*held,4);QVERIFY(versions);
   QCOMPARE((*versions)->next(1000000).value(),2000);QFile saved(path);QVERIFY(saved.open(QIODevice::ReadOnly));const auto firstBytes=saved.readAll();saved.close();auto parsed=core::Document::parse(firstBytes.toStdString());QVERIFY(parsed);QCOMPARE(parsed->find("next")->integer,std::int64_t{2128});QCOMPARE(parsed->find("future")->bytes,std::string_view("raw"));
   for(int i=1;i<128;++i){auto next=(*versions)->next(0);QVERIFY(next);QCOMPARE(*next,2000+i);}QVERIFY(saved.open(QIODevice::ReadOnly));QCOMPARE(saved.readAll(),firstBytes);saved.close();QCOMPARE((*versions)->next(0).value(),2128);
   versions->reset();auto restarted=storage::AzureusValueVersions::open(*held,4);QVERIFY(restarted);QCOMPARE((*restarted)->next(0).value(),2256);restarted->reset();
   QVERIFY(QFile::remove(path));QVERIFY(write(root.path()+"/dht/net4/version.saving","d4:nexti9000e6:future3:rawe"));auto recovered=storage::AzureusValueVersions::open(*held,4);QVERIFY(recovered);QCOMPARE((*recovered)->next(0).value(),9000);QVERIFY(!QFile::exists(root.path()+"/dht/net4/version.saving"));
   QVERIFY(write(root.path()+"/dht/net4/version.saving","d4:nexti9999ee"));QVERIFY(!(*recovered)->next(0));QVERIFY(!storage::AzureusValueVersions::open(*held,4));QVERIFY(QFile::exists(root.path()+"/dht/net4/version.saving"));
 }
 void versionOwnerRetainsCallerLeaseAndRefusesLockOrDirectoryReplacement(){
   QTemporaryDir root;QVERIFY(root.isValid());auto acquired=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(acquired);std::optional<storage::ProfileLease> held{std::move(*acquired)};auto versions=storage::AzureusValueVersions::open(*held,0);QVERIFY(versions);held.reset();QVERIFY(!storage::ProfileLease::acquire(root.path().toStdString()));QVERIFY((*versions)->next(1000000));
   QVERIFY(QFile::rename(root.path()+"/.azlock",root.path()+"/.azlock.old"));auto replacementResult=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(replacementResult);std::optional<storage::ProfileLease> replacement{std::move(*replacementResult)};const auto stale=(*versions)->next(1000000);QVERIFY(!stale);QCOMPARE(stale.error().code,storage::ErrorCode::Conflict);versions->reset();replacement.reset();
   QTemporaryDir another;QVERIFY(another.isValid());auto other=storage::ProfileLease::acquire(another.path().toStdString());QVERIFY(other);auto owned=storage::AzureusValueVersions::open(*other,0);QVERIFY(owned);QVERIFY(QDir(another.path()).rename("dht","dht-old"));QVERIFY(QDir(another.path()).mkdir("dht"));QVERIFY(!(*owned)->next(1000));
 }
 void localPublicationUsesActualTokenStoreAndReadbackFromGeneratedReceiver(){
   QTemporaryDir root;QVERIFY(root.isValid());auto held=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(held);auto versions=storage::AzureusValueVersions::open(*held,4);QVERIFY(versions);
   auto policy=options();policy.valueVersions=*versions;auto made=app::OwnedAzureusService::create(policy);QVERIFY(made);auto client=std::move(*made);
   const std::string key="owned-original-rating-key",payload("d1:ci4e1:n4:teste\0",18);const auto digest=QCryptographicHash::hash(QByteArray::fromStdString(key),QCryptographicHash::Sha1);const std::string encodedKey(digest.constData(),static_cast<std::size_t>(digest.size()));
   udp::Config config;config.generatedStoreService.emplace();config.generatedStoreService->volatileDatabase.emplace();services::azureus_store_policy::Key expected;expected.bytes=encodedKey;expected.closest=true;config.generatedStoreService->keyStates={expected};
   udp::OwnedPingTransport server(std::move(config));QVERIFY(server.start());QVERIFY(server.allowPeer(client->endpoint()));
   // The generated receiver's explicit test-only closest fact is not production
   // routing authority. The real owner must separately admit its endpoint.
   const auto rejected=client->publishValue(server.endpoint(),key,payload);QVERIFY(rejected);QTRY_VERIFY_WITH_TIMEOUT(finished(*client,*rejected),3000);QCOMPARE(client->observation().publishSnapshot->error,std::optional{app::AzureusServiceError::Invalid});QCOMPARE(server.statistics().received,std::uint64_t{0});
   const auto allowed=client->allowPeer(server.endpoint());QVERIFY(allowed);QTRY_VERIFY_WITH_TIMEOUT(successful(*client,*allowed),3000);
   const auto request=client->publishValue(server.endpoint(),key,payload);QVERIFY(request);QTRY_VERIFY_WITH_TIMEOUT(finished(*client,*request),5000);const auto snapshot=client->observation().publishSnapshot;QVERIFY(snapshot);QCOMPARE(snapshot->request,*request);QCOMPARE(snapshot->state,app::AzureusPublishState::Complete);QVERIFY(snapshot->localValue&&snapshot->localValue->isLocal());QCOMPARE(*snapshot->localValue->payload().view(),std::string_view(payload));QVERIFY(!snapshot->readback.empty());QCOMPARE(snapshot->readback[0].bytes,payload);QCOMPARE(snapshot->readback[0].version,snapshot->localValue->version());QVERIFY(snapshot->readback[0].originator.address==test::ddbAddress(client->endpoint()));
   const auto state=server.databaseState();QVERIFY(state);QCOMPARE(state->stores,std::uint64_t{1});QCOMPARE(state->reads,std::uint64_t{1});QVERIFY(client->close());QVERIFY(client->observation().joined);QCOMPARE(snapshot->state,app::AzureusPublishState::Complete);server.close();
 }
 void localPublicationCancellationAndChangedProfileNeverSendStore(){
   QTemporaryDir root;QVERIFY(root.isValid());auto held=storage::ProfileLease::acquire(root.path().toStdString());QVERIFY(held);auto versions=storage::AzureusValueVersions::open(*held,4);QVERIFY(versions);auto policy=options();policy.valueVersions=*versions;auto made=app::OwnedAzureusService::create(policy);QVERIFY(made);auto client=std::move(*made);
   QUdpSocket sink;QVERIFY(sink.bind(QHostAddress::LocalHost,0,QUdpSocket::DontShareAddress));const udp::Peer peer{sink.localAddress(),sink.localPort()};const auto allowed=client->allowPeer(peer);QVERIFY(allowed);QTRY_VERIFY_WITH_TIMEOUT(successful(*client,*allowed),3000);
   const auto request=client->publishValue(peer,"owned-cancel","binary");QVERIFY(request);QTRY_VERIFY_WITH_TIMEOUT(sink.hasPendingDatagrams()&&client->observation().snapshot->transport().pending()==1,3000);const auto tokenRequest=sink.receiveDatagram();QVERIFY(tokenRequest.isValid());const auto cancel=client->cancel(*request);QVERIFY(cancel);QTRY_VERIFY_WITH_TIMEOUT(client->observation().outstandingCommands==0,3000);QCOMPARE(client->observation().publishSnapshot->state,app::AzureusPublishState::Cancelled);QCOMPARE(client->observation().snapshot->transport().counters().sent,std::uint64_t{1});
   QFile changed(root.path()+"/dht/net4/version.dat");QVERIFY(changed.open(QIODevice::WriteOnly|QIODevice::Truncate));QCOMPARE(changed.write("d4:nexti1ee"),qint64{11});changed.close();const auto stale=client->publishValue(peer,"owned-stale","binary");QVERIFY(stale);QTRY_VERIFY_WITH_TIMEOUT(finished(*client,*stale),3000);QCOMPARE(client->observation().publishSnapshot->error,std::optional{app::AzureusServiceError::Profile});QCOMPARE(client->observation().snapshot->transport().counters().sent,std::uint64_t{1});QVERIFY(client->close());QVERIFY(client->observation().joined);
 }
 void defaultAndInvalidPolicyNeverCreateAnOwner(){
   auto disabled=app::OwnedAzureusService::create();QVERIFY(!disabled);QCOMPARE(disabled.error(),app::AzureusServiceError::Disabled);
   auto invalid=options();invalid.bindAddress=QHostAddress(QStringLiteral("192.0.2.1"));auto result=app::OwnedAzureusService::create(invalid);QVERIFY(!result);QCOMPARE(result.error(),app::AzureusServiceError::Invalid);
   invalid=options();invalid.maximumPending=33;result=app::OwnedAzureusService::create(invalid);QVERIFY(!result);invalid=options();invalid.maximumCommands=65;QVERIFY(!app::OwnedAzureusService::create(invalid));
 }
 void stdWorkerCreatesAndJoinsTheDedicatedQtOwner(){
   std::promise<bool> done;auto result=done.get_future();std::jthread worker([&]{try{auto service=make();const auto before=service->observation();const bool valid=before.snapshot&&before.snapshot->transport().state()==udp::TransportStatisticsState::Bound&&before.snapshot->transport().generation()!=0&&before.snapshot->transport().family()==udp::TransportStatisticsFamily::IPv4;
     const auto closed=service->close();const auto after=service->observation();done.set_value(valid&&bool(closed)&&after.joined&&!after.current&&after.state==app::AzureusServiceState::Closed&&after.snapshot&&after.snapshot->transport().state()==udp::TransportStatisticsState::Closed);}catch(...){done.set_value(false);}});
   QTRY_VERIFY_WITH_TIMEOUT(result.wait_for(0ms)==std::future_status::ready,5000);QVERIFY(result.get());
 }
 void twoActualServiceOwnersExchangeRttCoordinatesAndKeepOldSnapshots(){
   auto server=make(),client=make();QVERIFY(server->endpoint().port);QVERIFY(client->endpoint().port);const auto allow=server->allowPeer(client->endpoint());QVERIFY(allow);QTRY_VERIFY_WITH_TIMEOUT(successful(*server,*allow),3000);
   const auto initial=client->observation();QVERIFY(initial.snapshot);QCOMPARE(initial.snapshot->transport().counters().sent,std::uint64_t{0});
   const auto request=client->ping(server->endpoint());QVERIFY(request);QTRY_VERIFY_WITH_TIMEOUT(successful(*client,*request),3000);QTRY_VERIFY_WITH_TIMEOUT(client->observation().snapshot->coordinateObservations()==1,3000);
   const auto first=client->observation();QVERIFY(first.current);QVERIFY(first.snapshot->lastRttNanoseconds()&&*first.snapshot->lastRttNanoseconds()>0);QVERIFY(first.snapshot->remotePosition());QVERIFY(hasEvent(first,udp::OperationEventKind::Sent));QVERIFY(hasEvent(first,udp::OperationEventKind::Reply));
   const auto reverse=server->ping(client->endpoint());QVERIFY(reverse);QTRY_VERIFY_WITH_TIMEOUT(successful(*server,*reverse),3000);QTRY_VERIFY_WITH_TIMEOUT(server->observation().snapshot->coordinateObservations()==1,3000);
   const auto second=client->ping(server->endpoint());QVERIFY(second);QTRY_VERIFY_WITH_TIMEOUT(successful(*client,*second),3000);QCOMPARE(initial.snapshot->transport().counters().sent,std::uint64_t{0});QCOMPARE(first.snapshot->coordinateObservations(),std::uint64_t{1});QVERIFY(client->observation().snapshot->events().size()<=64);
 }
 void existingActualFindNodeAndStreamingFindValueRunOnTheQtOwner(){
   udp::Config remote{};remote.findNodeReply.emplace();udp::FindValueSlice partial;partial.continuation=true;partial.values=std::vector<udp::lookup::values::Value>{};udp::FindValueSlice terminal;terminal.values=std::vector<udp::lookup::values::Value>{};remote.findValueReply=std::vector<udp::FindValueSlice>{partial,terminal};
   udp::OwnedPingTransport server(std::move(remote));QVERIFY(server.start());auto client=make();QVERIFY(server.allowPeer(client->endpoint()));
   const auto node=client->findNode(server.endpoint(),udp::find::nodes::NodeId{});QVERIFY(node);QTRY_VERIFY_WITH_TIMEOUT(successful(*client,*node),3000);
   const auto values=client->findValue(server.endpoint(),udp::lookup::nodes::NodeId{},0,255);QVERIFY(values);QTRY_VERIFY_WITH_TIMEOUT(successful(*client,*values),3000);
   const auto observed=client->observation();QVERIFY(hasEvent(observed,udp::OperationEventKind::PartialReply));QVERIFY(hasEvent(observed,udp::OperationEventKind::Reply));QVERIFY(observed.snapshot->transport().counters().replies>=3);QCOMPARE(observed.outstandingCommands,std::size_t{0});
 }
 void actualTimeoutCancellationAndJoinedCloseRetainTheirOwnEvents(){
   auto silent=make(false),client=make();const auto allowed=silent->allowPeer(client->endpoint());QVERIFY(allowed);QTRY_VERIFY_WITH_TIMEOUT(successful(*silent,*allowed),3000);
   const auto timeout=client->ping(silent->endpoint(),30ms);QVERIFY(timeout);QTRY_VERIFY_WITH_TIMEOUT(finished(*client,*timeout),3000);QCOMPARE(client->observation().lastResult->transportFailure,std::optional{udp::Failure::Timeout});QVERIFY(hasEvent(client->observation(),udp::OperationEventKind::Timeout));
   const auto pending=client->ping(silent->endpoint(),2s);QVERIFY(pending);QTRY_VERIFY_WITH_TIMEOUT(client->observation().snapshot->transport().pending()==1,3000);const auto cancel=client->cancel(*pending);QVERIFY(cancel);
   QTRY_VERIFY_WITH_TIMEOUT(client->observation().outstandingCommands==0,3000);QVERIFY(hasEvent(client->observation(),udp::OperationEventKind::Cancelled));
   const auto closing=client->ping(silent->endpoint(),2s);QVERIFY(closing);QTRY_VERIFY_WITH_TIMEOUT(client->observation().snapshot->transport().pending()==1,3000);const auto retained=client->observation().snapshot;
   QVERIFY(client->close());const auto closed=client->observation();QVERIFY(closed.joined);QVERIFY(!closed.current);QCOMPARE(closed.state,app::AzureusServiceState::Closed);QVERIFY(hasEvent(closed,udp::OperationEventKind::Closed));QCOMPARE(closed.snapshot->transport().pending(),std::size_t{0});QCOMPARE(retained->transport().pending(),std::size_t{1});QVERIFY(client->close());QVERIFY(!client->ping(silent->endpoint()));
 }
 void boundedAdmissionAndCloseDoNotDependOnFreeCommandSlots(){
   auto silent=make(false);auto policy=options();policy.maximumCommands=1;policy.maximumPending=1;auto made=app::OwnedAzureusService::create(policy);QVERIFY(made);auto client=std::move(*made);const auto allowed=silent->allowPeer(client->endpoint());QVERIFY(allowed);QTRY_VERIFY_WITH_TIMEOUT(successful(*silent,*allowed),3000);
   const auto first=client->ping(silent->endpoint(),2s);QVERIFY(first);QTRY_VERIFY_WITH_TIMEOUT(client->observation().snapshot->transport().pending()==1,3000);const auto second=client->ping(silent->endpoint());QVERIFY(!second);QCOMPARE(second.error(),app::AzureusServiceError::Capacity);
   QVERIFY(client->close());QVERIFY(client->observation().joined);QCOMPARE(client->observation().outstandingCommands,std::size_t{0});
 }
 void invalidPublicPeersAndDeadlinesNeverSendOrChangeTheMailbox(){
   auto service=make();const auto before=service->observation();QVERIFY(before.snapshot);QVERIFY(!service->ping({QHostAddress(QStringLiteral("192.0.2.1")),1234}));QVERIFY(!service->ping({QHostAddress::LocalHost,0}));QVERIFY(!service->ping({QHostAddress::LocalHost,1234},0ns));QVERIFY(!service->ping({QHostAddress::LocalHost,1234},31s));QVERIFY(!service->cancel(0));QVERIFY(!service->findValue({QHostAddress::LocalHost,1234},{},0,257));
   const auto after=service->observation();QCOMPARE(after.sequence,before.sequence);QVERIFY(after.snapshot==before.snapshot);QCOMPARE(after.snapshot->transport().counters().sent,std::uint64_t{0});
 }
 void passiveReadsNeverQueueCommandsOrRepublishUnchangedFacts(){
   auto service=make();const auto before=service->observation();QVERIFY(before.snapshot);for(unsigned i=0;i<1000;++i){const auto value=service->observation();QCOMPARE(value.sequence,before.sequence);QVERIFY(value.snapshot==before.snapshot);QCOMPARE(value.outstandingCommands,std::size_t{0});}
   QCOMPARE(service->observation().snapshot->coordinateObservations(),std::uint64_t{0});QVERIFY(service->observation().snapshot->events().empty());
 }
 void concurrentMailboxReadsAndImmediatePendingDestructionJoinSafely(){
   auto silent=make(false);for(unsigned iteration=0;iteration<4;++iteration){auto client=make();const auto request=client->ping(silent->endpoint(),2s);QVERIFY(request);std::atomic<bool> stop=false;std::atomic<unsigned> reads=0;
     std::jthread reader([&](std::stop_token token){while(!token.stop_requested()&&!stop.load()){const auto value=client->observation();if(value.snapshot){++reads;}}});QTRY_VERIFY_WITH_TIMEOUT(reads.load()>0,3000);QVERIFY(client->close());stop=true;reader.request_stop();reader.join();QVERIFY(client->observation().joined);}
 }
};
QTEST_GUILESS_MAIN(OwnedAzureusServiceTests)
#include "owned_azureus_service_tests.moc"
